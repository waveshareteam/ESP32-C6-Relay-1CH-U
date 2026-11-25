#pragma once
#include "esp_err.h"
#include "cJSON.h"
esp_err_t relay_network_start(void);
esp_err_t relay_network_scan(void);
esp_err_t relay_network_ping(void);
cJSON *relay_network_json(void);
