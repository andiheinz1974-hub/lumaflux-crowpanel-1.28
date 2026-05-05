// board_hal.cpp — CrowPanel 1.28 hardware abstraction layer
// GC9A01 SPI display, CST816D touch (I2C_NUM_1), rotary encoder, button, backlight.
//
// Public API is IDENTICAL to CP21 board_hal so upper layers require no changes.

#include "board_hal.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
// All SPI/LCD init is in spi_bus_init.c (plain C) to avoid the spi_ll.h
// constexpr operator conflict with esp_attr.h in this ESP-IDF version.
#include "spi_bus_init.h"
// Only panel ops needed here (draw_bitmap, invert_color etc.) — no SPI headers.
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "hardware_config.h"

namespace {
constexpr const char *kTag = "board_hal";

// Encoder pins
constexpr gpio_num_t kEncoderAPin = GPIO_NUM_45;
constexpr gpio_num_t kEncoderBPin = GPIO_NUM_42;

// Button: direct GPIO, active-low, internal pull-up
constexpr gpio_num_t kButtonPin = GPIO_NUM_41;

// Touch: CST816D on dedicated I2C bus
constexpr i2c_port_t kTouchI2cPort = I2C_NUM_1;
constexpr gpio_num_t kTouchSdaPin = GPIO_NUM_4;
constexpr gpio_num_t kTouchSclPin = GPIO_NUM_5;
constexpr gpio_num_t kTouchIntPin = GPIO_NUM_6;
constexpr gpio_num_t kTouchRstPin = GPIO_NUM_7;
constexpr uint8_t kTouchAddr = TOUCH_I2C_ADDR;

// CST816D register map (same as CST8XX family)
constexpr uint8_t kTouchRegFingerCount = 0x02;
constexpr uint8_t kTouchRegPointData = 0x03;  // 4 bytes: XH XL YH YL

// Backlight LEDC
constexpr gpio_num_t kBacklightPin = GPIO_NUM_46;
constexpr ledc_mode_t kBacklightSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_0;
constexpr uint32_t kBacklightFreqHz = 5000;
constexpr ledc_timer_bit_t kBacklightResolution = LEDC_TIMER_8_BIT;

// Diagnostic throttle periods
constexpr int64_t kI2cWarnPeriodUs = 2000000;
constexpr int64_t kDisplayDiagPeriodUs = 3000000;
constexpr int64_t kNoPanelFlushWarnPeriodUs = 2000000;
constexpr int64_t kInputProbePeriodUs = 2000000;

// State
uint8_t s_backlight_percent = 0;
bool s_encoder_a_last = false;
bool s_button_pressed = false;
bool s_display_ready = false;
uint32_t s_flush_count = 0;
esp_lcd_panel_handle_t s_panel_handle = nullptr;
esp_err_t s_display_init_error = ESP_OK;

int64_t s_last_touch_warn_us = 0;
int64_t s_last_display_diag_us = 0;
int64_t s_last_no_panel_flush_warn_us = 0;
int64_t s_last_touch_probe_us = 0;
int64_t s_last_encoder_probe_us = 0;
int64_t s_last_button_probe_us = 0;

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

void log_heap_state(const char *label) {
    const uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(kTag, "heap_%s internal_free=%u psram_free=%u",
             label,
             static_cast<unsigned>(internal_free),
             static_cast<unsigned>(psram_free));
}

bool touch_read_registers(uint8_t reg, uint8_t *buffer, size_t len) {
    if (!buffer || len == 0) { return false; }

    const esp_err_t err = i2c_master_write_read_device(
        kTouchI2cPort,
        kTouchAddr,
        &reg, 1,
        buffer, len,
        pdMS_TO_TICKS(20));

    if (err != ESP_OK) {
        const int64_t now_us = esp_timer_get_time();
        if ((now_us - s_last_touch_warn_us) >= kI2cWarnPeriodUs) {
            s_last_touch_warn_us = now_us;
            ESP_LOGW(kTag, "touch_read_failed addr=0x%02X reg=0x%02X len=%u err=0x%x",
                     kTouchAddr, reg, static_cast<unsigned>(len), static_cast<unsigned>(err));
        }
        return false;
    }
    return true;
}

int16_t clamp_coord(int32_t value, int16_t max_inclusive) {
    if (value < 0) { return 0; }
    if (value > max_inclusive) { return max_inclusive; }
    return static_cast<int16_t>(value);
}

// -------------------------------------------------------------------------
// Display initialisation — delegates to the plain-C spi_bus_init.c to avoid
// the spi_ll.h constexpr operator conflicts in this ESP-IDF version.
// -------------------------------------------------------------------------

esp_err_t init_spi_panel() {
    return cp128_display_init(&s_panel_handle);
}

// -------------------------------------------------------------------------
// Touch controller: CST816D reset sequence
// -------------------------------------------------------------------------

void cst816d_hardware_reset() {
    // RST active-low: pull low 10 ms, release, wait 50 ms for firmware ready.
    gpio_config_t rst_cfg = {};
    rst_cfg.pin_bit_mask = (1ULL << kTouchRstPin);
    rst_cfg.mode = GPIO_MODE_OUTPUT;
    rst_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    rst_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&rst_cfg);

