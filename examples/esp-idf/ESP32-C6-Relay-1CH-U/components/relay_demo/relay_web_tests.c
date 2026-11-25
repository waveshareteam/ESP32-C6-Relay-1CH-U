/* SPDX-License-Identifier: Apache-2.0 */
#include "relay_web_tests.h"
#include "relay_network.h"

static esp_err_t reply(httpd_req_t *request, cJSON *root)
{
    if (root == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, body);
    cJSON_free(body);
    return err;
}

static esp_err_t operation_reply(httpd_req_t *request, esp_err_t err)
{
    if (err != ESP_OK) return httpd_resp_send_err(request,
        err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_STATE ? HTTPD_400_BAD_REQUEST : HTTPD_500_INTERNAL_SERVER_ERROR,
        esp_err_to_name(err));
    cJSON *root = cJSON_CreateObject();
    if (root != NULL) cJSON_AddBoolToObject(root, "ok", true);
    return reply(request, root);
}

esp_err_t relay_web_wifi_status(httpd_req_t *request) { return reply(request, relay_network_json()); }
esp_err_t relay_web_wifi_scan(httpd_req_t *request) { return operation_reply(request, relay_network_scan()); }
esp_err_t relay_web_wifi_ping(httpd_req_t *request) { return operation_reply(request, relay_network_ping()); }
