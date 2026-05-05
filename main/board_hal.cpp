// board_hal.cpp — CrowPanel 1.28 HAL stub
// C2 will replace this with the full GC9A01 + CST816D + encoder + backlight implementation.

#include "board_hal.h"
#include "hardware_config.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "board_hal";

static uint8_t s_backlight_pct = 100;

esp_err_t board_hal_init(void) {
    ESP_LOGI(TAG, "board_hal_init stub — full init coming in C2");
    return ESP_OK;
}

void board_hal_display_flush(const lv_area_t * /*area*/, lv_color_t * /*color_map*/) {
    lv_disp_flush_ready(lv_disp_get_default()->driver);
}

bool board_hal_display_is_ready(void) {
    return true;
}

void board_hal_log_diagnostics(void) {
    ESP_LOGI(TAG, "board_hal_log_diagnostics stub");
}

bool board_hal_read_touch(BoardTouchPoint *point) {
    if (point) { point->x = -1; point->y = -1; }
    return false;
}

int16_t board_hal_read_encoder_diff(void) {
    return 0;
}

bool board_hal_read_encoder_button(void) {
    return false;
}

void board_hal_set_backlight_percent(uint8_t percent) {
    s_backlight_pct = percent;
}

uint8_t board_hal_get_backlight_percent(void) {
    return s_backlight_pct;
}
