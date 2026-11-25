/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "relay_demo_internal.h"
#include "relay_time.h"

/* POSIX TZ uses the opposite sign: CST-8 is China Standard Time, UTC+8. */
#define RELAY_TIMEZONE "CST-8"

static const char *TAG = "relay_time";
static SemaphoreHandle_t s_time_lock;
static relay_time_status_t s_status = {.source = "rtc"};
static esp_err_t write_utc_to_rtc(time_t utc_seconds, const char *source)
{
    struct tm local = {0};
    if (localtime_r(&utc_seconds, &local) == NULL ||
        local.tm_year + 1900 < 2020 || local.tm_year + 1900 > 2069) {
        return ESP_ERR_INVALID_ARG;
    }
    const relay_datetime_t datetime = {
        .year = (uint16_t)(local.tm_year + 1900),
        .month = (uint8_t)(local.tm_mon + 1),
        .day = (uint8_t)local.tm_mday,
        .weekday = (uint8_t)local.tm_wday,
        .hour = (uint8_t)local.tm_hour,
        .minute = (uint8_t)local.tm_min,
        .second = (uint8_t)local.tm_sec,
    };
    xSemaphoreTake(s_time_lock, portMAX_DELAY);
    esp_err_t err = relay_rtc_set(&datetime);
    relay_datetime_t readback = {0};
    if (err == ESP_OK) {
        err = relay_rtc_read(&readback);
        if (err == ESP_OK && (!readback.calendar_valid || readback.oscillator_stopped || readback.stopped)) {
            err = ESP_ERR_INVALID_STATE;
        }
    }
    s_status.last_error = err;
    if (err == ESP_OK) {
        s_status.last_sync = utc_seconds;
        s_status.source = source;
        snprintf(s_status.last_sync_rtc, sizeof(s_status.last_sync_rtc),
                 "%04u-%02u-%02u %02u:%02u:%02u", (unsigned)readback.year,
                 readback.month, readback.day, readback.hour, readback.minute, readback.second);
    }
    xSemaphoreGive(s_time_lock);
    if (err == ESP_OK) {
        char text[80];
        relay_datetime_format(&readback, text, sizeof(text));
        ESP_LOGI(TAG, "RTC synchronized from %s, RTC readback: %s (UTC+8)", source, text);
    } else {
        ESP_LOGW(TAG, "RTC time update failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t relay_time_init(void)
{
    if (s_time_lock != NULL) {
        return ESP_OK;
    }
    if (setenv("TZ", RELAY_TIMEZONE, 1) != 0) {
        return ESP_ERR_NO_MEM;
    }
    tzset();
    s_time_lock = xSemaphoreCreateMutex();
    if (s_time_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t relay_time_set_from_browser(time_t utc_seconds)
{
    return write_utc_to_rtc(utc_seconds, "browser");
}

void relay_time_get_status(relay_time_status_t *status)
{
    xSemaphoreTake(s_time_lock, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_time_lock);
}