    gpio_set_level(kTouchRstPin, 0);
    delay_ms(10);
    gpio_set_level(kTouchRstPin, 1);
    delay_ms(50);
    ESP_LOGI(kTag, "cst816d_reset done rst_pin=%d", kTouchRstPin);
}

}  // namespace

// -------------------------------------------------------------------------
// board_hal public interface
// -------------------------------------------------------------------------

esp_err_t board_hal_init(void) {
    ESP_LOGI(kTag, "=== BOARD_HAL_INIT START ===");
    log_heap_state("before_display");

    // --- Display (GC9A01 SPI) ---
    s_display_init_error = init_spi_panel();
    if (s_display_init_error == ESP_OK) {
        s_display_ready = true;
        ESP_LOGI(kTag, "display_ready=YES");
    } else {
        s_display_ready = false;
        s_panel_handle = nullptr;
        ESP_LOGE(kTag, "display_init FAILED err=0x%x — degraded mode",
                 static_cast<unsigned>(s_display_init_error));
    }
    log_heap_state("after_display");

    // --- Touch (CST816D, I2C_NUM_1) ---
    ESP_LOGI(kTag, "TOUCH_RESET: CST816D hardware reset");
    cst816d_hardware_reset();

    // Configure INT pin as input (we poll, no ISR needed for now)
    gpio_config_t int_cfg = {};
    int_cfg.pin_bit_mask = (1ULL << kTouchIntPin);
    int_cfg.mode = GPIO_MODE_INPUT;
    int_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    int_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    int_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&int_cfg);

    ESP_LOGI(kTag, "I2C_TOUCH_INIT: port=%d sda=%d scl=%d freq=400k",
             kTouchI2cPort, kTouchSdaPin, kTouchSclPin);
    i2c_config_t i2c_config = {};
    i2c_config.mode = I2C_MODE_MASTER;
    i2c_config.sda_io_num = kTouchSdaPin;
    i2c_config.scl_io_num = kTouchSclPin;
    i2c_config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.master.clk_speed = TOUCH_I2C_FREQ_HZ;
    i2c_config.clk_flags = 0;

    esp_err_t i2c_err = i2c_param_config(kTouchI2cPort, &i2c_config);
    if (i2c_err != ESP_OK) {
        ESP_LOGE(kTag, "i2c_param_config FAILED err=0x%x", static_cast<unsigned>(i2c_err));
    } else {
        const esp_err_t install_err = i2c_driver_install(kTouchI2cPort, I2C_MODE_MASTER, 0, 0, 0);
        if (install_err != ESP_OK && install_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(kTag, "i2c_driver_install FAILED err=0x%x", static_cast<unsigned>(install_err));
        } else {
            ESP_LOGI(kTag, "I2C_TOUCH_INIT SUCCESS");
        }
    }

    // --- Encoder (GPIO45=A, GPIO42=B) ---
    ESP_LOGI(kTag, "ENCODER_GPIO: A=%d B=%d", kEncoderAPin, kEncoderBPin);
    gpio_config_t enc_cfg = {};
    enc_cfg.pin_bit_mask = (1ULL << kEncoderAPin) | (1ULL << kEncoderBPin);
    enc_cfg.mode = GPIO_MODE_INPUT;
    enc_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    enc_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    enc_cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&enc_cfg));
    s_encoder_a_last = gpio_get_level(kEncoderAPin) != 0;
    ESP_LOGI(kTag, "ENCODER_GPIO configured encoder_a_initial=%d", s_encoder_a_last ? 1 : 0);

    // --- Button (GPIO41, active-low, internal pull-up) ---
    ESP_LOGI(kTag, "BUTTON_GPIO: pin=%d (active-low)", kButtonPin);
    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = (1ULL << kButtonPin);
    btn_cfg.mode = GPIO_MODE_INPUT;
    btn_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn_cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));
    ESP_LOGI(kTag, "BUTTON_GPIO configured");

    // --- Backlight (GPIO46, LEDC ch0) ---
    ESP_LOGI(kTag, "LEDC_INIT: backlight pin=%d freq=5kHz", kBacklightPin);
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = kBacklightSpeedMode;
    timer_cfg.duty_resolution = kBacklightResolution;
    timer_cfg.timer_num = kBacklightTimer;
    timer_cfg.freq_hz = static_cast<int>(kBacklightFreqHz);
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num = kBacklightPin;
    ch_cfg.speed_mode = kBacklightSpeedMode;
    ch_cfg.channel = kBacklightChannel;
    ch_cfg.intr_type = LEDC_INTR_DISABLE;
    ch_cfg.timer_sel = kBacklightTimer;
    ch_cfg.duty = 0;
    ch_cfg.hpoint = 0;
    ch_cfg.flags.output_invert = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    board_hal_set_backlight_percent(s_backlight_percent);
    ESP_LOGI(kTag, "LEDC backlight configured pin=%d", kBacklightPin);

    ESP_LOGI(kTag, "=== BOARD_HAL_INIT COMPLETE display_ready=%s ===",
             s_display_ready ? "YES" : "NO");
    return ESP_OK;
}

