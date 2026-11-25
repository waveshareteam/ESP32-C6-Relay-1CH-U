#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

typedef struct {
    time_t last_sync;
    char last_sync_rtc[32];
    esp_err_t last_error;
    const char *source;
} relay_time_status_t;

esp_err_t relay_time_init(void);
esp_err_t relay_time_set_from_browser(time_t utc_seconds);
void relay_time_get_status(relay_time_status_t *status);
