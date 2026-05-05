#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline uint32_t millis_now() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static inline void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// Native LVGL resolution for CrowPanel 1.28.
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 240

// Physical panel resolution for 1.28 model.
#define PANEL_WIDTH  240
#define PANEL_HEIGHT 240
#define DISPLAY_SCALE_FACTOR 1

// GC9A01 SPI display (SPI2_HOST)
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_SPI_MOSI_PIN  11
#define LCD_SPI_SCLK_PIN  12
#define LCD_SPI_CS_PIN    10
#define LCD_SPI_DC_PIN    13
#define LCD_SPI_RST_PIN   14
#define LCD_SPI_CLK_HZ    40000000

// Touch: CST816D on dedicated I2C bus (I2C_NUM_1)
#define TOUCH_I2C_PORT    I2C_NUM_1
#define TOUCH_I2C_SDA_PIN 4
#define TOUCH_I2C_SCL_PIN 5
#define TOUCH_I2C_INT_PIN 6
#define TOUCH_I2C_RST_PIN 7
#define TOUCH_I2C_ADDR    0x15
#define TOUCH_I2C_FREQ_HZ 400000

// Rotary encoder
#define ENCODER_CLK_PIN  45   // A phase
#define ENCODER_DT_PIN   42   // B phase
#define ENCODER_SW_PIN   -1   // unused (button is separate)
#define BUTTON_PIN       41   // active-low, internal pull-up

// Display backlight (LEDC PWM)
#define DISPLAY_BACKLIGHT_PIN    46
#define DISPLAY_BACKLIGHT_PWM_CH 0
#define DISPLAY_BACKLIGHT_ACTIVE 255

// Sleep and timing
#define DISPLAY_SLEEP_TIMEOUT_MS 120000

// LVGL Configuration
// 60 lines is DMA-efficient for SPI; two draw buffers = 240*60*2 = ~28 KB each.
#define LVGL_BUF_LINES 60
#define LVGL_BUF_SIZE  (DISPLAY_WIDTH * LVGL_BUF_LINES)
#define LVGL_TICK_RATE_MS 5

// Recovery mode: keeps firmware alive with minimal boot path while isolating
// display-memory and GUI initialization failures.
#ifndef RECOVERY_SAFE_BOOT
#define RECOVERY_SAFE_BOOT 0
#endif

// Splash module toggle. Keep disabled until explicitly required.
#ifndef ENABLE_SPLASH_SCREEN
#define ENABLE_SPLASH_SCREEN 0
#endif

#endif // HARDWARE_CONFIG_H
