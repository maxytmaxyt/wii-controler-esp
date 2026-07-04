/*
 * wiimote_bt.c
 *
 * Core Bluetooth GAP + authentication logic for the Wii Remote emulator.
 *
 * Phase 2 additions: Core button reporting (report 0x30), including D-Pad.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "nvs_flash.h"

#include "wiimote_bt.h"
#include "wiimote_sdp.h"
#include "wiimote_linkkey.h"
#include "wiimote_l2cap.h"

static const char *TAG = "wiimote_bt";

/* -----------------------------------------------------------------------
 * State
 * ----------------------------------------------------------------------- */
static wiimote_state_t  s_state         = WIIMOTE_STATE_IDLE;
static esp_bd_addr_t    s_wii_bda       = {0};
static bool             s_wii_bda_known = false;

/*
 * Reporting mode the Wii has requested.
 * 0x30 = Core Buttons only (our default when not set by Wii).
 * We track it so we can send the right report format.
 */
static uint8_t s_report_mode = 0x30;

#define NVS_KEY_WII_BDA  "wii_bda"

/* -----------------------------------------------------------------------
 * Forward declarations
 * ----------------------------------------------------------------------- */
static void gap_event_handler(esp_bt_gap_cb_event_t event,
                              esp_bt_gap_cb_param_t *param);
static void output_report_handler(const uint8_t *data, uint16_t len);
static void send_status_report(void);
static esp_err_t load_wii_bda(void);
static esp_err_t save_wii_bda(const esp_bd_addr_t bda);

/* -----------------------------------------------------------------------
 * GAP Event Handler
 * ----------------------------------------------------------------------- */
static void gap_event_handler(esp_bt_gap_cb_event_t event,
                              esp_bt_gap_cb_param_t *param)
{
    switch (event) {

    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Auth complete: %02X:%02X:%02X:%02X:%02X:%02X",
                     param->auth_cmpl.bda[0], param->auth_cmpl.bda[1],
                     param->auth_cmpl.bda[2], param->auth_cmpl.bda[3],
                     param->auth_cmpl.bda[4], param->auth_cmpl.bda[5]);

            memcpy(s_wii_bda, param->auth_cmpl.bda, sizeof(esp_bd_addr_t));
            s_wii_bda_known = true;
            save_wii_bda(s_wii_bda);

            s_state = WIIMOTE_STATE_CONNECTED;
            ESP_LOGI(TAG, "Authentication successful — state: CONNECTED");
        } else {
            ESP_LOGE(TAG, "Authentication FAILED: status=%d",
                     param->auth_cmpl.stat);
            s_state = WIIMOTE_STATE_IDLE;
        }
        break;
    }

    case ESP_BT_GAP_PIN_REQ_EVT: {
        ESP_LOGI(TAG, "PIN request from %02X:%02X:%02X:%02X:%02X:%02X",
                 param->pin_req.bda[0], param->pin_req.bda[1],
                 param->pin_req.bda[2], param->pin_req.bda[3],
                 param->pin_req.bda[4], param->pin_req.bda[5]);

        const uint8_t *own_bda = esp_bt_dev_get_address();
        uint8_t pin[6];
        for (int i = 0; i < 6; i++) {
            pin[i] = own_bda[5 - i];
        }
        ESP_LOGI(TAG, "Responding with PIN (reversed BDA): "
                 "%02X %02X %02X %02X %02X %02X",
                 pin[0], pin[1], pin[2], pin[3], pin[4], pin[5]);

        esp_bt_pin_code_t pin_code;
        memcpy(pin_code, pin, 6);
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 6, pin_code);

        s_state = WIIMOTE_STATE_AUTHENTICATING;
        break;
    }

    case ESP_BT_GAP_LINK_KEY_NOTIF_EVT: {
        ESP_LOGI(TAG, "Link key notification from "
                 "%02X:%02X:%02X:%02X:%02X:%02X  type=%d",
                 param->link_key_notif.bda[0], param->link_key_notif.bda[1],
                 param->link_key_notif.bda[2], param->link_key_notif.bda[3],
                 param->link_key_notif.bda[4], param->link_key_notif.bda[5],
                 param->link_key_notif.key_type);

        wiimote_linkkey_store(param->link_key_notif.bda,
                              param->link_key_notif.link_key,
                              (uint8_t)param->link_key_notif.key_type);
        break;
    }

    case ESP_BT_GAP_LINK_KEY_REQ_EVT: {
        ESP_LOGI(TAG, "Link key request from "
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 param->link_key_req.bda[0], param->link_key_req.bda[1],
                 param->link_key_req.bda[2], param->link_key_req.bda[3],
                 param->link_key_req.bda[4], param->link_key_req.bda[5]);

        uint8_t key[16];
        uint8_t key_type;
        if (wiimote_linkkey_find(param->link_key_req.bda, key, &key_type)) {
            ESP_LOGI(TAG, "Found link key — sending to Wii");
            esp_bt_gap_set_link_key(param->link_key_req.bda, key, key_type);
        } else {
            ESP_LOGW(TAG, "No link key found — negative reply");
            /*
             * Correct negative reply: pass the BDA with a zeroed key buffer
             * and key_type = 0xFF to signal "no key".
             * The Bluedroid stack interprets key_type >= 0x10 as invalid
             * and will respond with a negative key reply (HCI_LK_NEG_REPLY).
             */
            uint8_t zero_key[16] = {0};
            esp_bt_gap_set_link_key(param->link_key_req.bda,
                                    zero_key, 0xFF);
        }
        break;
    }

    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        break;

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGD(TAG, "BT mode changed: mode=%d", param->mode_chg.mode);
        break;

    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        ESP_LOGI(TAG, "ACL connection complete: status=%d",
                 param->acl_conn_cmpl_stat.stat);
        if (param->acl_conn_cmpl_stat.stat == ESP_BT_STATUS_SUCCESS) {
            s_state = WIIMOTE_STATE_AUTHENTICATING;
        }
        break;

    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        ESP_LOGI(TAG, "ACL disconnected: status=%d reason=%d",
                 param->acl_disconn_cmpl_stat.stat,
                 param->acl_disconn_cmpl_stat.reason);
        s_state = WIIMOTE_STATE_IDLE;
        /* Reset report mode to default on disconnect */
        s_report_mode = 0x30;
        break;

    default:
        ESP_LOGD(TAG, "Unhandled GAP event: %d", event);
        break;
    }
}

