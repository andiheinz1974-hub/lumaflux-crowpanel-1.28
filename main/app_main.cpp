// app_main.cpp — CrowPanel 1.28 minimal bring-up stub
// C3 will replace this with full LVGL init, display buffer setup, and task loop.

#include "board_hal.h"
#include "hardware_config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== LumaFlux CrowPanel 1.28 booting ===");

    esp_err_t err = board_hal_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board_hal_init failed: %s", esp_err_to_name(err));
    }

    // C3 will initialise LVGL, register display/input devices, and launch the
    // GUI task. For now just spin so the watchdog can be verified at idle.
    ESP_LOGI(TAG, "Stub boot complete — awaiting C3 LVGL init");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
