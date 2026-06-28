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
#define WP7_TILE_PROGRESS_UNIT       1000
#define WP7_TILE_STAGGER_UNIT        (WP7_TILE_PROGRESS_UNIT / 2)
#define WP7_DRAG_DISTANCE_PERMILLE   550
#define WP7_DRAG_THRESHOLD_PERMILLE  20
#define WP7_RELEASE_COMMIT_PERMILLE  220
#define WP7_MAX_PROGRESS_PER_MS      24
#define WP7_RELEASE_MIN_MS           220

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *label;
    int32_t x;
    int32_t y;
    int32_t size;
} wp7_tile_t;

typedef enum {
    WP7_DIR_NONE,
    WP7_DIR_NEXT,
    WP7_DIR_PREV,
} wp7_page_dir_t;

typedef struct {
    wp7_tile_t tiles[WP7_MAX_TILES];
    int32_t tile_count;
    int32_t screen_h;
    int32_t page;
    int32_t target_page;
    int32_t drag_start_y;
    int32_t drag_progress;
    int32_t drag_target_progress;
    uint32_t drag_last_tick;
    bool drag_active;
    bool animating;
    bool commit_transition;
    wp7_page_dir_t drag_dir;
    wp7_page_dir_t anim_dir;
} wp7_screen_t;

static wp7_screen_t s_wp7;

static int32_t scaled_px(int32_t base, int32_t permille)
{
    int32_t value = (base * permille) / 1000;
    return value > 0 ? value : 1;
}

static int32_t clamp_i32(int32_t value, int32_t min, int32_t max)
{
    if (value < min) {
        return min;
    }

    if (value > max) {
        return max;
    }

    return value;
}

static int32_t transition_progress_max(void)
{
    if (s_wp7.tile_count <= 0) {
        return 0;
    }

    return (((s_wp7.tile_count - 1) * WP7_TILE_STAGGER_UNIT) + WP7_TILE_PROGRESS_UNIT) * 2;
}

static int32_t transition_drag_distance(void)
{
    return scaled_px(s_wp7.screen_h, WP7_DRAG_DISTANCE_PERMILLE);
}

static int32_t transition_drag_threshold(void)
{
    return scaled_px(s_wp7.screen_h, WP7_DRAG_THRESHOLD_PERMILLE);
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

static int32_t ordered_out_index(wp7_page_dir_t dir, int32_t index)
{
    return dir == WP7_DIR_PREV ? s_wp7.tile_count - 1 - index : index;
}

static int32_t step_progress(int32_t progress, int32_t order)
{
    return clamp_i32(progress - order * WP7_TILE_STAGGER_UNIT, 0, WP7_TILE_PROGRESS_UNIT);
}

static void render_static_page(int32_t page)
{
    for (int32_t i = 0; i < s_wp7.tile_count; i++) {
        wp7_tile_t *tile = &s_wp7.tiles[i];

        set_tile_number(tile, page, i);
        set_tile_geometry(tile, tile->y, tile->size);
    }
}

static void render_transition(wp7_page_dir_t dir, int32_t progress)
{
    const int32_t out_phase_end = transition_progress_max() / 2;
    const int32_t max_progress = transition_progress_max();

    progress = clamp_i32(progress, 0, max_progress);

    if (progress < out_phase_end) {
        for (int32_t i = 0; i < s_wp7.tile_count; i++) {
            const int32_t order = ordered_out_index(dir, i);
            const int32_t local_progress = step_progress(progress, order);
            wp7_tile_t *tile = &s_wp7.tiles[i];
            const int32_t height = tile->size * (WP7_TILE_PROGRESS_UNIT - local_progress) / WP7_TILE_PROGRESS_UNIT;
            const int32_t y = dir == WP7_DIR_NEXT ? tile->y : tile->y + (tile->size - height);

            set_tile_number(tile, s_wp7.page, i);
            set_tile_geometry(tile, y, height);
        }

        return;
    }

    progress -= out_phase_end;

    for (int32_t i = 0; i < s_wp7.tile_count; i++) {
        const int32_t local_progress = step_progress(progress, i);
        wp7_tile_t *tile = &s_wp7.tiles[i];
        const int32_t height = tile->size * local_progress / WP7_TILE_PROGRESS_UNIT;
        const int32_t y = dir == WP7_DIR_NEXT ? tile->y + (tile->size - height) : tile->y;

        set_tile_number(tile, s_wp7.target_page, i);
        set_tile_geometry(tile, y, height);
    }
}

static void transition_anim_cb(void *var, int32_t progress)
{
    (void)var;
    render_transition(s_wp7.anim_dir, progress);
}

static void transition_anim_completed_cb(lv_anim_t *anim)
{
    (void)anim;

    if (s_wp7.commit_transition) {
        s_wp7.page = s_wp7.target_page;
    }

    render_static_page(s_wp7.page);
    s_wp7.animating = false;
    s_wp7.commit_transition = false;
    s_wp7.anim_dir = WP7_DIR_NONE;
    s_wp7.drag_dir = WP7_DIR_NONE;
    s_wp7.drag_progress = 0;
    s_wp7.drag_target_progress = 0;
}

static void start_progress_anim(wp7_page_dir_t dir, int32_t start, int32_t end, bool commit)
{
    s_wp7.animating = true;
    s_wp7.commit_transition = commit;
    s_wp7.anim_dir = dir;

    if (start == end) {
        render_transition(dir, end);
        transition_anim_completed_cb(NULL);
        return;
    }

    lv_anim_t anim;

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_wp7);
    lv_anim_set_exec_cb(&anim, transition_anim_cb);
    lv_anim_set_values(&anim, start, end);
    int32_t duration = (end > start ? end - start : start - end) / WP7_MAX_PROGRESS_PER_MS;
    if (duration < WP7_RELEASE_MIN_MS) {
        duration = WP7_RELEASE_MIN_MS;
    }
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&anim, transition_anim_completed_cb);
    lv_anim_start(&anim);
}

