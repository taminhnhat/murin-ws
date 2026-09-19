#include "diff_drive_host_stubs.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/timers.h"

struct host_timer {
  TimerCallbackFunction_t callback;
};

diff_drive_host_state_t diff_drive_host_state;
static int64_t host_time_us;
static struct host_timer command_timer;
static unsigned int hardware_call_count;
static unsigned int failing_hardware_call;
static int fail_timer_create;

static esp_err_t hardware_result(void)
{
  hardware_call_count++;
  return hardware_call_count == failing_hardware_call ? ESP_FAIL : ESP_OK;
}

void diff_drive_host_reset(void)
{
  memset(&diff_drive_host_state, 0, sizeof(diff_drive_host_state));
  host_time_us = 0;
  command_timer.callback = NULL;
  hardware_call_count = 0;
  failing_hardware_call = 0;
  fail_timer_create = 0;
}

void diff_drive_host_set_time_ms(uint32_t time_ms) { host_time_us = (int64_t)time_ms * 1000; }

void diff_drive_host_fire_timer(void)
{
  if (command_timer.callback != NULL)
    command_timer.callback(&command_timer);
}

void diff_drive_host_fail_hardware_call(unsigned int call_number)
{
  hardware_call_count = 0;
  failing_hardware_call = call_number;
}

void diff_drive_host_fail_timer_create(int fail) { fail_timer_create = fail; }

int64_t esp_timer_get_time(void) { return host_time_us; }

esp_err_t ledc_timer_config(const ledc_timer_config_t *config)
{
  (void)config;
  diff_drive_host_state.timer_config_count++;
  return hardware_result();
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *config)
{
  (void)config;
  diff_drive_host_state.channel_config_count++;
  return hardware_result();
}

esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty)
{
  (void)speed_mode;
  diff_drive_host_state.duty[channel] = duty;
  return hardware_result();
}

esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel)
{
  (void)speed_mode;
  (void)channel;
  diff_drive_host_state.duty_update_count++;
  return hardware_result();
}

esp_err_t gpio_config(const gpio_config_t *config)
{
  (void)config;
  return hardware_result();
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
  diff_drive_host_state.gpio_level[gpio_num] = level;
  return hardware_result();
}

TimerHandle_t xTimerCreate(const char *name, uint32_t period, int auto_reload, void *id,
                           TimerCallbackFunction_t callback)
{
  (void)name;
  (void)auto_reload;
  (void)id;
  diff_drive_host_state.timer_period = period;
  command_timer.callback = callback;
  return fail_timer_create ? NULL : &command_timer;
}

int xTimerReset(TimerHandle_t timer, uint32_t ticks_to_wait)
{
  (void)timer;
  (void)ticks_to_wait;
  diff_drive_host_state.timer_reset_count++;
  return 1;
}

int xTimerStop(TimerHandle_t timer, uint32_t ticks_to_wait)
{
  (void)timer;
  (void)ticks_to_wait;
  diff_drive_host_state.timer_stop_count++;
  return 1;
}
