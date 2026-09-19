#include <gtest/gtest.h>

#include <cmath>
#include <limits>

extern "C" {
#include "config.h"
#include "diff_drive.h"
#include "diff_drive_host_stubs.h"
}

namespace {

diff_drive_state_t last_telemetry{};
size_t telemetry_count;

void CaptureTelemetry(const diff_drive_state_t *state)
{
  last_telemetry = *state;
  telemetry_count++;
}

TEST(DiffDriveTest, ControlsMotorsAndEnforcesCommandSafety)
{
  diff_drive_host_reset();
  motor_set_monitor_callback(CaptureTelemetry);

  EXPECT_FALSE(motor_set(0.1f, 0.1f));

  motor_init();
  EXPECT_EQ(diff_drive_host_state.timer_config_count, 1u);
  EXPECT_EQ(diff_drive_host_state.channel_config_count, 4u);
  EXPECT_EQ(diff_drive_host_state.timer_period, DIFF_DRIVE_COMMAND_TIMEOUT_MS);
  EXPECT_EQ(last_telemetry.safety_state, DRIVE_IDLE);
  EXPECT_EQ(last_telemetry.stop_reason, DRIVE_STOP_NONE);
  EXPECT_TRUE(last_telemetry.brake[0]);
  EXPECT_TRUE(last_telemetry.brake[1]);

  const unsigned int configured_channels = diff_drive_host_state.channel_config_count;
  motor_init();
  EXPECT_EQ(diff_drive_host_state.channel_config_count, configured_channels);

  diff_drive_host_set_time_ms(1234);
  ASSERT_TRUE(motor_set(0.25f, -0.5f));
  EXPECT_EQ(diff_drive_host_state.duty[0], 512u);
  EXPECT_EQ(diff_drive_host_state.duty[1], 512u);
  EXPECT_EQ(diff_drive_host_state.duty[2], 1023u);
  EXPECT_EQ(diff_drive_host_state.duty[3], 1023u);
  EXPECT_EQ(diff_drive_host_state.gpio_level[MOT_BRAKE_1], 0u);
  EXPECT_EQ(diff_drive_host_state.gpio_level[MOT_BRAKE_2], 0u);
  EXPECT_EQ(diff_drive_host_state.gpio_level[MOT_DIR_1], 1u);
  EXPECT_EQ(diff_drive_host_state.gpio_level[MOT_DIR_2], 0u);
  EXPECT_EQ(diff_drive_host_state.timer_reset_count, 1u);
  EXPECT_EQ(last_telemetry.safety_state, DRIVE_ACTIVE);
  EXPECT_EQ(last_telemetry.last_command_time_ms, 1234u);
  EXPECT_EQ(last_telemetry.pwm_percent[0], 50u);
  EXPECT_EQ(last_telemetry.pwm_percent[2], 100u);

  float left = 0, right = 0;
  motor_get(&left, &right);
  EXPECT_FLOAT_EQ(left, 0.25f);
  EXPECT_FLOAT_EQ(right, -0.5f);
  motor_get(nullptr, nullptr);

  EXPECT_FALSE(motor_set(std::numeric_limits<float>::infinity(), 0.0f));
  EXPECT_EQ(last_telemetry.stop_reason, DRIVE_STOP_INVALID_COMMAND);
  EXPECT_FALSE(motor_set(0.0f, std::numeric_limits<float>::quiet_NaN()));
  EXPECT_FALSE(motor_set(DIFF_DRIVE_MAX_SPEED_MPS + 0.1f, 0.0f));
  EXPECT_FALSE(motor_set(0.0f, DIFF_DRIVE_MAX_SPEED_MPS + 0.1f));
  motor_get(&left, &right);
  EXPECT_FLOAT_EQ(left, 0.25f);
  EXPECT_FLOAT_EQ(right, -0.5f);

  ASSERT_TRUE(motor_set(0.0f, 0.0f));
  EXPECT_EQ(diff_drive_host_state.timer_stop_count, 1u);
  EXPECT_EQ(last_telemetry.safety_state, DRIVE_IDLE);
  EXPECT_EQ(last_telemetry.stop_reason, DRIVE_STOP_COMMAND);
  EXPECT_EQ(diff_drive_host_state.gpio_level[MOT_BRAKE_1], 1u);
  EXPECT_EQ(diff_drive_host_state.gpio_level[MOT_BRAKE_2], 1u);

  ASSERT_TRUE(motor_set(-0.1f, 0.1f));
  diff_drive_host_fire_timer();
  motor_get(&left, &right);
  EXPECT_FLOAT_EQ(left, 0.0f);
  EXPECT_FLOAT_EQ(right, 0.0f);
  EXPECT_EQ(last_telemetry.safety_state, DRIVE_IDLE);
  EXPECT_EQ(last_telemetry.stop_reason, DRIVE_STOP_TIMEOUT);
  EXPECT_EQ(last_telemetry.target_left_mps, 0.0f);
  EXPECT_EQ(last_telemetry.target_right_mps, 0.0f);
  EXPECT_GT(telemetry_count, 0u);

  ASSERT_TRUE(motor_set(0.0f, 0.1f));
  motor_set_monitor_callback(nullptr);
  EXPECT_FALSE(motor_set(std::numeric_limits<float>::quiet_NaN(), 0.0f));

  // Each hardware operation in motor_output has an independent CHECK early
  // return. Inject one failure at every position to cover all of those paths.
  for (unsigned int call = 1; call <= 12; call++) {
    diff_drive_host_fail_hardware_call(call);
    EXPECT_TRUE(motor_set(0.1f, 0.1f));
  }
  diff_drive_host_fail_hardware_call(0);

  // Restart the unit under test to inject a failure into every guarded
  // initialization operation. Firmware builds do not expose this reset hook.
  for (unsigned int call = 1; call <= 10; call++) {
    motor_test_reset();
    diff_drive_host_reset();
    diff_drive_host_fail_hardware_call(call);
    motor_init();
  }

  motor_test_reset();
  diff_drive_host_reset();
  diff_drive_host_fail_timer_create(1);
  motor_init();
  EXPECT_TRUE(motor_set(0.1f, 0.1f));
  EXPECT_EQ(diff_drive_host_state.timer_reset_count, 0u);
}

} // namespace
