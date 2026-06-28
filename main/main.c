/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_err.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#define WP7_COLUMNS                  2
#define WP7_MAX_TILES                6
#define WP7_STATUS_BAR_PERMILLE      50
#define WP7_SCREEN_PAD_PERMILLE      34
#define WP7_TILE_GAP_PERMILLE        34

static int32_t scaled_px(int32_t base, int32_t permille)
{
    int32_t value = (base * permille) / 1000;
    return value > 0 ? value : 1;
}

static void create_status_bar(lv_obj_t *screen, int32_t screen_w, int32_t status_h, int32_t pad)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, screen_w, status_h);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0F1D2E), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    lv_obj_t *wifi_label = lv_label_create(bar);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_label, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_20, 0);
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, pad, 0);

    lv_obj_t *time_label = lv_label_create(bar);
    lv_label_set_text(time_label, "09:41");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *battery_label = lv_label_create(bar);
    lv_label_set_text(battery_label, LV_SYMBOL_BATTERY_3);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_20, 0);
    lv_obj_align(battery_label, LV_ALIGN_RIGHT_MID, -pad, 0);
}

static void create_tile_grid(lv_obj_t *screen, int32_t screen_w, int32_t screen_h, int32_t status_h)
{
    static const char * const tile_labels[WP7_MAX_TILES] = {"1", "2", "3", "4", "5", "6"};
    const int32_t pad = scaled_px(screen_w, WP7_SCREEN_PAD_PERMILLE);
    const int32_t gap = scaled_px(screen_w, WP7_TILE_GAP_PERMILLE);
    const int32_t content_top = status_h + pad;
    const int32_t content_h = screen_h - content_top - pad;
    const int32_t tile = (screen_w - (pad * 2) - gap) / WP7_COLUMNS;
    const int32_t rows = (content_h + gap) / (tile + gap);

    for (int32_t row = 0; row < rows; row++) {
        for (int32_t col = 0; col < WP7_COLUMNS; col++) {
            const int32_t tile_number = row * WP7_COLUMNS + col + 1;
            if (tile_number > WP7_MAX_TILES) {
                return;
            }

            lv_obj_t *tile_obj = lv_obj_create(screen);
            lv_obj_remove_style_all(tile_obj);
            lv_obj_set_size(tile_obj, tile, tile);
            lv_obj_set_pos(tile_obj, pad + col * (tile + gap), content_top + row * (tile + gap));
            lv_obj_set_style_bg_color(tile_obj, lv_color_hex(0x61C9FF), 0);
            lv_obj_set_style_bg_opa(tile_obj, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(tile_obj, 0, 0);

            lv_obj_t *label = lv_label_create(tile_obj);
            lv_label_set_text(label, tile_labels[tile_number - 1]);
            lv_obj_set_style_text_color(label, lv_color_hex(0x06324A), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
            lv_obj_center(label);
        }
    }
}

static void create_wp7_first_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    const int32_t screen_w = BSP_LCD_H_RES;
    const int32_t screen_h = BSP_LCD_V_RES;
    const int32_t status_h = scaled_px(screen_h, WP7_STATUS_BAR_PERMILLE);
    const int32_t pad = scaled_px(screen_w, WP7_SCREEN_PAD_PERMILLE);

    lv_obj_clean(screen);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x08111F), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    create_status_bar(screen, screen_w, status_h, pad);
    create_tile_grid(screen, screen_w, screen_h, status_h);
}

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
    create_wp7_first_screen();
    bsp_display_unlock();

    bsp_display_brightness_set(100);
}
