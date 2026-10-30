/*
 * wiimote_bt.c
 *
 * Core Bluetooth GAP + authentication logic for the Wii Remote emulator.
 *
 * Phase 2 additions: Core button reporting (report 0x30), including D-Pad.
 *
 * Fix (init order): SDP must be registered before HID init triggers
 *   connect_or_pair — see wiimote_bt_init() for details.
 *
 * PIN protocol (WiiBrew reference: https://wiibrew.org/wiki/Wiimote):
 *   BONDING  (red Sync button): PIN = Wii's own BDA, reversed (LSB first)
 *   TEMPORARY (1+2 buttons)  : PIN = Wiimote's BDA, reversed (LSB first)
 *
 * The Wii is always the HOST.  The ESP32 is the WIIMOTE (HID device).
 * During a Sync-button bond, param->pin_req.bda holds the Wii's address
 * — so we reply with that address reversed.  This is what the original
 * code did, and it is correct per the protocol spec.
 *
 * The previous "fix" that replaced it with our own BDA was wrong and has
 * been reverted.
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
#include "esp_event.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_hidd_api.h"
#include "nvs_flash.h"

#include "wiimote_bt.h"
#include "wiimote_sdp.h"

static const char *TAG = "wiimote_bt";

/* -----------------------------------------------------------------------
 * State
 * ----------------------------------------------------------------------- */
static wiimote_state_t  s_state         = WIIMOTE_STATE_IDLE;
static esp_bd_addr_t    s_wii_bda       = {0};
static bool             s_wii_bda_known = false;

/* Flag set once SDP registration is complete so START_EVENT knows it is safe */
static volatile bool    s_sdp_ready     = false;

/*
 * Reporting mode the Wii has requested.
 * 0x30 = Core Buttons only (default until the Wii sends 0x12).
 */
static uint8_t s_report_mode = 0x30;

#define NVS_KEY_WII_BDA  "wii_bda"
#define WIIMOTE_MAX_REPORT_LEN 21

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = WIIMOTE_HID_DESCRIPTOR,
        .len = 0,
    },
};

static const esp_hid_device_config_t s_hid_config = {
    .vendor_id = WIIMOTE_VENDOR_ID,
    .product_id = WIIMOTE_PRODUCT_ID,
    .version = WIIMOTE_VERSION,
    .device_name = WIIMOTE_DEVICE_NAME,
    .manufacturer_name = "Nintendo",
    .serial_number = "000000000001",
    .report_maps = s_report_maps,
    .report_maps_len = sizeof(s_report_maps) / sizeof(s_report_maps[0]),
};

static esp_hidd_dev_t *s_hid_dev;

/* -----------------------------------------------------------------------
 * Forward declarations
 * ----------------------------------------------------------------------- */
static void gap_event_handler(esp_bt_gap_cb_event_t event,
                              esp_bt_gap_cb_param_t *param);
static void hidd_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void output_report_handler(const uint8_t *data, uint16_t len);
static void send_status_report(void);
static esp_err_t load_wii_bda(void);
static esp_err_t save_wii_bda(const esp_bd_addr_t bda);
static esp_err_t send_input_report(uint8_t report_id,
                                   const uint8_t *payload, uint16_t len);

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

            s_state = WIIMOTE_STATE_AUTHENTICATING;
            ESP_LOGI(TAG, "Authentication successful");
        } else {
            ESP_LOGE(TAG, "Authentication FAILED: status=%d",
                     param->auth_cmpl.stat);
            s_state = WIIMOTE_STATE_IDLE;
            wiimote_bt_start_discoverable();
        }
        break;
    }

    case ESP_BT_GAP_PIN_REQ_EVT: {
        ESP_LOGI(TAG, "PIN request from %02X:%02X:%02X:%02X:%02X:%02X",
                 param->pin_req.bda[0], param->pin_req.bda[1],
                 param->pin_req.bda[2], param->pin_req.bda[3],
                 param->pin_req.bda[4], param->pin_req.bda[5]);

        /*
         * WiiBrew Wiimote#Bluetooth_Pairing:
         *   "The PIN code for bonding is the binary Bluetooth address of
         *    the host machine, in reverse."
         *
         * The HOST is the Wii.  param->pin_req.bda IS the Wii's address.
         * So the correct PIN is the Wii's BDA reversed (LSB first).
         *
         * Confirmed by MaxLiebe/wiimote-anything:
         *   "Pairing during sync uses the Wii's BD_ADDR reversed as the PIN."
         */
        uint8_t pin[6];
        for (int i = 0; i < 6; i++) {
            pin[i] = param->pin_req.bda[5 - i];   /* Wii BDA, LSB first */
        }

        ESP_LOGI(TAG, "Responding with Wii BDA reversed (correct bonding PIN): "
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 pin[0], pin[1], pin[2], pin[3], pin[4], pin[5]);

        esp_bt_pin_code_t pin_code;
        memcpy(pin_code, pin, 6);
        esp_err_t ret = esp_bt_gap_pin_reply(param->pin_req.bda, true, 6,
                                             pin_code);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reply to PIN request: %s",
                     esp_err_to_name(ret));
            s_state = WIIMOTE_STATE_IDLE;
            wiimote_bt_start_discoverable();
            break;
        }

        s_state = WIIMOTE_STATE_AUTHENTICATING;
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
        ESP_LOGI(TAG, "ACL disconnected: reason=%d",
                 param->acl_disconn_cmpl_stat.reason);
        s_state = WIIMOTE_STATE_IDLE;
        s_report_mode = 0x30;
        break;

    default:
        ESP_LOGD(TAG, "Unhandled GAP event: %d", event);
        break;
    }
}

