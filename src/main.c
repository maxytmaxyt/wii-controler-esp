/* ESP32 Wii Remote emulator with browser-based controls. */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "web_control.h"
#include "wiimote_bt.h"

static const char *TAG = "main";

/* -----------------------------------------------------------------------
 * app_main
 * ----------------------------------------------------------------------- */
void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " ESP32 Wii Remote Emulator — Phase 2");
    ESP_LOGI(TAG, " Bluetooth + D-Pad + A-Button");
    ESP_LOGI(TAG, "==============================================");

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires erase — stored bonds will be lost");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialised");

    ret = wiimote_bt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wiimote_bt_init failed: %s — halting",
                 esp_err_to_name(ret));
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    ESP_LOGI(TAG, "Wiimote BT stack running");
    ESP_LOGI(TAG, "Connect to the WiiRemote-ESP32 Wi-Fi network to use the controls");

    ESP_ERROR_CHECK(web_control_start());

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        wiimote_state_t state = wiimote_bt_get_state();
        const char *s;
        switch (state) {
        case WIIMOTE_STATE_IDLE:           s = "IDLE";           break;
        case WIIMOTE_STATE_DISCOVERABLE:   s = "DISCOVERABLE";   break;
        case WIIMOTE_STATE_CONNECTING:     s = "CONNECTING";     break;
        case WIIMOTE_STATE_AUTHENTICATING: s = "AUTHENTICATING"; break;
        case WIIMOTE_STATE_CONNECTED:      s = "CONNECTED";      break;
        case WIIMOTE_STATE_RECONNECTING:   s = "RECONNECTING";   break;
        default:                           s = "UNKNOWN";        break;
        }
        ESP_LOGI(TAG, "State: %s", s);
    }
}
