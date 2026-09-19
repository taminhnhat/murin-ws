/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bldc_control.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "bldc_control";

typedef struct {
  int pwm_gpio;
  ledc_channel_t pwm_channel;
  int enc_gpio_a;
  int enc_gpio_b;
} bldc_encoder_config_t;

static const bldc_encoder_config_t encoder_configs[NUM_MOTORS] = {
    {
        .pwm_gpio = BLDC_PWM_GPIO_1,
        .pwm_channel = LEDC_CHANNEL_0,
        .enc_gpio_a = BDC_ENCODER_GPIO_A_1,
        .enc_gpio_b = BDC_ENCODER_GPIO_B_1,
    },
    {
        .pwm_gpio = BLDC_PWM_GPIO_2,
        .pwm_channel = LEDC_CHANNEL_1,
        .enc_gpio_a = BDC_ENCODER_GPIO_A_2,
        .enc_gpio_b = BDC_ENCODER_GPIO_B_2,
    },
    {
        .pwm_gpio = BLDC_PWM_GPIO_3,
        .pwm_channel = LEDC_CHANNEL_2,
        .enc_gpio_a = BDC_ENCODER_GPIO_A_3,
        .enc_gpio_b = BDC_ENCODER_GPIO_B_3,
    },
    {
        .pwm_gpio = BLDC_PWM_GPIO_4,
        .pwm_channel = LEDC_CHANNEL_3,
        .enc_gpio_a = BDC_ENCODER_GPIO_A_4,
        .enc_gpio_b = BDC_ENCODER_GPIO_B_4,
    },
};

static esp_err_t bldc_pwm_init(int wheel_index, const bldc_encoder_config_t *config)
{
  ESP_LOGI(TAG, "Init BLDC wheel %d PWM GPIO=%d channel=%d", wheel_index + 1, config->pwm_gpio, config->pwm_channel);

  ledc_channel_config_t channel_config = {
      .gpio_num = config->pwm_gpio,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = config->pwm_channel,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LEDC_TIMER_0,
      .duty = 0,
      .hpoint = 0,
      .flags.output_invert = 0,
  };

  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "ledc_channel_config failed");
  return ESP_OK;
}

static esp_err_t bldc_encoder_init(int wheel_index, const bldc_encoder_config_t *config, pcnt_unit_handle_t *out_unit)
{
  ESP_LOGI(TAG, "Init BLDC wheel %d encoder A=%d B=%d", wheel_index + 1, config->enc_gpio_a, config->enc_gpio_b);

  pcnt_unit_config_t unit_config = {
      .high_limit = BDC_ENCODER_PCNT_HIGH_LIMIT,
      .low_limit = BDC_ENCODER_PCNT_LOW_LIMIT,
      .flags.accum_count = true,
  };
  pcnt_unit_handle_t pcnt_unit = NULL;
  ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_config, &pcnt_unit), TAG, "pcnt_new_unit failed");

  pcnt_glitch_filter_config_t filter_config = {
      .max_glitch_ns = BLDC_ENCODER_GLITCH_FILTER_NS,
  };
  ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config), TAG,
                      "pcnt_unit_set_glitch_filter failed");

  pcnt_chan_config_t chan_a_config = {
      .edge_gpio_num = config->enc_gpio_a,
      .level_gpio_num = config->enc_gpio_b,
  };
  pcnt_channel_handle_t pcnt_chan_a = NULL;
  ESP_RETURN_ON_ERROR(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a), TAG, "pcnt_new_channel A failed");

  pcnt_chan_config_t chan_b_config = {
      .edge_gpio_num = config->enc_gpio_b,
      .level_gpio_num = config->enc_gpio_a,
  };
  pcnt_channel_handle_t pcnt_chan_b = NULL;
  ESP_RETURN_ON_ERROR(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b), TAG, "pcnt_new_channel B failed");

  ESP_RETURN_ON_ERROR(
      pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE),
      TAG, "pcnt_channel_set_edge_action A failed");
  ESP_RETURN_ON_ERROR(
      pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
      TAG, "pcnt_channel_set_level_action A failed");
  ESP_RETURN_ON_ERROR(
      pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE),
      TAG, "pcnt_channel_set_edge_action B failed");
  ESP_RETURN_ON_ERROR(
      pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
      TAG, "pcnt_channel_set_level_action B failed");

  ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(pcnt_unit, BDC_ENCODER_PCNT_HIGH_LIMIT), TAG, "pcnt high watch failed");
  ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(pcnt_unit, BDC_ENCODER_PCNT_LOW_LIMIT), TAG, "pcnt low watch failed");
  ESP_RETURN_ON_ERROR(pcnt_unit_enable(pcnt_unit), TAG, "pcnt_unit_enable failed");
  ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(pcnt_unit), TAG, "pcnt_unit_clear_count failed");
  ESP_RETURN_ON_ERROR(pcnt_unit_start(pcnt_unit), TAG, "pcnt_unit_start failed");

  *out_unit = pcnt_unit;
  return ESP_OK;
}

