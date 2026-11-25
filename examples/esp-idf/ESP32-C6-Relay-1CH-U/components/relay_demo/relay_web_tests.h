#pragma once
#include "esp_http_server.h"
esp_err_t relay_web_wifi_status(httpd_req_t *request);
esp_err_t relay_web_wifi_scan(httpd_req_t *request);
esp_err_t relay_web_wifi_ping(httpd_req_t *request);