void board_hal_display_flush(const lv_area_t *area, lv_color_t *color_map) {
    if (!s_panel_handle || !area || !color_map) {
        if (!s_panel_handle) {
            const int64_t now_us = esp_timer_get_time();
            if ((now_us - s_last_no_panel_flush_warn_us) >= kNoPanelFlushWarnPeriodUs) {
                s_last_no_panel_flush_warn_us = now_us;
                ESP_LOGW(kTag, "flush_skipped display_err=0x%x", static_cast<unsigned>(s_display_init_error));
            }
        }
        return;
    }

    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;
    if (w <= 0 || h <= 0) { return; }

    if (s_flush_count < 5) {
        ESP_LOGI(kTag, "draw_bitmap #%lu area=[%d,%d]-[%d,%d] w=%d h=%d",
                 static_cast<unsigned long>(s_flush_count),
                 area->x1, area->y1, area->x2, area->y2, w, h);
    }

    const esp_err_t ret = esp_lcd_panel_draw_bitmap(
        s_panel_handle,
        area->x1, area->y1,
        area->x2 + 1, area->y2 + 1,
        color_map);
    ++s_flush_count;

    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "draw_bitmap_failed err=0x%x area=[%d,%d]-[%d,%d]",
                 static_cast<unsigned>(ret), area->x1, area->y1, area->x2, area->y2);
    }

    // SPI transfer is synchronous: signal LVGL immediately.
    lv_disp_flush_ready(lv_disp_get_default()->driver);
}

bool board_hal_read_touch(BoardTouchPoint *point) {
    if (!point) { return false; }

    uint8_t finger_count = 0;
    const bool fc_ok = touch_read_registers(kTouchRegFingerCount, &finger_count, 1);

    uint8_t pd[4] = {0};
    bool pd_ok = false;
    if (fc_ok && (finger_count & 0x0FU) != 0U) {
        pd_ok = touch_read_registers(kTouchRegPointData, pd, sizeof(pd));
    }

    // Periodic probe for diagnostics
    const int64_t now_us = esp_timer_get_time();
    if ((now_us - s_last_touch_probe_us) >= kInputProbePeriodUs) {
        s_last_touch_probe_us = now_us;
        ESP_LOGI(kTag,
                 "probe_touch addr=0x%02X fc_ok=%d fc=0x%02X pd_ok=%d raw=[0x%02X 0x%02X 0x%02X 0x%02X]",
                 kTouchAddr, fc_ok ? 1 : 0,
                 static_cast<unsigned>(finger_count), pd_ok ? 1 : 0,
                 pd[0], pd[1], pd[2], pd[3]);
    }

    if (!fc_ok || (finger_count & 0x0FU) == 0U || !pd_ok) { return false; }

    // Registers 0x03-0x06: [XH XL YH YL]
    // High nibble of XH/YH = event flag; low nibble = coordinate[11:8].
    const int32_t raw_x = ((static_cast<int32_t>(pd[0] & 0x0F) << 8) | pd[1]);
    const int32_t raw_y = ((static_cast<int32_t>(pd[2] & 0x0F) << 8) | pd[3]);

    point->x = clamp_coord(raw_x, DISPLAY_WIDTH - 1);
    point->y = clamp_coord(raw_y, DISPLAY_HEIGHT - 1);
    return true;
}

