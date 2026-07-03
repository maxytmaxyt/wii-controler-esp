#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Raw HID Descriptor as dumped from a real Nintendo RVL-CNT-01.
 *
 * Source: Bluetooth_Specs page (yts.rdy.jp), confirmed against:
 *   - WiiBrew Wiimote/SDP_Information
 *   - BlueZ hid-wiimote driver captures
 *   - PythonUSBIP hid_wiimote.py
 *
 * The Wiimote does NOT follow standard HID usage types.
 * It only defines report IDs and payload lengths; the actual
 * meaning of each byte within a report is proprietary.
 *
 * Descriptor byte layout per entry:
 *   05 01          USAGE_PAGE (Generic Desktop)
 *   09 05          USAGE (Gamepad)
 *   a1 01          COLLECTION (Application)
 *   85 RR          REPORT_ID (RR)
 *   ... size ...
 *   81/91 00       INPUT/OUTPUT (non-standard, vendor-defined array)
 * etc.
 */
extern const uint8_t  WIIMOTE_HID_DESCRIPTOR[];
extern const uint16_t WIIMOTE_HID_DESCRIPTOR_LEN;

#ifdef __cplusplus
}
#endif
