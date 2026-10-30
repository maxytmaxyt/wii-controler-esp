#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_bt_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * wiimote_l2cap.h
 *
 * Manages the two HID L2CAP channels:
 *   PSM 0x11  HID Control   (bidirectional, mainly SET_REPORT / GET_REPORT)
 *   PSM 0x13  HID Interrupt (main data pipe: input + output reports)
 *
 * On initial pairing the Wii connects to us (outbound from the Wii's
 * perspective, inbound for us). On subsequent auto-reconnect the
 * Wiimote (i.e. us) connects to the Wii.
 */

/*
 * Maximum HID payload we will ever send.
 * The largest Wiimote input report is 21 bytes (e.g. 0x34–0x37).
 * The packet on the wire adds 2 bytes (0xA1 prefix + report_id),
 * so the internal buffer is L2CAP_MAX_PAYLOAD_LEN + 2 bytes.
 */
#define L2CAP_MAX_PAYLOAD_LEN  21

/* Callback type for output reports received from the Wii */
typedef void (*l2cap_output_report_cb_t)(const uint8_t *data, uint16_t len);

/* Initialise L2CAP: register callback, init stack, listen on PSM 0x11+0x13 */
esp_err_t wiimote_l2cap_init(l2cap_output_report_cb_t output_cb);

/* Connect outbound to a Wii (for auto-reconnect) */
esp_err_t wiimote_l2cap_connect(const esp_bd_addr_t wii_bda);

/* Disconnect both channels */
void wiimote_l2cap_disconnect(void);

/* Send an input report to the Wii over the interrupt channel */
esp_err_t wiimote_l2cap_send_input(uint8_t report_id,
                                   const uint8_t *payload,
                                   uint16_t len);

/* Send a handshake response over the control channel */
esp_err_t wiimote_l2cap_send_handshake(uint8_t result_code);

/* True when both channels are open */
bool wiimote_l2cap_is_connected(void);

#ifdef __cplusplus
}
#endif
