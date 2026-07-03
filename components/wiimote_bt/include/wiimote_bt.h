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
 * Source: WiiBrew wiki, hcitool info dumps, BlueZ hid-wiimote driver
 * ----------------------------------------------------------------------- */

/* Device name exactly as reported by a real Wii Remote */
#define WIIMOTE_DEVICE_NAME        "Nintendo RVL-CNT-01"

/*
 * Class of Device: 0x002504
 *   Major Service Class : 0x000 (none / uncategorized)
 *   Major Device Class  : 0x05  (Peripheral)
 *   Minor Device Class  : 0x01  (Joystick)
 * Confirmed from hcidump: class 0x002504
 */
#define WIIMOTE_COD_MAJOR          0x0500   /* Peripheral */
#define WIIMOTE_COD_MINOR          0x04     /* Joystick */

/*
 * L2CAP PSM channels (HID profile)
 *   0x11 = HID Control  (output reports / SET_REPORT)
 *   0x13 = HID Interrupt (input + output data reports)
 */
#define HID_PSM_CONTROL            0x11
#define HID_PSM_INTERRUPT          0x13

/*
 * HID channel byte prefixes (Bluetooth HID spec sections 7.3 / 7.4)
 *   0xA1 = DATA | INPUT  (device → host)
 *   0xA2 = DATA | OUTPUT (host → device, sent on interrupt channel)
 *   0x50 = HANDSHAKE SUCCESSFUL
 */
#define HID_PREFIX_INPUT           0xA1
#define HID_PREFIX_OUTPUT          0xA2
#define HID_HANDSHAKE_SUCCESSFUL   0x00

/*
 * Wiimote report IDs (output reports sent by the Wii to us)
 * Source: WiiBrew Wiimote#Output_Reports
 */
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

/*
 * Wiimote report IDs (input reports we send to the Wii)
 * Source: WiiBrew Wiimote#Input_Reports
 */
#define WIIMOTE_IN_STATUS          0x20
#define WIIMOTE_IN_READ_DATA_REPLY 0x21
#define WIIMOTE_IN_ACK             0x22
#define WIIMOTE_IN_BUTTONS         0x30
#define WIIMOTE_IN_BUTTONS_ACCEL   0x31

/*
 * Status flags for report 0x20
 * Byte 3 bit layout: [battery_low][ext_connected][speaker_enabled][led4..led1][ir_enabled]
 */
#define STATUS_FLAG_BATTERY_EMPTY  0x01
#define STATUS_FLAG_EXT_CONNECTED  0x02
#define STATUS_FLAG_SPEAKER_ON     0x04
#define STATUS_FLAG_IR_ON          0x08
#define STATUS_FLAG_LED1           0x10
#define STATUS_FLAG_LED2           0x20
#define STATUS_FLAG_LED3           0x40
#define STATUS_FLAG_LED4           0x80

/*
 * Wiimote PnP identifiers (from SDP PnP record, confirmed via hcitool)
 *   VendorID  = 0x057E (Nintendo)
 *   ProductID = 0x0306 (Wii Remote RVL-CNT-01)
 *   Version   = 0x0001
 */
#define WIIMOTE_VENDOR_ID          0x057E
#define WIIMOTE_PRODUCT_ID         0x0306
#define WIIMOTE_VERSION            0x0001

/*
 * NVS namespace/key used to persist link keys across reboots
 */
#define NVS_NAMESPACE              "wiimote"
#define NVS_KEY_LINKKEYS           "link_keys"

/* Connection state machine */
typedef enum {
    WIIMOTE_STATE_IDLE = 0,          /* Not connected, not discoverable */
    WIIMOTE_STATE_DISCOVERABLE,      /* Advertising / discoverable */
    WIIMOTE_STATE_CONNECTING,        /* Baseband connecting */
    WIIMOTE_STATE_AUTHENTICATING,    /* Pairing / auth in progress */
    WIIMOTE_STATE_CONNECTED,         /* L2CAP channels up, ready */
    WIIMOTE_STATE_RECONNECTING,      /* Attempting auto-reconnect to Wii */
} wiimote_state_t;

/* Public API */
esp_err_t wiimote_bt_init(void);
void      wiimote_bt_start_discoverable(void);
void      wiimote_bt_stop_discoverable(void);
wiimote_state_t wiimote_bt_get_state(void);

#ifdef __cplusplus
}
#endif
