/*
 * wiimote_sdp.c
 *
 * Registers the two SDP service records that a real Nintendo RVL-CNT-01
 * exposes:
 *   Record 1 (handle 0x10000): HID service  (UUID 0x1124)
 *   Record 2 (handle 0x10001): PnP service  (UUID 0x1200)
 *
 * All values are taken verbatim from documented Wiimote SDP dumps:
 *   - WiiBrew Wiimote/SDP_Information wiki page
 *   - Bluetooth_Specs dump at yts.rdy.jp
 *   - jloehr HID-Wiimote bachelor thesis captures
 *
 * The HID descriptor (SDP attribute 0x0206) is the raw byte sequence
 * captured from a real Wii Remote over hcidump. It describes report IDs
 * and payload lengths only; the Wiimote does NOT use standard HID usages
 * for the actual data content.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_sdp_api.h"
#include "wiimote_sdp.h"
#include "wiimote_bt.h"

static const char *TAG = "wiimote_sdp";

/* -----------------------------------------------------------------------
 * HID Descriptor (SDP attribute 0x0206, type 0x22 = HID descriptor)
 *
 * Byte sequence captured from Nintendo RVL-CNT-01 via hcidump.
 * Source: http://www.yts.rdy.jp/pic/GB002/Bluetooth_Specs.htm
 *         (attribute 0x206 raw data)
 *
 * Human-readable decode (abridged):
 *   05 01       USAGE_PAGE (Generic Desktop)
 *   09 05       USAGE (Gamepad)
 *   a1 01       COLLECTION (Application)
 *   85 10       REPORT_ID (0x10)  Output  1 byte  (Rumble/LED)
 *   ...
 *   85 30       REPORT_ID (0x30)  Input   2 bytes (Core Buttons)
 *   85 31       REPORT_ID (0x31)  Input   5 bytes (Buttons+Accel)
 *   85 32       REPORT_ID (0x32)  Input  10 bytes (Buttons+8ext)
 *   85 33       REPORT_ID (0x33)  Input  17 bytes (Buttons+Accel+12IR)
 *   ...
 *   c0          END_COLLECTION
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
 * SDP Record Registration
 *
 * esp-idf exposes esp_sdp_create_record() / esp_sdp_init() which accept
 * a raw esp_sdp_record_t. We build the two records manually to match the
 * exact attribute set of a real Wii Remote.
 * ----------------------------------------------------------------------- */

/*
 * Build and register the HID SDP service record (UUID 0x1124).
 *
 * Attributes (from real Wiimote SDP dump):
 *   0x0000  ServiceRecordHandle
 *   0x0001  ServiceClassIDList      [0x1124 HID]
 *   0x0004  ProtocolDescriptorList  L2CAP(PSM=0x11) + HIDP
 *   0x0005  BrowseGroupList         [0x1002 PublicBrowseGroup]
 *   0x0006  LanguageBaseAttrIDList  en, UTF-8, base=0x100
 *   0x0009  BluetoothProfileDescriptorList [0x1124 v1.00]
 *   0x000D  AdditionalProtocolDescriptorLists L2CAP(0x13) + HIDP
 *   0x0100  ServiceName             "Nintendo RVL-CNT-01"
 *   0x0101  ServiceDescription      "Nintendo RVL-CNT-01"
 *   0x0102  ServiceProvider         "Nintendo"
 *   0x0200  HIDParserVersion        0x0111
 *   0x0201  HIDDeviceSubclass       0x48  (gamepad + connectable)
 *   0x0202  HIDCountryCode          0x00
 *   0x0203  HIDVirtualCable         true
 *   0x0204  HIDReconnectInitiate    true
 *   0x0205  HIDDescriptorList       (type=0x22, descriptor bytes)
 *   0x0206  HIDLANGIDBaseList       [0x0409, 0x0100]
 *   0x0207  HIDSDPDisable           false
 *   0x0208  HIDBatteryPower         true
 *   0x0209  HIDRemoteWake           true
 *   0x020A  HIDProfileVersion       0x0100
 *   0x020B  HIDSupervisionTimeout   0x0C80  (3200 slots = 2 seconds)
 *   0x020C  HIDNormallyConnectable  false
 *   0x020D  HIDBootDevice           false
 *
 * ESP-IDF's SDP API (esp_sdp_api.h) uses esp_sdp_record_t which is a
 * flexible raw record type. We use esp_sdp_create_raw_record() to push
 * the pre-encoded SDP PDU directly.
 */

