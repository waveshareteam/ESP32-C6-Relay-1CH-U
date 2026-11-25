/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "relay_time.h"
#include "esp_random.h"
#include "relay_network.h"
#include "relay_web_tests.h"

#include "relay_demo_internal.h"

static const char *TAG = "relay_wifi";
#define RELAY_WEB_UI_VERSION "20261028-c6-r2"
static char s_boot_id[9];
const char *relay_wifi_boot_id(void) { return s_boot_id; }
static httpd_handle_t s_http_server;

extern const unsigned char index_start[] asm("_binary_index_html_start");
extern const unsigned char index_end[] asm("_binary_index_html_end");
extern const unsigned char css_start[] asm("_binary_style_css_start");
extern const unsigned char css_end[] asm("_binary_style_css_end");
extern const unsigned char js_start[] asm("_binary_app_js_start");
extern const unsigned char js_end[] asm("_binary_app_js_end");

static esp_err_t send_asset(httpd_req_t *request, const char *type,
                             const unsigned char *start, const unsigned char *end)
{
    httpd_resp_set_type(request, type);
    httpd_resp_set_hdr(request, "X-Relay-UI", RELAY_WEB_UI_VERSION);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store, max-age=0");
    return httpd_resp_send(request, (const char *)start, (ssize_t)(end - start - 1));
}

static esp_err_t css_handler(httpd_req_t *request)
{
    return send_asset(request, "text/css; charset=utf-8", css_start, css_end);
}

static esp_err_t js_handler(httpd_req_t *request)
{
    return send_asset(request, "application/javascript; charset=utf-8", js_start, js_end);
}

static esp_err_t send_json(httpd_req_t *request, cJSON *object)
{
    if (object == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    char *json = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    if (json == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return err;
}

static esp_err_t status_handler(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return send_json(request, NULL);
    }
    const bool connected = relay_wifi_is_connected();
    cJSON_AddStringToObject(root, "board", BOARD_NAME);
    cJSON_AddStringToObject(root, "uiVersion", RELAY_WEB_UI_VERSION);
    cJSON_AddStringToObject(root, "bootId", s_boot_id);
    cJSON_AddBoolToObject(root, "relay", relay_get_state());
    cJSON_AddBoolToObject(root, "connected", connected);
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);
    char ssid[33] = {0};
    const char *if_key;
#if CONFIG_RELAY_DEMO_MODE_WIFI_AP
    cJSON_AddStringToObject(root, "mode", "AP");
    if_key = "WIFI_AP_DEF";
    wifi_config_t wifi = {0};
    if (esp_wifi_get_config(WIFI_IF_AP, &wifi) == ESP_OK) {
        memcpy(ssid, wifi.ap.ssid, 32);
    }
    wifi_sta_list_t stations = {0};
    esp_wifi_ap_get_sta_list(&stations);
    cJSON_AddNumberToObject(root, "clients", stations.num);
    cJSON_AddNullToObject(root, "rssi");
#else
    cJSON_AddStringToObject(root, "mode", "STA");
    if_key = "WIFI_STA_DEF";
    wifi_config_t wifi = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi) == ESP_OK) {
        memcpy(ssid, wifi.sta.ssid, 32);
    }
    wifi_ap_record_t ap = {0};
    if (connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddNumberToObject(root, "rssi", ap.rssi);
    } else {
        cJSON_AddNullToObject(root, "rssi");
    }
    cJSON_AddNumberToObject(root, "clients", 0);
