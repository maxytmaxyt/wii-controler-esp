#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Raw HID Descriptor as dumped from a real Nintendo RVL-CNT-01.
 */
extern const uint8_t  WIIMOTE_HID_DESCRIPTOR[];
extern const uint16_t WIIMOTE_HID_DESCRIPTOR_LEN;

/* Register both HID + PnP SDP records with the Bluedroid stack */
esp_err_t wiimote_sdp_register(void);

#ifdef __cplusplus
}
#endif