/* -----------------------------------------------------------------------
 * Output report handler — called when the Wii sends us a report
 * ----------------------------------------------------------------------- */
static void output_report_handler(const uint8_t *data, uint16_t len) {
    if (len < 1) return;

    uint8_t report_id = data[0];

    ESP_LOGD(TAG, "Output report 0x%02X len=%d", report_id, len);

    switch (report_id) {
    case WIIMOTE_OUT_STATUS_REQ:
        /*
         * 0x15: Status request — must respond within ~1 second or Wii
         * disconnects the controller.
         */
        ESP_LOGI(TAG, "Status request received → sending status report");
        send_status_report();
        break;

    case WIIMOTE_OUT_LED:
        ESP_LOGD(TAG, "LED report: 0x%02X", (len > 1) ? data[1] : 0);
        break;

    case WIIMOTE_OUT_DATA_MODE:
        /*
         * 0x12: Data reporting mode
         * Byte 1: flags (bit 2 = continuous)
         * Byte 2: report mode ID (e.g. 0x30 = core buttons)
         *
         * Store the requested mode and send an ACK (0x22).
         * If we don't ACK, the Wii may resend or disconnect.
         */
        if (len >= 3) {
            s_report_mode = data[2];
            ESP_LOGI(TAG, "Data mode set: continuous=%d mode=0x%02X",
                     (data[1] & 0x04) >> 2, s_report_mode);
        }
        /* Send ACK for 0x12 */
        {
            uint8_t ack[4] = {
                0x00, 0x00,          /* Core buttons: none */
                WIIMOTE_OUT_DATA_MODE, /* report ID being ACKed */
                0x00,                /* error code 0 = success */
            };
            wiimote_l2cap_send_input(WIIMOTE_IN_ACK, ack, sizeof(ack));
        }
        break;

    case WIIMOTE_OUT_RUMBLE:
        ESP_LOGD(TAG, "Rumble: %s",
                 (len > 1 && (data[1] & 0x01)) ? "ON" : "OFF");
        break;

    default:
        ESP_LOGD(TAG, "Unhandled output report 0x%02X", report_id);
        break;
    }
}

/* -----------------------------------------------------------------------
 * Send status report 0x20 to the Wii
 * ----------------------------------------------------------------------- */
static void send_status_report(void) {
    uint8_t status[6] = {
        0x00, 0x00,           /* Core buttons: none pressed */
        STATUS_FLAG_LED1,     /* LED1 on, battery OK */
        0x00,
        0x00,
        0x80,                 /* Battery level: full */
    };

    esp_err_t ret = wiimote_l2cap_send_input(WIIMOTE_IN_STATUS,
                                              status, sizeof(status));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send status report: %s",
                 esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Status report sent");
    }
}

/* -----------------------------------------------------------------------
 * Public: Send core button report (Phase 2 — D-Pad + buttons)
 *
 * Report 0x30: Core Buttons (2 bytes)
 *   Byte 0: D-Pad + Plus  (BTN_BYTE0_*)
 *   Byte 1: A/B/1/2/Minus/Home  (BTN_BYTE1_*)
 *
 * The Wii expects to receive this report continuously in "continuous" mode
 * (set via 0x12 data mode). In non-continuous mode, send on state change.
 * We always send on state change regardless; the Wii handles both.
 * ----------------------------------------------------------------------- */
