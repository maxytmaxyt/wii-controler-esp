/*
 * wiimote_l2cap.c
 *
 * L2CAP HID channel management for the Wiimote emulator.
 *
 * Protocol notes (WiiBrew + xwiimote PROTOCOL doc):
 *
 *  1. The Wii opens PSM 0x11 (control) first, then PSM 0x13 (interrupt).
 *  2. All output reports from the Wii arrive on the interrupt channel
 *     (prefix 0xA2 + report_id + payload).
 *  3. Input reports from us go to the Wii on the interrupt channel
 *     (prefix 0xA1 + report_id + payload).
 *  4. The control channel is largely unused for data but must be kept
 *     open. SET_REPORT on the control channel is acknowledged with a
 *     HANDSHAKE (0x00) response.
 *  5. On auto-reconnect we initiate the outbound connection to the Wii
 *     on the same two PSMs.
 *
 * ESP-IDF provides esp_l2cap_bt_register() for registering a PSM as a
 * server (incoming connections) and esp_l2cap_bt_connect() for outbound.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_l2cap_bt_api.h"
#include "wiimote_l2cap.h"
#include "wiimote_bt.h"

static const char *TAG = "wiimote_l2cap";

/* Channel state */
typedef struct {
    uint32_t cid;       /* L2CAP channel identifier */
    bool     open;
} l2cap_channel_t;

static l2cap_channel_t s_ctrl  = {0, false};
static l2cap_channel_t s_intr  = {0, false};
static l2cap_output_report_cb_t s_output_cb = NULL;

/* -----------------------------------------------------------------------
 * L2CAP event callback — called by Bluedroid for both PSMs
 * ----------------------------------------------------------------------- */