esp_err_t bldc_control_init(bldc_control_system_t *system)
{
  if (system == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(system, 0, sizeof(*system));

  ledc_timer_config_t timer_config = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_10_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = BLDC_PWM_FREQ_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "ledc_timer_config failed");

  for (int i = 0; i < NUM_MOTORS; i++) {
    ESP_RETURN_ON_ERROR(bldc_pwm_init(i, &encoder_configs[i]), TAG, "bldc pwm init failed");
    system->pwm_channels[i] = encoder_configs[i].pwm_channel;
    system->pwm_duties[i] = 0;

    ESP_RETURN_ON_ERROR(bldc_encoder_init(i, &encoder_configs[i], &system->encoders[i]), TAG,
                        "bldc encoder init failed");
  }

  system->initialized = true;
  return bldc_control_update(system);
}

esp_err_t bldc_control_set_pwm_duty(bldc_control_system_t *system, int motor_index, uint32_t duty)
{
  if (system == NULL || !system->initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (motor_index < 0 || motor_index >= NUM_MOTORS) {
    return ESP_ERR_INVALID_ARG;
  }

  if (duty > BLDC_PWM_DUTY_MAX) {
    duty = BLDC_PWM_DUTY_MAX;
  }

  const ledc_channel_t channel = system->pwm_channels[motor_index];
  ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty), TAG, "ledc_set_duty failed");
  ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel), TAG, "ledc_update_duty failed");

  system->pwm_duties[motor_index] = duty;
  return ESP_OK;
}

esp_err_t bldc_control_set_pwm_percent(bldc_control_system_t *system, int motor_index, float duty_percent)
{
  if (!isfinite(duty_percent)) {
    return ESP_ERR_INVALID_ARG;
  }

  if (duty_percent < 0.0f) {
    duty_percent = 0.0f;
  } else if (duty_percent > 100.0f) {
    duty_percent = 100.0f;
  }

  const uint32_t duty = (uint32_t)((duty_percent * (float)BLDC_PWM_DUTY_MAX / 100.0f) + 0.5f);
  return bldc_control_set_pwm_duty(system, motor_index, duty);
}

esp_err_t bldc_control_set_drive(bldc_control_system_t *system, int16_t left, int16_t right)
{
  // const int32_t left_duty = (left < 0) ? -(int32_t)left : (int32_t)left;
  // const int32_t right_duty = (right < 0) ? -(int32_t)right : (int32_t)right;

  // ESP_RETURN_ON_ERROR(bldc_control_set_pwm_percent(system, 0, left_percent),
  // TAG, "set motor 1 PWM failed");
  // ESP_RETURN_ON_ERROR(bldc_control_set_pwm_percent(system, 1, right_percent),
  // TAG, "set motor 2 PWM failed");
  // ESP_RETURN_ON_ERROR(bldc_control_set_pwm_percent(system, 2, left_percent),
  // TAG, "set motor 3 PWM failed");
  // ESP_RETURN_ON_ERROR(bldc_control_set_pwm_percent(system, 3, right_percent),
  // TAG, "set motor 4 PWM failed");

  ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, system->pwm_channels[0], left), TAG, "ledc_set_duty failed");
  ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, system->pwm_channels[1], left), TAG, "ledc_set_duty failed");
  ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, system->pwm_channels[2], right), TAG, "ledc_set_duty failed");
  ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, system->pwm_channels[3], right), TAG, "ledc_set_duty failed");
  ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, system->pwm_channels[0]), TAG, "ledc_update_duty failed");
  ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, system->pwm_channels[1]), TAG, "ledc_update_duty failed");
  ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, system->pwm_channels[2]), TAG, "ledc_update_duty failed");
  ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, system->pwm_channels[3]), TAG, "ledc_update_duty failed");

  // system->pwm_duties[i] = duty;

  return ESP_OK;
}

esp_err_t bldc_control_update(bldc_control_system_t *system)
{
  if (system == NULL || !system->initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  const uint32_t elapsed_ms = (system->last_update_ms == 0) ? 0 : (now_ms - system->last_update_ms);
  const float elapsed_s = (elapsed_ms > 0) ? ((float)elapsed_ms / 1000.0f) : 0.0f;

  system->snapshot.timestamp_ms = now_ms;
  system->snapshot.update_count++;

  for (int i = 0; i < NUM_MOTORS; i++) {
    int cur_count = 0;
    ESP_RETURN_ON_ERROR(pcnt_unit_get_count(system->encoders[i], &cur_count), TAG, "pcnt_unit_get_count failed");

    const int32_t total_count = (int32_t)cur_count;
    const int32_t delta_count = total_count - system->last_counts[i];
    system->snapshot.total_counts[i] = total_count;
    system->snapshot.delta_counts[i] = delta_count;
    system->snapshot.velocity_rps[i] =
        (elapsed_s > 0.0f) ? ((float)delta_count / WHEEL_ENCODER_COUNTS_PER_REV) / elapsed_s : 0.0f;
    system->snapshot.velocity_rpm[i] = system->snapshot.velocity_rps[i] * 60.0f;
    system->last_counts[i] = total_count;
  }

  system->last_update_ms = now_ms;
  system->snapshot.valid = true;
  return ESP_OK;
}

void bldc_control_get_snapshot(const bldc_control_system_t *system, bldc_encoder_snapshot_t *snapshot)
{
  if (snapshot == NULL) {
    return;
  }

  if (system == NULL || !system->initialized) {
    memset(snapshot, 0, sizeof(*snapshot));
    return;
  }

  *snapshot = system->snapshot;
}

void bldc_encoder_loop_cb(void *args)
{
  bldc_control_system_t *system = (bldc_control_system_t *)args;
  const esp_err_t ret = bldc_control_update(system);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "BLDC encoder update failed: %s", esp_err_to_name(ret));
  }
}
