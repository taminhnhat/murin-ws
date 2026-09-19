#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_LOG_MESSAGE_MAX 192

typedef struct {
  int64_t timestamp_us;
  char message[SYSTEM_LOG_MESSAGE_MAX];
} system_log_record_t;

typedef void (*system_log_monitor_fn_t)(const system_log_record_t *record);

void system_log_init(void);
void system_log_set_monitor(system_log_monitor_fn_t monitor);

#ifdef __cplusplus
}
#endif