static void hidd_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    esp_hidd_event_t event = (esp_hidd_event_t)event_id;
    esp_hidd_event_data_t *param = event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        if (param->start.status != ESP_OK) {
            ESP_LOGE(TAG, "HID device profile failed to start: %s",
                     esp_err_to_name(param->start.status));
            return;
        }

        /*
         * Guard: only attempt connect/pair once SDP is registered.
         * wiimote_bt_init() sets s_sdp_ready=true after sdp_register()
         * succeeds, which happens before esp_hidd_dev_init() returns.
         * This guard catches any race where the event fires before that.
         */
        if (!s_sdp_ready) {
            ESP_LOGW(TAG, "HID start event before SDP ready — "
                     "going discoverable and waiting");
            wiimote_bt_start_discoverable();
            break;
        }

        {
            esp_err_t ret = wiimote_bt_connect_or_pair();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start Wii connection/pairing: %s",
                         esp_err_to_name(ret));
            }
        }
        break;

    case ESP_HIDD_CONNECT_EVENT:
        if (param->connect.status != ESP_OK) {
            ESP_LOGE(TAG, "HID connection failed: %s",
                     esp_err_to_name(param->connect.status));
            s_state = WIIMOTE_STATE_IDLE;
            wiimote_bt_start_discoverable();
            break;
        }
        ESP_LOGI(TAG, "HID host connected");
        s_state = WIIMOTE_STATE_CONNECTED;
        {
            esp_err_t ret = esp_bt_gap_set_scan_mode(
                ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to disable discoverability: %s",
                         esp_err_to_name(ret));
            }
        }
        break;

    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGI(TAG, "HID host disconnected");
        s_state = WIIMOTE_STATE_IDLE;
        s_report_mode = 0x30;
        wiimote_bt_start_discoverable();
        break;

    case ESP_HIDD_OUTPUT_EVENT: {
        uint8_t report[WIIMOTE_MAX_REPORT_LEN + 1];
        if (param->output.length > WIIMOTE_MAX_REPORT_LEN ||
            (param->output.length > 0 && param->output.data == NULL)) {
            ESP_LOGE(TAG, "Invalid HID output report length: %u",
                     param->output.length);
            break;
        }
        report[0] = (uint8_t)param->output.report_id;
        if (param->output.length > 0) {
            memcpy(&report[1], param->output.data, param->output.length);
        }
        output_report_handler(report, param->output.length + 1);
        break;
    }

    case ESP_HIDD_FEATURE_EVENT:
        if (param->feature.trans_type == ESP_HID_TRANS_SET_REPORT) {
            uint8_t report[WIIMOTE_MAX_REPORT_LEN + 1];
            if (param->feature.length > WIIMOTE_MAX_REPORT_LEN ||
                (param->feature.length > 0 && param->feature.data == NULL)) {
                ESP_LOGE(TAG, "Invalid HID feature report length: %u",
                         param->feature.length);
                break;
            }
            report[0] = (uint8_t)param->feature.report_id;
            if (param->feature.length > 0) {
                memcpy(&report[1], param->feature.data,
                       param->feature.length);
            }
            output_report_handler(report, param->feature.length + 1);
        }
        break;

    default:
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
         * 0x15: Status request — must respond within ~1 second or the
         * Wii disconnects the controller.
         *
         * WiiBrew: "If this report is received when NOT requested, the
         * application MUST send report 0x12 to change the data reporting
         * mode, otherwise no further data reports will be received."
         * We handle 0x12 in the DATA_MODE case below; the Wii always
         * sends 0x12 after receiving our 0x20 reply, so this is fine.
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
         * Store the requested mode and send ACK 0x22.
         */
        if (len >= 3) {
            s_report_mode = data[2];
            ESP_LOGI(TAG, "Data mode set: continuous=%d mode=0x%02X",
                     (data[1] & 0x04) >> 2, s_report_mode);
        }
        {
            uint8_t ack[4] = {
                0x00, 0x00,
                WIIMOTE_OUT_DATA_MODE,
                0x00,
            };
            send_input_report(WIIMOTE_IN_ACK, ack, sizeof(ack));
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
 *
 * WiiBrew format: (a1) 20 BB BB LF 00 00 VV
 *   BB BB = core buttons (none pressed → 0x00 0x00)
 *   LF    = flags byte:  bits 4-7 = LED state, bits 0-3 = status flags
 *   VV    = battery level
 *
 * The Wii will respond with output report 0x12 to (re-)set the data mode.
 * ----------------------------------------------------------------------- */
static void send_status_report(void) {
    /*
     * payload[0..1] = core buttons (none)
     * payload[2]    = LF byte: LED1 on (bit 4 = 0x10), battery OK
     * payload[3..4] = 0x00 (reserved)
     * payload[5]    = 0x80 (battery level: full)
     */
    uint8_t status[6] = {
        0x00, 0x00,
        STATUS_FLAG_LED1,
        0x00,
        0x00,
        0x80,
    };

    esp_err_t ret = send_input_report(WIIMOTE_IN_STATUS,
                                      status, sizeof(status));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send status report: %s",
                 esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Status report sent");
    }
}

