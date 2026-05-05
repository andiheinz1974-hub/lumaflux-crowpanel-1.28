#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

struct BoardTouchPoint {
    int16_t x;
    int16_t y;
};

esp_err_t board_hal_init(void);
void board_hal_display_flush(const lv_area_t *area, lv_color_t *color_map);
bool board_hal_display_is_ready(void);
void board_hal_log_diagnostics(void);

bool board_hal_read_touch(BoardTouchPoint *point);
int16_t board_hal_read_encoder_diff(void);
bool board_hal_read_encoder_button(void);

void board_hal_set_backlight_percent(uint8_t percent);
uint8_t board_hal_get_backlight_percent(void);
