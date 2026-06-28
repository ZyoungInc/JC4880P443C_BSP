/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "esp_memory_utils.h"
#include "esp_dsp.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "lv_demos.h"

static lv_obj_t *fft_value[80];


void app_main(void)
{
     ESP_ERROR_CHECK(bsp_extra_codec_init());

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };

    cfg.lvgl_port_cfg.task_stack = 32 * 1024;
    cfg.lvgl_port_cfg.task_priority = 5;

    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(0);

    // lv_demo_music();
    // lv_demo_benchmark();
     lv_demo_widgets();

    bsp_display_unlock();

    bsp_display_brightness_set(100);
}
