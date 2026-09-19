#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void uart_bridge_init(void);
void uart_bridge_task(void *pvParameters);
void uart_bridge_write(const char *data, size_t len);

#ifdef __cplusplus
}
#endif
