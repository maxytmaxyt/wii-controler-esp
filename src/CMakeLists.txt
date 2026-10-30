idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        wiimote_bt
        nvs_flash
        freertos
        driver
)
