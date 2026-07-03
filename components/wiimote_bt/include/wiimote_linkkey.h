#pragma once

#include <stdint.h>
#include "esp_bt_defs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * wiimote_linkkey.h
 *
 * Stores/loads Bluetooth link keys to/from NVS flash so the bond
 * with the Wii persists across power cycles.
 *
 * The Wii will refuse to use a previously paired remote unless the
 * link key produced during pairing is re-presented during authentication
 * on subsequent connections.  Without NVS persistence the ESP32 would
 * have to re-pair every time it is rebooted.
 */

#define LINKKEY_MAX_ENTRIES  4  /* Support up to 4 simultaneous bonds */

typedef struct {
    esp_bd_addr_t bda;
    uint8_t       link_key[16];
    uint8_t       key_type;   /* esp_bt_link_key_type_t */
    bool          valid;
} linkkey_entry_t;

esp_err_t wiimote_linkkey_init(void);
esp_err_t wiimote_linkkey_store(const esp_bd_addr_t bda,
                                const uint8_t *key,
                                uint8_t key_type);
bool      wiimote_linkkey_find(const esp_bd_addr_t bda,
                               uint8_t *key_out,
                               uint8_t *key_type_out);
esp_err_t wiimote_linkkey_delete(const esp_bd_addr_t bda);
void      wiimote_linkkey_list_all(void);

#ifdef __cplusplus
}
#endif