int16_t board_hal_read_encoder_diff(void) {
    const bool a_now = gpio_get_level(kEncoderAPin) != 0;
    const bool b_now = gpio_get_level(kEncoderBPin) != 0;
    int16_t diff = 0;

    // Both-edge decode: one detent = one half-quadrature cycle of A.
    // Clockwise: B leads A → a != b at both edges → diff = -1.
    // Counter-clockwise: a == b at both edges → diff = +1.
    // Sign convention: CW = positive (same as CP21).
    if (a_now != s_encoder_a_last) {
        diff = (a_now == b_now) ? static_cast<int16_t>(+1)
                                : static_cast<int16_t>(-1);
        ESP_LOGD(kTag, "encoder_edge a=%d b=%d diff=%d", a_now ? 1 : 0, b_now ? 1 : 0, diff);
    }
    s_encoder_a_last = a_now;

    const int64_t now_us = esp_timer_get_time();
    if ((now_us - s_last_encoder_probe_us) >= kInputProbePeriodUs) {
        s_last_encoder_probe_us = now_us;
        ESP_LOGI(kTag, "probe_encoder A=%d B=%d a=%d b=%d",
                 kEncoderAPin, kEncoderBPin, a_now ? 1 : 0, b_now ? 1 : 0);
    }
    return diff;
}

bool board_hal_read_encoder_button(void) {
    const bool pressed = gpio_get_level(kButtonPin) == 0;  // active-low

    const int64_t now_us = esp_timer_get_time();
    if ((now_us - s_last_button_probe_us) >= kInputProbePeriodUs) {
        s_last_button_probe_us = now_us;
        ESP_LOGI(kTag, "probe_button pin=%d level=%d pressed=%d",
                 kButtonPin, gpio_get_level(kButtonPin), pressed ? 1 : 0);
    }

    const bool old_pressed = s_button_pressed;
    s_button_pressed = pressed;
    if (old_pressed != s_button_pressed) {
        ESP_LOGD(kTag, "button_change pressed=%d", s_button_pressed ? 1 : 0);
    }
    return s_button_pressed;
}

void board_hal_set_backlight_percent(uint8_t percent) {
    if (percent > 100) { percent = 100; }
    s_backlight_percent = percent;

    const uint32_t duty_max = (1U << kBacklightResolution) - 1U;
    const uint32_t duty = (static_cast<uint32_t>(s_backlight_percent) * duty_max) / 100U;
    ESP_ERROR_CHECK(ledc_set_duty(kBacklightSpeedMode, kBacklightChannel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(kBacklightSpeedMode, kBacklightChannel));
    ESP_LOGD(kTag, "backlight percent=%u duty=%u",
             static_cast<unsigned>(s_backlight_percent), static_cast<unsigned>(duty));
}

uint8_t board_hal_get_backlight_percent(void) {
    return s_backlight_percent;
}

bool board_hal_display_is_ready(void) {
    return s_display_ready;
}

void board_hal_log_diagnostics(void) {
    const int64_t now_us = esp_timer_get_time();
    if ((now_us - s_last_display_diag_us) < kDisplayDiagPeriodUs) { return; }
    s_last_display_diag_us = now_us;

    ESP_LOGI(kTag,
             "diag display_ready=%d display_err=0x%x backlight=%u flush_count=%lu",
             s_display_ready ? 1 : 0,
             static_cast<unsigned>(s_display_init_error),
             static_cast<unsigned>(s_backlight_percent),
             static_cast<unsigned long>(s_flush_count));
    log_heap_state("periodic");
}
