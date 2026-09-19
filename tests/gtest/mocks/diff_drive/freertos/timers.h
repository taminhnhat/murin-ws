#pragma once

#include <stdint.h>

typedef struct host_timer *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t timer);

TimerHandle_t xTimerCreate(const char *name, uint32_t period, int auto_reload, void *id,
                           TimerCallbackFunction_t callback);
int xTimerReset(TimerHandle_t timer, uint32_t ticks_to_wait);
int xTimerStop(TimerHandle_t timer, uint32_t ticks_to_wait);
