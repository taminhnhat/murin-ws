#pragma once

#include <stdarg.h>

typedef int (*vprintf_like_t)(const char *format, va_list args);

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func);

#define ESP_LOGE(tag, format, ...) ((void)0)
#define ESP_LOGI(tag, format, ...) ((void)0)
#define ESP_LOGW(tag, format, ...) ((void)0)
#define ESP_LOGD(tag, format, ...) ((void)0)
