/*
 * main.c — ESP32 Wii Remote Emulator
 *
 * Phase 2: Bluetooth pairing + persistent connection + D-Pad input.
 *
 * D-Pad GPIO wiring (active-low with internal pull-up):
 *   GPIO 32 → D-Pad Up
 *   GPIO 33 → D-Pad Down
 *   GPIO 25 → D-Pad Left
 *   GPIO 26 → D-Pad Right
 *
 * Change the pin numbers below to match your actual wiring.
 * The buttons connect GPIO to GND; no external resistor needed.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "wiimote_bt.h"

static const char *TAG = "main";

/* -----------------------------------------------------------------------
 * D-Pad GPIO pin assignments (active-low, internal pull-up)
 *
 * Adapt these to your board wiring.
 * ----------------------------------------------------------------------- */
#define GPIO_DPAD_UP    32
#define GPIO_DPAD_DOWN  33
#define GPIO_DPAD_LEFT  25
#define GPIO_DPAD_RIGHT 26

/* Debounce time in milliseconds */
#define DEBOUNCE_MS     20

/* -----------------------------------------------------------------------
 * Button polling task
 *
 * Reads the four D-Pad GPIO pins, debounces, and sends a core button
 * report (0x30) to the Wii on any state change.
 *
 * Active-low: GPIO reads 0 when button is pressed.
 * ----------------------------------------------------------------------- */
static void button_task(void *arg)
{
    /* Configure GPIO pins as input with pull-up */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_DPAD_UP)   |
                        (1ULL << GPIO_DPAD_DOWN)  |
                        (1ULL << GPIO_DPAD_LEFT)  |
                        (1ULL << GPIO_DPAD_RIGHT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    uint8_t prev_byte0 = 0x00;
    uint8_t prev_byte1 = 0x00;

    /* Debounce state */
    uint8_t stable_byte0 = 0x00;
    TickType_t last_change = 0;

    ESP_LOGI(TAG, "Button task started (D-Pad on GPIO %d/%d/%d/%d)",
             GPIO_DPAD_UP, GPIO_DPAD_DOWN,
             GPIO_DPAD_LEFT, GPIO_DPAD_RIGHT);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10)); /* Poll every 10 ms */

        /* Only send when connected */
        if (wiimote_bt_get_state() != WIIMOTE_STATE_CONNECTED) {
            prev_byte0 = 0x00;
            prev_byte1 = 0x00;
            continue;
        }

        /*
         * Read pins (active-low: 0 = pressed).
         * gpio_get_level returns 0 or 1.
         */
        uint8_t byte0 = 0x00;

        if (gpio_get_level(GPIO_DPAD_UP)    == 0) byte0 |= BTN_BYTE0_DPAD_UP;
        if (gpio_get_level(GPIO_DPAD_DOWN)  == 0) byte0 |= BTN_BYTE0_DPAD_DOWN;
        if (gpio_get_level(GPIO_DPAD_LEFT)  == 0) byte0 |= BTN_BYTE0_DPAD_LEFT;
        if (gpio_get_level(GPIO_DPAD_RIGHT) == 0) byte0 |= BTN_BYTE0_DPAD_RIGHT;

        uint8_t byte1 = 0x00; /* No other buttons wired yet */

        /* Debounce: only accept state if stable for DEBOUNCE_MS */
        if (byte0 != stable_byte0) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_change) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
                stable_byte0 = byte0;
                last_change  = now;
            }
            /* Don't send yet — wait for stability */
            continue;
        }

        /* Send on any change */
        if (stable_byte0 != prev_byte0 || byte1 != prev_byte1) {
            ESP_LOGD(TAG, "Buttons changed: byte0=0x%02X byte1=0x%02X",
                     stable_byte0, byte1);

            esp_err_t ret = wiimote_bt_send_buttons(stable_byte0, byte1);
            if (ret == ESP_OK) {
                prev_byte0 = stable_byte0;
                prev_byte1 = byte1;
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * app_main
 * ----------------------------------------------------------------------- */
void app_main(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " ESP32 Wii Remote Emulator — Phase 2");
    ESP_LOGI(TAG, " Bluetooth + D-Pad input");
    ESP_LOGI(TAG, "==============================================");

    /* NVS flash (required by BT stack and link key store) */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires erase — stored bonds will be lost");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialised");

    /* Start Wiimote Bluetooth stack */
    ret = wiimote_bt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wiimote_bt_init failed: %s — halting",
                 esp_err_to_name(ret));
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    ESP_LOGI(TAG, "Wiimote BT stack running");
    ESP_LOGI(TAG, "Press the SYNC button on the Wii to pair");
    ESP_LOGI(TAG, "D-Pad: UP=%d DOWN=%d LEFT=%d RIGHT=%d",
             GPIO_DPAD_UP, GPIO_DPAD_DOWN,
             GPIO_DPAD_LEFT, GPIO_DPAD_RIGHT);

    /* Launch button polling task */
    xTaskCreate(button_task, "wii_buttons", 2048, NULL, 10, NULL);

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
