idf_component_register(
    SRCS
        "app_main.cpp"
        "board_hal.cpp"
        "spi_bus_init.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_timer driver esp_lcd esp_lcd_gc9a01 nvs_flash json esp_wifi esp_netif esp_event esp_http_client
)