/* ----------------------------------------------------------------------- */
static esp_err_t send_input_report(uint8_t report_id,
                                   const uint8_t *payload, uint16_t len) {
    if (s_hid_dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t report[64];
    if (len > sizeof(report)) return ESP_ERR_INVALID_ARG;
    if (len > 0 && payload) memcpy(report, payload, len);
    return esp_hidd_dev_input_set(s_hid_dev, 0, report_id, report, len);
}

esp_err_t wiimote_bt_send_buttons(uint8_t btn_byte0, uint8_t btn_byte1) {
    if (s_state != WIIMOTE_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t payload[2] = { btn_byte0, btn_byte1 };
    esp_err_t ret = send_input_report(WIIMOTE_IN_BUTTONS,
                                      payload, sizeof(payload));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "send_buttons failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t wiimote_bt_send_buttons_accel(uint8_t btn_byte0, uint8_t btn_byte1,
                                       uint8_t ax, uint8_t ay, uint8_t az) {
    if (s_state != WIIMOTE_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t payload[5] = { btn_byte0, btn_byte1, ax, ay, az };
    esp_err_t ret = send_input_report(0x31, payload, sizeof(payload));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "send_buttons_accel failed: %s", esp_err_to_name(ret));
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
 * Public API: wiimote_bt_init
 *
 * Init order: SDP first, then HID (fixes race where START_EVENT fired
 * before SDP record was registered and the Wii rejected the connection).
 * ----------------------------------------------------------------------- */
esp_err_t wiimote_bt_init(void) {
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

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

    ret = esp_bt_gap_set_device_name(WIIMOTE_DEVICE_NAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

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

    /* IO capability: none → legacy pairing (PIN-based, no SSP) */
    {
        esp_bt_sp_param_t sp_param = ESP_BT_SP_IOCAP_MODE;
        uint8_t iocap = ESP_BT_IO_CAP_NONE;
        esp_bt_gap_set_security_param(sp_param, &iocap, sizeof(iocap));
    }

    ret = load_wii_bda();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load Wii address: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register SDP BEFORE initialising HID so the record exists when
     * ESP_HIDD_START_EVENT fires and triggers connect_or_pair(). */
    ret = wiimote_sdp_register();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PnP SDP initialization failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    s_report_maps[0].len = WIIMOTE_HID_DESCRIPTOR_LEN;
    ret = esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BT,
                            hidd_event_handler, &s_hid_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID device initialization failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    /* Signal to the event handler that SDP is in place */
    s_sdp_ready = true;

    ESP_LOGI(TAG, "Wiimote BT stack initialised");
    ESP_LOGI(TAG, "Device name : %s", WIIMOTE_DEVICE_NAME);
    {
        const uint8_t *bda = esp_bt_dev_get_address();
        ESP_LOGI(TAG, "Own BDA     : %02X:%02X:%02X:%02X:%02X:%02X",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    }

    return ESP_OK;
}

esp_err_t wiimote_bt_start_discoverable(void) {
    ESP_LOGI(TAG, "Going discoverable (SYNC mode)...");
    esp_err_t ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                              ESP_BT_GENERAL_DISCOVERABLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable discoverability: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    s_state = WIIMOTE_STATE_DISCOVERABLE;
    return ESP_OK;
}

esp_err_t wiimote_bt_connect_or_pair(void) {
    if (s_state == WIIMOTE_STATE_CONNECTED ||
        s_state == WIIMOTE_STATE_CONNECTING ||
        s_state == WIIMOTE_STATE_AUTHENTICATING ||
        s_state == WIIMOTE_STATE_RECONNECTING) {
        return ESP_OK;
    }

    if (!s_wii_bda_known) {
        ESP_LOGI(TAG, "No Wii address stored; waiting for Wii pairing");
        return wiimote_bt_start_discoverable();
    }

    ESP_LOGI(TAG, "Starting connection to stored Wii address");
    s_state = WIIMOTE_STATE_RECONNECTING;
    esp_err_t ret = esp_bt_hid_device_connect(s_wii_bda);
    if (ret == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Could not start HID reconnect: %s; enabling pairing mode",
             esp_err_to_name(ret));
    s_state = WIIMOTE_STATE_IDLE;
    return wiimote_bt_start_discoverable();
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
