#pragma once
#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise SPI2 bus, GC9A01 panel IO and panel device.
 * Applies mandatory color-inversion (invert_color=true).
 * Returns the ready panel handle via @p out_handle on success.
 */
esp_err_t cp128_display_init(esp_lcd_panel_handle_t *out_handle);

#ifdef __cplusplus
}
#endif
