/*
 * main.c — ESP32 Wii Remote Emulator
 *
 * Phase 2: Bluetooth pairing + persistent connection + D-Pad + A-Button.
 *
 * Button wiring (active-low, internal pull-up, connect GPIO to GND):
 *
 *   GPIO 18 → D-Pad Up
 *   GPIO 19 → D-Pad Down
 *   GPIO 21 → D-Pad Left
 *   GPIO 22 → D-Pad Right
 *   GPIO 23 → A Button  (confirm / select in Wii menu)
 *
 * Change the pin numbers in the #define section below to match your wiring.
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
 * Pin assignments — change these to match your wiring
 * ----------------------------------------------------------------------- */
#define GPIO_DPAD_UP    18
#define GPIO_DPAD_DOWN  19
#define GPIO_DPAD_LEFT  21
#define GPIO_DPAD_RIGHT 22
#define GPIO_BTN_A      23

/* Debounce time in milliseconds */
#define DEBOUNCE_MS     20

/* Poll interval in milliseconds */
#define POLL_MS         10

/* -----------------------------------------------------------------------
 * Button polling task
 *
 * Reads GPIO pins every POLL_MS ms, debounces each individually, and
 * sends a core button report (0x30) to the Wii on any state change.
 *
 * Active-low: GPIO reads 0 when pressed, 1 when released.
 * ----------------------------------------------------------------------- */

/* Per-button debounce state */
typedef struct {
    int      gpio;
    uint8_t  byte;      /* which byte (0 or 1) of the report */
    uint8_t  mask;      /* bitmask within that byte */
    uint8_t  raw;       /* last raw reading */
    uint8_t  stable;    /* debounced stable reading */
    uint32_t last_ms;   /* tick of last raw change */
} btn_state_t;

static btn_state_t s_buttons[] = {
    { GPIO_DPAD_UP,    0, BTN_BYTE0_DPAD_UP,    1, 0, 0 },
    { GPIO_DPAD_DOWN,  0, BTN_BYTE0_DPAD_DOWN,  1, 0, 0 },
    { GPIO_DPAD_LEFT,  0, BTN_BYTE0_DPAD_LEFT,  1, 0, 0 },
    { GPIO_DPAD_RIGHT, 0, BTN_BYTE0_DPAD_RIGHT, 1, 0, 0 },
    { GPIO_BTN_A,      1, BTN_BYTE1_A,          1, 0, 0 },
};

#define NUM_BUTTONS (sizeof(s_buttons) / sizeof(s_buttons[0]))

static void button_task(void *arg)
{
    /* Configure all pins as input with pull-up */
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_buttons[i].gpio),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    ESP_LOGI(TAG, "Button task started");
    ESP_LOGI(TAG, "  D-Up=%d  D-Down=%d  D-Left=%d  D-Right=%d  A=%d",
             GPIO_DPAD_UP, GPIO_DPAD_DOWN,
             GPIO_DPAD_LEFT, GPIO_DPAD_RIGHT, GPIO_BTN_A);

    uint8_t prev_byte0 = 0x00;
    uint8_t prev_byte1 = 0x00;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));

        /* Only send reports when connected */
        if (wiimote_bt_get_state() != WIIMOTE_STATE_CONNECTED) {
            /* Reset all debounce state on disconnect */
            for (size_t i = 0; i < NUM_BUTTONS; i++) {
                s_buttons[i].raw    = 1; /* pull-up high = not pressed */
                s_buttons[i].stable = 0;
                s_buttons[i].last_ms = 0;
            }
            prev_byte0 = 0x00;
            prev_byte1 = 0x00;
            continue;
        }

        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        /* Debounce each button independently */
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            uint8_t raw = (uint8_t)gpio_get_level(s_buttons[i].gpio);
            if (raw != s_buttons[i].raw) {
                s_buttons[i].raw     = raw;
                s_buttons[i].last_ms = now_ms;
            }
            if ((now_ms - s_buttons[i].last_ms) >= DEBOUNCE_MS) {
                /*
                 * Active-low: level 0 → pressed → bit set in report.
                 * stable holds 1 if the button is currently pressed.
                 */
                s_buttons[i].stable = (s_buttons[i].raw == 0) ? 1u : 0u;
            }
        }

        /* Build report bytes from stable state */
        uint8_t byte0 = 0x00;
        uint8_t byte1 = 0x00;
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            if (s_buttons[i].stable) {
                if (s_buttons[i].byte == 0) byte0 |= s_buttons[i].mask;
                else                        byte1 |= s_buttons[i].mask;
            }
        }

        /* Send report only on state change */
        if (byte0 != prev_byte0 || byte1 != prev_byte1) {
            ESP_LOGD(TAG, "Buttons: byte0=0x%02X byte1=0x%02X", byte0, byte1);
            esp_err_t ret = wiimote_bt_send_buttons(byte0, byte1);
            if (ret == ESP_OK) {
                prev_byte0 = byte0;
                prev_byte1 = byte1;
            }
        }
    }
}

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
    ESP_LOGI(TAG, "Press the SYNC button on the Wii to pair");

    xTaskCreate(button_task, "wii_buttons", 2048, NULL, 10, NULL);

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
