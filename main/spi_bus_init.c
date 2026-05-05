/* spi_bus_init.c — full GC9A01 display initialisation in plain C.
 *
 * esp_lcd_gc9a01.h and esp_lcd_panel_io.h transitively include spi_ll.h,
 * which defines constexpr C++ operator overloads that conflict with
 * esp_attr.h when compiled in C++ mode on this ESP-IDF version.
 * Keeping ALL SPI/LCD init here (plain C TU) avoids the conflict entirely.
 */
#include "spi_bus_init.h"

#include "driver/spi_master.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "hardware_config.h"

static const char *TAG = "spi_display";

esp_err_t cp128_display_init(esp_lcd_panel_handle_t *out_handle)
{
    if (!out_handle) { return ESP_ERR_INVALID_ARG; }
    *out_handle = NULL;

    /* SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num     = LCD_SPI_MOSI_PIN,
        .miso_io_num     = -1,
        .sclk_io_num     = LCD_SPI_SCLK_PIN,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = PANEL_WIDTH * PANEL_HEIGHT * 2,
    };
    esp_err_t ret = spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize FAILED err=0x%x", (unsigned)ret);
        return ret;
    }
    ESP_LOGI(TAG, "spi_bus_initialize OK mosi=%d sclk=%d", LCD_SPI_MOSI_PIN, LCD_SPI_SCLK_PIN);

    /* Panel IO */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num        = LCD_SPI_DC_PIN,
        .cs_gpio_num        = LCD_SPI_CS_PIN,
        .pclk_hz            = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits       = 8,
        .lcd_param_bits     = 8,
        .spi_mode           = 0,
        .trans_queue_depth  = 10,
    };
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)(uintptr_t)LCD_SPI_HOST,
                                   &io_cfg, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi FAILED err=0x%x", (unsigned)ret);
        return ret;
    }
    ESP_LOGI(TAG, "panel_io OK dc=%d cs=%d clk=%u", LCD_SPI_DC_PIN, LCD_SPI_CS_PIN,
             (unsigned)LCD_SPI_CLK_HZ);

    /* GC9A01 panel device */
    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num  = LCD_SPI_RST_PIN,
        .color_space     = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel  = 16,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ret = esp_lcd_new_panel_gc9a01(io_handle, &dev_cfg, &panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_gc9a01 FAILED err=0x%x", (unsigned)ret);
        return ret;
    }
    ESP_LOGI(TAG, "gc9a01 panel created handle=%p", (void *)panel);

    ret = esp_lcd_panel_reset(panel);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "panel_reset FAILED"); return ret; }

    ret = esp_lcd_panel_init(panel);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "panel_init FAILED"); return ret; }

    /* HARDWARE-CRITICAL: GC9A01 needs color inversion for correct RGB565 colors. */
    ret = esp_lcd_panel_invert_color(panel, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "invert_color FAILED err=0x%x (non-fatal)", (unsigned)ret);
    }
    ESP_LOGI(TAG, "invert_color(true) applied");

    *out_handle = panel;
    ESP_LOGI(TAG, "=== cp128_display_init SUCCESS ===");
    return ESP_OK;
}

