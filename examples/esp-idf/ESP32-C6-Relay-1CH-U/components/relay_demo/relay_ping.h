#pragma once
#include "esp_err.h"
#include "esp_netif.h"
#include "cJSON.h"
esp_err_t relay_ping_init(void);
bool relay_ping_busy(void);
esp_err_t relay_ping_start(esp_netif_t *sta);
void relay_ping_cancel(const char *reason);
cJSON *relay_ping_json(void);