/* Helper: encode a uint8 SDP data element (type=1 size=0 → 0x08) */
static uint8_t *sdp_put_u8(uint8_t *p, uint8_t val) {
    *p++ = 0x08; /* type=1 (uint), size=0 (1 byte) */
    *p++ = val;
    return p;
}

/* Helper: encode a uint16 SDP data element (0x09) */
static uint8_t *sdp_put_u16(uint8_t *p, uint16_t val) {
    *p++ = 0x09;
    *p++ = (uint8_t)(val >> 8);
    *p++ = (uint8_t)(val & 0xFF);
    return p;
}

/* Helper: encode a boolean SDP data element (type=5 size=0 → 0x28) */
static uint8_t *sdp_put_bool(uint8_t *p, bool val) {
    *p++ = 0x28;
    *p++ = val ? 0x01 : 0x00;
    return p;
}

/* Helper: encode a UUID16 data element (type=3 size=1 → 0x19) */
static uint8_t *sdp_put_uuid16(uint8_t *p, uint16_t uuid) {
    *p++ = 0x19;
    *p++ = (uint8_t)(uuid >> 8);
    *p++ = (uint8_t)(uuid & 0xFF);
    return p;
}

/* Helper: encode a text string data element (type=4, size descriptor) */
static uint8_t *sdp_put_str(uint8_t *p, const char *str) {
    uint8_t len = (uint8_t)strlen(str);
    *p++ = 0x25; /* type=4 (text), size=5 (1-byte length follows) */
    *p++ = len;
    memcpy(p, str, len);
    return p + len;
}

/* Helper: write a 2-byte SDP attribute ID header */
static uint8_t *sdp_put_attr_id(uint8_t *p, uint16_t attr_id) {
    *p++ = 0x09; /* uint16 */
    *p++ = (uint8_t)(attr_id >> 8);
    *p++ = (uint8_t)(attr_id & 0xFF);
    return p;
}

/* Helper: data sequence with known length (type=6, size=5 → 0x35 LL) */
static uint8_t *sdp_seq_begin(uint8_t *p, uint8_t *len_byte_ptr_out[1]) {
    *p++ = 0x35; /* type=6 (sequence), size indicator=5 (1-byte length) */
    *len_byte_ptr_out[0] = p; /* caller fills this in afterwards */
    *p++ = 0x00; /* placeholder */
    return p;
}

/* -----------------------------------------------------------------------
 * Register SDP records via ESP-IDF esp_sdp_create_record().
 *
 * ESP-IDF 5.x provides esp_sdp_init() + esp_sdp_create_record() which
 * take an esp_sdp_record_t. The record type for raw HID records is
 * ESP_SDP_TYPE_RAW_SDP.  We build a flat byte buffer containing the
 * full SDP AttributeList PDU and hand it to the API.
 * ----------------------------------------------------------------------- */

/*
 * Rather than building the PDU byte-by-byte (error-prone), we use the
 * Bluedroid internal SDP helper which ESP-IDF re-exports for application
 * use as of IDF 4.4+:
 *
 *   esp_sdp_search_record_t    (query)
 *   esp_sdp_record_t           (create)
 *
 * For HID device registration the cleanest approach supported by the
 * current ESP-IDF stable release is to call esp_bt_gap_set_scan_mode()
 * for discoverability and esp_bt_hid_device_register_app() … HOWEVER
 * the built-in HID device profile limits the descriptor and doesn't
 * give us full control over every SDP attribute.
 *
 * Therefore we use the LOW-LEVEL Bluedroid SDP API exposed through
 * esp_sdp_api.h (ESP-IDF ≥ 4.4):
 *
 *   ESP_SDP_TYPE_RAW_SDP  — pass raw attribute list
 *
 * Ref: esp-idf/components/bt/host/bluedroid/api/include/api/esp_sdp_api.h
 */

#include "esp_sdp_api.h"

/* We store the handle so we could de-register it later if needed */
static int s_hid_sdp_handle  = -1;
static int s_pnp_sdp_handle  = -1;

/*
 * Build the HID SDP record attribute list (condensed but complete).
 *
 * Layout of the buffer we construct:
 *   DataElement SEQUENCE { ... all attribute ID + value pairs ... }
 *
 * The esp_sdp_create_record() raw variant expects JUST the attribute
 * list (not the outer sequence wrapper), so we pass the inner bytes.
 */
