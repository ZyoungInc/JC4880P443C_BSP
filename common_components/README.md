# JC4880P443C BSP Component Set

This repository is intended to be used as the local `common_components` tree for
the JC4880P443C ESP-IDF demos.

## Components

- `JC4880P443C_BSP`: board support package with display, touch, audio, SD card,
  USB, SPIFFS, and the former `bsp_extra` helper API.
- `espressif__esp_lcd_st7701`: local ST7701 RGB/MIPI-DSI panel driver.
- `espressif__esp_lcd_touch`: local base touch API component.
- `espressif__esp_lcd_touch_gt911`: local GT911 touch controller driver.
- `espressif__esp_codec_dev`: local audio codec device driver used by the
  ES8311 board audio path.

The audio player, file iterator, and MP3 decoder components are intentionally
not vendored here. They remain Component Manager dependencies because they are
media/application helpers rather than board-specific hardware drivers.

## Use In A Project

Point ESP-IDF at this directory as an extra component directory:

```cmake
set(EXTRA_COMPONENT_DIRS
    ./common_components
)
```

The BSP manifest uses relative `path` dependencies for the local panel, touch,
and codec components in this directory.
