// sd.h — microSD (SDMMC a 1 bit) montata su /sdcard
#pragma once
#include <stdbool.h>
#define SD_MOUNT "/sdcard"
bool sd_mount(void);
bool sd_ok(void);
float sd_size_gb(void);
