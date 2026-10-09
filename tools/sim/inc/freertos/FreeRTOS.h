#pragma once
// nel simulatore niente FreeRTOS: i task non partono
#include <stdint.h>
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
