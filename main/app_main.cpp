#include "board_hal.h"
#include "hardware_config.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace {

static const char *TAG = "app_main";

constexpr int kHorRes = DISPLAY_WIDTH;
constexpr int kVerRes = DISPLAY_HEIGHT;
constexpr int kLvglFallbackLines[] = {LVGL_BUF_LINES, 40, 20, 10};
constexpr uint8_t kBootBacklightTargetPercent = 35;
constexpr uint32_t kBootBacklightRampStepMs = 40;
constexpr uint32_t kDiagLogPeriodMs = 2000;

lv_disp_draw_buf_t s_drawBuf;
lv_color_t *s_buf1 = nullptr;
lv_color_t *s_buf2 = nullptr;
esp_timer_handle_t s_lvglTickTimer = nullptr;

lv_obj_t *s_statusLabel = nullptr;
bool s_toggleState = false;

lv_color_t *alloc_draw_buf(size_t bytes) {
    return static_cast<lv_color_t *>(
        heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
}

void lvgl_tick_cb(void *) {
    lv_tick_inc(LVGL_TICK_RATE_MS);
}

void lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_map) {
    (void)disp_drv;
    board_hal_display_flush(area, color_map);
}

void lvgl_touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    (void)indev_drv;

    static lv_point_t lastPoint = {kHorRes / 2, kVerRes / 2};
    BoardTouchPoint point{};
    if (board_hal_read_touch(&point)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = point.x;
        data->point.y = point.y;
        lastPoint = data->point;
        return;
    }

    data->state = LV_INDEV_STATE_REL;
    data->point = lastPoint;
}

void lvgl_encoder_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    (void)indev_drv;
    data->enc_diff = board_hal_read_encoder_diff();
    data->state = board_hal_read_encoder_button() ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

void smoke_button_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    s_toggleState = !s_toggleState;

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen,
                              s_toggleState ? lv_palette_lighten(LV_PALETTE_GREEN, 3)
                                            : lv_palette_lighten(LV_PALETTE_BLUE, 4),
                              LV_PART_MAIN);
    lv_label_set_text(s_statusLabel, s_toggleState ? "TOUCH OK" : "READY");
}

void create_smoke_screen(lv_group_t *encoderGroup) {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_palette_lighten(LV_PALETTE_BLUE, 4), LV_PART_MAIN);

    lv_obj_t *topMarker = lv_label_create(screen);
    lv_label_set_text(topMarker, "TOP");
    lv_obj_set_style_text_font(topMarker, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(topMarker, LV_ALIGN_TOP_MID, 0, 8);

    s_statusLabel = lv_label_create(screen);
    lv_label_set_text(s_statusLabel, "READY");
    lv_obj_set_style_text_font(s_statusLabel, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_statusLabel, LV_ALIGN_CENTER, 0, -42);

    lv_obj_t *slider = lv_slider_create(screen);
    lv_obj_set_width(slider, 132);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 35, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 2);
    lv_group_add_obj(encoderGroup, slider);

    lv_obj_t *button = lv_btn_create(screen);
    lv_obj_set_size(button, 124, 50);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 58);
    lv_obj_add_event_cb(button, smoke_button_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(encoderGroup, button);

    lv_obj_t *buttonLabel = lv_label_create(button);
    lv_label_set_text(buttonLabel, "TOGGLE");
    lv_obj_center(buttonLabel);

    lv_group_focus_obj(slider);
}

bool init_lvgl_draw_buffers(void) {
    for (int lines : kLvglFallbackLines) {
        const size_t pxCount = static_cast<size_t>(kHorRes) * static_cast<size_t>(lines);
        const size_t bytes = pxCount * sizeof(lv_color_t);

        s_buf1 = alloc_draw_buf(bytes);
        if (!s_buf1) {
            continue;
        }

        s_buf2 = alloc_draw_buf(bytes);
        if (!s_buf2) {
            ESP_LOGW(TAG, "LVGL using single draw buffer (%d lines, %u bytes)", lines,
                     static_cast<unsigned>(bytes));
        } else {
            ESP_LOGI(TAG, "LVGL using double draw buffer (%d lines, %u bytes each)", lines,
                     static_cast<unsigned>(bytes));
        }

        lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2, pxCount);
        return true;
    }

    return false;
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== LumaFlux CrowPanel 1.28 booting (C3) ===");

    const esp_reset_reason_t rr = esp_reset_reason();
    ESP_LOGI(TAG, "reset_reason=%d", static_cast<int>(rr));

    esp_err_t err = board_hal_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board_hal_init failed: %s", esp_err_to_name(err));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    board_hal_set_backlight_percent(0);

    lv_init();

    esp_timer_create_args_t tickTimerArgs = {};
    tickTimerArgs.callback = lvgl_tick_cb;
    tickTimerArgs.name = "lvgl_tick";
    ESP_ERROR_CHECK(esp_timer_create(&tickTimerArgs, &s_lvglTickTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvglTickTimer,
                                             static_cast<uint64_t>(LVGL_TICK_RATE_MS) * 1000ULL));

    if (!init_lvgl_draw_buffers()) {
        ESP_LOGE(TAG, "Failed to allocate any LVGL draw buffer");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = kHorRes;
    dispDrv.ver_res = kVerRes;
    dispDrv.flush_cb = lvgl_flush_cb;
    dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&dispDrv);

    static lv_indev_drv_t touchDrv;
    lv_indev_drv_init(&touchDrv);
    touchDrv.type = LV_INDEV_TYPE_POINTER;
    touchDrv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&touchDrv);

    static lv_indev_drv_t encoderDrv;
    lv_indev_drv_init(&encoderDrv);
    encoderDrv.type = LV_INDEV_TYPE_ENCODER;
    encoderDrv.read_cb = lvgl_encoder_read_cb;
    lv_indev_t *encoderIndev = lv_indev_drv_register(&encoderDrv);

    lv_group_t *encoderGroup = lv_group_create();
    lv_indev_set_group(encoderIndev, encoderGroup);

    create_smoke_screen(encoderGroup);

    for (uint8_t percent = 0; percent <= kBootBacklightTargetPercent; percent += 5) {
        board_hal_set_backlight_percent(percent);
        vTaskDelay(pdMS_TO_TICKS(kBootBacklightRampStepMs));
    }

    ESP_LOGI(TAG, "LVGL ready: touch + encoder smoke screen active");

    uint32_t lastDiagLogMs = millis_now();
    while (true) {
        const uint32_t waitMs = lv_timer_handler();

        if ((millis_now() - lastDiagLogMs) >= kDiagLogPeriodMs) {
            board_hal_log_diagnostics();
            lastDiagLogMs = millis_now();
        }

        const uint32_t clampedWaitMs = (waitMs < 5U) ? 5U : ((waitMs > 30U) ? 30U : waitMs);
        vTaskDelay(pdMS_TO_TICKS(clampedWaitMs));
    }
}
