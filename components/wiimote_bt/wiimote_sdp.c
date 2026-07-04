/*
 * wiimote_sdp.c
 *
 * Registers the two SDP service records that a real Nintendo RVL-CNT-01
 * exposes:
 *   Record 1 (handle 0x10000): HID service  (UUID 0x1124)
 *   Record 2 (handle 0x10001): PnP service  (UUID 0x1200)
 */

#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_sdp_api.h"
#include "wiimote_sdp.h"
#include "wiimote_bt.h"

static const char *TAG = "wiimote_sdp";

/* -----------------------------------------------------------------------
 * HID Descriptor — byte sequence captured from Nintendo RVL-CNT-01
 * ----------------------------------------------------------------------- */
const uint8_t WIIMOTE_HID_DESCRIPTOR[] = {
    0x05, 0x01,  /* USAGE_PAGE (Generic Desktop)         */
    0x09, 0x05,  /* USAGE (Gamepad)                      */
    0xa1, 0x01,  /* COLLECTION (Application)             */

    /* --- Output Reports (Wii → Wiimote) --- */
    0x85, 0x10,  /* REPORT_ID (0x10) Rumble              */
    0x15, 0x00,  /* LOGICAL_MINIMUM (0)                  */
    0x26, 0xff, 0x00, /* LOGICAL_MAXIMUM (255)           */
    0x75, 0x08,  /* REPORT_SIZE (8)                      */
    0x95, 0x01,  /* REPORT_COUNT (1)                     */
    0x06, 0x00, 0xff, /* USAGE_PAGE (Vendor Defined ff00)*/
    0x09, 0x01,  /* USAGE (Vendor 0x01)                  */
    0x91, 0x00,  /* OUTPUT (Data,Ary,Abs)                */

    0x85, 0x11,  /* REPORT_ID (0x11) Player LEDs         */
    0x95, 0x01,  0x09, 0x01, 0x91, 0x00,

    0x85, 0x12,  /* REPORT_ID (0x12) Data Reporting Mode */
    0x95, 0x02,  0x09, 0x01, 0x91, 0x00,

    0x85, 0x13,  /* REPORT_ID (0x13) IR Enable           */
    0x95, 0x01,  0x09, 0x01, 0x91, 0x00,

    0x85, 0x14,  /* REPORT_ID (0x14) Speaker Enable      */
    0x95, 0x01,  0x09, 0x01, 0x91, 0x00,

    0x85, 0x15,  /* REPORT_ID (0x15) Status Request      */
    0x95, 0x01,  0x09, 0x01, 0x91, 0x00,

    0x85, 0x16,  /* REPORT_ID (0x16) Write Memory        */
    0x95, 0x15,  0x09, 0x01, 0x91, 0x00,

    0x85, 0x17,  /* REPORT_ID (0x17) Read Memory         */
    0x95, 0x06,  0x09, 0x01, 0x91, 0x00,

    0x85, 0x18,  /* REPORT_ID (0x18) Speaker Data        */
    0x95, 0x15,  0x09, 0x01, 0x91, 0x00,

    0x85, 0x19,  /* REPORT_ID (0x19) Speaker Mute        */
    0x95, 0x01,  0x09, 0x01, 0x91, 0x00,

    0x85, 0x1a,  /* REPORT_ID (0x1A) IR Enable 2         */
    0x95, 0x01,  0x09, 0x01, 0x91, 0x00,

    /* --- Input Reports (Wiimote → Wii) --- */
    0x85, 0x20,  /* REPORT_ID (0x20) Status Information  6 bytes */
    0x95, 0x06,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x21,  /* REPORT_ID (0x21) Read Data Reply     21 bytes */
    0x95, 0x15,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x22,  /* REPORT_ID (0x22) Acknowledge         4 bytes */
    0x95, 0x04,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x30,  /* REPORT_ID (0x30) Core Buttons        2 bytes */
    0x95, 0x02,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x31,  /* REPORT_ID (0x31) Buttons+Accel       5 bytes */
    0x95, 0x05,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x32,  /* REPORT_ID (0x32) Buttons+8ext        10 bytes */
    0x95, 0x0a,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x33,  /* REPORT_ID (0x33) Buttons+Accel+12IR  17 bytes */
    0x95, 0x11,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x34,  /* REPORT_ID (0x34) Buttons+19ext       21 bytes */
    0x95, 0x15,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x35,  /* REPORT_ID (0x35) Buttons+Accel+16ext 21 bytes */
    0x95, 0x15,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x36,  /* REPORT_ID (0x36) Buttons+10IR+9ext   21 bytes */
    0x95, 0x15,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x37,  /* REPORT_ID (0x37) Buttons+Accel+10IR  21 bytes */
    0x95, 0x15,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x3d,  /* REPORT_ID (0x3D) 21ext               21 bytes */
    0x95, 0x15,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x3e,  /* REPORT_ID (0x3E) Interleaved 1       21 bytes */
    0x95, 0x15,  0x09, 0x01, 0x81, 0x00,

    0x85, 0x3f,  /* REPORT_ID (0x3F) Interleaved 2       21 bytes */
    0x95, 0x15,  0x09, 0x01, 0x81, 0x00,

    0xc0         /* END_COLLECTION                       */
};