esp_err_t wiimote_bt_send_buttons(uint8_t btn_byte0, uint8_t btn_byte1) {
    if (s_state != WIIMOTE_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * We always send report 0x30 (Core Buttons) regardless of
     * s_report_mode because it is the baseline format. If the Wii
     * requests 0x31 (buttons + accelerometer) we would need to pad
     * with 3 accelerometer bytes — Phase 3 concern.
     */
    uint8_t payload[2] = { btn_byte0, btn_byte1 };

    esp_err_t ret = wiimote_l2cap_send_input(WIIMOTE_IN_BUTTONS,
                                              payload, sizeof(payload));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "send_buttons failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* -----------------------------------------------------------------------
 * NVS: persist the Wii console BDA
 * ----------------------------------------------------------------------- */
static nvs_handle_t s_nvs;

static esp_err_t load_wii_bda(void) {
    size_t len = sizeof(esp_bd_addr_t);
    esp_err_t ret = nvs_get_blob(s_nvs, NVS_KEY_WII_BDA, s_wii_bda, &len);
    if (ret == ESP_OK && len == sizeof(esp_bd_addr_t)) {
        s_wii_bda_known = true;
        ESP_LOGI(TAG, "Loaded Wii BDA: %02X:%02X:%02X:%02X:%02X:%02X",
                 s_wii_bda[0], s_wii_bda[1], s_wii_bda[2],
                 s_wii_bda[3], s_wii_bda[4], s_wii_bda[5]);
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No Wii BDA stored yet");
        ret = ESP_OK;
    }
    return ret;
}

static esp_err_t save_wii_bda(const esp_bd_addr_t bda) {
    esp_err_t ret = nvs_set_blob(s_nvs, NVS_KEY_WII_BDA,
                                  bda, sizeof(esp_bd_addr_t));
    if (ret == ESP_OK) ret = nvs_commit(s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Wii BDA: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Saved Wii BDA to NVS");
    }
    return ret;
}

/* -----------------------------------------------------------------------
 * Auto-reconnect task
 * ----------------------------------------------------------------------- */
static void reconnect_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (!s_wii_bda_known) {
        ESP_LOGI(TAG, "No Wii BDA — skipping auto-reconnect");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting auto-reconnect to Wii "
             "%02X:%02X:%02X:%02X:%02X:%02X",
             s_wii_bda[0], s_wii_bda[1], s_wii_bda[2],
             s_wii_bda[3], s_wii_bda[4], s_wii_bda[5]);

    s_state = WIIMOTE_STATE_RECONNECTING;

    for (int attempt = 0; attempt < 10; attempt++) {
        if (s_state == WIIMOTE_STATE_CONNECTED) break;
        ESP_LOGI(TAG, "Reconnect attempt %d/10...", attempt + 1);
        wiimote_l2cap_connect(s_wii_bda);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    if (s_state != WIIMOTE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Auto-reconnect failed — going discoverable");
        wiimote_bt_start_discoverable();
    }

    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * Public API: wiimote_bt_init
 * ----------------------------------------------------------------------- */
esp_err_t wiimote_bt_init(void) {
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = wiimote_linkkey_init();
    if (ret != ESP_OK) return ret;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_dev_set_device_name(WIIMOTE_DEVICE_NAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /*
     * Set Class of Device: 0x002504
     * esp_bt_cod_t is a struct — use designated initialiser (C99/C11).
     * We declare it as a variable first to avoid compound-literal
     * compatibility issues with some IDF versions.
     */
    {
        esp_bt_cod_t cod;
        memset(&cod, 0, sizeof(cod));
        cod.major   = WIIMOTE_COD_MAJOR;
        cod.minor   = WIIMOTE_COD_MINOR;
        cod.service = 0;
        ret = esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Set CoD failed (non-fatal): %s",
                     esp_err_to_name(ret));
        }
    }

    /* IO capability: none → legacy pairing (PIN-based) */
    {
        esp_bt_sp_param_t sp_param = ESP_BT_SP_IOCAP_MODE;
        uint8_t iocap = ESP_BT_IO_CAP_NONE;
        esp_bt_gap_set_security_param(sp_param, &iocap, sizeof(iocap));
    }

    ret = wiimote_l2cap_init(output_report_handler);
    if (ret != ESP_OK) return ret;

    ret = wiimote_sdp_register();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SDP register returned: %s — continuing",
                 esp_err_to_name(ret));
    }

    load_wii_bda();

    ESP_LOGI(TAG, "Wiimote BT stack initialised");
    ESP_LOGI(TAG, "Device name : %s", WIIMOTE_DEVICE_NAME);
    {
        const uint8_t *bda = esp_bt_dev_get_address();
        ESP_LOGI(TAG, "Own BDA     : %02X:%02X:%02X:%02X:%02X:%02X",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    }

    if (s_wii_bda_known) {
        xTaskCreate(reconnect_task, "wii_reconnect", 4096, NULL, 5, NULL);
    } else {
        wiimote_bt_start_discoverable();
    }

    s_state = WIIMOTE_STATE_IDLE;
    return ESP_OK;
}

void wiimote_bt_start_discoverable(void) {
    ESP_LOGI(TAG, "Going discoverable (SYNC mode)...");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    s_state = WIIMOTE_STATE_DISCOVERABLE;
}

void wiimote_bt_stop_discoverable(void) {
    ESP_LOGI(TAG, "Stopping discoverability");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    if (s_state == WIIMOTE_STATE_DISCOVERABLE) {
        s_state = WIIMOTE_STATE_IDLE;
    }
}

wiimote_state_t wiimote_bt_get_state(void) {
    return s_state;
}
