#pragma once
#include "freertos/FreeRTOS.h"
// i task partono davvero (thread) solo se una scena lo chiede con sim_tasks = 1
// (microfono finto per Accordatore, Berciometro, Theremin…); altrimenti non partono
extern int sim_tasks;
int xTaskCreatePinnedToCore(void (*f)(void *), const char *n, uint32_t s, void *a, int p, TaskHandle_t *h, int c);
void vTaskDelay(TickType_t t);
void vTaskDelete(TaskHandle_t h);
