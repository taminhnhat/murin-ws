#include "diff_drive.h"

#include <math.h>
#include <stddef.h>

#include "config.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "diff_drive";
#define MOTOR_PWM_TIMER LEDC_TIMER_0
#define MOTOR_PWM_MODE LEDC_LOW_SPEED_MODE
#define MOTOR_PWM_RESOLUTION LEDC_TIMER_10_BIT
#define MOTOR_PWM_MAX ((1U << 10) - 1U)

typedef struct {
  ledc_channel_t channel;
  int gpio;
} motor_pwm_t;

static const motor_pwm_t pwm_outputs[] = {
    {LEDC_CHANNEL_0, MOT_PWM_1},
    {LEDC_CHANNEL_1, MOT_PWM_2},
    {LEDC_CHANNEL_2, MOT_PWM_3},
    {LEDC_CHANNEL_3, MOT_PWM_4},
};

static bool initialized;
static TimerHandle_t command_timeout_timer;
static float applied_left_mps, applied_right_mps;
static float target_left_mps, target_right_mps;
static uint32_t last_command_time_ms;
static diff_drive_state_t telemetry;
static motor_monitor_fn_t motor_monitor;
static void motor_timeout_callback(TimerHandle_t timer);

#define CHECK(call, name)                                                                                              \
  do {                                                                                                                 \
    const esp_err_t err = (call);                                                                                      \
    if (err != ESP_OK) {                                                                                               \
      ESP_LOGE(TAG, "%s failed: %s", name, esp_err_to_name(err));                                                      \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

static uint32_t monotonic_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static bool valid_velocity(float left, float right)
{
  return isfinite(left) && isfinite(right) && fabsf(left) <= DIFF_DRIVE_MAX_SPEED_MPS &&
         fabsf(right) <= DIFF_DRIVE_MAX_SPEED_MPS;
}

static uint32_t command_to_duty(float command)
{
  const float magnitude = fminf(fabsf(command), DIFF_DRIVE_MAX_SPEED_MPS);
  return (uint32_t)((magnitude * MOTOR_PWM_MAX / DIFF_DRIVE_MAX_SPEED_MPS) + 0.5f);
}

static void publish_telemetry(void)
{
  telemetry.target_left_mps = target_left_mps;
  telemetry.target_right_mps = target_right_mps;
  telemetry.measured_left_mps = applied_left_mps;
  telemetry.measured_right_mps = applied_right_mps;
  telemetry.last_command_time_ms = last_command_time_ms;
  if (motor_monitor != NULL)
    motor_monitor(&telemetry);
}

/* Hardware layer only: no watchdog, arming, or command policy belongs here. */
static void motor_output(float left, float right)
{
  const uint32_t ld = command_to_duty(left), rd = command_to_duty(right);
  CHECK(ledc_set_duty(MOTOR_PWM_MODE, pwm_outputs[0].channel, ld), "set left 1");
  CHECK(ledc_set_duty(MOTOR_PWM_MODE, pwm_outputs[1].channel, ld), "set left 2");
  CHECK(ledc_set_duty(MOTOR_PWM_MODE, pwm_outputs[2].channel, rd), "set right 1");
  CHECK(ledc_set_duty(MOTOR_PWM_MODE, pwm_outputs[3].channel, rd), "set right 2");
  CHECK(gpio_set_level(MOT_BRAKE_1, ld == 0 ? 1 : 0), "brake left");
  CHECK(gpio_set_level(MOT_BRAKE_2, rd == 0 ? 1 : 0), "brake right");
  /* Keep direction pins forward while stopped. The brakes are asserted
   * first, so changing direction at zero duty cannot command motion. */
  CHECK(gpio_set_level(MOT_DIR_1, ld == 0 ? 1 : (left < 0 ? 0 : 1)), "direction left");
  CHECK(gpio_set_level(MOT_DIR_2, rd == 0 ? 1 : (right < 0 ? 0 : 1)), "direction right");
  CHECK(ledc_update_duty(MOTOR_PWM_MODE, pwm_outputs[0].channel), "update left 1");
  CHECK(ledc_update_duty(MOTOR_PWM_MODE, pwm_outputs[1].channel), "update left 2");
  CHECK(ledc_update_duty(MOTOR_PWM_MODE, pwm_outputs[2].channel), "update right 1");
  CHECK(ledc_update_duty(MOTOR_PWM_MODE, pwm_outputs[3].channel), "update right 2");
  applied_left_mps = left;
  applied_right_mps = right;
  telemetry.pwm_percent[0] = telemetry.pwm_percent[1] = (uint8_t)((ld * 100U + MOTOR_PWM_MAX / 2U) / MOTOR_PWM_MAX);
  telemetry.pwm_percent[2] = telemetry.pwm_percent[3] = (uint8_t)((rd * 100U + MOTOR_PWM_MAX / 2U) / MOTOR_PWM_MAX);
  telemetry.brake[0] = ld == 0;
  telemetry.brake[1] = rd == 0;
  telemetry.direction[0] = left >= 0;
  telemetry.direction[1] = right >= 0;
}

static void motor_timeout_callback(TimerHandle_t timer)
{
  (void)timer;
  target_left_mps = 0;
  target_right_mps = 0;
  telemetry.safety_state = DRIVE_IDLE;
  telemetry.stop_reason = DRIVE_STOP_TIMEOUT;
  motor_output(0, 0);
  publish_telemetry();
}

void motor_init(void)
{
  if (initialized)
    return;
  const ledc_timer_config_t tc = {.speed_mode = MOTOR_PWM_MODE,
                                  .duty_resolution = MOTOR_PWM_RESOLUTION,
                                  .timer_num = MOTOR_PWM_TIMER,
                                  .freq_hz = BLDC_PWM_FREQ_HZ,
                                  .clk_cfg = LEDC_AUTO_CLK};
  CHECK(ledc_timer_config(&tc), "timer config");
  for (size_t i = 0; i < sizeof(pwm_outputs) / sizeof(pwm_outputs[0]); ++i) {
    const ledc_channel_config_t cc = {.gpio_num = pwm_outputs[i].gpio,
                                      .speed_mode = MOTOR_PWM_MODE,
                                      .channel = pwm_outputs[i].channel,
                                      .intr_type = LEDC_INTR_DISABLE,
                                      .timer_sel = MOTOR_PWM_TIMER,
                                      .duty = 0,
                                      .hpoint = 0,
                                      .flags.output_invert = 0};
    CHECK(ledc_channel_config(&cc), "channel config");
  }
  const gpio_config_t gc = {.pin_bit_mask = (1ULL << MOT_DIR_1) | (1ULL << MOT_DIR_2) | (1ULL << MOT_BRAKE_1) |
                                            (1ULL << MOT_BRAKE_2),
                            .mode = GPIO_MODE_OUTPUT,
                            .pull_up_en = GPIO_PULLUP_DISABLE,
                            .pull_down_en = GPIO_PULLDOWN_DISABLE,
                            .intr_type = GPIO_INTR_DISABLE};
  CHECK(gpio_config(&gc), "gpio config");
  CHECK(gpio_set_level(MOT_BRAKE_1, 1), "initial brake left");
  CHECK(gpio_set_level(MOT_BRAKE_2, 1), "initial brake right");
  CHECK(gpio_set_level(MOT_DIR_1, 1), "initial direction left");
  CHECK(gpio_set_level(MOT_DIR_2, 1), "initial direction right");
  initialized = true;
  command_timeout_timer = xTimerCreate("motor_timeout", pdMS_TO_TICKS(DIFF_DRIVE_COMMAND_TIMEOUT_MS), pdFALSE, NULL,
                                       motor_timeout_callback);
  if (command_timeout_timer == NULL)
    ESP_LOGE(TAG, "Unable to create motor command timeout timer");
  motor_output(0, 0);
  telemetry.safety_state = DRIVE_IDLE;
  telemetry.stop_reason = DRIVE_STOP_NONE;
  publish_telemetry();
}

bool motor_set(float left, float right)
{
  if (!initialized) {
    ESP_LOGE(TAG, "Motor command rejected: motor is not initialized");
    return false;
  }
  if (!valid_velocity(left, right)) {
    telemetry.stop_reason = DRIVE_STOP_INVALID_COMMAND;
    publish_telemetry();
    return false;
  }
  last_command_time_ms = monotonic_ms();
  target_left_mps = left;
  target_right_mps = right;
  telemetry.safety_state = (left == 0 && right == 0) ? DRIVE_IDLE : DRIVE_ACTIVE;
  telemetry.stop_reason = (left == 0 && right == 0) ? DRIVE_STOP_COMMAND : DRIVE_STOP_NONE;
  motor_output(left, right);
  if (command_timeout_timer != NULL) {
    if (left == 0 && right == 0)
      (void)xTimerStop(command_timeout_timer, 0);
    else
      (void)xTimerReset(command_timeout_timer, 0);
  }
  publish_telemetry();
  return true;
}

void motor_get(float *left, float *right)
{
  if (left)
    *left = applied_left_mps;
  if (right)
    *right = applied_right_mps;
}

void motor_set_monitor_callback(motor_monitor_fn_t monitor) { motor_monitor = monitor; }

#ifdef UNIT_TEST
void motor_test_reset(void)
{
  initialized = false;
  command_timeout_timer = NULL;
  applied_left_mps = 0;
  applied_right_mps = 0;
  target_left_mps = 0;
  target_right_mps = 0;
  last_command_time_ms = 0;
  telemetry = (diff_drive_state_t){0};
  motor_monitor = NULL;
}
#endif
