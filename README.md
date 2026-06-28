# LVGL Demo V9 Project Notes

This project is an ESP-IDF LVGL 9 demo for the ESP32-P4 board with an ST7701
MIPI-DSI display path.

## Display And Tearing Changes

- Enabled three DPI frame buffers with `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3`.
- Enabled LVGL/display synchronization with `CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y`.
- Switched the LVGL render mode to direct mode:
  - `CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y`
  - `CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH` is disabled.
- Updated `main/main.c` display config to use a full-screen buffer:
  - `.buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES`
  - `.flags.sw_rotate = false`
- Kept LVGL draw buffers in PSRAM with `.flags.buff_spiram = true`.

Direct mode requires the LVGL buffer size to be full-screen. The `bsp_display_cfg_t
cfg` in `main/main.c` still matters: the BSP reads its `buffer_size`, allocation
flags, and `lvgl_port_cfg` task settings before registering the display.

## LVGL And Performance Settings

Manual tuning already applied before this note:

- Disabled LVGL fast-memory IRAM placement:
  - `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` is disabled.
- Enabled PPA acceleration:
  - `CONFIG_LVGL_PORT_ENABLE_PPA=y`
  - `CONFIG_LV_USE_PPA=y`
  - `CONFIG_LV_USE_PPA_IMG=y`
- Set draw-buffer alignment to 128 bytes:
  - `CONFIG_LV_DRAW_BUF_ALIGN=128`
- Set the opacity/widget layer buffer size:
  - `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=131072`
- Locked the ESP32-P4 minimum chip revision to v1.0+:
  - `CONFIG_ESP32P4_REV_MIN_100=y`
  - `CONFIG_ESP32P4_REV_MIN_FULL=100`
- Increased the LVGL task resources in `main/main.c`:
  - `cfg.lvgl_port_cfg.task_stack = 32 * 1024`
  - `cfg.lvgl_port_cfg.task_priority = 5`

## Driver Cleanup

- Removed the unused EK79007 and ILI9881C LCD panel paths from the BSP.
- Fixed the BSP display path to the local ST7701 MIPI-DSI driver.
- Removed EK79007/ILI9881C dependencies from the BSP manifest and lock file.
- Removed the downloaded `managed_components` directories for:
  - `espressif__esp_lcd_ek79007`
  - `espressif__esp_lcd_ili9881c`

After reconfiguration, the ESP-IDF component list uses the local
`espressif__esp_lcd_st7701` component and no longer includes EK79007 or ILI9881C.

## BSP Merge

- Renamed the local BSP component to `JC4880P443C_BSP`.
- Merged the former `common_components/bsp_extra` helper component into
  `common_components/JC4880P443C_BSP`.
- Kept the existing public helper API names such as `bsp_extra_codec_init()` so
  app/demo code does not need broad include or function-name changes.
- Moved `bsp_extra` dependencies (`chmorgan/esp-audio-player` and
  `chmorgan/esp-file-iterator`) into the new BSP manifest.
- Localized the hardware-bound touch and audio codec driver components under
  `common_components`:
  - `espressif__esp_lcd_touch`
  - `espressif__esp_lcd_touch_gt911`
  - `espressif__esp_codec_dev`
- Left the audio player, file iterator, and MP3 decoder components in
  `managed_components` because they are app/media helpers rather than board
  hardware drivers.
- Kept touch as two local components instead of folding it into the BSP source:
  the BSP owns board pins/orientation, while `esp_lcd_touch` and
  `esp_lcd_touch_gt911` keep the reusable touch API/GT911 driver boundary.

## Current App Entry

`main/main.c` currently initializes the codec, starts the BSP display with the
custom LVGL config, turns on the backlight, and runs the selected LVGL demo.

## Build Note

`idf.py reconfigure` has passed with ESP-IDF 5.5.4 after localizing the touch
and audio codec driver components. A full build was intentionally not run.