static int32_t limit_drag_progress(int32_t target_progress)
{
    const uint32_t tick = lv_tick_get();
    const uint32_t elapsed = tick - s_wp7.drag_last_tick;
    int32_t max_step = (int32_t)(elapsed > 0 ? elapsed : 1) * WP7_MAX_PROGRESS_PER_MS;

    s_wp7.drag_last_tick = tick;

    if (target_progress > s_wp7.drag_progress + max_step) {
        return s_wp7.drag_progress + max_step;
    }

    if (target_progress < s_wp7.drag_progress - max_step) {
        return s_wp7.drag_progress - max_step;
    }

    return target_progress;
}

static void update_drag_progress(int32_t y)
{
    const int32_t delta_y = y - s_wp7.drag_start_y;
    const int32_t threshold = transition_drag_threshold();
    const int32_t distance = transition_drag_distance();
    const int32_t max_progress = transition_progress_max();

    if (s_wp7.drag_dir == WP7_DIR_NONE) {
        if (delta_y < -threshold) {
            s_wp7.drag_dir = WP7_DIR_NEXT;
            s_wp7.target_page = s_wp7.page + 1;
        } else if (delta_y > threshold && s_wp7.page > 0) {
            s_wp7.drag_dir = WP7_DIR_PREV;
            s_wp7.target_page = s_wp7.page - 1;
        } else {
            return;
        }
    }

    const int32_t drag_distance = s_wp7.drag_dir == WP7_DIR_NEXT ? -delta_y : delta_y;
    const int32_t progress = clamp_i32(drag_distance * max_progress / distance, 0, max_progress);

    s_wp7.drag_target_progress = progress;
    s_wp7.drag_progress = limit_drag_progress(progress);
    render_transition(s_wp7.drag_dir, s_wp7.drag_progress);
}

static void finish_drag(void)
{
    if (!s_wp7.drag_active || s_wp7.drag_dir == WP7_DIR_NONE) {
        s_wp7.drag_active = false;
        return;
    }

    const int32_t max_progress = transition_progress_max();
    const int32_t commit_progress = max_progress * WP7_RELEASE_COMMIT_PERMILLE / 1000;
    const bool commit = s_wp7.drag_target_progress >= commit_progress;
    const int32_t end = commit ? max_progress : 0;

    s_wp7.drag_active = false;
    start_progress_anim(s_wp7.drag_dir, s_wp7.drag_progress, end, commit);
}

static void screen_touch_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point;

    if (indev == NULL) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        if (s_wp7.animating) {
            return;
        }

        lv_indev_get_point(indev, &point);
        s_wp7.drag_start_y = point.y;
        s_wp7.drag_progress = 0;
        s_wp7.drag_target_progress = 0;
        s_wp7.drag_last_tick = lv_tick_get();
        s_wp7.drag_dir = WP7_DIR_NONE;
        s_wp7.drag_active = true;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_wp7.drag_active || s_wp7.animating) {
            return;
        }

        lv_indev_get_point(indev, &point);
        update_drag_progress(point.y);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        finish_drag();
    }
}

static void create_status_bar(lv_obj_t *screen, int32_t screen_w, int32_t status_h, int32_t pad)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, screen_w, status_h);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *wifi_label = lv_label_create(bar);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_label, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_20, 0);
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, pad, 0);
    lv_obj_add_flag(wifi_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *time_label = lv_label_create(bar);
    lv_label_set_text(time_label, "09:41");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(time_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *battery_label = lv_label_create(bar);
    lv_label_set_text(battery_label, LV_SYMBOL_BATTERY_3);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_20, 0);
    lv_obj_align(battery_label, LV_ALIGN_RIGHT_MID, -pad, 0);
    lv_obj_add_flag(battery_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
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
            lv_obj_add_flag(tile_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

            lv_obj_t *label = lv_label_create(tile_obj);
            lv_label_set_text_fmt(label, "%ld", (long)tile_number);
            lv_obj_set_style_text_color(label, lv_color_hex(0x06324A), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
            lv_obj_center(label);
            lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

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
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(screen, screen_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen, screen_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(screen, screen_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(screen, screen_touch_cb, LV_EVENT_PRESS_LOST, NULL);

    s_wp7 = (wp7_screen_t) {
        .page = 0,
        .target_page = 0,
        .screen_h = screen_h,
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
