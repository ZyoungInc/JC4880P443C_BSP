
idf_component_register(
    SRCS "JC4880P443C_BSP.c" "bsp_board_extra.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES driver
    PRIV_REQUIRES esp_lcd usb spiffs fatfs esp_timer esp_psram esp_mm
)
