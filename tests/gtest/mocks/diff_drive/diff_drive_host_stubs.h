#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t duty[4];
  uint32_t gpio_level[64];
  unsigned int timer_config_count;
  unsigned int channel_config_count;
  unsigned int duty_update_count;
  unsigned int timer_reset_count;
  unsigned int timer_stop_count;
  uint32_t timer_period;
} diff_drive_host_state_t;

extern diff_drive_host_state_t diff_drive_host_state;
void diff_drive_host_reset(void);
void diff_drive_host_set_time_ms(uint32_t time_ms);
void diff_drive_host_fire_timer(void);
void diff_drive_host_fail_hardware_call(unsigned int call_number);
void diff_drive_host_fail_timer_create(int fail);

#ifdef __cplusplus
}
#endif
