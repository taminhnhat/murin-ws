#pragma once

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef unsigned int TickType_t;
typedef unsigned int portMUX_TYPE;

#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(milliseconds) (milliseconds)
#define portMAX_DELAY ((TickType_t)~0u)
#define portMUX_INITIALIZER_UNLOCKED 0

#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock) ((void)(lock))
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))