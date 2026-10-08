#pragma once
#include "freertos/FreeRTOS.h"
static inline int xTaskCreatePinnedToCore(void (*f)(void *), const char *n, uint32_t s, void *a, int p, TaskHandle_t *h, int c) { return 1; }
static inline void vTaskDelay(TickType_t t) {}
