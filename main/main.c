/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#define WP7_COLUMNS                  2
#define WP7_MAX_ROWS                 4
#define WP7_MAX_TILES                (WP7_COLUMNS * WP7_MAX_ROWS)
#define WP7_MAX_LIST_ITEMS           WP7_MAX_TILES
#define WP7_STATUS_BAR_PERMILLE      50
#define WP7_SCREEN_PAD_PERMILLE      34
#define WP7_TILE_GAP_PERMILLE        34
#define WP7_LIST_ROW_H_PERMILLE      78
#define WP7_LIST_ROW_GAP_PERMILLE    18
#define WP7_TILE_PROGRESS_UNIT       1000
#define WP7_TILE_STAGGER_UNIT        (WP7_TILE_PROGRESS_UNIT / 2)
#define WP7_DRAG_DISTANCE_PERMILLE   550
#define WP7_DRAG_THRESHOLD_PERMILLE  20
#define WP7_RELEASE_COMMIT_PERMILLE  220
#define WP7_BASE_PROGRESS_PER_MS     8
#define WP7_BASE_RELEASE_MIN_MS      620
#define WP7_SETTINGS_TILE_PHASE_UNIT (WP7_TILE_PROGRESS_UNIT * 2)
#define WP7_SETTINGS_TITLE_PHASE_UNIT (WP7_TILE_PROGRESS_UNIT * 3 / 2)
#define WP7_SETTINGS_TILE_INDEX      5
#define WP7_SETTINGS_CONTENT_COUNT   7
#define WP7_ANIM_SPEED_MIN           50
#define WP7_ANIM_SPEED_DEFAULT       100
#define WP7_ANIM_SPEED_MAX           145
#define WP7_ACTUAL_SPEED_MIN_PERMILLE 280
#define WP7_ACTUAL_SPEED_DEFAULT_PERMILLE 1000
#define WP7_ACTUAL_SPEED_MAX_PERMILLE 1420
#define WP7_HORIZONTAL_SPEED_PERMILLE 820
#define WP7_SETTINGS_SPEED_PERMILLE 760
#define WP7_BRIGHTNESS_MIN           5
#define WP7_BRIGHTNESS_DEFAULT       100
#define WP7_BRIGHTNESS_MAX           100
#define WP7_THEME_COLOR_COUNT        6
#define WP7_NVS_NAMESPACE            "wp7_ui"
#define WP7_NVS_SPEED_KEY            "anim_spd"
#define WP7_NVS_BRIGHTNESS_KEY       "bright"
#define WP7_NVS_THEME_KEY            "theme"

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *label;
    int32_t x;
    int32_t y;
    int32_t size;
} wp7_tile_t;

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *label;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} wp7_list_item_t;

typedef struct {
    lv_obj_t *obj;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} wp7_setting_item_t;

typedef enum {
    WP7_DIR_NONE,
    WP7_DIR_NEXT,
    WP7_DIR_PREV,
    WP7_DIR_LIST,
    WP7_DIR_HOME,
    WP7_DIR_SETTINGS_OPEN,
    WP7_DIR_SETTINGS_CLOSE,
} wp7_page_dir_t;

typedef enum {
    WP7_SETTINGS_ITEM_BRIGHTNESS_LABEL,
    WP7_SETTINGS_ITEM_BRIGHTNESS_SLIDER,
    WP7_SETTINGS_ITEM_THEME_LABEL,
    WP7_SETTINGS_ITEM_THEME_PICKER,
    WP7_SETTINGS_ITEM_SPEED_LABEL,
    WP7_SETTINGS_ITEM_SPEED_SLIDER,
    WP7_SETTINGS_ITEM_RESET_BUTTON,
} wp7_setting_item_index_t;

typedef struct {
    wp7_tile_t tiles[WP7_MAX_TILES];
    wp7_list_item_t list_items[WP7_MAX_LIST_ITEMS];
    wp7_setting_item_t settings_items[WP7_SETTINGS_CONTENT_COUNT];
    lv_obj_t *status_bar;
    lv_obj_t *settings_title;
    lv_obj_t *brightness_slider;
    lv_obj_t *theme_picker;
    lv_obj_t *theme_buttons[WP7_THEME_COLOR_COUNT];
    lv_obj_t *settings_slider;
    lv_obj_t *settings_reset_button;
    lv_obj_t *settings_reset_label;
    int32_t tile_count;
    int32_t list_count;
    int32_t screen_w;
    int32_t screen_h;
    int32_t settings_title_x;
    int32_t settings_title_y;
    int32_t settings_title_w;
    int32_t settings_title_h;
    int32_t settings_tile_index;
    int32_t anim_speed_percent;
    int32_t brightness_percent;
    int32_t theme_index;
    int32_t page;
    int32_t target_page;
    int32_t drag_start_x;
    int32_t drag_start_y;
    int32_t drag_progress;
    int32_t drag_target_progress;
    uint32_t drag_last_tick;
    bool drag_active;
    bool animating;
    bool commit_transition;
    bool in_list;
    bool in_settings;
    wp7_page_dir_t drag_dir;
    wp7_page_dir_t anim_dir;
} wp7_screen_t;

static wp7_screen_t s_wp7;

static const uint32_t s_wp7_theme_colors[WP7_THEME_COLOR_COUNT] = {
    0x61C9FF,
    0x00A300,
    0xB4009E,
    0x6A00FF,
    0x00ABA9,
    0xFA6800,
};

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

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t staggered_phase_max(int32_t item_count)
{
    if (item_count <= 0) {
        return 0;
    }

    return ((item_count - 1) * WP7_TILE_STAGGER_UNIT) + WP7_TILE_PROGRESS_UNIT;
}

static bool is_horizontal_dir(wp7_page_dir_t dir)
{
    return dir == WP7_DIR_LIST || dir == WP7_DIR_HOME;
}

static int32_t settings_other_tile_count(void)
{
    return s_wp7.tile_count > 0 ? s_wp7.tile_count - 1 : 0;
}

static int32_t transition_progress_max_for_dir(wp7_page_dir_t dir)
{
    if (dir == WP7_DIR_SETTINGS_OPEN) {
        return staggered_phase_max(settings_other_tile_count()) +
               WP7_SETTINGS_TILE_PHASE_UNIT +
               staggered_phase_max(WP7_SETTINGS_CONTENT_COUNT + 1);
    }

    if (dir == WP7_DIR_SETTINGS_CLOSE) {
        return staggered_phase_max(WP7_SETTINGS_CONTENT_COUNT) +
               WP7_SETTINGS_TITLE_PHASE_UNIT +
               WP7_SETTINGS_TILE_PHASE_UNIT +
               staggered_phase_max(settings_other_tile_count());
    }

    return staggered_phase_max(s_wp7.tile_count) * 2;
}

static int32_t transition_progress_max(void)
{
    if (s_wp7.drag_dir != WP7_DIR_NONE) {
        return transition_progress_max_for_dir(s_wp7.drag_dir);
    }

    return transition_progress_max_for_dir(s_wp7.anim_dir);
}

