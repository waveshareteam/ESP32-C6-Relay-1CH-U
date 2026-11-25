/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "relay_demo_internal.h"

static const char *TAG = "relay_ble";

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
    0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x4a, 0x48, 0xb5, 0xbe);

static uint8_t s_own_addr_type;
static uint16_t s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_value_handle;
static bool s_notify_enabled;

static int gap_event_handler(struct ble_gap_event *event, void *argument);
static void start_advertising(void);

static uint8_t bcd_to_decimal(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

static void notify_text(const char *text)
{
    if (!s_notify_enabled || s_connection_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    struct os_mbuf *packet = ble_hs_mbuf_from_flat(text, strlen(text));
    if (packet == NULL) {
        return;
    }
    const int rc = ble_gatts_notify_custom(s_connection_handle, s_tx_value_handle, packet);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE notification failed: %d", rc);
    }
}

void relay_ble_notify_state(bool on)
{
    notify_text(on ? "relay=on" : "relay=off");
}

static void handle_rtc_event(const uint8_t *data)
{
    if (data[0] != 0xA1 || data[6] != 0xAA || data[13] != 0xFF) {
        ESP_LOGW(TAG, "Invalid BLE RTC event frame");
        return;
    }
    relay_datetime_t datetime = {
        .year = (uint16_t)(bcd_to_decimal(data[1]) * 100U + bcd_to_decimal(data[2])),
        .month = bcd_to_decimal(data[3]),
        .day = bcd_to_decimal(data[4]),
        .weekday = bcd_to_decimal(data[5]),
        .hour = bcd_to_decimal(data[7]),
        .minute = bcd_to_decimal(data[8]),
        .second = bcd_to_decimal(data[9]),
    };
    relay_action_t action;
    if (data[11] != 0) {
        action = (data[10] & 0x01U) ? RELAY_ACTION_ON : RELAY_ACTION_OFF;
    } else {
        const uint8_t channel = data[10] >> 4;
        if (channel != 1 || (data[10] & 0x0FU) > RELAY_ACTION_ON) {
            ESP_LOGW(TAG, "BLE RTC event contains an invalid relay channel/state");
            return;
        }
        action = (relay_action_t)(data[10] & 0x0FU);
    }
    uint8_t event_id = 0;
    esp_err_t err = relay_schedule_add(
        &datetime, action, (relay_repeat_t)data[12], &event_id);
    if (err == ESP_OK) {
        char response[24];
        snprintf(response, sizeof(response), "event=%u", event_id);
        notify_text(response);
    } else {
        notify_text("event=error");
    }
}

static int tx_access(uint16_t connection_handle,
                     uint16_t attribute_handle,
                     struct ble_gatt_access_ctxt *context,
                     void *argument)
{
    (void)connection_handle;
    (void)attribute_handle;
    (void)argument;
    if (context->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const char *state = relay_get_state() ? "relay=on" : "relay=off";
    return os_mbuf_append(context->om, state, strlen(state)) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int rx_access(uint16_t connection_handle,
                     uint16_t attribute_handle,
                     struct ble_gatt_access_ctxt *context,
                     void *argument)
{
    (void)connection_handle;
    (void)attribute_handle;
    (void)argument;
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t data[32];
    uint16_t length = 0;
    if (OS_MBUF_PKTLEN(context->om) > sizeof(data) ||
        ble_hs_mbuf_to_flat(context->om, data, sizeof(data), &length) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    if (length == 1) {
        relay_handle_command_byte(data[0], "Bluetooth");
    } else if (length == 2 && data[0] == 0x06) {
        if (relay_rs485_send_extension(data[1]) != ESP_OK) {
            notify_text("rs485=error");
        }
    } else if (length == 14) {
        handle_rtc_event(data);
    } else {
        ESP_LOGW(TAG, "Unsupported BLE command length: %u", length);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return 0;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = tx_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_value_handle,
            },
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = rx_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {0},
        },
    },
    {0},
};

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set BLE advertising fields: %d", rc);
        return;
    }

    struct ble_hs_adv_fields response = {0};
    response.name = (uint8_t *)BOARD_NAME;
    response.name_len = strlen(BOARD_NAME);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set BLE scan response: %d", rc);
        return;
    }

    struct ble_gap_adv_params parameters = {0};
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &parameters, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start BLE advertising: %d", rc);
    }
}

static int gap_event_handler(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connection_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE connected");
        } else {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected");
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        start_advertising();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_value_handle) {
            s_notify_enabled = event->subscribe.cur_notify != 0;
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "Unable to determine BLE address: %d", rc);
        return;
    }
    start_advertising();
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t relay_ble_start(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_svc_gap_device_name_set(BOARD_NAME);
    if (rc == 0) {
        rc = ble_gatts_count_cfg(s_services);
    }
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_services);
    }
    if (rc != 0) {
        return ESP_FAIL;
    }
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "BLE advertising as %s", BOARD_NAME);
    return ESP_OK;
}
