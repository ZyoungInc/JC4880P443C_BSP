/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#define WP7_COLUMNS                  2
#define WP7_MAX_ROWS                 4
#define WP7_MAX_TILES                (WP7_COLUMNS * WP7_MAX_ROWS)
#define WP7_STATUS_BAR_PERMILLE      50
#define WP7_SCREEN_PAD_PERMILLE      34
#define WP7_TILE_GAP_PERMILLE        34
#define WP7_TILE_ANIM_MS             90

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *label;
    int32_t x;
    int32_t y;
    int32_t size;
} wp7_tile_t;

typedef enum {
    WP7_PHASE_IDLE,
    WP7_PHASE_NEXT_OUT,
    WP7_PHASE_NEXT_IN,
    WP7_PHASE_PREV_OUT,
    WP7_PHASE_PREV_IN,
} wp7_anim_phase_t;

typedef struct {
    wp7_tile_t tiles[WP7_MAX_TILES];
    int32_t tile_count;
    int32_t page;
    int32_t target_page;
    int32_t anim_index;
    bool animating;
    wp7_anim_phase_t phase;
} wp7_screen_t;

static wp7_screen_t s_wp7;

static int32_t scaled_px(int32_t base, int32_t permille)
{
    int32_t value = (base * permille) / 1000;
    return value > 0 ? value : 1;
}

static void set_tile_number(wp7_tile_t *tile, int32_t page, int32_t index)
{
    lv_label_set_text_fmt(tile->label, "%ld", (long)(page * s_wp7.tile_count + index + 1));
    lv_obj_center(tile->label);
}

static void set_tile_geometry(wp7_tile_t *tile, int32_t y, int32_t height)
{
    if (height <= 0) {
        lv_obj_add_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(tile->obj, tile->x, y);
    lv_obj_set_size(tile->obj, tile->size, height);
    lv_obj_center(tile->label);
}

static void tile_contract_up_cb(void *var, int32_t height)
{
    wp7_tile_t *tile = (wp7_tile_t *)var;

    set_tile_geometry(tile, tile->y - (tile->size - height), height);
}

static void tile_contract_down_cb(void *var, int32_t height)
{
    wp7_tile_t *tile = (wp7_tile_t *)var;

    set_tile_geometry(tile, tile->y + (tile->size - height), height);
}

static void tile_expand_from_bottom_cb(void *var, int32_t height)
{
    wp7_tile_t *tile = (wp7_tile_t *)var;

    set_tile_geometry(tile, tile->y + (tile->size - height), height);
}

static void tile_expand_from_top_cb(void *var, int32_t height)
{
    wp7_tile_t *tile = (wp7_tile_t *)var;

    set_tile_geometry(tile, tile->y, height);
}

static void start_next_tile_anim(void);

static void tile_anim_completed_cb(lv_anim_t *anim)
{
    (void)anim;
    start_next_tile_anim();
}

static int32_t ordered_tile_index(void)
{
    if (s_wp7.phase == WP7_PHASE_PREV_OUT) {
        return s_wp7.tile_count - 1 - s_wp7.anim_index;
    }

    return s_wp7.anim_index;
}

static void start_tile_anim(wp7_tile_t *tile, lv_anim_exec_xcb_t exec_cb, int32_t start, int32_t end)
{
    lv_anim_t anim;

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, tile);
    lv_anim_set_exec_cb(&anim, exec_cb);
    lv_anim_set_values(&anim, start, end);
    lv_anim_set_duration(&anim, WP7_TILE_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&anim, tile_anim_completed_cb);
    lv_anim_start(&anim);
}

