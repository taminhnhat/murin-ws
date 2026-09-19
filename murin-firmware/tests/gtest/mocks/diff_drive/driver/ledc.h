#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef int ledc_mode_t;
typedef int ledc_channel_t;

#define LEDC_LOW_SPEED_MODE 0
#define LEDC_CHANNEL_0 0
#define LEDC_CHANNEL_1 1
#define LEDC_CHANNEL_2 2
#define LEDC_CHANNEL_3 3
#define LEDC_TIMER_0 0
#define LEDC_TIMER_10_BIT 10
#define LEDC_AUTO_CLK 0
#define LEDC_INTR_DISABLE 0

typedef struct {
  ledc_mode_t speed_mode;
  int duty_resolution;
  int timer_num;
  uint32_t freq_hz;
  int clk_cfg;
} ledc_timer_config_t;

typedef struct {
  int gpio_num;
  ledc_mode_t speed_mode;
  ledc_channel_t channel;
  int intr_type;
  int timer_sel;
  uint32_t duty;
  int hpoint;

  struct {
    unsigned int output_invert : 1;
  } flags;
} ledc_channel_config_t;

esp_err_t ledc_timer_config(const ledc_timer_config_t *config);
esp_err_t ledc_channel_config(const ledc_channel_config_t *config);
esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty);
esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel);