#endif
    cJSON_AddStringToObject(root, "ssid", ssid);
    char ip[16] = "--";
    esp_netif_ip_info_t info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(if_key);
    if (netif != NULL && connected && esp_netif_get_ip_info(netif, &info) == ESP_OK) {
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    }
    cJSON_AddStringToObject(root, "ip", ip);
    relay_datetime_t rtc = {0};
    const esp_err_t rtc_error = relay_rtc_read(&rtc);
    const bool rtc_read_ok = rtc_error == ESP_OK;
    const bool rtc_valid = rtc_read_ok && rtc.calendar_valid && !rtc.oscillator_stopped && !rtc.stopped;
    char rtc_text[32] = "";
    if (rtc_read_ok) {
        snprintf(rtc_text, sizeof(rtc_text), "%04u-%02u-%02uT%02u:%02u:%02u",
                 (unsigned)rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
    }
    cJSON_AddBoolToObject(root, "rtcReadOk", rtc_read_ok);
    cJSON_AddStringToObject(root, "rtcStartupAction", relay_rtc_startup_action());
    cJSON_AddStringToObject(root, "rtcReadError", esp_err_to_name(rtc_error));
    cJSON_AddBoolToObject(root, "rtcCalendarValid", rtc_read_ok && rtc.calendar_valid);
    cJSON_AddBoolToObject(root, "rtcOscillatorStopped", rtc.oscillator_stopped);
    cJSON_AddBoolToObject(root, "rtcStopped", rtc.stopped);
    cJSON_AddBoolToObject(root, "rtcValid", rtc_valid);
    cJSON_AddStringToObject(root, "rtc", rtc_text);
    cJSON_AddNumberToObject(root, "rtcWeekday", rtc.weekday);
    char registers[33] = "";
    if (rtc_read_ok) {
        for (unsigned i = 0; i < sizeof(rtc.registers); ++i) {
            snprintf(registers + i * 3, sizeof(registers) - i * 3, "%02X%s", rtc.registers[i], i == 10 ? "" : " ");
        }
    }
    cJSON_AddStringToObject(root, "rtcRegisters", registers);
    cJSON_AddBoolToObject(root, "autoSync", false);
    relay_time_status_t clock;
    relay_time_get_status(&clock);
    cJSON_AddStringToObject(root, "timeSource", clock.source);
    cJSON_AddStringToObject(root, "timeError", esp_err_to_name(clock.last_error));
    cJSON_AddStringToObject(root, "lastSync", clock.last_sync_rtc);
    struct tm local = {0};
    time_t epoch = time(NULL);
    localtime_r(&epoch, &local);
    char offset[8];
    strftime(offset, sizeof(offset), "%z", &local);
    cJSON_AddStringToObject(root, "utcOffset", offset);
    return send_json(request, root);
}

static esp_err_t browser_time_handler(httpd_req_t *request)
{
    char body[32];
    if (request->content_len <= 0 || request->content_len >= sizeof(body)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected UTC epoch seconds");
    }
    size_t used = 0;
    while (used < request->content_len) {
        int count = httpd_req_recv(request, body + used, request->content_len - used);
        if (count <= 0) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete time request");
        }
        used += (size_t)count;
    }
    body[used] = '\0';
    for (size_t i = 0; i < used; ++i) {
        if (body[i] < '0' || body[i] > '9') {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected UTC epoch seconds");
        }
    }
    char *end;
    errno = 0;
    const long long epoch = strtoll(body, &end, 10);
    if (errno != 0 || *end != '\0' || epoch < 1577836800LL || epoch > 3155759999LL) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Supported years: 2020..2069");
    }
    const esp_err_t err = relay_time_set_from_browser((time_t)epoch);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, err == ESP_ERR_INVALID_ARG ? HTTPD_400_BAD_REQUEST :
                                   HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    cJSON *root = cJSON_CreateObject();
    if (root != NULL) {
        cJSON_AddBoolToObject(root, "ok", true);
    }
    return send_json(request, root);
}

static esp_err_t send_text(httpd_req_t *request, const char *text)
{
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, text);
}

static esp_err_t root_handler(httpd_req_t *request)
{
    return send_asset(request, "text/html; charset=utf-8", index_start, index_end);
}

static esp_err_t state_handler(httpd_req_t *request)
{
    return send_text(request, relay_get_state() ? "[1]" : "[0]");
}

static esp_err_t toggle_handler(httpd_req_t *request)
{
    relay_toggle_state("WiFi");
    return send_text(request, "OK");
}

static esp_err_t relay_on_handler(httpd_req_t *request)
{
    relay_set_state(true, "WiFi");
    return send_text(request, "OK");
}

static esp_err_t relay_off_handler(httpd_req_t *request)
{
    relay_set_state(false, "WiFi");
    return send_text(request, "OK");
}

static void percent_decode(char *text)
{
    char *source = text;
    char *destination = text;
    while (*source != '\0') {
        if (*source == '+') {
            *destination++ = ' ';
            ++source;
        } else if (*source == '%' && source[1] != '\0' && source[2] != '\0') {
            unsigned value;
            if (sscanf(source + 1, "%2x", &value) == 1) {
                *destination++ = (char)value;
                source += 3;
            } else {
                *destination++ = *source++;
            }
        } else {
            *destination++ = *source++;
        }
    }
    *destination = '\0';
}

