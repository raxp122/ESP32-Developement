# Driver ES7210 da espressif/esp-bsp (Apache-2.0), adattato al nuovo driver i2c_master
idf_component_register(SRCS "es7210.c" INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "priv_include" REQUIRES esp_driver_i2c)
