/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "relay_demo.h"
#include "relay_demo_internal.h"
#include "relay_serial.h"

static const char *TAG = "relay_demo";

#define RTC_CTRL_1_REG              0x00
#define RTC_SECONDS_REG             0x04
#define RTC_CTRL_1_CAP_SEL          0x01
#define RTC_YEAR_OFFSET             1970
#define RTC_IO_TIMEOUT_MS           1000
#define SCHEDULE_MAX_EVENTS         10

typedef struct {
    bool enabled;
    relay_datetime_t time;
    relay_action_t action;
    relay_repeat_t repeat;
} schedule_event_t;

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_rtc_device;
static bool s_rtc_ready;
static SemaphoreHandle_t s_rtc_lock;
static const char *s_rtc_startup_action = "read-failed";
static bool s_relay_on;
static SemaphoreHandle_t s_lock;
static schedule_event_t s_events[SCHEDULE_MAX_EVENTS];

static uint8_t bcd_to_decimal(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

static uint8_t decimal_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static bool datetime_is_valid(const relay_datetime_t *datetime)
{
    if (datetime == NULL || datetime->year < 1970 || datetime->year > 2069 ||
        datetime->month < 1 || datetime->month > 12 || datetime->weekday > 6 ||
        datetime->hour > 23 || datetime->minute > 59 || datetime->second > 59) {
        return false;
    }
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned maximum = days[datetime->month - 1];
    const unsigned year = datetime->year;
    if (datetime->month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
        ++maximum;
    }
    return datetime->day >= 1 && datetime->day <= maximum;
}

bool relay_get_state(void)
{
    bool state;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    state = s_relay_on;
    xSemaphoreGive(s_lock);
    return state;
}

void relay_set_state(bool on, const char *source)
{
    bool changed;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    changed = s_relay_on != on;
    s_relay_on = on;
    gpio_set_level(RELAY_GPIO, on ? 1 : 0);
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Relay CH1: %s (%s)", on ? "ON" : "OFF",
             source != NULL ? source : "unknown");
    if (changed) {
        relay_ble_notify_state(on);
    }
}

void relay_toggle_state(const char *source)
{
    relay_set_state(!relay_get_state(), source);
}

void relay_handle_command_byte(uint8_t command, const char *source)
{
    switch (command) {
    case '1':
        relay_toggle_state(source);
        break;
    case '9':
        relay_set_state(true, source);
        break;
    case '0':
        relay_set_state(false, source);
        break;
    default:
        ESP_LOGW(TAG, "Unsupported relay command 0x%02X from %s", command,
                 source != NULL ? source : "unknown");
        break;
    }
}