static int32_t transition_drag_distance(void)
{
    if (is_horizontal_dir(s_wp7.drag_dir)) {
        return scaled_px(s_wp7.screen_w, WP7_DRAG_DISTANCE_PERMILLE);
    }

    return scaled_px(s_wp7.screen_h, WP7_DRAG_DISTANCE_PERMILLE);
}

static int32_t transition_drag_threshold(void)
{
    return scaled_px(s_wp7.screen_h, WP7_DRAG_THRESHOLD_PERMILLE);
}

static int32_t sanitize_animation_speed(int32_t speed)
{
    return clamp_i32(speed, WP7_ANIM_SPEED_MIN, WP7_ANIM_SPEED_MAX);
}

static int32_t sanitize_brightness(int32_t brightness)
{
    return clamp_i32(brightness, WP7_BRIGHTNESS_MIN, WP7_BRIGHTNESS_MAX);
}

static int32_t sanitize_theme_index(int32_t index)
{
    return clamp_i32(index, 0, WP7_THEME_COLOR_COUNT - 1);
}

static uint32_t theme_color_hex(void)
{
    return s_wp7_theme_colors[sanitize_theme_index(s_wp7.theme_index)];
}

static lv_color_t theme_color(void)
{
    return lv_color_hex(theme_color_hex());
}

static lv_color_t theme_text_color(void)
{
    const uint32_t color = theme_color_hex();
    const int32_t red = (color >> 16) & 0xFF;
    const int32_t green = (color >> 8) & 0xFF;
    const int32_t blue = color & 0xFF;
    const int32_t luma = red * 299 + green * 587 + blue * 114;

    return luma > 145000 ? lv_color_hex(0x06324A) : lv_color_hex(0xFFFFFF);
}

static int32_t animation_speed_permille(void)
{
    const int32_t speed = sanitize_animation_speed(s_wp7.anim_speed_percent);

    if (speed <= WP7_ANIM_SPEED_DEFAULT) {
        return WP7_ACTUAL_SPEED_MIN_PERMILLE +
               (speed - WP7_ANIM_SPEED_MIN) *
               (WP7_ACTUAL_SPEED_DEFAULT_PERMILLE - WP7_ACTUAL_SPEED_MIN_PERMILLE) /
               (WP7_ANIM_SPEED_DEFAULT - WP7_ANIM_SPEED_MIN);
    }

    return WP7_ACTUAL_SPEED_DEFAULT_PERMILLE +
           (speed - WP7_ANIM_SPEED_DEFAULT) *
           (WP7_ACTUAL_SPEED_MAX_PERMILLE - WP7_ACTUAL_SPEED_DEFAULT_PERMILLE) /
           (WP7_ANIM_SPEED_MAX - WP7_ANIM_SPEED_DEFAULT);
}

static int32_t animation_dir_permille(wp7_page_dir_t dir)
{
    if (is_horizontal_dir(dir)) {
        return WP7_HORIZONTAL_SPEED_PERMILLE;
    }

    if (dir == WP7_DIR_SETTINGS_OPEN || dir == WP7_DIR_SETTINGS_CLOSE) {
        return WP7_SETTINGS_SPEED_PERMILLE;
    }

    return 1000;
}

static int32_t animation_progress_per_ms(wp7_page_dir_t dir)
{
    const int64_t scaled_progress = (int64_t)WP7_BASE_PROGRESS_PER_MS *
                                    animation_speed_permille() *
                                    animation_dir_permille(dir);
    const int32_t progress_per_ms = (int32_t)((scaled_progress + 500000) / 1000000);

    return progress_per_ms > 0 ? progress_per_ms : 1;
}

static int32_t animation_duration_ms(wp7_page_dir_t dir, int32_t progress_delta)
{
    const int64_t scaled_progress = (int64_t)WP7_BASE_PROGRESS_PER_MS *
                                    animation_speed_permille() *
                                    animation_dir_permille(dir);
    const int64_t scaled_delta = (int64_t)progress_delta * 1000000;

    return (int32_t)((scaled_delta + scaled_progress - 1) / scaled_progress);
}

static int32_t animation_release_min_ms(wp7_page_dir_t dir)
{
    const int64_t scaled_speed = (int64_t)animation_speed_permille() *
                                 animation_dir_permille(dir);
    const int64_t scaled_duration = (int64_t)WP7_BASE_RELEASE_MIN_MS * 1000000;

    return (int32_t)((scaled_duration + scaled_speed - 1) / scaled_speed);
}

static bool animation_speed_is_default(void)
{
    return sanitize_animation_speed(s_wp7.anim_speed_percent) == WP7_ANIM_SPEED_DEFAULT;
}

static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
}

