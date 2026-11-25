/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "relay_demo_internal.h"
#include "relay_network.h"
#include "relay_ping.h"

#define RECONNECT_INTERVAL_US 1000000LL
#define CONNECT_TIMEOUT_US 30000000LL
#define SCAN_MAX 24
#define AP_READY BIT0
#define STA_READY BIT1
static EventGroupHandle_t s_events;
static SemaphoreHandle_t s_lock;
static wifi_ap_record_t s_scan[SCAN_MAX];
static uint16_t s_scan_count;
static unsigned s_scan_channel, s_scan_progress, s_scan_total;
static bool s_scanning, s_wanted, s_sta_started, s_connecting;
static TaskHandle_t s_reconnect_task;
static esp_err_t s_scan_error;
static esp_err_t s_last_error;
static const char *s_state = "idle";
static unsigned s_reason, s_disconnects, s_retries;
static int64_t s_connected_at, s_connect_started, s_next_retry_at;
static const char *TAG = "relay_network";

static void wake_reconnect(void)
{
    if (s_reconnect_task != NULL) xTaskNotifyGive(s_reconnect_task);
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        xEventGroupSetBits(s_events, AP_READY);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STOP) {
        xEventGroupClearBits(s_events, AP_READY);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_sta_started = true; s_next_retry_at = 0;
        xSemaphoreGive(s_lock);
        wake_reconnect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_sta_started = false; s_connecting = false; s_state = "disconnected";
        xEventGroupClearBits(s_events, STA_READY);
        xSemaphoreGive(s_lock);
        relay_ping_cancel("STA stopped during Ping");
        wake_reconnect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        xEventGroupClearBits(s_events, STA_READY);
        s_connecting = false;
        if (s_wanted) {
            s_reason = event->reason; ++s_disconnects; s_state = "connecting";
            s_next_retry_at = esp_timer_get_time() + RECONNECT_INTERVAL_US;
        } else s_state = "disconnected";
        xSemaphoreGive(s_lock);
        relay_ping_cancel("STA disconnected during Ping");
        ESP_LOGW(TAG, "STA disconnected: reason=%u; retry after 1 second", event->reason);
        wake_reconnect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool wanted = s_wanted && s_sta_started;
        if (wanted) {
            s_state = "connected"; s_reason = 0; s_retries = 0; s_last_error = ESP_OK;
            s_connected_at = esp_timer_get_time(); s_connecting = false;
            s_next_retry_at = 0;
            xEventGroupSetBits(s_events, STA_READY);
        }
        xSemaphoreGive(s_lock);
        if (wanted) esp_netif_set_default_netif(event->esp_netif);
        ESP_LOGI(TAG, "STA IP=" IPSTR, IP2STR(&event->ip_info.ip));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        xEventGroupClearBits(s_events, STA_READY);
        if (s_wanted) {
            /* Allow DHCP to recover; the worker restarts a stalled connection. */
            s_state = "connecting"; s_connecting = true;
            s_connect_started = esp_timer_get_time();
        }
        xSemaphoreGive(s_lock);
        relay_ping_cancel("STA lost IP during Ping");
        wake_reconnect();
    }
}

static void merge_scan_records(const wifi_ap_record_t *records, uint16_t count)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (unsigned n = 0; n < count; ++n) {
        if (records[n].ssid[0] == 0) continue;
        unsigned index = 0;
        while (index < s_scan_count && memcmp(s_scan[index].bssid, records[n].bssid, 6)) ++index;
        if (index == s_scan_count) {
            if (s_scan_count < SCAN_MAX) ++s_scan_count;
            else {
                index = 0;
                for (unsigned i = 1; i < SCAN_MAX; ++i) if (s_scan[i].rssi < s_scan[index].rssi) index = i;
                if (records[n].rssi <= s_scan[index].rssi) continue;
            }
        }
        s_scan[index] = records[n];
    }
    xSemaphoreGive(s_lock);
}

