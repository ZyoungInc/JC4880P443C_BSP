
idf_component_register(
    SRCS "JC4880P443C_BSP.c" "bsp_board_extra.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES
        driver
        esp_lcd
        espressif__esp_codec_dev
        espressif__esp_lvgl_port
        lvgl__lvgl
        chmorgan__esp-audio-player
        chmorgan__esp-file-iterator
    PRIV_REQUIRES usb spiffs fatfs esp_timer esp_psram esp_mm
)