static void load_ui_settings(void)
{
    nvs_handle_t handle;
    uint16_t speed = WP7_ANIM_SPEED_DEFAULT;
    uint16_t brightness = WP7_BRIGHTNESS_DEFAULT;
    uint16_t theme = 0;

    s_wp7.anim_speed_percent = WP7_ANIM_SPEED_DEFAULT;
    s_wp7.brightness_percent = WP7_BRIGHTNESS_DEFAULT;
    s_wp7.theme_index = 0;

    if (nvs_open(WP7_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    if (nvs_get_u16(handle, WP7_NVS_SPEED_KEY, &speed) == ESP_OK) {
        s_wp7.anim_speed_percent = sanitize_animation_speed(speed);
    }

    if (nvs_get_u16(handle, WP7_NVS_BRIGHTNESS_KEY, &brightness) == ESP_OK) {
        s_wp7.brightness_percent = sanitize_brightness(brightness);
    }

    if (nvs_get_u16(handle, WP7_NVS_THEME_KEY, &theme) == ESP_OK) {
        s_wp7.theme_index = sanitize_theme_index(theme);
    }

    nvs_close(handle);
}

static void save_u16_setting(const char *key, uint16_t value)
{
    nvs_handle_t handle;

    if (nvs_open(WP7_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    if (nvs_set_u16(handle, key, value) == ESP_OK) {
        nvs_commit(handle);
    }

    nvs_close(handle);
}

static void save_animation_speed(void)
{
    save_u16_setting(WP7_NVS_SPEED_KEY,
                     (uint16_t)sanitize_animation_speed(s_wp7.anim_speed_percent));
}

static void save_brightness(void)
{
    save_u16_setting(WP7_NVS_BRIGHTNESS_KEY,
                     (uint16_t)sanitize_brightness(s_wp7.brightness_percent));
}

static void save_theme(void)
{
    save_u16_setting(WP7_NVS_THEME_KEY, (uint16_t)sanitize_theme_index(s_wp7.theme_index));
}

static void set_tile_number(wp7_tile_t *tile, int32_t page, int32_t index)
{
    if (page == 0 && index == WP7_SETTINGS_TILE_INDEX) {
        lv_label_set_text(tile->label, "UI\nSettings");
    } else {
        lv_label_set_text_fmt(tile->label, "%ld", (long)(page * s_wp7.tile_count + index + 1));
    }

    lv_obj_set_style_text_color(tile->label, theme_text_color(), 0);
    lv_obj_set_style_text_align(tile->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(tile->label);
}

static void apply_theme_to_tiles(void)
{
    for (int32_t i = 0; i < s_wp7.tile_count; i++) {
        if (s_wp7.tiles[i].obj == NULL) {
            continue;
        }

        lv_obj_set_style_bg_color(s_wp7.tiles[i].obj, theme_color(), 0);

        if (s_wp7.tiles[i].label != NULL) {
            lv_obj_set_style_text_color(s_wp7.tiles[i].label, theme_text_color(), 0);
        }
    }
}

static void set_tile_geometry(wp7_tile_t *tile, int32_t y, int32_t height)
{
    if (height <= 0) {
        lv_obj_add_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(tile->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale(tile->obj, 256, 0);
    lv_obj_set_pos(tile->obj, tile->x, y);
    lv_obj_set_size(tile->obj, tile->size, height);
    lv_obj_center(tile->label);
}

static void set_tile_x(wp7_tile_t *tile, int32_t x)
{
    if (x <= -tile->size || x >= s_wp7.screen_w) {
        lv_obj_add_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(tile->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale(tile->obj, 256, 0);
    lv_obj_set_pos(tile->obj, x, tile->y);
    lv_obj_set_size(tile->obj, tile->size, tile->size);
    lv_obj_center(tile->label);
}

static void set_tile_width(wp7_tile_t *tile, int32_t width)
{
    if (width <= 0) {
        lv_obj_add_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(tile->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale(tile->obj, 256, 0);
    lv_obj_set_pos(tile->obj, tile->x, tile->y);
    lv_obj_set_size(tile->obj, width, tile->size);
    lv_obj_center(tile->label);
}

static void set_tile_box(wp7_tile_t *tile, int32_t x, int32_t y, int32_t w, int32_t h, lv_opa_t opa)
{
    if (w <= 0 || h <= 0 || opa == 0) {
        lv_obj_add_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(tile->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(tile->obj, opa, 0);
    lv_obj_set_style_transform_scale(tile->obj, 256, 0);
    lv_obj_set_pos(tile->obj, x, y);
    lv_obj_set_size(tile->obj, w, h);
    lv_obj_center(tile->label);
}

static void set_list_item_geometry(wp7_list_item_t *item, int32_t x)
{
    if (x <= -item->w || x >= s_wp7.screen_w) {
        lv_obj_add_flag(item->obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(item->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(item->obj, x, item->y);
    lv_obj_set_size(item->obj, item->w, item->h);
    lv_obj_align(item->label, LV_ALIGN_LEFT_MID, scaled_px(s_wp7.screen_w, 28), 0);
}

static void set_setting_item_geometry(wp7_setting_item_t *item, int32_t x, lv_opa_t opa)
{
    if (x <= -item->w || x >= s_wp7.screen_w || opa == 0) {
        lv_obj_add_flag(item->obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(item->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(item->obj, opa, 0);
    lv_obj_set_pos(item->obj, x, item->y);
    lv_obj_set_size(item->obj, item->w, item->h);
}

static void set_settings_title_geometry(int32_t x, int32_t scale, lv_opa_t opa)
{
    if (opa == 0 || scale <= 0) {
        lv_obj_add_flag(s_wp7.settings_title, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(s_wp7.settings_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_wp7.settings_title, opa, 0);
    lv_obj_set_style_transform_scale(s_wp7.settings_title, scale, 0);
    lv_obj_set_pos(s_wp7.settings_title, x, s_wp7.settings_title_y);
    lv_obj_set_size(s_wp7.settings_title, s_wp7.settings_title_w, s_wp7.settings_title_h);
}

static void show_status_bar(void)
{
    if (s_wp7.status_bar != NULL) {
        lv_obj_remove_flag(s_wp7.status_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_tiles(void)
{
    for (int32_t i = 0; i < s_wp7.tile_count; i++) {
        lv_obj_add_flag(s_wp7.tiles[i].obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_list_items(void)
{
    for (int32_t i = 0; i < s_wp7.list_count; i++) {
        lv_obj_add_flag(s_wp7.list_items[i].obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_settings_items(void)
{
    if (s_wp7.settings_title != NULL) {
        lv_obj_add_flag(s_wp7.settings_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_transform_scale(s_wp7.settings_title, 256, 0);
        lv_obj_set_style_opa(s_wp7.settings_title, LV_OPA_COVER, 0);
    }

    for (int32_t i = 0; i < WP7_SETTINGS_CONTENT_COUNT; i++) {
        if (s_wp7.settings_items[i].obj != NULL) {
            lv_obj_add_flag(s_wp7.settings_items[i].obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(s_wp7.settings_items[i].obj, LV_OPA_COVER, 0);
        }
    }
}

static int32_t tile_row(int32_t index)
{
    return index / WP7_COLUMNS;
}

static int32_t tile_col(int32_t index)
{
    return index % WP7_COLUMNS;
}

static int32_t tile_distance_from_settings(int32_t index)
{
    return abs_i32(tile_row(index) - tile_row(s_wp7.settings_tile_index)) +
           abs_i32(tile_col(index) - tile_col(s_wp7.settings_tile_index));
}

static int32_t ordered_settings_other_index(int32_t order)
{
    const int32_t clicked = s_wp7.settings_tile_index;

    for (int32_t i = 0; i < s_wp7.tile_count; i++) {
        int32_t rank = 0;

        if (i == clicked) {
            continue;
        }

        for (int32_t j = 0; j < s_wp7.tile_count; j++) {
            if (j == clicked || j == i) {
                continue;
            }

            const int32_t dist_i = tile_distance_from_settings(i);
            const int32_t dist_j = tile_distance_from_settings(j);

            if (dist_j < dist_i || (dist_j == dist_i && j < i)) {
                rank++;
            }
        }

        if (rank == order) {
            return i;
        }
    }

    return clicked;
}

static int32_t ordered_out_index(wp7_page_dir_t dir, int32_t index)
{
    return dir == WP7_DIR_PREV ? s_wp7.tile_count - 1 - index : index;
}

static int32_t ordered_column_index(int32_t order)
{
    const int32_t rows = s_wp7.tile_count / WP7_COLUMNS;

    if (order < rows) {
        return order * WP7_COLUMNS;
    }

    return (order - rows) * WP7_COLUMNS + 1;
}

static int32_t step_progress(int32_t progress, int32_t order)
{
    return clamp_i32(progress - order * WP7_TILE_STAGGER_UNIT, 0, WP7_TILE_PROGRESS_UNIT);
}

static int32_t phase_progress(int32_t progress, int32_t phase_unit)
{
    progress = clamp_i32(progress, 0, phase_unit);

    return (int32_t)((int64_t)progress * WP7_TILE_PROGRESS_UNIT / phase_unit);
}

static int32_t ease_in_out_cubic(int32_t progress)
{
    progress = clamp_i32(progress, 0, WP7_TILE_PROGRESS_UNIT);

    if (progress < WP7_TILE_PROGRESS_UNIT / 2) {
        return (int32_t)(4LL * progress * progress * progress /
                         (WP7_TILE_PROGRESS_UNIT * WP7_TILE_PROGRESS_UNIT));
    }

    int32_t inverse = WP7_TILE_PROGRESS_UNIT - progress;

    return WP7_TILE_PROGRESS_UNIT -
           (int32_t)(4LL * inverse * inverse * inverse /
                     (WP7_TILE_PROGRESS_UNIT * WP7_TILE_PROGRESS_UNIT));
}

static void render_static_page(int32_t page)
{
    show_status_bar();
    hide_list_items();
    hide_settings_items();

    for (int32_t i = 0; i < s_wp7.tile_count; i++) {
        wp7_tile_t *tile = &s_wp7.tiles[i];

        set_tile_number(tile, page, i);
        set_tile_geometry(tile, tile->y, tile->size);
    }
}

static void render_static_list(void)
{
    show_status_bar();
    hide_tiles();
    hide_settings_items();

    for (int32_t i = 0; i < s_wp7.list_count; i++) {
        set_list_item_geometry(&s_wp7.list_items[i], s_wp7.list_items[i].x);
    }
}

static void update_reset_button_state(void)
{
    if (s_wp7.settings_reset_button == NULL || s_wp7.settings_reset_label == NULL) {
        return;
    }

    if (animation_speed_is_default()) {
        lv_obj_add_state(s_wp7.settings_reset_button, LV_STATE_DISABLED);
        lv_obj_remove_flag(s_wp7.settings_reset_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(s_wp7.settings_reset_button, lv_color_hex(0x2C2C2C), 0);
        lv_obj_set_style_text_color(s_wp7.settings_reset_label, lv_color_hex(0x777777), 0);
    } else {
        lv_obj_remove_state(s_wp7.settings_reset_button, LV_STATE_DISABLED);
        lv_obj_add_flag(s_wp7.settings_reset_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(s_wp7.settings_reset_button, theme_color(), 0);
        lv_obj_set_style_text_color(s_wp7.settings_reset_label, theme_text_color(), 0);
    }
}

static void update_theme_buttons(void)
{
    for (int32_t i = 0; i < WP7_THEME_COLOR_COUNT; i++) {
        if (s_wp7.theme_buttons[i] == NULL) {
            continue;
        }

        const bool selected = i == sanitize_theme_index(s_wp7.theme_index);

        lv_obj_set_style_border_width(s_wp7.theme_buttons[i], selected ? 4 : 0, 0);
        lv_obj_set_style_border_color(s_wp7.theme_buttons[i], lv_color_hex(0xEAF7FF), 0);
    }
}

static void update_theme_controls(void)
{
    if (s_wp7.brightness_slider != NULL) {
        lv_obj_set_style_bg_color(s_wp7.brightness_slider, theme_color(), LV_PART_INDICATOR);
    }

    if (s_wp7.settings_slider != NULL) {
        lv_obj_set_style_bg_color(s_wp7.settings_slider, theme_color(), LV_PART_INDICATOR);
    }

    apply_theme_to_tiles();
    update_theme_buttons();
    update_reset_button_state();
}

static void render_static_settings(void)
{
    show_status_bar();
    hide_tiles();
    hide_list_items();

    if (s_wp7.brightness_slider != NULL) {
        lv_slider_set_value(s_wp7.brightness_slider,
                            sanitize_brightness(s_wp7.brightness_percent),
                            LV_ANIM_OFF);
    }

    if (s_wp7.settings_slider != NULL) {
        lv_slider_set_value(s_wp7.settings_slider, sanitize_animation_speed(s_wp7.anim_speed_percent), LV_ANIM_OFF);
    }

    set_settings_title_geometry(s_wp7.settings_title_x, 256, LV_OPA_COVER);

    for (int32_t i = 0; i < WP7_SETTINGS_CONTENT_COUNT; i++) {
        set_setting_item_geometry(&s_wp7.settings_items[i], s_wp7.settings_items[i].x, LV_OPA_COVER);
    }

    update_theme_controls();
}

static void render_vertical_transition(wp7_page_dir_t dir, int32_t progress)
{
    const int32_t out_phase_end = transition_progress_max() / 2;
    const int32_t max_progress = transition_progress_max();

    hide_list_items();
    hide_settings_items();
    progress = clamp_i32(progress, 0, max_progress);

    if (progress < out_phase_end) {
        for (int32_t i = 0; i < s_wp7.tile_count; i++) {
            const int32_t order = ordered_out_index(dir, i);
            const int32_t local_progress = step_progress(progress, order);
            const int32_t eased_progress = ease_in_out_cubic(local_progress);
            wp7_tile_t *tile = &s_wp7.tiles[i];
            const int32_t height = tile->size * (WP7_TILE_PROGRESS_UNIT - eased_progress) / WP7_TILE_PROGRESS_UNIT;
            const int32_t y = dir == WP7_DIR_NEXT ? tile->y : tile->y + (tile->size - height);

            set_tile_number(tile, s_wp7.page, i);
            set_tile_geometry(tile, y, height);
        }

        return;
    }

    progress -= out_phase_end;

    for (int32_t i = 0; i < s_wp7.tile_count; i++) {
        const int32_t local_progress = step_progress(progress, i);
        const int32_t eased_progress = ease_in_out_cubic(local_progress);
        wp7_tile_t *tile = &s_wp7.tiles[i];
        const int32_t height = tile->size * eased_progress / WP7_TILE_PROGRESS_UNIT;
        const int32_t y = dir == WP7_DIR_NEXT ? tile->y + (tile->size - height) : tile->y;

        set_tile_number(tile, s_wp7.target_page, i);
        set_tile_geometry(tile, y, height);
    }
}

static void render_horizontal_transition(wp7_page_dir_t dir, int32_t progress)
{
    const int32_t out_phase_end = transition_progress_max() / 2;
    const int32_t max_progress = transition_progress_max();

    hide_settings_items();
    progress = clamp_i32(progress, 0, max_progress);

    if (progress < out_phase_end) {
        if (dir == WP7_DIR_LIST) {
            hide_list_items();
        } else {
            hide_tiles();
        }

        for (int32_t order = 0; order < s_wp7.tile_count; order++) {
            const int32_t index = ordered_column_index(order);
            const int32_t local_progress = step_progress(progress, order);
            const int32_t eased_progress = ease_in_out_cubic(local_progress);

            if (dir == WP7_DIR_LIST) {
                wp7_tile_t *tile = &s_wp7.tiles[index];
                const int32_t x = tile->x - ((tile->x + tile->size) * eased_progress / WP7_TILE_PROGRESS_UNIT);

                set_tile_number(tile, s_wp7.page, index);
                set_tile_x(tile, x);
            } else {
                wp7_list_item_t *item = &s_wp7.list_items[order];
                const int32_t x = item->x + ((s_wp7.screen_w - item->x) * eased_progress / WP7_TILE_PROGRESS_UNIT);

                set_list_item_geometry(item, x);
            }
        }

        return;
    }

    progress -= out_phase_end;

    if (dir == WP7_DIR_LIST) {
        hide_tiles();
    } else {
        hide_list_items();
    }

    for (int32_t order = 0; order < s_wp7.tile_count; order++) {
        const int32_t index = ordered_column_index(order);
        const int32_t local_progress = step_progress(progress, order);
        const int32_t eased_progress = ease_in_out_cubic(local_progress);

        if (dir == WP7_DIR_LIST) {
            wp7_list_item_t *item = &s_wp7.list_items[order];
            const int32_t x = s_wp7.screen_w - ((s_wp7.screen_w - item->x) * eased_progress / WP7_TILE_PROGRESS_UNIT);

            set_list_item_geometry(item, x);
        } else {
            wp7_tile_t *tile = &s_wp7.tiles[index];

            set_tile_number(tile, s_wp7.page, index);

            if ((index % WP7_COLUMNS) == 0) {
                const int32_t x = -tile->size + ((tile->x + tile->size) * eased_progress / WP7_TILE_PROGRESS_UNIT);
                set_tile_x(tile, x);
            } else {
                const int32_t width = tile->size * eased_progress / WP7_TILE_PROGRESS_UNIT;
                set_tile_width(tile, width);
            }
        }
    }
}

static void render_tile_collapse(wp7_tile_t *tile, int32_t eased_progress, bool toward_right)
{
    const int32_t width = tile->size * (WP7_TILE_PROGRESS_UNIT - eased_progress) / WP7_TILE_PROGRESS_UNIT;
    const int32_t x = toward_right ? tile->x + (tile->size - width) : tile->x;

    set_tile_box(tile, x, tile->y, width, tile->size, LV_OPA_COVER);
}

static void render_tile_restore(wp7_tile_t *tile, int32_t eased_progress, bool grow_to_right)
{
    const int32_t width = tile->size * eased_progress / WP7_TILE_PROGRESS_UNIT;
    const int32_t x = grow_to_right ? tile->x : tile->x + (tile->size - width);

    set_tile_box(tile, x, tile->y, width, tile->size, LV_OPA_COVER);
}

static void render_clicked_tile_fade(wp7_tile_t *tile, int32_t eased_progress, bool appear)
{
    lv_opa_t opa;

    if (appear) {
        opa = (lv_opa_t)(LV_OPA_COVER * eased_progress / WP7_TILE_PROGRESS_UNIT);
    } else {
        opa = (lv_opa_t)(LV_OPA_COVER * (WP7_TILE_PROGRESS_UNIT - eased_progress) / WP7_TILE_PROGRESS_UNIT);
    }

    set_tile_box(tile, tile->x, tile->y, tile->size, tile->size, opa);
}

static void render_settings_content_in(int32_t progress)
{
    const int32_t title_local = step_progress(progress, 0);
    const int32_t title_eased = ease_in_out_cubic(title_local);
    const int32_t title_x = -s_wp7.settings_title_w +
                            ((s_wp7.settings_title_x + s_wp7.settings_title_w) * title_eased /
                             WP7_TILE_PROGRESS_UNIT);

    set_settings_title_geometry(title_x, 256, (lv_opa_t)(LV_OPA_COVER * title_eased / WP7_TILE_PROGRESS_UNIT));

    for (int32_t i = 0; i < WP7_SETTINGS_CONTENT_COUNT; i++) {
        wp7_setting_item_t *item = &s_wp7.settings_items[i];
        const int32_t local_progress = step_progress(progress, i + 1);
        const int32_t eased_progress = ease_in_out_cubic(local_progress);
        const int32_t x = -item->w + ((item->x + item->w) * eased_progress / WP7_TILE_PROGRESS_UNIT);

        set_setting_item_geometry(item, x, (lv_opa_t)(LV_OPA_COVER * eased_progress / WP7_TILE_PROGRESS_UNIT));
    }
}

static void render_settings_content_out(int32_t progress)
{
    set_settings_title_geometry(s_wp7.settings_title_x, 256, LV_OPA_COVER);

    for (int32_t order = 0; order < WP7_SETTINGS_CONTENT_COUNT; order++) {
        const int32_t index = WP7_SETTINGS_CONTENT_COUNT - 1 - order;
        wp7_setting_item_t *item = &s_wp7.settings_items[index];
        const int32_t local_progress = step_progress(progress, order);
        const int32_t eased_progress = ease_in_out_cubic(local_progress);
        const int32_t x = item->x - ((item->x + item->w) * eased_progress / WP7_TILE_PROGRESS_UNIT);

        set_setting_item_geometry(item, x, (lv_opa_t)(LV_OPA_COVER *
                                  (WP7_TILE_PROGRESS_UNIT - eased_progress) /
                                  WP7_TILE_PROGRESS_UNIT));
    }
}

static void render_settings_title_out(int32_t progress)
{
    const int32_t eased_progress = ease_in_out_cubic(progress);
    const int32_t scale = 256 - (128 * eased_progress / WP7_TILE_PROGRESS_UNIT);
    const lv_opa_t opa = (lv_opa_t)(LV_OPA_COVER *
                                    (WP7_TILE_PROGRESS_UNIT - eased_progress) /
                                    WP7_TILE_PROGRESS_UNIT);

    set_settings_title_geometry(s_wp7.settings_title_x, scale, opa);
}

static void render_settings_open_transition(int32_t progress)
{
    const int32_t other_count = settings_other_tile_count();
    const int32_t other_phase = staggered_phase_max(other_count);
    const int32_t clicked_phase_start = other_phase;
    const int32_t content_phase_start = clicked_phase_start + WP7_SETTINGS_TILE_PHASE_UNIT;
    const int32_t max_progress = transition_progress_max_for_dir(WP7_DIR_SETTINGS_OPEN);
    const bool collapse_right = tile_col(s_wp7.settings_tile_index) == 0;

    progress = clamp_i32(progress, 0, max_progress);
    show_status_bar();
    hide_list_items();

    if (progress < other_phase) {
        hide_settings_items();

        for (int32_t order = 0; order < other_count; order++) {
            const int32_t index = ordered_settings_other_index(order);
            const int32_t local_progress = step_progress(progress, order);
            const int32_t eased_progress = ease_in_out_cubic(local_progress);
            wp7_tile_t *tile = &s_wp7.tiles[index];

            set_tile_number(tile, s_wp7.page, index);
            render_tile_collapse(tile, eased_progress, collapse_right);
        }

        wp7_tile_t *clicked_tile = &s_wp7.tiles[s_wp7.settings_tile_index];
        set_tile_number(clicked_tile, s_wp7.page, s_wp7.settings_tile_index);
        set_tile_geometry(clicked_tile, clicked_tile->y, clicked_tile->size);
        return;
    }

    if (progress < content_phase_start) {
        hide_settings_items();
        hide_tiles();

        wp7_tile_t *clicked_tile = &s_wp7.tiles[s_wp7.settings_tile_index];
        const int32_t local_progress = phase_progress(progress - clicked_phase_start,
                                                     WP7_SETTINGS_TILE_PHASE_UNIT);
        const int32_t eased_progress = ease_in_out_cubic(local_progress);

        set_tile_number(clicked_tile, s_wp7.page, s_wp7.settings_tile_index);
        render_clicked_tile_fade(clicked_tile, eased_progress, false);
        return;
    }

    hide_tiles();
    render_settings_content_in(progress - content_phase_start);
}

static void render_settings_close_transition(int32_t progress)
{
    const int32_t other_count = settings_other_tile_count();
    const int32_t content_phase = staggered_phase_max(WP7_SETTINGS_CONTENT_COUNT);
    const int32_t title_phase_start = content_phase;
    const int32_t clicked_phase_start = title_phase_start + WP7_SETTINGS_TITLE_PHASE_UNIT;
    const int32_t other_phase_start = clicked_phase_start + WP7_SETTINGS_TILE_PHASE_UNIT;
    const int32_t max_progress = transition_progress_max_for_dir(WP7_DIR_SETTINGS_CLOSE);
    const bool grow_to_right = tile_col(s_wp7.settings_tile_index) != 0;

    progress = clamp_i32(progress, 0, max_progress);
    show_status_bar();
    hide_list_items();

    if (progress < content_phase) {
        hide_tiles();
        render_settings_content_out(progress);
        return;
    }

    if (progress < clicked_phase_start) {
        hide_tiles();

        for (int32_t i = 0; i < WP7_SETTINGS_CONTENT_COUNT; i++) {
            lv_obj_add_flag(s_wp7.settings_items[i].obj, LV_OBJ_FLAG_HIDDEN);
        }

        render_settings_title_out(phase_progress(progress - title_phase_start,
                                  WP7_SETTINGS_TITLE_PHASE_UNIT));
        return;
    }

    if (progress < other_phase_start) {
        hide_settings_items();
        hide_tiles();

        wp7_tile_t *clicked_tile = &s_wp7.tiles[s_wp7.settings_tile_index];
        const int32_t local_progress = phase_progress(progress - clicked_phase_start,
                                                     WP7_SETTINGS_TILE_PHASE_UNIT);
        const int32_t eased_progress = ease_in_out_cubic(local_progress);

        set_tile_number(clicked_tile, s_wp7.page, s_wp7.settings_tile_index);
        render_clicked_tile_fade(clicked_tile, eased_progress, true);
        return;
    }

    hide_settings_items();

    for (int32_t i = 0; i < s_wp7.tile_count; i++) {
        set_tile_number(&s_wp7.tiles[i], s_wp7.page, i);
    }

    wp7_tile_t *clicked_tile = &s_wp7.tiles[s_wp7.settings_tile_index];
    set_tile_geometry(clicked_tile, clicked_tile->y, clicked_tile->size);

    progress -= other_phase_start;

    for (int32_t order = 0; order < other_count; order++) {
        const int32_t index = ordered_settings_other_index(order);
        const int32_t reverse_order = other_count - 1 - order;
        const int32_t local_progress = step_progress(progress, reverse_order);
        const int32_t eased_progress = ease_in_out_cubic(local_progress);

        render_tile_restore(&s_wp7.tiles[index], eased_progress, grow_to_right);
    }
}

static void render_transition(wp7_page_dir_t dir, int32_t progress)
{
    if (dir == WP7_DIR_SETTINGS_OPEN) {
        render_settings_open_transition(progress);
        return;
    }

    if (dir == WP7_DIR_SETTINGS_CLOSE) {
        render_settings_close_transition(progress);
        return;
    }

    if (is_horizontal_dir(dir)) {
        render_horizontal_transition(dir, progress);
        return;
    }

    render_vertical_transition(dir, progress);
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
        if (s_wp7.anim_dir == WP7_DIR_LIST) {
            s_wp7.in_list = true;
        } else if (s_wp7.anim_dir == WP7_DIR_HOME) {
            s_wp7.in_list = false;
        } else if (s_wp7.anim_dir == WP7_DIR_SETTINGS_OPEN) {
            s_wp7.in_settings = true;
        } else if (s_wp7.anim_dir == WP7_DIR_SETTINGS_CLOSE) {
            s_wp7.in_settings = false;
        } else {
            s_wp7.page = s_wp7.target_page;
        }
    }

    if (s_wp7.in_settings) {
        render_static_settings();
    } else if (s_wp7.in_list) {
        render_static_list();
    } else {
        render_static_page(s_wp7.page);
    }

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
    const int32_t progress_delta = end > start ? end - start : start - end;
    int32_t duration = animation_duration_ms(dir, progress_delta);
    const int32_t release_min_ms = animation_release_min_ms(dir);

    if (duration < release_min_ms) {
        duration = release_min_ms;
    }
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_completed_cb(&anim, transition_anim_completed_cb);
    lv_anim_start(&anim);
}

static int32_t limit_drag_progress(int32_t target_progress)
{
    const uint32_t tick = lv_tick_get();
    const uint32_t elapsed = tick - s_wp7.drag_last_tick;
    int32_t max_step = (int32_t)(elapsed > 0 ? elapsed : 1) *
                       animation_progress_per_ms(s_wp7.drag_dir);

    s_wp7.drag_last_tick = tick;

    if (target_progress > s_wp7.drag_progress + max_step) {
        return s_wp7.drag_progress + max_step;
    }

    if (target_progress < s_wp7.drag_progress - max_step) {
        return s_wp7.drag_progress - max_step;
    }

    return target_progress;
}

static void update_drag_progress(int32_t x, int32_t y)
{
    const int32_t delta_x = x - s_wp7.drag_start_x;
    const int32_t delta_y = y - s_wp7.drag_start_y;
    const int32_t threshold = transition_drag_threshold();

    if (s_wp7.drag_dir == WP7_DIR_NONE) {
        const int32_t abs_x = abs_i32(delta_x);
        const int32_t abs_y = abs_i32(delta_y);

        if (abs_x > threshold && abs_x > abs_y && !s_wp7.in_list && delta_x < -threshold) {
            s_wp7.drag_dir = WP7_DIR_LIST;
        } else if (abs_x > threshold && abs_x > abs_y && s_wp7.in_list && delta_x > threshold) {
            s_wp7.drag_dir = WP7_DIR_HOME;
        } else if (abs_y > threshold && abs_y >= abs_x && !s_wp7.in_list && delta_y < -threshold) {
            s_wp7.drag_dir = WP7_DIR_NEXT;
            s_wp7.target_page = s_wp7.page + 1;
        } else if (abs_y > threshold && abs_y >= abs_x && !s_wp7.in_list && delta_y > threshold && s_wp7.page > 0) {
            s_wp7.drag_dir = WP7_DIR_PREV;
            s_wp7.target_page = s_wp7.page - 1;
        } else {
            return;
        }
    }

    const int32_t distance = transition_drag_distance();
    const int32_t max_progress = transition_progress_max();
    int32_t drag_distance = 0;

    if (s_wp7.drag_dir == WP7_DIR_LIST) {
        drag_distance = -delta_x;
    } else if (s_wp7.drag_dir == WP7_DIR_HOME) {
        drag_distance = delta_x;
    } else if (s_wp7.drag_dir == WP7_DIR_NEXT) {
        drag_distance = -delta_y;
    } else if (s_wp7.drag_dir == WP7_DIR_PREV) {
        drag_distance = delta_y;
    }

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

static void settings_tile_clicked_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    wp7_tile_t *tile = (wp7_tile_t *)lv_event_get_user_data(event);
    const int32_t index = (int32_t)(tile - s_wp7.tiles);

    if (index != WP7_SETTINGS_TILE_INDEX || s_wp7.page != 0 || s_wp7.in_list ||
            s_wp7.in_settings || s_wp7.animating || s_wp7.tile_count <= WP7_SETTINGS_TILE_INDEX) {
        return;
    }

    s_wp7.settings_tile_index = index;
    start_progress_anim(WP7_DIR_SETTINGS_OPEN, 0,
                        transition_progress_max_for_dir(WP7_DIR_SETTINGS_OPEN), true);
}

static void settings_title_clicked_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !s_wp7.in_settings || s_wp7.animating) {
        return;
    }

    start_progress_anim(WP7_DIR_SETTINGS_CLOSE, 0,
                        transition_progress_max_for_dir(WP7_DIR_SETTINGS_CLOSE), true);
}

static void settings_slider_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_VALUE_CHANGED) {
        s_wp7.anim_speed_percent = sanitize_animation_speed(lv_slider_get_value(s_wp7.settings_slider));
        update_reset_button_state();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_wp7.anim_speed_percent = sanitize_animation_speed(lv_slider_get_value(s_wp7.settings_slider));
        update_reset_button_state();
        save_animation_speed();
    }
}

static void settings_brightness_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_VALUE_CHANGED) {
        s_wp7.brightness_percent = sanitize_brightness(lv_slider_get_value(s_wp7.brightness_slider));
        bsp_display_brightness_set(s_wp7.brightness_percent);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_wp7.brightness_percent = sanitize_brightness(lv_slider_get_value(s_wp7.brightness_slider));
        bsp_display_brightness_set(s_wp7.brightness_percent);
        save_brightness();
    }
}

static void settings_theme_clicked_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const intptr_t index = (intptr_t)lv_event_get_user_data(event);

    s_wp7.theme_index = sanitize_theme_index((int32_t)index);
    update_theme_controls();
    save_theme();
}

static void settings_reset_clicked_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || animation_speed_is_default()) {
        return;
    }

    s_wp7.anim_speed_percent = WP7_ANIM_SPEED_DEFAULT;
    lv_slider_set_value(s_wp7.settings_slider, s_wp7.anim_speed_percent, LV_ANIM_ON);
    update_reset_button_state();
    save_animation_speed();
}

static void screen_touch_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point;

    if (s_wp7.in_settings) {
        return;
    }

    if (indev == NULL) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        if (s_wp7.animating) {
            return;
        }

        lv_indev_get_point(indev, &point);
        s_wp7.drag_start_x = point.x;
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
        update_drag_progress(point.x, point.y);
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
    s_wp7.status_bar = bar;

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
            lv_obj_set_style_bg_color(tile_obj, theme_color(), 0);
            lv_obj_set_style_bg_opa(tile_obj, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(tile_obj, 0, 0);
            lv_obj_remove_flag(tile_obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(tile_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

            lv_obj_t *label = lv_label_create(tile_obj);
            lv_label_set_text_fmt(label, "%ld", (long)tile_number);
            lv_obj_set_style_text_color(label, theme_text_color(), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(label);
            lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

            wp7_tile_t *tile_data = &s_wp7.tiles[tile_number - 1];
            tile_data->obj = tile_obj;
            tile_data->label = label;
            tile_data->x = pad + col * (tile + gap);
            tile_data->y = content_top + row * (tile + gap);
            tile_data->size = tile;

            lv_obj_add_event_cb(tile_obj, settings_tile_clicked_cb, LV_EVENT_CLICKED, tile_data);
        }
    }
}

static void create_list_page(lv_obj_t *screen, int32_t screen_w, int32_t screen_h, int32_t status_h)
{
    static const char * const item_labels[] = {
        "People",
        "Messages",
        "Photos",
        "Calendar",
        "Music",
        "Settings",
        "Store",
        "Office",
    };
    const int32_t pad = scaled_px(screen_w, WP7_SCREEN_PAD_PERMILLE);
    const int32_t gap = scaled_px(screen_h, WP7_LIST_ROW_GAP_PERMILLE);
    const int32_t row_h = scaled_px(screen_h, WP7_LIST_ROW_H_PERMILLE);
    const int32_t content_top = status_h + pad;
    const int32_t row_w = screen_w - pad * 2;

    s_wp7.list_count = s_wp7.tile_count;

    if (s_wp7.list_count > WP7_MAX_LIST_ITEMS) {
        s_wp7.list_count = WP7_MAX_LIST_ITEMS;
    }

    for (int32_t i = 0; i < s_wp7.list_count; i++) {
        lv_obj_t *item_obj = lv_obj_create(screen);
        lv_obj_remove_style_all(item_obj);
        lv_obj_set_size(item_obj, row_w, row_h);
        lv_obj_set_pos(item_obj, pad, content_top + i * (row_h + gap));
        lv_obj_set_style_bg_color(item_obj, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(item_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(item_obj, 0, 0);
        lv_obj_remove_flag(item_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t *label = lv_label_create(item_obj);
        lv_label_set_text(label, item_labels[i % (sizeof(item_labels) / sizeof(item_labels[0]))]);
        lv_obj_set_style_text_color(label, lv_color_hex(0xEAF7FF), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, scaled_px(screen_w, 28), 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

        wp7_list_item_t *item_data = &s_wp7.list_items[i];
        item_data->obj = item_obj;
        item_data->label = label;
        item_data->x = pad;
        item_data->y = content_top + i * (row_h + gap);
        item_data->w = row_w;
        item_data->h = row_h;

        lv_obj_add_flag(item_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_setting_item(wp7_setting_item_index_t index, lv_obj_t *obj,
                             int32_t x, int32_t y, int32_t w, int32_t h)
{
    s_wp7.settings_items[index] = (wp7_setting_item_t) {
        .obj = obj,
        .x = x,
        .y = y,
        .w = w,
        .h = h,
    };
}

static lv_obj_t *create_settings_label(lv_obj_t *screen, const char *text,
                                       int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *label = lv_label_create(screen);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_size(label, w, h);
    lv_obj_set_pos(label, x, y);

    return label;
}

static void style_settings_slider(lv_obj_t *slider, int32_t screen_w, int32_t screen_h)
{
    lv_obj_remove_style_all(slider);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2D2D2D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_height(slider, scaled_px(screen_h, 12), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, theme_color(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xEAF7FF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_width(slider, scaled_px(screen_w, 36), LV_PART_KNOB);
    lv_obj_set_style_height(slider, scaled_px(screen_h, 36), LV_PART_KNOB);
}

static void create_settings_page(lv_obj_t *screen, int32_t screen_w, int32_t screen_h, int32_t status_h)
{
    const int32_t pad = scaled_px(screen_w, WP7_SCREEN_PAD_PERMILLE);
    const int32_t title_y = status_h + scaled_px(screen_h, 42);
    const int32_t title_h = scaled_px(screen_h, 72);
    const int32_t label_h = scaled_px(screen_h, 44);
    const int32_t slider_h = scaled_px(screen_h, 42);
    const int32_t picker_h = scaled_px(screen_h, 58);
    const int32_t brightness_label_y = title_y + scaled_px(screen_h, 98);
    const int32_t brightness_slider_y = brightness_label_y + scaled_px(screen_h, 46);
    const int32_t theme_label_y = brightness_slider_y + scaled_px(screen_h, 62);
    const int32_t theme_picker_y = theme_label_y + scaled_px(screen_h, 46);
    const int32_t speed_label_y = theme_picker_y + scaled_px(screen_h, 76);
    const int32_t speed_slider_y = speed_label_y + scaled_px(screen_h, 46);
    const int32_t button_w = scaled_px(screen_w, 230);
    const int32_t button_h = scaled_px(screen_h, 70);
    const int32_t button_x = screen_w - pad - button_w;
    const int32_t button_y = speed_slider_y + scaled_px(screen_h, 62);
    const int32_t content_w = screen_w - pad * 2;
    const int32_t swatch = scaled_px(screen_w, 96);
    const int32_t swatch_gap = scaled_px(screen_w, 28);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "UI Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_style_transform_pivot_x(title, 0, 0);
    lv_obj_set_style_transform_pivot_y(title, title_h / 2, 0);
    lv_obj_set_size(title, content_w, title_h);
    lv_obj_set_pos(title, pad, title_y);
    lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(title, settings_title_clicked_cb, LV_EVENT_CLICKED, NULL);

    s_wp7.settings_title = title;
    s_wp7.settings_title_x = pad;
    s_wp7.settings_title_y = title_y;
    s_wp7.settings_title_w = content_w;
    s_wp7.settings_title_h = title_h;

    lv_obj_t *brightness_label = create_settings_label(screen, "Brightness",
                                                       pad, brightness_label_y,
                                                       content_w, label_h);
    set_setting_item(WP7_SETTINGS_ITEM_BRIGHTNESS_LABEL, brightness_label,
                     pad, brightness_label_y, content_w, label_h);

    lv_obj_t *brightness_slider = lv_slider_create(screen);
    style_settings_slider(brightness_slider, screen_w, screen_h);
    lv_slider_set_range(brightness_slider, WP7_BRIGHTNESS_MIN, WP7_BRIGHTNESS_MAX);
    lv_slider_set_value(brightness_slider, sanitize_brightness(s_wp7.brightness_percent), LV_ANIM_OFF);
    lv_obj_set_size(brightness_slider, content_w, slider_h);
    lv_obj_set_pos(brightness_slider, pad, brightness_slider_y);
    lv_obj_add_event_cb(brightness_slider, settings_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(brightness_slider, settings_brightness_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(brightness_slider, settings_brightness_cb, LV_EVENT_PRESS_LOST, NULL);
    s_wp7.brightness_slider = brightness_slider;
    set_setting_item(WP7_SETTINGS_ITEM_BRIGHTNESS_SLIDER, brightness_slider,
                     pad, brightness_slider_y, content_w, slider_h);

    lv_obj_t *theme_label = create_settings_label(screen, "Theme color",
                                                  pad, theme_label_y,
                                                  content_w, label_h);
    set_setting_item(WP7_SETTINGS_ITEM_THEME_LABEL, theme_label,
                     pad, theme_label_y, content_w, label_h);

    lv_obj_t *theme_picker = lv_obj_create(screen);
    lv_obj_remove_style_all(theme_picker);
    lv_obj_set_size(theme_picker, content_w, picker_h);
    lv_obj_set_pos(theme_picker, pad, theme_picker_y);
    lv_obj_remove_flag(theme_picker, LV_OBJ_FLAG_SCROLLABLE);
    s_wp7.theme_picker = theme_picker;
    set_setting_item(WP7_SETTINGS_ITEM_THEME_PICKER, theme_picker,
                     pad, theme_picker_y, content_w, picker_h);

    for (int32_t i = 0; i < WP7_THEME_COLOR_COUNT; i++) {
        lv_obj_t *swatch_obj = lv_obj_create(theme_picker);
        lv_obj_remove_style_all(swatch_obj);
        lv_obj_set_size(swatch_obj, swatch, swatch);
        lv_obj_set_pos(swatch_obj, i * (swatch + swatch_gap), 0);
        lv_obj_set_style_bg_color(swatch_obj, lv_color_hex(s_wp7_theme_colors[i]), 0);
        lv_obj_set_style_bg_opa(swatch_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(swatch_obj, 0, 0);
        lv_obj_set_style_border_opa(swatch_obj, LV_OPA_COVER, 0);
        lv_obj_remove_flag(swatch_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(swatch_obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(swatch_obj, settings_theme_clicked_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_wp7.theme_buttons[i] = swatch_obj;
    }

    lv_obj_t *speed_label = create_settings_label(screen, "Animation speed",
                                                  pad, speed_label_y,
                                                  content_w, label_h);
    set_setting_item(WP7_SETTINGS_ITEM_SPEED_LABEL, speed_label,
                     pad, speed_label_y, content_w, label_h);

    lv_obj_t *slider = lv_slider_create(screen);
    style_settings_slider(slider, screen_w, screen_h);
    lv_slider_set_range(slider, WP7_ANIM_SPEED_MIN, WP7_ANIM_SPEED_MAX);
    lv_slider_set_value(slider, sanitize_animation_speed(s_wp7.anim_speed_percent), LV_ANIM_OFF);
    lv_obj_set_size(slider, content_w, slider_h);
    lv_obj_set_pos(slider, pad, speed_slider_y);
    lv_obj_add_event_cb(slider, settings_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, settings_slider_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, settings_slider_cb, LV_EVENT_PRESS_LOST, NULL);
    s_wp7.settings_slider = slider;
    set_setting_item(WP7_SETTINGS_ITEM_SPEED_SLIDER, slider,
                     pad, speed_slider_y, content_w, slider_h);

    lv_obj_t *button = lv_obj_create(screen);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, button_w, button_h);
    lv_obj_set_pos(button, button_x, button_y);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 0, 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, settings_reset_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Reset");
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_24, 0);
    lv_obj_center(button_label);

    s_wp7.settings_reset_button = button;
    s_wp7.settings_reset_label = button_label;
    set_setting_item(WP7_SETTINGS_ITEM_RESET_BUTTON, button,
                     button_x, button_y, button_w, button_h);

    update_theme_controls();
    hide_settings_items();
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
        .screen_w = screen_w,
        .screen_h = screen_h,
        .settings_tile_index = WP7_SETTINGS_TILE_INDEX,
        .anim_speed_percent = WP7_ANIM_SPEED_DEFAULT,
        .brightness_percent = WP7_BRIGHTNESS_DEFAULT,
        .theme_index = 0,
    };
    load_ui_settings();

    create_status_bar(screen, screen_w, status_h, pad);
    create_tile_grid(screen, screen_w, screen_h, status_h);
    create_list_page(screen, screen_w, screen_h, status_h);
    create_settings_page(screen, screen_w, screen_h, status_h);
    render_static_page(0);
}

void app_main(void)
{
    init_nvs();
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

    bsp_display_lock(0);
    create_wp7_first_screen();
    bsp_display_unlock();

    bsp_display_brightness_set(s_wp7.brightness_percent);
}
