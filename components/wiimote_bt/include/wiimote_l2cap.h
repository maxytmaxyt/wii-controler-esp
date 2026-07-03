#pragma once

#include <stdint.h>
#include <stddef.h>
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
 *
 * Both modes are handled here.
 */

/* Maximum HID payload we will ever send or receive */
#define L2CAP_MAX_PAYLOAD_LEN  22  /* 1 prefix + 1 report ID + 20 data */

/* Callback types */
typedef void (*l2cap_output_report_cb_t)(const uint8_t *data, uint16_t len);

/* Initialise L2CAP server (listen on PSM 0x11 + 0x13) */
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
