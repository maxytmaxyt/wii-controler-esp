/*
 * wiimote_bt.c
 *
 * Core Bluetooth GAP + authentication logic for the Wii Remote emulator.
 *
 * Pairing Protocol (WiiBrew + xwiimote PROTOCOL):
 *
 *  1. The Wii sends a PIN request during pairing.
 *  2. The Wiimote's PIN is the REVERSE of its own Bluetooth address
 *     (6 bytes, binary, not ASCII).
 *     e.g. if BDA = AA:BB:CC:DD:EE:FF → PIN = FF EE DD CC BB AA
 *  3. After successful authentication, a link key notification arrives.
 *     We store this in NVS.
 *  4. On subsequent connections the Wii sends an authentication request.
 *     We reply with the stored link key (via esp_bt_gap_set_pin or the
 *     link key response).
 *
 * Auto-Reconnect (WiiBrew / xwiimote PROTOCOL):
 *
 *  After bonding, the Wii stores our BDA. On power-on the Wii waits for
 *  previously bonded remotes. The Wiimote is expected to initiate the
 *  connection (connect outbound to the Wii).
 *
 *  We therefore:
 *   a) Listen for incoming connections (in case the Wii is initiating).
 *   b) After boot, if we have a stored Wii BDA, attempt outbound connect.
 *
 * Class of Device: 0x002504 (confirmed from hcidump captures of real Wii
 * Remote: class 0x002504).
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
static wiimote_state_t  s_state     = WIIMOTE_STATE_IDLE;
static esp_bd_addr_t    s_wii_bda   = {0};      /* Stored Wii BDA */
static bool             s_wii_bda_known = false;

