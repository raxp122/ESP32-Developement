file(GLOB APP_SRCS "*.c" "apps/*.c" "fonts/*.c" "doom/*.c")

# Tastiera USB (Appunti): c'è solo se il componente esp_tinyusb è disponibile. In locale
# di solito non c'è (il registro dei componenti è irraggiungibile) e usbhid.c resta un
# guscio; la CI lo scarica e compila la tastiera vera. Vedi .github/workflows/build.yml.
set(USB_REQ "")
if(EXISTS "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_tinyusb" OR
   EXISTS "${CMAKE_SOURCE_DIR}/components/esp_tinyusb")
    set(USB_REQ esp_tinyusb)
endif()

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS "." "apps" "doom"
    REQUIRES lvgl axs15231b esp_lcd driver esp_adc esp_wifi esp_netif esp_event
             nvs_flash esp_http_server esp_timer bt lwip esp_driver_usb_serial_jtag fatfs sdmmc esp_driver_sdmmc prboom es8311 es7210 esp_driver_i2s
             app_update esp_app_format esp_https_ota esp_http_client mbedtls ${USB_REQ})

if(USB_REQ)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_USBHID=1)
endif()
