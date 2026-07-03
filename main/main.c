/*
 * main.c — ESP32 Wii Remote Emulator
 *
 * Phase 1: Bluetooth pairing and persistent connection only.
 *
 * What this does:
 *   - Identifies as "Nintendo RVL-CNT-01" over Bluetooth Classic
 *   - Uses the correct CoD (0x002504), SDP records, and HID descriptor
 *   - Implements the Wiimote PIN pairing protocol (reversed BDA)
 *   - Stores link keys in NVS for persistent bonding
 *   - Auto-reconnects to the Wii after reboot (no SYNC required)
 *   - Responds to status requests with a valid 0x20 status report
 *
 * What this does NOT do (future phases):
 *   - Button input
 *   - IR camera
 *   - Accelerometer
 *   - Rumble
 *   - LEDs (beyond the initial status report)
 *   - Speaker
 *   - Extensions (Nunchuk, Classic Controller, etc.)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "wiimote_bt.h"

static const char *TAG = "main";

void app_main(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " ESP32 Wii Remote Emulator — Phase 1");
    ESP_LOGI(TAG, " Bluetooth pairing + persistent connection");
    ESP_LOGI(TAG, "==============================================");

    /* Initialise NVS flash (required by BT stack and link key store) */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /*
         * NVS partition was truncated or has a version mismatch.
         * Erase and re-initialise.  This clears stored link keys and the
         * Wii BDA — a new SYNC will be required after this.
         */
        ESP_LOGW(TAG, "NVS requires erase — stored bonds will be lost");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialised");

    /* Start the Wiimote Bluetooth stack */
    ret = wiimote_bt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wiimote_bt_init failed: %s — halting",
                 esp_err_to_name(ret));
        /* Do not continue — BT stack is in an undefined state */
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "Wiimote BT stack running");
    ESP_LOGI(TAG, "Press the SYNC button on the Wii to pair");

    /* Main loop: print connection state every 5 seconds */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        wiimote_state_t state = wiimote_bt_get_state();
        const char *state_str;
        switch (state) {
        case WIIMOTE_STATE_IDLE:           state_str = "IDLE";           break;
        case WIIMOTE_STATE_DISCOVERABLE:   state_str = "DISCOVERABLE";   break;
        case WIIMOTE_STATE_CONNECTING:     state_str = "CONNECTING";     break;
        case WIIMOTE_STATE_AUTHENTICATING: state_str = "AUTHENTICATING"; break;
        case WIIMOTE_STATE_CONNECTED:      state_str = "CONNECTED";      break;
        case WIIMOTE_STATE_RECONNECTING:   state_str = "RECONNECTING";   break;
        default:                           state_str = "UNKNOWN";        break;
        }
        ESP_LOGI(TAG, "State: %s", state_str);
    }
}
