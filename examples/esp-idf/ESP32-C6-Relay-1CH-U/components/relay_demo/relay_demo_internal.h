#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "board_config.h"

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool oscillator_stopped;
    bool stopped;
    bool calendar_valid;
    uint8_t registers[11];
} relay_datetime_t;

typedef enum {
    RELAY_ACTION_OFF = 0,
    RELAY_ACTION_ON = 1,
    RELAY_ACTION_RETAIN = 2,
} relay_action_t;

typedef enum {
    RELAY_REPEAT_NONE = 0,
    RELAY_REPEAT_DAILY = 1,
    RELAY_REPEAT_WEEKLY = 2,
    RELAY_REPEAT_MONTHLY = 3,
} relay_repeat_t;

bool relay_get_state(void);
void relay_set_state(bool on, const char *source);
void relay_toggle_state(const char *source);
void relay_handle_command_byte(uint8_t command, const char *source);

esp_err_t relay_rtc_read(relay_datetime_t *datetime);
esp_err_t relay_rtc_set(const relay_datetime_t *datetime);
const char *relay_rtc_startup_action(void);
void relay_datetime_format(const relay_datetime_t *datetime, char *buffer, size_t size);

esp_err_t relay_schedule_add(const relay_datetime_t *datetime,
                             relay_action_t action,
                             relay_repeat_t repeat,
                             uint8_t *event_id);
esp_err_t relay_schedule_delete(uint8_t event_id);
size_t relay_schedule_json(char *buffer, size_t size);

esp_err_t relay_rs485_send_extension(uint8_t command);

esp_err_t relay_wifi_start(void);
bool relay_wifi_is_connected(void);
const char *relay_wifi_boot_id(void);
esp_err_t relay_ble_start(void);
void relay_ble_notify_state(bool on);
