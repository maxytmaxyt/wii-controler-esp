/*
 * wiimote_linkkey.c
 *
 * Persists Bluetooth link keys in NVS so the ESP32 can re-authenticate
 * with the Wii after a reboot without requiring a new SYNC pairing.
 *
 * Storage layout (NVS namespace "wiimote", key "link_keys"):
 *   Raw blob of LINKKEY_MAX_ENTRIES × sizeof(linkkey_entry_t) bytes.
 */

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "wiimote_linkkey.h"
#include "wiimote_bt.h"

static const char *TAG = "wiimote_lk";

static linkkey_entry_t s_entries[LINKKEY_MAX_ENTRIES];
static nvs_handle_t    s_nvs_handle;

esp_err_t wiimote_linkkey_init(void) {
    /* NVS should already be initialised by the caller (main) */
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Load existing keys from flash */
    size_t size = sizeof(s_entries);
    ret = nvs_get_blob(s_nvs_handle, NVS_KEY_LINKKEYS, s_entries, &size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No link keys in NVS yet — starting fresh");
        memset(s_entries, 0, sizeof(s_entries));
        ret = ESP_OK;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob failed: %s", esp_err_to_name(ret));
        memset(s_entries, 0, sizeof(s_entries));
    } else {
        ESP_LOGI(TAG, "Loaded %d link key slot(s) from NVS",
                 LINKKEY_MAX_ENTRIES);
        wiimote_linkkey_list_all();
    }
    return ret;
}

static esp_err_t save_to_nvs(void) {
    esp_err_t ret = nvs_set_blob(s_nvs_handle, NVS_KEY_LINKKEYS,
                                  s_entries, sizeof(s_entries));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_commit(s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t wiimote_linkkey_store(const esp_bd_addr_t bda,
                                const uint8_t *key,
                                uint8_t key_type) {
    /* Update existing entry if BDA already known */
    for (int i = 0; i < LINKKEY_MAX_ENTRIES; i++) {
        if (s_entries[i].valid &&
            memcmp(s_entries[i].bda, bda, sizeof(esp_bd_addr_t)) == 0) {
            memcpy(s_entries[i].link_key, key, 16);
            s_entries[i].key_type = key_type;
            ESP_LOGI(TAG, "Updated link key for "
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            return save_to_nvs();
        }
    }

    /* Find a free slot */
    for (int i = 0; i < LINKKEY_MAX_ENTRIES; i++) {
        if (!s_entries[i].valid) {
            memcpy(s_entries[i].bda, bda, sizeof(esp_bd_addr_t));
            memcpy(s_entries[i].link_key, key, 16);
            s_entries[i].key_type = key_type;
            s_entries[i].valid    = true;
            ESP_LOGI(TAG, "Stored new link key for "
                     "%02X:%02X:%02X:%02X:%02X:%02X (slot %d)",
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], i);
            return save_to_nvs();
        }
    }

    /* No free slot — overwrite oldest (slot 0) */
    ESP_LOGW(TAG, "Link key table full, evicting slot 0");
    memcpy(s_entries[0].bda, bda, sizeof(esp_bd_addr_t));
    memcpy(s_entries[0].link_key, key, 16);
    s_entries[0].key_type = key_type;
    s_entries[0].valid    = true;
    return save_to_nvs();
}

bool wiimote_linkkey_find(const esp_bd_addr_t bda,
                          uint8_t *key_out,
                          uint8_t *key_type_out) {
    for (int i = 0; i < LINKKEY_MAX_ENTRIES; i++) {
        if (s_entries[i].valid &&
            memcmp(s_entries[i].bda, bda, sizeof(esp_bd_addr_t)) == 0) {
            if (key_out)      memcpy(key_out, s_entries[i].link_key, 16);
            if (key_type_out) *key_type_out = s_entries[i].key_type;
            return true;
        }
    }
    return false;
}

esp_err_t wiimote_linkkey_delete(const esp_bd_addr_t bda) {
    for (int i = 0; i < LINKKEY_MAX_ENTRIES; i++) {
        if (s_entries[i].valid &&
            memcmp(s_entries[i].bda, bda, sizeof(esp_bd_addr_t)) == 0) {
            memset(&s_entries[i], 0, sizeof(linkkey_entry_t));
            ESP_LOGI(TAG, "Deleted link key for "
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            return save_to_nvs();
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void wiimote_linkkey_list_all(void) {
    ESP_LOGI(TAG, "Stored link keys:");
    bool any = false;
    for (int i = 0; i < LINKKEY_MAX_ENTRIES; i++) {
        if (s_entries[i].valid) {
            ESP_LOGI(TAG, "  [%d] %02X:%02X:%02X:%02X:%02X:%02X  type=%d",
                     i,
                     s_entries[i].bda[0], s_entries[i].bda[1],
                     s_entries[i].bda[2], s_entries[i].bda[3],
                     s_entries[i].bda[4], s_entries[i].bda[5],
                     s_entries[i].key_type);
            any = true;
        }
    }
    if (!any) ESP_LOGI(TAG, "  (none)");
}
