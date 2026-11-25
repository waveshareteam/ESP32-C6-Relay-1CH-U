/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "relay_demo_internal.h"
#include "relay_serial.h"
#include "relay_command_parser.h"

/* Collect a receive burst before driving the half-duplex bus back to the sender. */
#define RS485_ECHO_MAX_BYTES 1024
#define RS485_ECHO_IDLE_MS 20

static uint8_t s_echo_data[RS485_ECHO_MAX_BYTES];
static size_t s_echo_length;
static TickType_t s_echo_last_rx;
static bool s_echo_discard;
static SemaphoreHandle_t s_uart_lock;
static QueueHandle_t s_uart_events;
static bool s_uart_ready;
static const char *TAG = "relay_serial";
static const uint8_t s_extension_commands[][8] = {
    {0x01, 0x05, 0x00, 0x00, 0x55, 0x00, 0xF2, 0x9A},
    {0x01, 0x05, 0x00, 0x01, 0x55, 0x00, 0xA3, 0x5A},
    {0x01, 0x05, 0x00, 0x02, 0x55, 0x00, 0x53, 0x5A},
    {0x01, 0x05, 0x00, 0x03, 0x55, 0x00, 0x02, 0x9A},
    {0x01, 0x05, 0x00, 0x04, 0x55, 0x00, 0xB3, 0x5B},
    {0x01, 0x05, 0x00, 0x05, 0x55, 0x00, 0xE2, 0x9B},
    {0x01, 0x05, 0x00, 0x06, 0x55, 0x00, 0x12, 0x9B},
    {0x01, 0x05, 0x00, 0x07, 0x55, 0x00, 0x43, 0x5B},
    {0x01, 0x05, 0x00, 0xFF, 0xFF, 0xFF, 0xFC, 0x4A},
    {0x01, 0x05, 0x00, 0xFF, 0x00, 0x00, 0xFD, 0xFA},
};

/* Caller holds s_uart_lock. DTR half duplex restores RX after TX completes. */
static esp_err_t relay_serial_write_locked(const uint8_t *data, size_t length)
{
    if (!s_uart_ready) return ESP_ERR_INVALID_STATE;
    int written = uart_write_bytes(RS485_UART_PORT, data, length);
    esp_err_t err = written > 0 ?
        uart_wait_tx_done(RS485_UART_PORT, pdMS_TO_TICKS(3500)) : ESP_FAIL;
    return written == (int)length ? err : ESP_FAIL;
}