static void start_next_tile_anim(void)
{
    if (s_wp7.anim_index >= s_wp7.tile_count) {
        if (s_wp7.phase == WP7_PHASE_NEXT_OUT) {
            s_wp7.phase = WP7_PHASE_NEXT_IN;
            s_wp7.page = s_wp7.target_page;
            s_wp7.anim_index = 0;
        } else if (s_wp7.phase == WP7_PHASE_PREV_OUT) {
            s_wp7.phase = WP7_PHASE_PREV_IN;
            s_wp7.page = s_wp7.target_page;
            s_wp7.anim_index = 0;
        } else {
            s_wp7.phase = WP7_PHASE_IDLE;
            s_wp7.animating = false;
            return;
        }
    }

    const int32_t index = ordered_tile_index();
    wp7_tile_t *tile = &s_wp7.tiles[index];

    if (s_wp7.phase == WP7_PHASE_NEXT_OUT) {
        s_wp7.anim_index++;
        start_tile_anim(tile, tile_contract_up_cb, tile->size, 0);
    } else if (s_wp7.phase == WP7_PHASE_NEXT_IN) {
        set_tile_number(tile, s_wp7.page, index);
        set_tile_geometry(tile, tile->y + tile->size, 0);
        s_wp7.anim_index++;
        start_tile_anim(tile, tile_expand_from_bottom_cb, 0, tile->size);
    } else if (s_wp7.phase == WP7_PHASE_PREV_OUT) {
        s_wp7.anim_index++;
        start_tile_anim(tile, tile_contract_down_cb, tile->size, 0);
    } else if (s_wp7.phase == WP7_PHASE_PREV_IN) {
        set_tile_number(tile, s_wp7.page, index);
        set_tile_geometry(tile, tile->y, 0);
        s_wp7.anim_index++;
        start_tile_anim(tile, tile_expand_from_top_cb, 0, tile->size);
    }
}

static void start_page_change(bool next)
{
    if (s_wp7.animating || s_wp7.tile_count <= 0) {
        return;
    }

    if (!next && s_wp7.page <= 0) {
        return;
    }

    s_wp7.target_page = next ? s_wp7.page + 1 : s_wp7.page - 1;
    s_wp7.phase = next ? WP7_PHASE_NEXT_OUT : WP7_PHASE_PREV_OUT;
    s_wp7.anim_index = 0;
    s_wp7.animating = true;
    start_next_tile_anim();
}

static void screen_gesture_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    if (dir == LV_DIR_TOP) {
        start_page_change(true);
    } else if (dir == LV_DIR_BOTTOM) {
        start_page_change(false);
    }
}

static void create_status_bar(lv_obj_t *screen, int32_t screen_w, int32_t status_h, int32_t pad)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, screen_w, status_h);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);

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
    const int32_t pad = scaled_px(screen_w, WP7_SCREEN_PAD_PERMILLE);
    const int32_t gap = scaled_px(screen_w, WP7_TILE_GAP_PERMILLE);
    const int32_t content_top = status_h + pad;
    const int32_t content_h = screen_h - content_top - pad;
    const int32_t tile = (screen_w - (pad * 2) - gap) / WP7_COLUMNS;
    int32_t rows = (content_h + gap) / (tile + gap);

    if (rows > WP7_MAX_ROWS) {
        rows = WP7_MAX_ROWS;
    }

    s_wp7.tile_count = rows * WP7_COLUMNS;

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
            lv_obj_remove_flag(tile_obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(tile_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);

            lv_obj_t *label = lv_label_create(tile_obj);
            lv_label_set_text_fmt(label, "%ld", (long)tile_number);
            lv_obj_set_style_text_color(label, lv_color_hex(0x06324A), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
            lv_obj_center(label);

            wp7_tile_t *tile_data = &s_wp7.tiles[tile_number - 1];
            tile_data->obj = tile_obj;
            tile_data->label = label;
            tile_data->x = pad + col * (tile + gap);
            tile_data->y = content_top + row * (tile + gap);
            tile_data->size = tile;
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
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(screen, screen_gesture_cb, LV_EVENT_GESTURE, NULL);

    s_wp7 = (wp7_screen_t) {
        .page = 0,
        .target_page = 0,
        .phase = WP7_PHASE_IDLE,
    };

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