static void l2cap_event_handler(esp_bt_l2cap_cb_event_t event,
                                esp_bt_l2cap_cb_param_t *param)
{
    switch (event) {

    case ESP_BT_L2CAP_OPEN_EVT: {
        /* A channel has been opened (inbound or outbound) */
        uint16_t psm = param->open.psm;
        uint32_t cid = param->open.cid;
        ESP_LOGI(TAG, "L2CAP OPEN psm=0x%02X cid=0x%08" PRIx32
                 " status=%d", psm, cid, param->open.status);
        if (param->open.status != ESP_BT_L2CAP_SUCCESS) {
            ESP_LOGE(TAG, "Channel open failed (psm=0x%02X)", psm);
            break;
        }
        if (psm == HID_PSM_CONTROL) {
            s_ctrl.cid  = cid;
            s_ctrl.open = true;
        } else if (psm == HID_PSM_INTERRUPT) {
            s_intr.cid  = cid;
            s_intr.open = true;
        }
        ESP_LOGI(TAG, "ctrl=%s intr=%s",
                 s_ctrl.open ? "OPEN" : "closed",
                 s_intr.open ? "OPEN" : "closed");
        break;
    }

    case ESP_BT_L2CAP_CLOSE_EVT: {
        uint32_t cid = param->close.cid;
        ESP_LOGI(TAG, "L2CAP CLOSE cid=0x%08" PRIx32, cid);
        if (cid == s_ctrl.cid) { s_ctrl.cid = 0; s_ctrl.open = false; }
        if (cid == s_intr.cid) { s_intr.cid = 0; s_intr.open = false; }
        break;
    }

    case ESP_BT_L2CAP_DATA_IND_EVT: {
        /*
         * Incoming data from the Wii.
         *
         * Interrupt channel (PSM 0x13):
         *   Byte 0: 0xA2 (DATA | OUTPUT)
         *   Byte 1: Report ID
         *   Byte 2+: Payload
         *
         * Control channel (PSM 0x11):
         *   May receive SET_REPORT (0x52) for older Wiis.
         *   Respond with HANDSHAKE SUCCESSFUL (0x00).
         */
        uint8_t  *data = param->data_ind.data;
        uint16_t  len  = param->data_ind.len;
        uint32_t  cid  = param->data_ind.cid;

        if (len < 2) break;

        uint8_t hid_type = data[0];

        if (cid == s_intr.cid && hid_type == HID_PREFIX_OUTPUT) {
            /* Output report from the Wii — forward to the main handler */
            if (s_output_cb) {
                s_output_cb(data + 1, len - 1); /* strip 0xA2 prefix */
            }
        } else if (cid == s_ctrl.cid) {
            /* Control channel — typically a SET_REPORT (0x52) */
            if ((hid_type & 0xF0) == 0x50) {
                /* SET_REPORT on control — forward payload, send ACK */
                if (len > 1 && s_output_cb) {
                    s_output_cb(data + 1, len - 1);
                }
                /* Send HANDSHAKE SUCCESSFUL */
                wiimote_l2cap_send_handshake(HID_HANDSHAKE_SUCCESSFUL);
            }
        }
        break;
    }

    case ESP_BT_L2CAP_CL_INIT_EVT:
        ESP_LOGI(TAG, "L2CAP client init event status=%d",
                 param->cl_init.status);
        break;

    case ESP_BT_L2CAP_SRV_INIT_EVT:
        ESP_LOGI(TAG, "L2CAP server init psm=0x%02X status=%d",
                 param->srv_init.psm, param->srv_init.status);
        break;

    default:
        ESP_LOGD(TAG, "Unhandled L2CAP event %d", event);
        break;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

esp_err_t wiimote_l2cap_init(l2cap_output_report_cb_t output_cb) {
    s_output_cb = output_cb;

    esp_err_t ret;

    /* Register the L2CAP callback */
    ret = esp_bt_l2cap_register_callback(l2cap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_l2cap_register_callback: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    /* Initialise L2CAP layer */
    ret = esp_bt_l2cap_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_bt_l2cap_init: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register as server on HID Control PSM (0x11) */
    ret = esp_bt_l2cap_server_register(HID_PSM_CONTROL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register PSM 0x11 failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register as server on HID Interrupt PSM (0x13) */
    ret = esp_bt_l2cap_server_register(HID_PSM_INTERRUPT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register PSM 0x13 failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "L2CAP servers registered on PSM 0x11 and 0x13");
    return ESP_OK;
}

esp_err_t wiimote_l2cap_connect(const esp_bd_addr_t wii_bda) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Connecting outbound to Wii "
             "%02X:%02X:%02X:%02X:%02X:%02X",
             wii_bda[0], wii_bda[1], wii_bda[2],
             wii_bda[3], wii_bda[4], wii_bda[5]);

    /* Connect control channel first */
    ret = esp_bt_l2cap_connect(HID_PSM_CONTROL, (esp_bd_addr_t *)wii_bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Connect PSM 0x11 failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Small delay to let control channel settle before opening interrupt */
    vTaskDelay(pdMS_TO_TICKS(100));

    ret = esp_bt_l2cap_connect(HID_PSM_INTERRUPT, (esp_bd_addr_t *)wii_bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Connect PSM 0x13 failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

void wiimote_l2cap_disconnect(void) {
    if (s_intr.open) {
        esp_bt_l2cap_disconnect(s_intr.cid);
        s_intr.open = false;
        s_intr.cid  = 0;
    }
    if (s_ctrl.open) {
        esp_bt_l2cap_disconnect(s_ctrl.cid);
        s_ctrl.open = false;
        s_ctrl.cid  = 0;
    }
}

esp_err_t wiimote_l2cap_send_input(uint8_t report_id,
                                   const uint8_t *payload,
                                   uint16_t len)
{
    if (!s_intr.open) {
        ESP_LOGW(TAG, "send_input: interrupt channel not open");
        return ESP_ERR_INVALID_STATE;
    }

    /* Build packet: 0xA1 + report_id + payload */
    uint8_t  pkt[L2CAP_MAX_PAYLOAD_LEN + 2];
    uint16_t pkt_len = (uint16_t)(len + 2);
    if (pkt_len > sizeof(pkt)) {
        ESP_LOGE(TAG, "send_input: payload too large (%d bytes)", len);
        return ESP_ERR_INVALID_ARG;
    }

    pkt[0] = HID_PREFIX_INPUT;  /* 0xA1 */
    pkt[1] = report_id;
    if (len > 0 && payload) memcpy(&pkt[2], payload, len);

    esp_err_t ret = esp_bt_l2cap_write(s_intr.cid, pkt, pkt_len, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "l2cap_write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t wiimote_l2cap_send_handshake(uint8_t result_code) {
    if (!s_ctrl.open) return ESP_ERR_INVALID_STATE;

    uint8_t pkt = (uint8_t)(0x00 | (result_code & 0x0F)); /* HANDSHAKE */
    return esp_bt_l2cap_write(s_ctrl.cid, &pkt, 1, NULL);
}

bool wiimote_l2cap_is_connected(void) {
    return s_ctrl.open && s_intr.open;
}