static esp_err_t relay_serial_send(const uint8_t *data, size_t length)
{
    if (s_uart_lock == NULL) return ESP_ERR_INVALID_STATE;
    if (data == NULL || length == 0 || length > RS485_ECHO_MAX_BYTES) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_uart_lock, pdMS_TO_TICKS(4000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = relay_serial_write_locked(data, length);
    xSemaphoreGive(s_uart_lock);
    return err;
}

static void echo_discard_burst(TickType_t now)
{
    s_echo_length = 0; s_echo_discard = true; s_echo_last_rx = now;
}

static void echo_receive(const uint8_t *data, size_t length, TickType_t now)
{
    s_echo_last_rx = now;
    if (s_echo_discard) return;
    if (length > sizeof(s_echo_data) - s_echo_length) {
        echo_discard_burst(now); return;
    }
    memcpy(s_echo_data + s_echo_length, data, length);
    s_echo_length += length;
}

/* Caller holds s_uart_lock and has rechecked that the UART RX buffer is empty. */
static void echo_flush_idle(TickType_t now)
{
    const TickType_t idle_ticks = pdMS_TO_TICKS(RS485_ECHO_IDLE_MS);
    if ((!s_echo_length && !s_echo_discard) ||
        (TickType_t)(now - s_echo_last_rx) < (idle_ticks ? idle_ticks : 1)) return;
    if (s_echo_discard) {
        ESP_LOGW(TAG, "RS485 echo skipped: RX error or burst exceeds %u bytes", (unsigned)RS485_ECHO_MAX_BYTES);
    } else {
        esp_err_t err = relay_serial_write_locked(s_echo_data, s_echo_length);
        if (err == ESP_OK) ESP_LOGI(TAG, "RS485 echoed %u bytes", (unsigned)s_echo_length);
        else ESP_LOGW(TAG, "RS485 echo failed: %s", esp_err_to_name(err));
    }
    /* Never replay a failed/partially transmitted burst automatically. */
    s_echo_length = 0; s_echo_discard = false;
}

esp_err_t relay_rs485_send_extension(uint8_t command)
{
    if (command == 0 || command > 10) return ESP_ERR_INVALID_ARG;
    return relay_serial_send(s_extension_commands[command - 1], 8);
}

static esp_err_t relay_serial_configure(int baud, int bits, int parity, int stops)
{
    if (baud < 1200 || baud > 921600 || bits < 5 || bits > 8 ||
        parity < 0 || parity > 2 || (stops != 1 && stops != 2)) return ESP_ERR_INVALID_ARG;
    if (s_uart_lock == NULL) return ESP_ERR_INVALID_STATE;
    const uart_config_t config = {
        .baud_rate = baud,
        .data_bits = (uart_word_length_t)(UART_DATA_5_BITS + bits - 5),
        .parity = parity == 1 ? UART_PARITY_ODD : parity == 2 ? UART_PARITY_EVEN : UART_PARITY_DISABLE,
        .stop_bits = stops == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    if (xSemaphoreTake(s_uart_lock, pdMS_TO_TICKS(4000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = uart_wait_tx_done(RS485_UART_PORT, pdMS_TO_TICKS(3500));
    if (err == ESP_OK) {
        s_uart_ready = false;
        err = uart_param_config(RS485_UART_PORT, &config);
        /* IDF uart_param_config() calls uart_hal_init(), which resets hardware to
           UART_MODE_UART. Restore half duplex on EVERY parameter change. */
        if (err == ESP_OK) err = uart_set_mode(RS485_UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
        if (err == ESP_OK) err = uart_set_rx_timeout(RS485_UART_PORT, 2);
        s_uart_ready = err == ESP_OK;
    }
    if (err == ESP_OK) {
        uart_flush_input(RS485_UART_PORT);
        xQueueReset(s_uart_events);
    }
    xSemaphoreGive(s_uart_lock);
    return err;
}

static void on_relay_command(uint8_t command, void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "RS485 relay command matched: %c", command);
    relay_handle_command_byte(command, "RS485");
}

static void serial_task(void *argument)
{
    (void)argument;
    uint8_t chunk[128];
    relay_command_parser_t parser = {0};
    while (true) {
        xSemaphoreTake(s_uart_lock, portMAX_DELAY);
        uart_event_t event;
        while (xQueueReceive(s_uart_events, &event, 0) == pdTRUE) {
            bool overflow = event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL;
            if (overflow || event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR) {
                ESP_LOGW(TAG, "RS485 receive error: %d", event.type);
                relay_command_parser_reset(&parser);
                echo_discard_burst(xTaskGetTickCount());
                if (overflow) { uart_flush_input(RS485_UART_PORT); xQueueReset(s_uart_events); }
            }
        }
        size_t available = 0;
        esp_err_t read_err = uart_get_buffered_data_len(RS485_UART_PORT, &available);
        /* Never hold the UART/configuration mutex during a blocking read. */
        int received = read_err == ESP_OK && available ? uart_read_bytes(RS485_UART_PORT, chunk,
            available < sizeof(chunk) ? available : sizeof(chunk), 0) : 0;
        if (received > 0) {
            echo_receive(chunk, received, xTaskGetTickCount());
        } else if (read_err == ESP_OK && available == 0) {
            /* Wait for an idle gap, not just the end of a 128-byte read chunk. */
            echo_flush_idle(xTaskGetTickCount());
        } else if (read_err != ESP_OK || received < 0) {
            echo_discard_burst(xTaskGetTickCount());
        }
        xSemaphoreGive(s_uart_lock);
        if (received > 0) {
            /* Relay callbacks can notify BLE; never invoke them under the UART mutex. */
            relay_command_parser_feed(&parser, chunk, received, on_relay_command, NULL);
        }
        if (received <= 0) vTaskDelay(1); else taskYIELD();
    }
}

esp_err_t relay_serial_init(void)
{
    s_uart_lock = xSemaphoreCreateMutex();
    if (s_uart_lock == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = uart_driver_install(RS485_UART_PORT, 2048, 512, 24, &s_uart_events, 0);
    if (err == ESP_OK) err = uart_set_pin(RS485_UART_PORT, RS485_TX_GPIO, RS485_RX_GPIO,
        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, RS485_DIR_GPIO, UART_PIN_NO_CHANGE);
    if (err == ESP_OK) err = relay_serial_configure(RS485_BAUD_RATE, 8, 0, 1);
    if (err != ESP_OK) return err;
    if (xTaskCreate(serial_task, "rs485", 4096, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "RS485 relay receiver + raw echo ready: %d 8N1 TX=%d RX=%d DIR=%d", RS485_BAUD_RATE,
        RS485_TX_GPIO, RS485_RX_GPIO, RS485_DIR_GPIO);
    return ESP_OK;
}
