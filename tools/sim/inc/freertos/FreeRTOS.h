#pragma once
// nel simulatore niente FreeRTOS: i task non partono
#include <stdint.h>
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