static esp_err_t register_hid_sdp_record(void) {
    /*
     * We use the ESP-IDF HID device SDP convenience struct
     * esp_hid_sdp_desc_list_t and esp_hid_sdp_attr_t where available,
     * but for maximum control we build a raw esp_sdp_record_t.
     *
     * The simplest reliable path on ESP-IDF 4.4 / 5.x is to call
     * esp_sdp_create_record() with type ESP_SDP_TYPE_RAW_SDP and supply
     * a pre-encoded attribute list.  We build that list now.
     */

    /* Maximum record size estimate (conservative) */
    static uint8_t  buf[512];
    uint8_t        *p    = buf;
    uint8_t        *seq_len;  /* pointer to the sequence length byte */

    /* Outer sequence wrapping entire attribute list */
    /* 0x35 = sequence, 0x?? = length — fill at end */
    uint8_t *outer_hdr = p;
    *p++ = 0x35;
    uint8_t *outer_len_p = p;
    *p++ = 0x00; /* placeholder */

    /* 0x0001 ServiceClassIDList = { UUID16:0x1124 } */
    p = sdp_put_attr_id(p, 0x0001);
    { uint8_t *s = p; *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      p = sdp_put_uuid16(p, 0x1124);
      *sl = (uint8_t)(p - sl - 1); (void)s; }

    /* 0x0004 ProtocolDescriptorList = { {L2CAP, PSM=0x11}, {HIDP} } */
    p = sdp_put_attr_id(p, 0x0004);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      /* inner seq 1: L2CAP + PSM */
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        p = sdp_put_uuid16(p, 0x0100); /* L2CAP */
        p = sdp_put_u16(p, HID_PSM_CONTROL);
        *il = (uint8_t)(p - il - 1); }
      /* inner seq 2: HIDP */
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        p = sdp_put_uuid16(p, 0x0011); /* HIDP */
        *il = (uint8_t)(p - il - 1); }
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0005 BrowseGroupList = { UUID16:0x1002 } */
    p = sdp_put_attr_id(p, 0x0005);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      p = sdp_put_uuid16(p, 0x1002);
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0006 LanguageBaseAttributeIDList = { en, UTF-8, base=0x100 } */
    p = sdp_put_attr_id(p, 0x0006);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      p = sdp_put_u16(p, 0x656e); /* "en" ISO-639 */
      p = sdp_put_u16(p, 0x006a); /* UTF-8 encoding (MIBenum 106) */
      p = sdp_put_u16(p, 0x0100); /* base offset 0x100 */
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0009 BluetoothProfileDescriptorList = { {HID 0x1124, v1.00} } */
    p = sdp_put_attr_id(p, 0x0009);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        p = sdp_put_uuid16(p, 0x1124);
        p = sdp_put_u16(p, 0x0100);
        *il = (uint8_t)(p - il - 1); }
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x000D AdditionalProtocolDescriptorLists = HID Interrupt channel */
    p = sdp_put_attr_id(p, 0x000D);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;  /* outer */
      { *p++ = 0x35; uint8_t *ml = p; *p++ = 0; /* middle */
        { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
          p = sdp_put_uuid16(p, 0x0100); /* L2CAP */
          p = sdp_put_u16(p, HID_PSM_INTERRUPT);
          *il = (uint8_t)(p - il - 1); }
        { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
          p = sdp_put_uuid16(p, 0x0011); /* HIDP */
          *il = (uint8_t)(p - il - 1); }
        *ml = (uint8_t)(p - ml - 1); }
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0100 ServiceName (language base + 0 = 0x100) */
    p = sdp_put_attr_id(p, 0x0100);
    p = sdp_put_str(p, WIIMOTE_DEVICE_NAME);

    /* 0x0101 ServiceDescription */
    p = sdp_put_attr_id(p, 0x0101);
    p = sdp_put_str(p, WIIMOTE_DEVICE_NAME);

    /* 0x0102 ServiceProvider */
    p = sdp_put_attr_id(p, 0x0102);
    p = sdp_put_str(p, "Nintendo");

    /* 0x0200 HIDParserVersion = 0x0111 */
    p = sdp_put_attr_id(p, 0x0200);
    p = sdp_put_u16(p, 0x0111);

    /* 0x0201 HIDDeviceSubclass = 0x48 (gamepad, connectable) */
    p = sdp_put_attr_id(p, 0x0201);
    p = sdp_put_u8(p, 0x48);

    /* 0x0202 HIDCountryCode = 0 */
    p = sdp_put_attr_id(p, 0x0202);
    p = sdp_put_u8(p, 0x00);

    /* 0x0203 HIDVirtualCable = true */
    p = sdp_put_attr_id(p, 0x0203);
    p = sdp_put_bool(p, true);

    /* 0x0204 HIDReconnectInitiate = true */
    p = sdp_put_attr_id(p, 0x0204);
    p = sdp_put_bool(p, true);

    /* 0x0205 HIDDescriptorList = { { type=0x22, descriptor_bytes } } */
    p = sdp_put_attr_id(p, 0x0205);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        /* descriptor type: 0x22 = Report Descriptor */
        p = sdp_put_u8(p, 0x22);
        /* descriptor bytes as SDP octet string */
        *p++ = 0x25; /* text/octet string with 1-byte length */
        *p++ = (uint8_t)WIIMOTE_HID_DESCRIPTOR_LEN;
        memcpy(p, WIIMOTE_HID_DESCRIPTOR, WIIMOTE_HID_DESCRIPTOR_LEN);
        p += WIIMOTE_HID_DESCRIPTOR_LEN;
        *il = (uint8_t)(p - il - 1); }
      *sl = (uint8_t)(p - sl - 1); }

    /* 0x0206 HIDLANGIDBaseList = { { 0x0409, 0x0100 } } */
    p = sdp_put_attr_id(p, 0x0206);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      { *p++ = 0x35; uint8_t *il = p; *p++ = 0;
        p = sdp_put_u16(p, 0x0409); /* English (US) */
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

    /* 0x020A HIDProfileVersion = 0x0100 */
    p = sdp_put_attr_id(p, 0x020A);
    p = sdp_put_u16(p, 0x0100);

    /* 0x020B HIDSupervisionTimeout = 0x0C80 (3200 BT slots ≈ 2 s) */
    p = sdp_put_attr_id(p, 0x020B);
    p = sdp_put_u16(p, 0x0C80);

    /* 0x020C HIDNormallyConnectable = false */
    p = sdp_put_attr_id(p, 0x020C);
    p = sdp_put_bool(p, false);

    /* 0x020D HIDBootDevice = false */
    p = sdp_put_attr_id(p, 0x020D);
    p = sdp_put_bool(p, false);

    /* Fill outer sequence length */
    *outer_len_p = (uint8_t)(p - outer_len_p - 1);
    (void)outer_hdr;

    uint16_t record_len = (uint16_t)(p - buf);

    esp_sdp_record_t sdp_rec = {
        .type         = ESP_SDP_TYPE_RAW_SDP,
        .sdp_raw.len  = record_len,
        .sdp_raw.val  = buf,
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
 * PnP SDP record (UUID 0x1200) — required for Wii to recognise device
 *
 * Attributes:
 *   0x0001  ServiceClassIDList  [0x1200 PnPInformation]
 *   0x0200  SpecificationID     0x0100
 *   0x0201  VendorID            0x057E (Nintendo)
 *   0x0202  ProductID           0x0306 (Wii Remote)
 *   0x0203  Version             0x0001
 *   0x0204  PrimaryRecord       true
 *   0x0205  VendorIDSource      0x0002 (Bluetooth SIG)
 * ----------------------------------------------------------------------- */
static esp_err_t register_pnp_sdp_record(void) {
    static uint8_t  buf[128];
    uint8_t        *p = buf;

    *p++ = 0x35; uint8_t *ol = p; *p++ = 0; /* outer sequence */

    /* ServiceClassIDList */
    p = sdp_put_attr_id(p, 0x0001);
    { *p++ = 0x35; uint8_t *sl = p; *p++ = 0;
      p = sdp_put_uuid16(p, 0x1200);
      *sl = (uint8_t)(p - sl - 1); }

    /* SpecificationID = 0x0100 */
    p = sdp_put_attr_id(p, 0x0200);
    p = sdp_put_u16(p, 0x0100);

    /* VendorID = 0x057E */
    p = sdp_put_attr_id(p, 0x0201);
    p = sdp_put_u16(p, WIIMOTE_VENDOR_ID);

    /* ProductID = 0x0306 */
    p = sdp_put_attr_id(p, 0x0202);
    p = sdp_put_u16(p, WIIMOTE_PRODUCT_ID);

    /* Version = 0x0001 */
    p = sdp_put_attr_id(p, 0x0203);
    p = sdp_put_u16(p, WIIMOTE_VERSION);

    /* PrimaryRecord = true */
    p = sdp_put_attr_id(p, 0x0204);
    p = sdp_put_bool(p, true);

    /* VendorIDSource = 0x0002 (Bluetooth SIG) */
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

/* Public: register both SDP records */
esp_err_t wiimote_sdp_register(void) {
    esp_err_t r;

    r = esp_sdp_init();
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_sdp_init failed: %s", esp_err_to_name(r));
        return r;
    }

    r = register_hid_sdp_record();
    if (r != ESP_OK) return r;

    r = register_pnp_sdp_record();
    return r;
}