const uint16_t WIIMOTE_HID_DESCRIPTOR_LEN =
    (uint16_t)sizeof(WIIMOTE_HID_DESCRIPTOR);

/* -----------------------------------------------------------------------
 * SDP encoding helpers
 * ----------------------------------------------------------------------- */

static uint8_t *sdp_put_u8(uint8_t *p, uint8_t val) {
    *p++ = 0x08;
    *p++ = val;
    return p;
}

static uint8_t *sdp_put_u16(uint8_t *p, uint16_t val) {
    *p++ = 0x09;
    *p++ = (uint8_t)(val >> 8);
    *p++ = (uint8_t)(val & 0xFF);
    return p;
}

static uint8_t *sdp_put_bool(uint8_t *p, bool val) {
    *p++ = 0x28;
    *p++ = val ? 0x01 : 0x00;
    return p;
}

static uint8_t *sdp_put_uuid16(uint8_t *p, uint16_t uuid) {
    *p++ = 0x19;
    *p++ = (uint8_t)(uuid >> 8);
    *p++ = (uint8_t)(uuid & 0xFF);
    return p;
}

static uint8_t *sdp_put_str(uint8_t *p, const char *str) {
    uint8_t len = (uint8_t)strlen(str);
    *p++ = 0x25;
    *p++ = len;
    memcpy(p, str, len);
    return p + len;
}

static uint8_t *sdp_put_attr_id(uint8_t *p, uint16_t attr_id) {
    *p++ = 0x09;
    *p++ = (uint8_t)(attr_id >> 8);
    *p++ = (uint8_t)(attr_id & 0xFF);
    return p;
}

/* We store the handle so we could de-register later if needed */
static int s_hid_sdp_handle = -1;
static int s_pnp_sdp_handle = -1;

/* -----------------------------------------------------------------------
 * HID SDP record
 * ----------------------------------------------------------------------- */
static esp_err_t register_hid_sdp_record(void) {
    static uint8_t buf[512];
    uint8_t *p = buf;

    uint8_t *outer_len_p;
    *p++ = 0x35;
    outer_len_p = p;
    *p++ = 0x00; /* placeholder */

    /* 0x0001 ServiceClassIDList = { UUID16:0x1124 } */
    p = sdp_put_attr_id(p, 0x0001);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      p = sdp_put_uuid16(p, 0x1124);
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0004 ProtocolDescriptorList */
    p = sdp_put_attr_id(p, 0x0004);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        p = sdp_put_uuid16(p, 0x0100); /* L2CAP */
        p = sdp_put_u16(p, HID_PSM_CONTROL);
        *il = (uint8_t)(p - il - 1); }
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        p = sdp_put_uuid16(p, 0x0011); /* HIDP */
        *il = (uint8_t)(p - il - 1); }
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0005 BrowseGroupList */
    p = sdp_put_attr_id(p, 0x0005);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      p = sdp_put_uuid16(p, 0x1002);
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0006 LanguageBaseAttributeIDList */
    p = sdp_put_attr_id(p, 0x0006);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      p = sdp_put_u16(p, 0x656e);
      p = sdp_put_u16(p, 0x006a);
      p = sdp_put_u16(p, 0x0100);
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0009 BluetoothProfileDescriptorList */
    p = sdp_put_attr_id(p, 0x0009);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        p = sdp_put_uuid16(p, 0x1124);
        p = sdp_put_u16(p, 0x0100);
        *il = (uint8_t)(p - il - 1); }
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x000D AdditionalProtocolDescriptorLists */
    p = sdp_put_attr_id(p, 0x000D);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      { *p++ = 0x35; uint8_t *ml = p; *p++ = 0;
        { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
          p = sdp_put_uuid16(p, 0x0100);
          p = sdp_put_u16(p, HID_PSM_INTERRUPT);
          *il = (uint8_t)(p - il - 1); }
        { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
          p = sdp_put_uuid16(p, 0x0011);
          *il = (uint8_t)(p - il - 1); }
        *ml = (uint8_t)(p - ml - 1); }
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0100 ServiceName */
    p = sdp_put_attr_id(p, 0x0100);
    p = sdp_put_str(p, WIIMOTE_DEVICE_NAME);

    /* 0x0101 ServiceDescription */
    p = sdp_put_attr_id(p, 0x0101);
    p = sdp_put_str(p, WIIMOTE_DEVICE_NAME);

    /* 0x0102 ServiceProvider */
    p = sdp_put_attr_id(p, 0x0102);
    p = sdp_put_str(p, "Nintendo");

    /* 0x0200 HIDParserVersion */
    p = sdp_put_attr_id(p, 0x0200);
    p = sdp_put_u16(p, 0x0111);

    /* 0x0201 HIDDeviceSubclass = 0x48 */
    p = sdp_put_attr_id(p, 0x0201);
    p = sdp_put_u8(p, 0x48);

    /* 0x0202 HIDCountryCode */
    p = sdp_put_attr_id(p, 0x0202);
    p = sdp_put_u8(p, 0x00);

    /* 0x0203 HIDVirtualCable = true */
    p = sdp_put_attr_id(p, 0x0203);
    p = sdp_put_bool(p, true);

    /* 0x0204 HIDReconnectInitiate = true */
    p = sdp_put_attr_id(p, 0x0204);
    p = sdp_put_bool(p, true);

    /* 0x0205 HIDDescriptorList */
    p = sdp_put_attr_id(p, 0x0205);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        p = sdp_put_u8(p, 0x22);
        *p++ = 0x25;
        *p++ = (uint8_t)WIIMOTE_HID_DESCRIPTOR_LEN;
        memcpy(p, WIIMOTE_HID_DESCRIPTOR, WIIMOTE_HID_DESCRIPTOR_LEN);
        p += WIIMOTE_HID_DESCRIPTOR_LEN;
        *il = (uint8_t)(p - il - 1); }
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0206 HIDLANGIDBaseList */
    p = sdp_put_attr_id(p, 0x0206);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        p = sdp_put_u16(p, 0x0409);
        p = sdp_put_u16(p, 0x0100);
        *il = (uint8_t)(p - il - 1); }
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0207 HIDSDPDisable = false */
    p = sdp_put_attr_id(p, 0x0207);
    p = sdp_put_bool(p, false);

    /* 0x0208 HIDBatteryPower = true */
    p = sdp_put_attr_id(p, 0x0208);
    p = sdp_put_bool(p, true);

    /* 0x0209 HIDRemoteWake = true */
    p = sdp_put_attr_id(p, 0x0209);
    p = sdp_put_bool(p, true);

    /* 0x020A HIDProfileVersion */
    p = sdp_put_attr_id(p, 0x020A);
    p = sdp_put_u16(p, 0x0100);

    /* 0x020B HIDSupervisionTimeout */
    p = sdp_put_attr_id(p, 0x020B);
    p = sdp_put_u16(p, 0x0C80);

    /* 0x020C HIDNormallyConnectable = false */
    p = sdp_put_attr_id(p, 0x020C);
    p = sdp_put_bool(p, false);

    /* 0x020D HIDBootDevice = false */
    p = sdp_put_attr_id(p, 0x020D);
    p = sdp_put_bool(p, false);

    *outer_len_p = (uint8_t)(p - outer_len_p - 1);

    uint16_t record_len = (uint16_t)(p - buf);

    esp_sdp_record_t sdp_rec = {
        .type        = ESP_SDP_TYPE_RAW_SDP,
        .sdp_raw.len = record_len,
        .sdp_raw.val = buf,
    };

    esp_err_t ret = esp_sdp_create_record(&sdp_rec, &s_hid_sdp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register HID SDP record: %s",
                 esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "HID SDP record registered (handle=%d)",
                 s_hid_sdp_handle);
    }
    return ret;
}