static esp_err_t rtc_write_registers(uint8_t start_register, const uint8_t *data, size_t length)
{
    if (!s_rtc_ready || data == NULL || length == 0 || length > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t tx_buffer[8] = {start_register};
    memcpy(&tx_buffer[1], data, length);
    return i2c_master_transmit(s_rtc_device, tx_buffer, length + 1, RTC_IO_TIMEOUT_MS);
}

static esp_err_t rtc_read_registers(uint8_t start_register, uint8_t *data, size_t length)
{
    if (!s_rtc_ready || data == NULL || length == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(
        s_rtc_device, &start_register, 1, data, length, RTC_IO_TIMEOUT_MS);
}

static bool rtc_bcd_valid(uint8_t value)
{
    return (value & 0x0FU) <= 9 && (value >> 4) <= 9;
}

static bool rtc_raw_bcd_valid(const uint8_t *raw, bool mode_12h)
{
    return rtc_bcd_valid(raw[0] & 0x7FU) && rtc_bcd_valid(raw[1] & 0x7FU) &&
           rtc_bcd_valid(raw[2] & (mode_12h ? 0x1FU : 0x3FU)) &&
           rtc_bcd_valid(raw[3] & 0x3FU) && (raw[4] & 0x07U) <= 6 &&
           rtc_bcd_valid(raw[5] & 0x1FU) && rtc_bcd_valid(raw[6]);
}

static esp_err_t rtc_initialize(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = RTC_I2C_PORT,
        .sda_io_num = RTC_I2C_SDA_GPIO,
        .scl_io_num = RTC_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (err != ESP_OK) {
        return err;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDRESS,
        .scl_speed_hz = RTC_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &device_config, &s_rtc_device);
    if (err != ESP_OK) {
        return err;
    }

    s_rtc_lock = xSemaphoreCreateMutex();
    if (s_rtc_lock == NULL) return ESP_ERR_NO_MEM;
    /* Register initialization is decided by the chip's OS/calendar state below. */
    s_rtc_ready = true;
    return ESP_OK;
}

esp_err_t relay_rtc_read(relay_datetime_t *datetime)
{
    if (!s_rtc_ready || datetime == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t registers[11] = {0};
    xSemaphoreTake(s_rtc_lock, portMAX_DELAY);
    esp_err_t err = rtc_read_registers(0x00, registers, sizeof(registers));
    xSemaphoreGive(s_rtc_lock);
    if (err != ESP_OK) {
        return err;
    }
    memcpy(datetime->registers, registers, sizeof(registers));
    const uint8_t *raw = &registers[4];
    const bool mode_12h = (registers[0] & 0x02U) != 0;
    datetime->oscillator_stopped = (raw[0] & 0x80U) != 0;
    datetime->stopped = (registers[0] & 0x20U) != 0;
    datetime->second = bcd_to_decimal(raw[0] & 0x7FU);
    datetime->minute = bcd_to_decimal(raw[1] & 0x7FU);
    datetime->hour = bcd_to_decimal(raw[2] & (mode_12h ? 0x1FU : 0x3FU));
    bool hour_valid = true;
    if (mode_12h) {
        hour_valid = datetime->hour >= 1 && datetime->hour <= 12;
        datetime->hour = (uint8_t)(datetime->hour % 12U + ((raw[2] & 0x20U) ? 12U : 0U));
    }
    datetime->day = bcd_to_decimal(raw[3] & 0x3FU);
    datetime->weekday = raw[4] & 0x07U;
    datetime->month = bcd_to_decimal(raw[5] & 0x1FU);
    datetime->year = (uint16_t)(RTC_YEAR_OFFSET + bcd_to_decimal(raw[6]));
    datetime->calendar_valid = rtc_raw_bcd_valid(raw, mode_12h) && hour_valid && datetime_is_valid(datetime);
    /* Successful I2C reads are displayable even with OS=1 or an unset calendar. */
    return ESP_OK;
}

esp_err_t relay_rtc_set(const relay_datetime_t *datetime)
{
    if (!s_rtc_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!datetime_is_valid(datetime)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* An explicit time write selects normal 24-hour operation and starts the RTC. */
    const uint8_t control_1 = RTC_CTRL_1_CAP_SEL;
    xSemaphoreTake(s_rtc_lock, portMAX_DELAY);
    esp_err_t err = rtc_write_registers(RTC_CTRL_1_REG, &control_1, 1);
    if (err != ESP_OK) {
        xSemaphoreGive(s_rtc_lock);
        return err;
    }
    const uint8_t raw[7] = {
        decimal_to_bcd(datetime->second),
        decimal_to_bcd(datetime->minute),
        decimal_to_bcd(datetime->hour),
        decimal_to_bcd(datetime->day),
        decimal_to_bcd(datetime->weekday),
        decimal_to_bcd(datetime->month),
        decimal_to_bcd((uint8_t)(datetime->year - RTC_YEAR_OFFSET)),
    };
    err = rtc_write_registers(RTC_SECONDS_REG, raw, sizeof(raw));
    xSemaphoreGive(s_rtc_lock);
    return err;
}

const char *relay_rtc_startup_action(void) { return s_rtc_startup_action; }

static esp_err_t rtc_initialize_lost_time(void)
{
    /* PCF85063A oscillator stabilization can take 2 seconds after complete power loss.
       This fixed baseline is never a saved calibration or a browser/network time. */
    vTaskDelay(pdMS_TO_TICKS(2200));
    const relay_datetime_t baseline = {
        .year = 1970, .month = 1, .day = 1, .weekday = 4,
        .hour = 0, .minute = 0, .second = 0,
    };
    esp_err_t err = relay_rtc_set(&baseline);
    relay_datetime_t readback = {0};
    if (err == ESP_OK) err = relay_rtc_read(&readback);
    if (err == ESP_OK && (!readback.calendar_valid || readback.oscillator_stopped || readback.stopped)) err = ESP_ERR_INVALID_STATE;
    s_rtc_startup_action = err == ESP_OK ? "initialized-1970" : "initialization-failed";
    return err;
}

void relay_datetime_format(const relay_datetime_t *datetime, char *buffer, size_t size)
{
    if (datetime == NULL || buffer == NULL || size == 0) {
        return;
    }
    snprintf(buffer, size, "%04u/%02u/%02u Week:%u %02u:%02u:%02u%s",
             (unsigned)datetime->year, datetime->month, datetime->day,
             datetime->weekday, datetime->hour, datetime->minute, datetime->second,
             datetime->oscillator_stopped ? " oscillator-stop" : "");
}

esp_err_t relay_schedule_add(const relay_datetime_t *datetime,
                             relay_action_t action,
                             relay_repeat_t repeat,
                             uint8_t *event_id)
{
    if (!datetime_is_valid(datetime) || action > RELAY_ACTION_RETAIN ||
        repeat > RELAY_REPEAT_MONTHLY) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < SCHEDULE_MAX_EVENTS; ++i) {
        if (!s_events[i].enabled) {
            s_events[i].enabled = true;
            s_events[i].time = *datetime;
            s_events[i].action = action;
            s_events[i].repeat = repeat;
            if (event_id != NULL) {
                *event_id = (uint8_t)(i + 1);
            }
            result = ESP_OK;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t relay_schedule_delete(uint8_t event_id)
{
    if (event_id == 0 || event_id > SCHEDULE_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t index = event_id - 1;
    const bool existed = s_events[index].enabled;
    memset(&s_events[index], 0, sizeof(s_events[index]));
    xSemaphoreGive(s_lock);
    return existed ? ESP_OK : ESP_ERR_NOT_FOUND;
}

size_t relay_schedule_json(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return 0;
    }
    buffer[0] = '\0';
    cJSON *root = cJSON_CreateObject();
    cJSON *events = cJSON_CreateArray();
    if (root == NULL || events == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(events);
        return 0;
    }
    cJSON_AddItemToObject(root, "events", events);
    relay_datetime_t now = {0};
    char text[128] = "RTC unavailable";
    if (relay_rtc_read(&now) == ESP_OK) {
        relay_datetime_format(&now, text, sizeof(text));
    }
    cJSON_AddStringToObject(root, "time", text);
    unsigned count = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < SCHEDULE_MAX_EVENTS; ++i) {
        if (!s_events[i].enabled) {
            continue;
        }
        const schedule_event_t *event = &s_events[i];
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            xSemaphoreGive(s_lock);
            cJSON_Delete(root);
            return 0;
        }
        ++count;
        cJSON_AddItemToArray(events, item);
        cJSON_AddNumberToObject(item, "id", i + 1);
        snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u",
                 (unsigned)event->time.year, event->time.month, event->time.day,
                 event->time.hour, event->time.minute, event->time.second);
        cJSON_AddStringToObject(item, "datetime", text);
        cJSON_AddNumberToObject(item, "weekday", event->time.weekday);
        cJSON_AddNumberToObject(item, "action", event->action);
        cJSON_AddNumberToObject(item, "repeat", event->repeat);
        char key[20];
        snprintf(key, sizeof(key), "eventStr%u", count);
        snprintf(text, sizeof(text), "ID:%u %04u/%02u/%02u %02u:%02u:%02u CH1:%u Cycle:%u",
                 (unsigned)(i + 1), (unsigned)event->time.year, event->time.month, event->time.day,
                 event->time.hour, event->time.minute, event->time.second, event->action, event->repeat);
        cJSON_AddStringToObject(root, key, text);
    }
    xSemaphoreGive(s_lock);
    cJSON_AddNumberToObject(root, "eventCount", count);
    const bool ok = cJSON_PrintPreallocated(root, buffer, (int)size, false);
    cJSON_Delete(root);
    if (!ok) {
        buffer[0] = '\0';
        return 0;
    }
    return strlen(buffer);
}

static bool schedule_matches(const schedule_event_t *event, const relay_datetime_t *now)
{
    if (event->time.hour != now->hour || event->time.minute != now->minute ||
        event->time.second != now->second) {
        return false;
    }
    switch (event->repeat) {
    case RELAY_REPEAT_NONE:
        return event->time.year == now->year && event->time.month == now->month &&
               event->time.day == now->day;
    case RELAY_REPEAT_DAILY:
        return true;
    case RELAY_REPEAT_WEEKLY:
        return event->time.weekday == now->weekday;
    case RELAY_REPEAT_MONTHLY:
        return event->time.day == now->day;
    default:
        return false;
    }
}

static void rtc_schedule_task(void *argument)
{
    (void)argument;
    uint8_t previous_second = 0xFF;
    while (true) {
        relay_datetime_t now = {0};
        if (relay_rtc_read(&now) == ESP_OK && now.calendar_valid &&
            !now.oscillator_stopped && !now.stopped && now.second != previous_second) {
            previous_second = now.second;
            for (size_t i = 0; i < SCHEDULE_MAX_EVENTS; ++i) {
                schedule_event_t event;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                event = s_events[i];
                xSemaphoreGive(s_lock);
                if (!event.enabled || !schedule_matches(&event, &now)) {
                    continue;
                }
                if (event.action == RELAY_ACTION_ON) {
                    relay_set_state(true, "RTC");
                } else if (event.action == RELAY_ACTION_OFF) {
                    relay_set_state(false, "RTC");
                }
                if (event.repeat == RELAY_REPEAT_NONE) {
                    relay_schedule_delete((uint8_t)(i + 1));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t relay_initialize(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err == ESP_OK) {
        gpio_set_level(RELAY_GPIO, 0);
        s_relay_on = false;
    }
    return err;
}

static esp_err_t storage_initialize(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t relay_demo_start(void)
{
    ESP_LOGI(TAG, "%s starting", BOARD_NAME);
    ESP_LOGI(TAG, "Pins: relay=%d RS485=%d/%d/%d I2C=%d/%d",
             RELAY_GPIO, RS485_TX_GPIO, RS485_RX_GPIO, RS485_DIR_GPIO,
             RTC_I2C_SDA_GPIO, RTC_I2C_SCL_GPIO);

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(storage_initialize());
    ESP_ERROR_CHECK(relay_initialize());

    esp_err_t rtc_err = rtc_initialize();
    if (rtc_err == ESP_OK) {
        relay_datetime_t startup = {0};
        esp_err_t read_err = relay_rtc_read(&startup);
        if (read_err == ESP_OK) {
            char text[80];
            relay_datetime_format(&startup, text, sizeof(text));
            ESP_LOGI(TAG, "RTC startup read: %s; OS=%u STOP=%u CALENDAR=%s", text,
                     (unsigned)startup.oscillator_stopped, (unsigned)startup.stopped,
                     startup.calendar_valid ? "VALID" : "INVALID");
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, startup.registers, sizeof(startup.registers), ESP_LOG_INFO);
            if (startup.oscillator_stopped || !startup.calendar_valid) {
                ESP_LOGW(TAG, "RTC OS/calendar invalid: initializing fixed 1970 baseline, no saved-time restore");
                esp_err_t init_err = rtc_initialize_lost_time();
                if (init_err != ESP_OK) ESP_LOGE(TAG, "RTC baseline initialization failed: %s", esp_err_to_name(init_err));
            } else {
                s_rtc_startup_action = "retained";
                ESP_LOGI(TAG, "RTC valid: keeping existing chip time; no register writes");
            }
        } else {
            ESP_LOGW(TAG, "RTC startup read failed: %s", esp_err_to_name(read_err));
        }
        xTaskCreate(rtc_schedule_task, "rtc_schedule", 4096, NULL, 4, NULL);
        ESP_LOGI(TAG, "PCF85063 ready: %s; current-time calibration is manual only", s_rtc_startup_action);
    } else {
        ESP_LOGE(TAG, "PCF85063 initialization failed: %s", esp_err_to_name(rtc_err));
    }

    ESP_ERROR_CHECK(relay_serial_init());

    ESP_ERROR_CHECK(relay_ble_start());
    ESP_ERROR_CHECK(relay_wifi_start());

    ESP_LOGI(TAG, "%s initialization complete", BOARD_NAME);
    return ESP_OK;
}
