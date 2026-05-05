/**
 * @file lv_conf.h
 * Configuration file for LVGL - CrowPanel 1.28 (ESP32-S3, 240x240)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

// ===== DISPLAY CONFIGURATION =====
#define LV_HOR_RES_MAX 240
#define LV_VER_RES_MAX 240
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

// ===== MEMORY CONFIGURATION =====
// Use the ESP-IDF system allocator — avoids pool fragmentation.
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc
#define LV_MEM_SIZE (64 * 1024)
#define LV_MEM_ADR 0

// ===== TICK TIMER =====
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "hardware_config.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis_now())

// ===== FONT CONFIGURATION =====
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// ===== FEATURE CONFIGURATION =====
#define LV_USE_LOG 0
#define LV_LOG_LEVEL LV_LOG_LEVEL_NONE

// Disable complex draw initially for CP128 bring-up stability.
// The 240x240 SPI panel is much faster than RGB-parallel; re-enable
// after first successful boot to validate hardware.
#define LV_DRAW_COMPLEX 0
#define LV_SHADOW_CACHE_SIZE 0
#define LV_USE_GPU_STM32_DMA2D 0

#define LV_USE_BTN 1
#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 0
#define LV_USE_LIST 1
#define LV_USE_OBJ_STYLE 1
#define LV_USE_STYLE_TRANSITIONS 0
#define LV_USE_ANIMATION 1

// ===== INPUT DEVICE CONFIGURATION =====
#define LV_INDEV_DEF_READ_PERIOD 12
#define LV_INDEV_DEF_DRAG_LIMIT 0

// ===== THEME CONFIGURATION =====
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE 0

// ===== PERFORMANCE OPTIMIZATIONS =====
// SPI at 40 MHz is faster per-pixel than CP21 RGB-parallel; 20 ms refresh
// comfortably targets 50 fps even during full-screen redraws at 240x240.
#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_MEMCPY_MEMSET_STD 1
#define LV_OBJ_DEF_HIDDEN 0

#endif // LV_CONF_H
