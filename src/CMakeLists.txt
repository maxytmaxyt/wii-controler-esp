idf_component_register(
    SRCS "main.c" "web_control.c"
    INCLUDE_DIRS "."
    REQUIRES
        wiimote_bt
        nvs_flash
        freertos
        driver
        esp_wifi
        esp_http_server
        esp_netif
        esp_event
)
