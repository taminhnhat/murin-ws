#pragma once

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;

BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack_depth, void *arg, UBaseType_t priority,
                       TaskHandle_t *task_handle);
void vTaskDelay(TickType_t ticks);