static esp_err_t read_query_value(httpd_req_t *request,
                                  const char *key,
                                  char *value,
                                  size_t value_size)
{
    const size_t query_length = httpd_req_get_url_query_len(request);
    if (query_length == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    char *query = malloc(query_length + 1);
    if (query == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = httpd_req_get_url_query_str(request, query, query_length + 1);
    if (err == ESP_OK) {
        err = httpd_query_key_value(query, key, value, value_size);
    }
    free(query);
    if (err == ESP_OK) {
        percent_decode(value);
    }
    return err;
}

static esp_err_t new_event_handler(httpd_req_t *request)
{
    char text[256] = {0};
    if (read_query_value(request, "data", text, sizeof(text)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return send_text(request, "data argument is required");
    }

    int year, month, day, weekday, hour, minute, second, action_value, repeat_value;
    int end = 0;
    const int parsed = sscanf(text,
        "Date: %d/%d/%d Week: %d Time: %d:%d:%d Relay CH1: %d Cycle: %d%n",
        &year, &month, &day, &weekday, &hour, &minute, &second,
        &action_value, &repeat_value, &end);
    if (parsed != 9 || text[end] != '\0' || year < 1970 || year > 2069 ||
        month < 1 || month > 12 || day < 1 || day > 31 || weekday < 0 || weekday > 6 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59 ||
        action_value < RELAY_ACTION_OFF || action_value > RELAY_ACTION_RETAIN ||
        repeat_value < RELAY_REPEAT_NONE || repeat_value > RELAY_REPEAT_MONTHLY) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid event date/time/action");
    }
    relay_datetime_t datetime = {
        .year = (uint16_t)year, .month = (uint8_t)month, .day = (uint8_t)day,
        .weekday = (uint8_t)weekday, .hour = (uint8_t)hour,
        .minute = (uint8_t)minute, .second = (uint8_t)second,
    };
    const relay_action_t action = (relay_action_t)action_value;
    const relay_repeat_t repeat = (relay_repeat_t)repeat_value;
    uint8_t event_id = 0;
    esp_err_t err = relay_schedule_add(&datetime, action, repeat, &event_id);
    if (err != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return send_text(request, esp_err_to_name(err));
    }
    char response[32];
    snprintf(response, sizeof(response), "Event %u added", event_id);
    return send_text(request, response);
}

static esp_err_t event_list_handler(httpd_req_t *request)
{
    char *json = malloc(4096);
    if (json == NULL || relay_schedule_json(json, 4096) == 0) {
        free(json);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to list events");
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, json);
    free(json);
    return err;
}

static esp_err_t delete_event_handler(httpd_req_t *request)
{
    char id_text[8] = {0};
    if (read_query_value(request, "id", id_text, sizeof(id_text)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return send_text(request, "id argument is required");
    }
    char *end;
    errno = 0;
    long id = strtol(id_text, &end, 10);
    if (errno != 0 || end == id_text || *end != '\0' || id < 1 || id > 10) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid event ID");
    }
    const uint8_t event_id = (uint8_t)id;
    esp_err_t err = relay_schedule_delete(event_id);
    if (err != ESP_OK) {
        httpd_resp_set_status(request, "404 Not Found");
        return send_text(request, esp_err_to_name(err));
    }
    return send_text(request, "OK");
}

static esp_err_t start_http_server(void)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/style.css", .method = HTTP_GET, .handler = css_handler},
        {.uri = "/app.js", .method = HTTP_GET, .handler = js_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/time", .method = HTTP_POST, .handler = browser_time_handler},
        {.uri = "/getData", .method = HTTP_GET, .handler = state_handler},
        {.uri = "/Switch1", .method = HTTP_GET, .handler = toggle_handler},
        {.uri = "/RelayOn", .method = HTTP_GET, .handler = relay_on_handler},
        {.uri = "/RelayOff", .method = HTTP_GET, .handler = relay_off_handler},
        {.uri = "/RTC_Event", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/WiFi_Test", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/api/wifi", .method = HTTP_GET, .handler = relay_web_wifi_status},
        {.uri = "/api/wifi/scan", .method = HTTP_POST, .handler = relay_web_wifi_scan},
        {.uri = "/api/wifi/ping", .method = HTTP_POST, .handler = relay_web_wifi_ping},
        {.uri = "/NewEvent", .method = HTTP_GET, .handler = new_event_handler},
        {.uri = "/getTimeAndEvent", .method = HTTP_GET, .handler = event_list_handler},
        {.uri = "/DeleteEvent", .method = HTTP_GET, .handler = delete_event_handler},
    };
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = sizeof(handlers) / sizeof(handlers[0]);
    config.stack_size = 8192;
    /* Leave sockets available for DNS and reclaim idle browser connections. */
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_http_server, &config);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_http_server, &handlers[i]));
    }
    ESP_LOGI(TAG, "Web UI: Relay Studio %s; HTTP server started", RELAY_WEB_UI_VERSION);
    return ESP_OK;
}

esp_err_t relay_wifi_start(void)
{
    snprintf(s_boot_id, sizeof(s_boot_id), "%08lx", (unsigned long)esp_random());
    ESP_LOGI(TAG, "Firmware UI %s; boot %s", RELAY_WEB_UI_VERSION, s_boot_id);
    ESP_ERROR_CHECK(relay_time_init());
    ESP_ERROR_CHECK(relay_network_start());
    return start_http_server();
}
