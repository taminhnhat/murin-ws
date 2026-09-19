#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void shell_uart_init(void);
void shell_write(const char *text);
void shell_printf(const char *format, ...);

#ifdef __cplusplus
}
#endif