/* NVS key for the Wii console BDA (so we know who to reconnect to) */
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
        /*
         * Authentication complete.
         * If successful, store the Wii BDA and transition to CONNECTED.
         * The link key notification comes separately (LINK_KEY_NOTIF_EVT).
         */
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Auth complete: %02X:%02X:%02X:%02X:%02X:%02X",
                     param->auth_cmpl.bda[0], param->auth_cmpl.bda[1],
                     param->auth_cmpl.bda[2], param->auth_cmpl.bda[3],
                     param->auth_cmpl.bda[4], param->auth_cmpl.bda[5]);

            /* Remember the Wii's BDA */
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
        /*
         * PIN request during legacy pairing.
         *
         * The Wiimote PIN is the reverse of its own BDA (6 bytes, binary).
         * Source: WiiBrew Wiimote#Bluetooth_Pairing
         *   "the bytes of the PIN will be 0x66 0x55 0x44 0x33 0x22 0x11"
         *   for BDA 11:22:33:44:55:66
         */
        ESP_LOGI(TAG, "PIN request from %02X:%02X:%02X:%02X:%02X:%02X",
                 param->pin_req.bda[0], param->pin_req.bda[1],
                 param->pin_req.bda[2], param->pin_req.bda[3],
                 param->pin_req.bda[4], param->pin_req.bda[5]);

        const uint8_t *own_bda = esp_bt_dev_get_address();
        uint8_t pin[6];
        /* Reverse of own BDA */
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
        /*
         * Link key notification — store for future reconnections.
         * Called after successful pairing/bonding.
         */
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
        /*
         * The remote device (Wii) is requesting the link key for
         * re-authentication. Look it up in NVS and respond.
         */
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
            ESP_LOGW(TAG, "No link key found — pairing required");
            /* Reply negative so the Wii initiates pairing */
            esp_bt_gap_set_link_key(param->link_key_req.bda, NULL, 0);
        }
        break;
    }

    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        /* Ignore — we don't query remote names */
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
         * The Wii sends 0x15 immediately after the interrupt channel opens
         * to request a status report. We MUST respond with report 0x20,
         * otherwise the Wii disconnects the controller.
         *
         * Status report 0x20 layout (6 bytes payload):
         *   Byte 0-1: Core buttons (all 0 = no buttons pressed)
         *   Byte 2:   Flags (battery_low | ext_connected | speaker | ir
         *                    | LED4 | LED3 | LED2 | LED1)
         *   Byte 3:   0x00 (reserved)
         *   Byte 4:   0x00 (reserved)
         *   Byte 5:   Battery level (0x80 = full battery)
         */
        ESP_LOGI(TAG, "Status request received → sending status report");
        send_status_report();
        break;

    case WIIMOTE_OUT_LED:
        /* 0x11: LED + rumble — ignore for now, Phase 1 only */
        ESP_LOGD(TAG, "LED report: 0x%02X",
                 (len > 1) ? data[1] : 0);
        break;

    case WIIMOTE_OUT_DATA_MODE:
        /* 0x12: Data reporting mode — ignore for now */
        ESP_LOGD(TAG, "Data mode report: continuous=%d mode=0x%02X",
                 (len > 1) ? (data[1] & 0x04) >> 2 : 0,
                 (len > 2) ? data[2] : 0);
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
    /*
     * Report 0x20 — Status Information (6 bytes)
     *   [0-1] Core buttons       : 0x00 0x00 (nothing pressed)
     *   [2]   Flags              : LED1 on (0x10), battery not low
     *   [3]   Reserved           : 0x00
     *   [4]   Reserved           : 0x00
     *   [5]   Battery level      : 0x80 (appears as "full" to Wii)
     *
     * LED1 is set to let the user know the controller is connected.
     * The Wii will normally override the LEDs immediately after pairing.
     */
    uint8_t status[6] = {
        0x00, 0x00,           /* Core buttons: none pressed */
        STATUS_FLAG_LED1,     /* LED1 on, no battery low, no ext */
        0x00,                 /* Reserved */
        0x00,                 /* Reserved */
        0x80,                 /* Battery level: 0x80 ≈ full */
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
 * NVS: persist the Wii console BDA so we can reconnect after reboot
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
 *
 * If we have a stored Wii BDA, periodically attempt to connect.
 * The Wii will be listening for previously paired remotes on startup.
 * ----------------------------------------------------------------------- */
static void reconnect_task(void *arg) {
    /* Wait a moment for the BT stack to fully start */
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

    /* Try up to 10 times with 3 second intervals */
    for (int attempt = 0; attempt < 10; attempt++) {
        if (s_state == WIIMOTE_STATE_CONNECTED) {
            break;
        }
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
 * Public API
 * ----------------------------------------------------------------------- */

esp_err_t wiimote_bt_init(void) {
    esp_err_t ret;

    /* Open NVS for this component */
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialise NVS link key store */
    ret = wiimote_linkkey_init();
    if (ret != ESP_OK) return ret;

    /* Initialise Bluetooth controller */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    /* Classic BT only, disable BLE to save memory */
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

    /* Initialise Bluedroid host stack */
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

    /* Register GAP callback */
    ret = esp_bt_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    /* Set device name exactly as a real Wii Remote */
    ret = esp_bt_dev_set_device_name(WIIMOTE_DEVICE_NAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /*
     * Set Class of Device: 0x002504
     *   Bits 23-13: Major Service Class = 0x000 (none)
     *   Bits 12-8:  Major Device Class  = 0x05  (Peripheral)
     *   Bits 7-2:   Minor Device Class  = 0x01  (Joystick)
     *   The CoD is passed as major << 8 | minor.
     */
    ret = esp_bt_gap_set_cod(
        (esp_bt_cod_t){
            .major       = WIIMOTE_COD_MAJOR,
            .minor       = WIIMOTE_COD_MINOR,
            .service     = 0,
        },
        ESP_BT_SET_COD_ALL
    );
    if (ret != ESP_OK) {
        /* Non-fatal — some IDF versions handle this differently */
        ESP_LOGW(TAG, "Set CoD failed (non-fatal): %s",
                 esp_err_to_name(ret));
    }

    /*
     * Security: require authentication + allow legacy pairing.
     * The Wii uses legacy pairing (PIN-based), not SSP.
     */
    esp_bt_sp_param_t sp_param = ESP_BT_SP_IOCAP_MODE;
    uint8_t iocap = ESP_BT_IO_CAP_NONE;  /* No I/O → Just Works / legacy */
    esp_bt_gap_set_security_param(sp_param, &iocap, sizeof(iocap));

    /* Allow pairing: enable authentication */
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE,
                                  &iocap, sizeof(iocap));

    /* Register L2CAP channels and the output report handler */
    ret = wiimote_l2cap_init(output_report_handler);
    if (ret != ESP_OK) return ret;

    /* Register SDP records */
    ret = wiimote_sdp_register();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SDP register returned: %s — continuing",
                 esp_err_to_name(ret));
    }

    /* Load stored Wii BDA */
    load_wii_bda();

    ESP_LOGI(TAG, "Wiimote BT stack initialised");
    ESP_LOGI(TAG, "Device name : %s", WIIMOTE_DEVICE_NAME);
    ESP_LOGI(TAG, "Own BDA     : %02X:%02X:%02X:%02X:%02X:%02X",
             esp_bt_dev_get_address()[0], esp_bt_dev_get_address()[1],
             esp_bt_dev_get_address()[2], esp_bt_dev_get_address()[3],
             esp_bt_dev_get_address()[4], esp_bt_dev_get_address()[5]);

    /*
     * If we have a stored Wii BDA, try auto-reconnect first.
     * Otherwise go discoverable immediately.
     */
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
