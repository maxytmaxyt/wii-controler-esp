#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_bt_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Authentic Nintendo Wii Remote (RVL-CNT-01) Bluetooth identity values
 * ----------------------------------------------------------------------- */

#define WIIMOTE_DEVICE_NAME        "Nintendo RVL-CNT-01"

#define WIIMOTE_COD_MAJOR          0x0500
#define WIIMOTE_COD_MINOR          0x04

#define HID_PSM_CONTROL            0x11
#define HID_PSM_INTERRUPT          0x13

#define HID_PREFIX_INPUT           0xA1
#define HID_PREFIX_OUTPUT          0xA2
#define HID_HANDSHAKE_SUCCESSFUL   0x00

/* Output reports (Wii → us) */
#define WIIMOTE_OUT_RUMBLE         0x10
#define WIIMOTE_OUT_LED            0x11
#define WIIMOTE_OUT_DATA_MODE      0x12
#define WIIMOTE_OUT_IR_ENABLE      0x13
#define WIIMOTE_OUT_SPEAKER_ENABLE 0x14
#define WIIMOTE_OUT_STATUS_REQ     0x15
#define WIIMOTE_OUT_WRITE_MEM      0x16
#define WIIMOTE_OUT_READ_MEM       0x17
#define WIIMOTE_OUT_SPEAKER_DATA   0x18
#define WIIMOTE_OUT_SPEAKER_MUTE   0x19
#define WIIMOTE_OUT_IR_ENABLE2     0x1A

/* Input reports (us → Wii) */
#define WIIMOTE_IN_STATUS          0x20
#define WIIMOTE_IN_READ_DATA_REPLY 0x21
#define WIIMOTE_IN_ACK             0x22
#define WIIMOTE_IN_BUTTONS         0x30
#define WIIMOTE_IN_BUTTONS_ACCEL   0x31

/* Status flags for report 0x20 */
#define STATUS_FLAG_BATTERY_EMPTY  0x01
#define STATUS_FLAG_EXT_CONNECTED  0x02
#define STATUS_FLAG_SPEAKER_ON     0x04
#define STATUS_FLAG_IR_ON          0x08
#define STATUS_FLAG_LED1           0x10
#define STATUS_FLAG_LED2           0x20
#define STATUS_FLAG_LED3           0x40
#define STATUS_FLAG_LED4           0x80

/* PnP identifiers */
#define WIIMOTE_VENDOR_ID          0x057E
#define WIIMOTE_PRODUCT_ID         0x0306
#define WIIMOTE_VERSION            0x0001

/* NVS */
#define NVS_NAMESPACE              "wiimote"
#define NVS_KEY_LINKKEYS           "link_keys"

/* -----------------------------------------------------------------------
 * Core Button bitmasks (WiiBrew Wiimote#Core_Buttons)
 *
 * Byte 0 (first byte of button data):
 *   bit 0: D-Pad Left
 *   bit 1: D-Pad Right
 *   bit 2: D-Pad Down
 *   bit 3: D-Pad Up
 *   bit 4: Plus (+)
 *   bits 5-7: reserved
 *
 * Byte 1 (second byte of button data):
 *   bit 0: 2
 *   bit 1: 1
 *   bit 2: B
 *   bit 3: A
 *   bit 4: Minus (-)
 *   bits 5-6: reserved
 *   bit 7: Home
 *
 * Source: https://wiibrew.org/wiki/Wiimote#Core_Buttons
 * ----------------------------------------------------------------------- */
#define BTN_BYTE0_DPAD_LEFT        0x01
#define BTN_BYTE0_DPAD_RIGHT       0x02
#define BTN_BYTE0_DPAD_DOWN        0x04
#define BTN_BYTE0_DPAD_UP          0x08
#define BTN_BYTE0_PLUS             0x10

#define BTN_BYTE1_TWO              0x01
#define BTN_BYTE1_ONE              0x02
#define BTN_BYTE1_B                0x04
#define BTN_BYTE1_A                0x08
#define BTN_BYTE1_MINUS            0x10
#define BTN_BYTE1_HOME             0x80

/* Connection state machine */
typedef enum {
    WIIMOTE_STATE_IDLE = 0,
    WIIMOTE_STATE_DISCOVERABLE,
    WIIMOTE_STATE_CONNECTING,
    WIIMOTE_STATE_AUTHENTICATING,
    WIIMOTE_STATE_CONNECTED,
    WIIMOTE_STATE_RECONNECTING,
} wiimote_state_t;

/* Public API */
esp_err_t       wiimote_bt_init(void);
void            wiimote_bt_start_discoverable(void);
void            wiimote_bt_stop_discoverable(void);
wiimote_state_t wiimote_bt_get_state(void);

/*
 * Send a core button report (0x30) to the Wii.
 *
 * btn_byte0: BTN_BYTE0_* bitmask (D-Pad + Plus)
 * btn_byte1: BTN_BYTE1_* bitmask (A, B, 1, 2, Minus, Home)
 *
 * Call this whenever button state changes. Both bytes should reflect the
 * FULL current state (i.e. pressed = bit set, released = bit clear).
 * The Wii uses polling, not edge events.
 */
esp_err_t wiimote_bt_send_buttons(uint8_t btn_byte0, uint8_t btn_byte1);

#ifdef __cplusplus
}
#endif