static void scan_task(void *arg)
{
    (void)arg;
    wifi_ap_record_t *records = calloc(SCAN_MAX, sizeof(*records));
    wifi_country_t country = {0};
    esp_err_t err = records ? esp_wifi_get_country(&country) : ESP_ERR_NO_MEM;
    unsigned first = country.schan ? country.schan : 1;
    unsigned end = country.nchan ? first + country.nchan : 12;
    if (end > 15) end = 15;
    xSemaphoreTake(s_lock, portMAX_DELAY); s_scan_total = end - first; xSemaphoreGive(s_lock);
    /* A short single-channel scan is followed by a home-channel service interval.
       Never stop Wi-Fi, disconnect STA, or change AP/STA mode just to scan. */
    for (unsigned channel = first; err == ESP_OK && channel < end; ++channel) {
        xSemaphoreTake(s_lock, portMAX_DELAY); s_scan_channel = channel; xSemaphoreGive(s_lock);
        wifi_scan_config_t config = {
            .channel = channel, .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
            .scan_time.active = {.min = 25, .max = 50}, .home_chan_dwell_time = 150,
            .coex_background_scan = true,
        };
        err = esp_wifi_scan_start(&config, true);
        uint16_t count = SCAN_MAX;
        if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);
        if (err != ESP_OK) { esp_wifi_clear_ap_list(); break; }
        merge_scan_records(records, count);
        xSemaphoreTake(s_lock, portMAX_DELAY); ++s_scan_progress; xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(180));
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan_error = err; s_scanning = false; s_scan_channel = 0;
    xSemaphoreGive(s_lock);
    free(records); vTaskDelete(NULL);
}

esp_err_t relay_network_scan(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_scanning || relay_ping_busy() || strcmp(s_state, "connecting") == 0) {
        xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE;
    }
    s_scanning = true; s_scan_count = 0; s_scan_progress = s_scan_total = s_scan_channel = 0;
    s_scan_error = ESP_OK;
    xSemaphoreGive(s_lock);
    if (xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 3, NULL) != pdPASS) {
        xSemaphoreTake(s_lock, portMAX_DELAY); s_scanning = false; xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t relay_network_ping(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_scanning && strcmp(s_state, "connected") == 0 &&
        (xEventGroupGetBits(s_events) & STA_READY) != 0) {
        err = relay_ping_start(sta);
    }
    xSemaphoreGive(s_lock);
    return err;
}

bool relay_wifi_is_connected(void)
{
    if (s_events == NULL) return false;
#if CONFIG_RELAY_DEMO_MODE_WIFI_AP
    return (xEventGroupGetBits(s_events) & AP_READY) != 0;
#else
    return (xEventGroupGetBits(s_events) & STA_READY) != 0;
#endif
}

static void add_ip(cJSON *root, const char *key, const esp_ip4_addr_t *address)
{
    char value[16]; snprintf(value, sizeof(value), IPSTR, IP2STR(address));
    cJSON_AddStringToObject(root, key, value);
}

cJSON *relay_network_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;
    cJSON_AddStringToObject(root, "bootId", relay_wifi_boot_id());
    bool connected = (xEventGroupGetBits(s_events) & STA_READY) != 0;
    wifi_config_t config = {0}; esp_wifi_get_config(WIFI_IF_STA, &config);
    char ssid[33] = {0}; memcpy(ssid, config.sta.ssid, 32);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddStringToObject(root, "state", s_state);
    cJSON_AddStringToObject(root, "error", esp_err_to_name(s_last_error));
    cJSON_AddNumberToObject(root, "reason", s_reason);
    cJSON_AddNumberToObject(root, "disconnects", s_disconnects);
    cJSON_AddNumberToObject(root, "retries", s_retries);
    cJSON_AddNumberToObject(root, "connectedSeconds", connected ? (esp_timer_get_time() - s_connected_at)/1000000 : 0);
    cJSON_AddBoolToObject(root, "scanning", s_scanning);
    cJSON_AddNumberToObject(root, "scanChannel", s_scan_channel);
    cJSON_AddNumberToObject(root, "scanProgress", s_scan_progress);
    cJSON_AddNumberToObject(root, "scanTotal", s_scan_total);
    cJSON_AddStringToObject(root, "scanError", esp_err_to_name(s_scan_error));
    cJSON *list = cJSON_AddArrayToObject(root, "networks");
    if (list == NULL) { xSemaphoreGive(s_lock); cJSON_Delete(root); return NULL; }
    for (unsigned i = 0; i < s_scan_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) { xSemaphoreGive(s_lock); cJSON_Delete(root); return NULL; }
        cJSON_AddStringToObject(item, "ssid", (char *)s_scan[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", s_scan[i].rssi);
        cJSON_AddNumberToObject(item, "channel", s_scan[i].primary);
        cJSON_AddBoolToObject(item, "open", s_scan[i].authmode == WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(list, item);
    }
    xSemaphoreGive(s_lock);
    cJSON_AddBoolToObject(root, "connected", connected);
    cJSON_AddStringToObject(root, "ssid", ssid);
    wifi_ap_record_t ap = {0};
    if (connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddNumberToObject(root, "rssi", ap.rssi);
        cJSON_AddNumberToObject(root, "channel", ap.primary);
    } else { cJSON_AddNullToObject(root, "rssi"); cJSON_AddNullToObject(root, "channel"); }
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info = {0};
    if (sta != NULL && connected) esp_netif_get_ip_info(sta, &info);
    add_ip(root, "ip", &info.ip); add_ip(root, "gateway", &info.gw); add_ip(root, "netmask", &info.netmask);
    esp_netif_dns_info_t dns = {0};
    if (sta != NULL && connected) esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns);
    char dns_text[48] = "--";
    if (connected && dns.ip.type == ESP_IPADDR_TYPE_V4) esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, dns_text, sizeof(dns_text));
    cJSON_AddStringToObject(root, "dns", dns_text);
    uint8_t mac[6] = {0}; esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_text[18]; snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X", MAC2STR(mac));
    cJSON_AddStringToObject(root, "mac", mac_text);
