/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "ping/ping_sock.h"
#include "esp_log.h"
#include "relay_ping.h"

#define PING_COUNT 10
typedef struct { uint16_t sequence; bool success; uint32_t ms, ttl, size; } reply_t;
typedef struct { uint32_t generation; int interface; SemaphoreHandle_t done; } ping_job_t;
static SemaphoreHandle_t s_lock;
static uint32_t s_generation, s_sent, s_received, s_sum_ms;
static unsigned s_row_count;
static bool s_active;
static const char *s_state = "idle";
static char s_address[16], s_error[96];
static reply_t s_rows[PING_COUNT];
static const char *TAG = "relay_ping";

esp_err_t relay_ping_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

bool relay_ping_busy(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY); bool active = s_active; xSemaphoreGive(s_lock);
    return active;
}

void relay_ping_cancel(const char *reason)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active) {
        ++s_generation; s_state = "cancelled";
        snprintf(s_error, sizeof(s_error), "%s", reason);
    }
    xSemaphoreGive(s_lock);
}

static bool current(ping_job_t *job)
{
    xSemaphoreTake(s_lock, portMAX_DELAY); bool valid = job->generation == s_generation; xSemaphoreGive(s_lock);
    return valid;
}

static void on_reply(esp_ping_handle_t handle, ping_job_t *job, bool success)
{
    reply_t row = {.success = success};
    esp_ping_get_profile(handle, ESP_PING_PROF_SEQNO, &row.sequence, sizeof(row.sequence));
    esp_ping_get_profile(handle, ESP_PING_PROF_TIMEGAP, &row.ms, sizeof(row.ms));
    if (success) {
        esp_ping_get_profile(handle, ESP_PING_PROF_TTL, &row.ttl, sizeof(row.ttl));
        esp_ping_get_profile(handle, ESP_PING_PROF_SIZE, &row.size, sizeof(row.size));
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (job->generation == s_generation) {
        if (s_row_count < PING_COUNT) s_rows[s_row_count++] = row;
        s_sent = row.sequence;
        if (success) { ++s_received; s_sum_ms += row.ms; }
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "www.baidu.com seq=%u %s time=%lu ms", row.sequence, success ? "reply" : "timeout", (unsigned long)row.ms);
}
static void on_success(esp_ping_handle_t handle, void *arg) { on_reply(handle, arg, true); }
static void on_timeout(esp_ping_handle_t handle, void *arg) { on_reply(handle, arg, false); }
static void on_end(esp_ping_handle_t handle, void *arg)
{
    (void)handle; ping_job_t *job = arg; xSemaphoreGive(job->done);
}

static void ping_task(void *arg)
{
    ping_job_t *job = arg;
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *result = NULL;
    int dns_error = getaddrinfo("www.baidu.com", NULL, &hints, &result);
    esp_err_t err = ESP_OK;
    esp_ping_handle_t handle = NULL;
    if (!current(job)) goto finish;
    if (dns_error != 0 || result == NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (job->generation == s_generation) {
            s_state = "failed"; snprintf(s_error, sizeof(s_error), "DNS resolution failed (%d)", dns_error);
        }
        xSemaphoreGive(s_lock); goto finish;
    }
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)result->ai_addr;
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = PING_COUNT; config.interval_ms = 1000; config.timeout_ms = 1000;
    config.task_stack_size = 4096; config.interface = job->interface;
    ip_addr_set_ip4_u32(&config.target_addr, ipv4->sin_addr.s_addr);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (job->generation == s_generation) {
        inet_ntop(AF_INET, &ipv4->sin_addr, s_address, sizeof(s_address)); s_state = "running";
    }
    xSemaphoreGive(s_lock);
    if (!current(job)) goto finish;
    const esp_ping_callbacks_t callbacks = {.cb_args = job, .on_ping_success = on_success, .on_ping_timeout = on_timeout, .on_ping_end = on_end};
    err = esp_ping_new_session(&config, &callbacks, &handle);
    if (err == ESP_OK) err = esp_ping_start(handle);
    if (err == ESP_OK) {
        while (xSemaphoreTake(job->done, pdMS_TO_TICKS(100)) != pdTRUE) {
            if (!current(job)) esp_ping_stop(handle);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (job->generation == s_generation) s_state = "complete";
        xSemaphoreGive(s_lock);
    } else {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (job->generation == s_generation) { s_state = "failed"; snprintf(s_error, sizeof(s_error), "%s", esp_err_to_name(err)); }
        xSemaphoreGive(s_lock);
    }
finish:
    if (handle) esp_ping_delete_session(handle);
    if (result) freeaddrinfo(result);
    vSemaphoreDelete(job->done); free(job);
    xSemaphoreTake(s_lock, portMAX_DELAY); s_active = false; xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

esp_err_t relay_ping_start(esp_netif_t *sta)
{
    if (sta == NULL || !esp_netif_is_netif_up(sta)) return ESP_ERR_INVALID_STATE;
    int interface = esp_netif_get_netif_impl_index(sta);
    if (interface <= 0) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active) { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE; }
    ping_job_t *job = calloc(1, sizeof(*job));
    if (job) job->done = xSemaphoreCreateBinary();
    if (job == NULL || job->done == NULL) {
        if (job) free(job);
        xSemaphoreGive(s_lock); return ESP_ERR_NO_MEM;
    }
    job->generation = ++s_generation; job->interface = interface;
    s_sent = s_received = s_sum_ms = s_row_count = 0;
    s_address[0] = s_error[0] = '\0'; memset(s_rows, 0, sizeof(s_rows));
    s_active = true; s_state = "resolving";
    if (xTaskCreate(ping_task, "baidu_test", 4096, job, 3, NULL) != pdPASS) {
        vSemaphoreDelete(job->done); free(job);
        s_active = false; s_state = "failed";
        snprintf(s_error, sizeof(s_error), "ESP_ERR_NO_MEM");
        xSemaphoreGive(s_lock); return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

cJSON *relay_ping_json(void)
{
    cJSON *root = cJSON_CreateObject(); if (root == NULL) return NULL;
    cJSON *rows = cJSON_AddArrayToObject(root, "rows"); if (rows == NULL) { cJSON_Delete(root); return NULL; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddStringToObject(root, "state", s_state); cJSON_AddBoolToObject(root, "busy", s_active);
    cJSON_AddStringToObject(root, "host", "www.baidu.com"); cJSON_AddStringToObject(root, "ip", s_address);
    cJSON_AddStringToObject(root, "error", s_error);
    cJSON_AddNumberToObject(root, "total", PING_COUNT); cJSON_AddNumberToObject(root, "sent", s_sent);
    cJSON_AddNumberToObject(root, "received", s_received);
    cJSON_AddNumberToObject(root, "loss", s_sent ? (s_sent - s_received) * 100.0 / s_sent : 0);
    if (s_received) cJSON_AddNumberToObject(root, "averageMs", (double)s_sum_ms / s_received);
    else cJSON_AddNullToObject(root, "averageMs");
    for (unsigned i = 0; i < s_row_count; ++i) {
        cJSON *row = cJSON_CreateObject(); if (!row) continue;
        cJSON_AddNumberToObject(row, "seq", s_rows[i].sequence); cJSON_AddBoolToObject(row, "success", s_rows[i].success);
        cJSON_AddNumberToObject(row, "ms", s_rows[i].ms); cJSON_AddNumberToObject(row, "ttl", s_rows[i].ttl); cJSON_AddNumberToObject(row, "bytes", s_rows[i].size);
        cJSON_AddItemToArray(rows, row);
    }
    xSemaphoreGive(s_lock); return root;
}