/* -----------------------------------------------------------------------
 * PnP SDP record (UUID 0x1200)
 * ----------------------------------------------------------------------- */
static esp_err_t register_pnp_sdp_record(void) {
    static uint8_t buf[128];
    uint8_t *p = buf;

    *p++ = 0x35; uint8_t *ol = p; *p++ = 0;

    p = sdp_put_attr_id(p, 0x0001);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      p = sdp_put_uuid16(p, 0x1200);
      *sl = (uint8_t)(p - sl - 1); }

    p = sdp_put_attr_id(p, 0x0200);
    p = sdp_put_u16(p, 0x0100);

    p = sdp_put_attr_id(p, 0x0201);
    p = sdp_put_u16(p, WIIMOTE_VENDOR_ID);

    p = sdp_put_attr_id(p, 0x0202);
    p = sdp_put_u16(p, WIIMOTE_PRODUCT_ID);

    p = sdp_put_attr_id(p, 0x0203);
    p = sdp_put_u16(p, WIIMOTE_VERSION);

    p = sdp_put_attr_id(p, 0x0204);
    p = sdp_put_bool(p, true);

    p = sdp_put_attr_id(p, 0x0205);
    p = sdp_put_u16(p, 0x0002);

    *ol = (uint8_t)(p - ol - 1);
    uint16_t len = (uint16_t)(p - buf);

    esp_sdp_record_t sdp_rec = {
        .type        = ESP_SDP_TYPE_RAW_SDP,
        .sdp_raw.len = len,
        .sdp_raw.val = buf,
    };

    esp_err_t ret = esp_sdp_create_record(&sdp_rec, &s_pnp_sdp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PnP SDP record: %s",
                 esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "PnP SDP record registered (handle=%d)",
                 s_pnp_sdp_handle);
    }
    return ret;
}

esp_err_t wiimote_sdp_register(void) {
    esp_err_t r;

    r = esp_sdp_init();
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_sdp_init failed: %s", esp_err_to_name(r));
        return r;
    }

    r = register_hid_sdp_record();
    if (r != ESP_OK) return r;

    return register_pnp_sdp_record();
}