#if CONFIG_RELAY_DEMO_MODE_WIFI_AP
    cJSON_AddBoolToObject(root, "apEnabled", true);
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ap_info = {0}; esp_netif_get_ip_info(ap_netif, &ap_info);
    add_ip(root, "apIp", &ap_info.ip);
    cJSON_AddStringToObject(root, "apSsid", CONFIG_RELAY_DEMO_AP_SSID);
    wifi_sta_list_t stations = {0}; esp_wifi_ap_get_sta_list(&stations);
    cJSON_AddNumberToObject(root, "clients", stations.num);
#else
    cJSON_AddBoolToObject(root, "apEnabled", false);
#endif
    cJSON *ping = relay_ping_json();
    if (!ping) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(root, "ping", ping);
    return root;
}

/* Called by one worker only. State and driver operations are serialized with
   scans. A failed attempt is retried after one second without a retry limit;
   an asynchronous connect/DHCP attempt is allowed to finish before retrying. */
#if !CONFIG_RELAY_DEMO_MODE_WIFI_AP
static void reconnect_once(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (!s_wanted || !s_sta_started || s_scanning || strcmp(s_state, "connected") == 0) {
        xSemaphoreGive(s_lock); return;
    }
    if (s_connecting) {
        if (now - s_connect_started >= CONNECT_TIMEOUT_US) {
            s_connecting = false; s_last_error = ESP_ERR_TIMEOUT;
            s_next_retry_at = now + RECONNECT_INTERVAL_US;
            esp_wifi_disconnect();
            ESP_LOGW(TAG, "STA connection/IP timed out; retry after 1 second");
        }
    } else if (now >= s_next_retry_at) {
        s_connecting = true; s_connect_started = now; s_state = "connecting";
        ++s_retries;
        esp_err_t err = esp_wifi_connect();
        s_last_error = err;
        if (err != ESP_OK) {
            s_connecting = false; s_next_retry_at = now + RECONNECT_INTERVAL_US;
            ESP_LOGW(TAG, "STA connect failed: %s; retry after 1 second", esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_lock);
}

static void reconnect_task(void *arg)
{
    (void)arg;
    while (true) {
        /* Disconnect/start events wake us immediately. The deadline prevents
           notifications from reducing the one-second retry interval. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        reconnect_once();
    }
}
#endif

esp_err_t relay_network_start(void)
{
    ESP_ERROR_CHECK(relay_ping_init());
    s_events = xEventGroupCreate(); s_lock = xSemaphoreCreateMutex();
    if (s_events == NULL || s_lock == NULL) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
#if CONFIG_RELAY_DEMO_MODE_WIFI_AP
    esp_netif_create_default_wifi_ap();
#endif
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    /* Always use the SSID/password configured in the firmware. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
#if CONFIG_RELAY_DEMO_MODE_WIFI_AP
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", CONFIG_RELAY_DEMO_AP_SSID);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", CONFIG_RELAY_DEMO_AP_PASSWORD);
    ap.ap.ssid_len = strlen(CONFIG_RELAY_DEMO_AP_SSID);
    ap.ap.channel = 1; ap.ap.max_connection = 4;
    ap.ap.authmode = strlen(CONFIG_RELAY_DEMO_AP_PASSWORD) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
#else
    wifi_config_t sta = {0};
    snprintf((char *)sta.sta.ssid, sizeof(sta.sta.ssid), "%s", CONFIG_RELAY_DEMO_STA_SSID);
    snprintf((char *)sta.sta.password, sizeof(sta.sta.password), "%s", CONFIG_RELAY_DEMO_STA_PASSWORD);
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    s_wanted = true; s_state = "connecting"; s_connect_started = esp_timer_get_time();
#endif
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
#if !CONFIG_RELAY_DEMO_MODE_WIFI_AP
    if (xTaskCreate(reconnect_task, "wifi_reconnect", 3072, NULL, 3, &s_reconnect_task) != pdPASS) return ESP_ERR_NO_MEM;
#endif
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) return err;
/* ESP32-C6 supports only 2.4 GHz; no dual-band API is needed. */
    return err;
}
