file(GLOB APP_SRCS "*.c" "apps/*.c" "fonts/*.c" "doom/*.c")
idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS "." "apps" "doom"
    REQUIRES lvgl axs15231b esp_lcd driver esp_adc esp_wifi esp_netif esp_event
             nvs_flash esp_http_server esp_timer bt lwip esp_driver_usb_serial_jtag fatfs sdmmc esp_driver_sdmmc prboom es8311 es7210 esp_driver_i2s
             app_update esp_app_format esp_https_ota esp_http_client mbedtls)
