idf_component_register(
    SRCS "axs15231b/esp_lcd_axs15231b.c" "touch/esp_lcd_touch.c"
    INCLUDE_DIRS "axs15231b" "touch"
    REQUIRES esp_lcd driver)
