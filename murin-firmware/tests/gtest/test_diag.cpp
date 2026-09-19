#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include "diag.h"
#include "diag_host_stubs.h"
#include "system_log.h"
}

namespace {

TEST(DiagTest, InitializesStorageAndRecordsAllLogTypes)
{
  diag_host_reset();
  diag_host_state.time_us = 100;
  diag_host_state.task_create_fails = true;
  diag_init();
  diag_host_set_return_previous_log_callback(true);
  system_log_init();

  EXPECT_EQ(diag_host_state.allocation_calls, 4u);
  EXPECT_TRUE(diag_host_state.battery_task_registered);

  diag_system_log_t system_record{};
  diag_log_system("system ready");
  ASSERT_EQ(diag_get_system_logs(&system_record, 1), 1u);
  EXPECT_EQ(system_record.timestamp_us, 100);
  EXPECT_STREQ(system_record.message, "system ready");

  diag_host_state.time_us = 150;
  EXPECT_EQ(diag_host_emit_log("battery=%d", 42), 123);
  EXPECT_EQ(diag_host_state.forwarded_log_calls, 1u);
  ASSERT_EQ(diag_get_system_logs(&system_record, 1), 1u);
  EXPECT_EQ(system_record.timestamp_us, 150);
  EXPECT_STREQ(system_record.message, "battery=42");

  EXPECT_EQ(diag_host_emit_log("forwarded"), 123);
  EXPECT_EQ(diag_host_state.forwarded_log_calls, 2u);

  rp3_signal_sample_t rp3{};
  rp3.timestamp_us = 200;
  rp3.uplink_link_quality = 77;
  diag_log_rp3(&rp3);
  rp3_signal_sample_t rp3_records[2]{};
  ASSERT_EQ(diag_get_rp3_logs(rp3_records, 2), 1u);
  EXPECT_EQ(rp3_records[0].timestamp_us, 200);
  EXPECT_EQ(rp3_records[0].uplink_link_quality, 77);

  const uint8_t payload[] = {1, 2, 3};
  diag_host_state.time_us = 300;
  diag_log_ros2(0x42, 9, payload, sizeof(payload));
  ros2_diag_message_t ros2_record{};
  ASSERT_EQ(diag_get_ros2_logs(&ros2_record, 1), 1u);
  EXPECT_EQ(ros2_record.timestamp_us, 300);
  EXPECT_EQ(ros2_record.msg_type, 0x42);
  EXPECT_EQ(ros2_record.seq, 9);
  EXPECT_EQ(ros2_record.payload_len, sizeof(payload));
  EXPECT_EQ(std::memcmp(ros2_record.payload, payload, sizeof(payload)), 0);
}

TEST(DiagTest, HandlesInvalidQueriesAndLogArguments)
{
  diag_system_log_t system_record{};
  EXPECT_EQ(diag_get_system_logs(nullptr, 1), 0u);
  EXPECT_EQ(diag_get_system_logs(&system_record, 0), 0u);
  EXPECT_EQ(diag_get_rp3_logs(nullptr, 1), 0u);
  EXPECT_EQ(diag_get_battery_logs(nullptr, 1), 0u);
  EXPECT_EQ(diag_get_ros2_logs(nullptr, 1), 0u);

  diag_log_system(nullptr);
  diag_log_rp3(nullptr);
  diag_log_ros2(1, 2, nullptr, 1);
  ASSERT_EQ(diag_get_system_logs(&system_record, 1), 1u);
  EXPECT_STREQ(system_record.message, "forwarded");
}

TEST(DiagTest, TruncatesOversizedRos2Payload)
{
  uint8_t payload[FRAMED_LINK_MAX_PAYLOAD_LEN + 8]{};
  payload[FRAMED_LINK_MAX_PAYLOAD_LEN - 1] = 0xA5;
  diag_log_ros2(3, 4, payload, sizeof(payload));

  ros2_diag_message_t record{};
  ASSERT_EQ(diag_get_ros2_logs(&record, 1), 1u);
  EXPECT_EQ(record.payload_len, FRAMED_LINK_MAX_PAYLOAD_LEN);
  EXPECT_EQ(record.payload[FRAMED_LINK_MAX_PAYLOAD_LEN - 1], 0xA5);
}

TEST(DiagTest, RecordsBatteryTaskResult)
{
  diag_host_state.time_us = 400;
  diag_host_state.battery_result = 0;
  diag_host_state.battery_data.valid = true;
  diag_host_state.battery_data.voltage = 12.5f;
  diag_host_state.battery_data.power = 4.0f;
  diag_host_run_battery_task_once();

  battery_diag_record_t record{};
  ASSERT_EQ(diag_get_battery_logs(&record, 1), 1u);
  EXPECT_EQ(record.timestamp_us, 400);
  EXPECT_EQ(record.status, 0);
  EXPECT_TRUE(record.data.valid);
  EXPECT_FLOAT_EQ(record.data.voltage, 12.5f);
  EXPECT_FLOAT_EQ(record.data.power, 4.0f);
}

TEST(DiagTest, SaturatesAndWrapsEveryLogRing)
{
  for (int index = 0; index <= 1024; index++)
    diag_log_system("ring");
  static diag_system_log_t system_records[1024];
  std::memset(system_records, 0, sizeof(system_records));
  EXPECT_EQ(diag_get_system_logs(system_records, 1024), 1024u);

  for (int index = 0; index <= 4096; index++) {
    rp3_signal_sample_t sample{};
    sample.timestamp_us = index;
    diag_log_rp3(&sample);
  }
  static rp3_signal_sample_t rp3_records[4096];
  std::memset(rp3_records, 0, sizeof(rp3_records));
  EXPECT_EQ(diag_get_rp3_logs(rp3_records, 4096), 4096u);
  EXPECT_EQ(rp3_records[0].timestamp_us, 1);
  EXPECT_EQ(rp3_records[4095].timestamp_us, 4096);

  const uint8_t payload = 0x5A;
  for (int index = 0; index <= 4096; index++)
    diag_log_ros2(1, static_cast<uint8_t>(index), &payload, 1);
  static ros2_diag_message_t ros2_records[4096];
  std::memset(ros2_records, 0, sizeof(ros2_records));
  EXPECT_EQ(diag_get_ros2_logs(ros2_records, 4096), 4096u);

  for (int index = 0; index <= 4096; index++)
    diag_host_run_battery_task_once();
  static battery_diag_record_t battery_records[4096];
  std::memset(battery_records, 0, sizeof(battery_records));
  EXPECT_EQ(diag_get_battery_logs(battery_records, 4096), 4096u);
}

TEST(DiagTest, CleansUpAllocationsWhenInitializationFails)
{
  diag_host_reset();
  diag_host_state.fail_allocation_call = 1;
  diag_init();
  EXPECT_EQ(diag_host_state.free_calls, 0u);

  diag_host_reset();
  diag_host_state.fail_allocation_call = 2;
  diag_init();
  EXPECT_EQ(diag_host_state.free_calls, 1u);

  diag_host_reset();
  diag_host_state.fail_allocation_call = 3;
  diag_init();
  EXPECT_EQ(diag_host_state.free_calls, 2u);

  diag_host_reset();
  diag_host_state.fail_allocation_call = 4;
  diag_init();
  EXPECT_EQ(diag_host_state.free_calls, 3u);
}

} // namespace
