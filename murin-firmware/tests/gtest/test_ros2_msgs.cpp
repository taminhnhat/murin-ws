#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "framed_link.h"
#include "ros2_msgs.h"
#include "ros2_msgs_host_stubs.h"
}

namespace {

constexpr uint8_t kHeartbeat = 0x00;
constexpr uint8_t kMotorCommand = 0x01;
constexpr uint8_t kAck = FRAMED_LINK_MSG_ACK;
constexpr uint8_t kNack = FRAMED_LINK_MSG_NACK;
constexpr uint8_t kErrorLength = FRAMED_LINK_ERR_LEN;
constexpr uint8_t kErrorType = 0x03;

struct WireCapture {
  uint8_t bytes[1024]{};
  size_t length = 0;
};

WireCapture *g_wire_capture = nullptr;

size_t CaptureWrite(uint8_t *data, size_t length)
{
  auto *capture = g_wire_capture;
  if (capture == nullptr) {
    return 0;
  }
  if (length > sizeof(capture->bytes)) {
    return 0;
  }

  std::memcpy(capture->bytes, data, length);
  capture->length = length;
  return length;
}

WireCapture *g_ros_response = nullptr;
std::vector<WireCapture> g_transmissions;

size_t CaptureRosWrite(uint8_t *data, size_t length)
{
  g_wire_capture = g_ros_response;
  const size_t written = CaptureWrite(data, length);
  if (written != 0)
    g_transmissions.push_back(*g_ros_response);
  return written;
}

struct DecodedFrame {
  uint8_t type = 0;
  uint8_t sequence = 0;
  uint8_t payload[256]{};
  size_t payload_length = 0;
  unsigned int count = 0;
};

void CaptureFrame(void *context, uint8_t type, uint8_t sequence, const uint8_t *payload, size_t payload_length)
{
  auto *frame = static_cast<DecodedFrame *>(context);
  frame->type = type;
  frame->sequence = sequence;
  frame->payload_length = payload_length;
  std::memcpy(frame->payload, payload, payload_length);
  ++frame->count;
}

DecodedFrame Decode(const WireCapture &wire)
{
  DecodedFrame decoded;
  framed_link_t parser{};
  framed_link_init(&parser, nullptr, nullptr, CaptureFrame, &decoded);

  uint8_t bytes[sizeof(wire.bytes)];
  std::memcpy(bytes, wire.bytes, wire.length);
  framed_link_process(&parser, bytes, wire.length);
  return decoded;
}

DecodedFrame g_monitored;

void Monitor(uint8_t type, uint8_t sequence, const uint8_t *payload, size_t length)
{
  CaptureFrame(&g_monitored, type, sequence, payload, length);
}

class Ros2MsgsTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    ros2_host_reset();
    ros2_msgs_set_monitor(nullptr);
    g_ros_response = &response_;
    framed_link_init(&messages_.link, CaptureRosWrite, nullptr, nullptr, nullptr);
    messages_.write = CaptureRosWrite;
    ros2_msgs_init();
    ros2_msgs_test_set_write(CaptureRosWrite);
    ros2_msgs_set_telemetry_enabled(true);
    Configure(ROS2_CFG_TELEM_MASK, UINT32_MAX);
    Configure(ROS2_CFG_TELEM_RATE_MS, 20);
    response_ = WireCapture{};
    g_transmissions.clear();
  }

  void TearDown() override
  {
    ros2_msgs_set_monitor(nullptr);
    g_ros_response = nullptr;
  }

  DecodedFrame Configure(uint8_t key, uint32_t value)
  {
    const uint8_t payload[] = {key, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                               static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    return SendToRos(ROS2_MSG_CMD_CONFIG, 30, payload, sizeof(payload));
  }

  DecodedFrame SendToRos(uint8_t type, uint8_t sequence, const uint8_t *payload, size_t payload_length)
  {
    response_ = WireCapture{};
    WireCapture request;
    framed_link_t host_link{};
    g_wire_capture = &request;
    framed_link_init(&host_link, CaptureWrite, nullptr, nullptr, nullptr);
    framed_link_send_frame(&host_link, type, sequence, payload, payload_length);

    ros2_msgs_test_process_frame(request.bytes, request.length);
    return Decode(response_);
  }

  ros2_msgs_ctx_t messages_{};
  WireCapture response_{};
};

TEST_F(Ros2MsgsTest, SendFramePreservesFieldsAndEscapesPayload)
{
  const uint8_t payload[] = {0xAA, 0x1B, 0x00, 0x55};

  ros2_msgs_send_frame(&messages_, 0x42, 17, payload, sizeof(payload));

  const DecodedFrame decoded = Decode(response_);
  ASSERT_EQ(decoded.count, 1u);
  EXPECT_EQ(decoded.type, 0x42);
  EXPECT_EQ(decoded.sequence, 17);
  ASSERT_EQ(decoded.payload_length, sizeof(payload));
  EXPECT_EQ(std::memcmp(decoded.payload, payload, sizeof(payload)), 0);
}

TEST_F(Ros2MsgsTest, BatteryStateUsesDedicatedMessageTypeAndPayload)
{
  ros2_msgs_send_battery_state(&messages_, 18);

  const DecodedFrame decoded = Decode(response_);
  ASSERT_EQ(decoded.count, 1u);
  EXPECT_EQ(decoded.type, ROS2_MSG_TELEMETRY_BATTERY_STATE);
  EXPECT_EQ(decoded.sequence, 18);
  ASSERT_EQ(decoded.payload_length, 22u);
  EXPECT_EQ(decoded.payload[0], 1u);
  EXPECT_EQ(decoded.payload[1], 0u);

  uint32_t timestamp = 0;
  float values[4]{};
  std::memcpy(&timestamp, decoded.payload + 2, sizeof(timestamp));
  std::memcpy(values, decoded.payload + 6, sizeof(values));
  EXPECT_EQ(timestamp, 123456789u);
  EXPECT_FLOAT_EQ(values[0], 12.34f);
  EXPECT_FLOAT_EQ(values[1], 1.23f);
  EXPECT_FLOAT_EQ(values[2], 15.2f);
  EXPECT_FLOAT_EQ(values[3], 123.4f);
}

TEST_F(Ros2MsgsTest, ImuStateUsesDedicatedMessageTypeAndPayload)
{
  ros2_msgs_send_imu_state(&messages_, 18);

  const DecodedFrame decoded = Decode(response_);
  ASSERT_EQ(decoded.count, 1u);
  EXPECT_EQ(decoded.type, ROS2_MSG_TELEMETRY_IMU_STATE);
  EXPECT_EQ(decoded.sequence, 18);
  ASSERT_EQ(decoded.payload_length, ROS2_TELEMETRY_IMU_PAYLOAD_SIZE);
  EXPECT_EQ(decoded.payload[0], 1u);
  EXPECT_EQ(decoded.payload[1], 0u);

  int64_t timestamp = 0;
  float acceleration[3]{};
  float angular_velocity[3]{};
  float magnetic_field[3]{};
  float quaternion[4]{};
  std::memcpy(&timestamp, decoded.payload + 2, sizeof(timestamp));
  std::memcpy(acceleration, decoded.payload + 10, sizeof(acceleration));
  std::memcpy(angular_velocity, decoded.payload + 22, sizeof(angular_velocity));
  std::memcpy(magnetic_field, decoded.payload + 34, sizeof(magnetic_field));
  std::memcpy(quaternion, decoded.payload + 46, sizeof(quaternion));
  EXPECT_EQ(timestamp, 987654321);
  EXPECT_FLOAT_EQ(acceleration[0], 1.0f);
  EXPECT_FLOAT_EQ(acceleration[1], 2.0f);
  EXPECT_FLOAT_EQ(acceleration[2], 3.0f);
  EXPECT_FLOAT_EQ(angular_velocity[0], 4.0f);
  EXPECT_FLOAT_EQ(angular_velocity[1], 5.0f);
  EXPECT_FLOAT_EQ(angular_velocity[2], 6.0f);
  EXPECT_FLOAT_EQ(magnetic_field[0], 7.0f);
  EXPECT_FLOAT_EQ(magnetic_field[1], 8.0f);
  EXPECT_FLOAT_EQ(magnetic_field[2], 9.0f);
  EXPECT_FLOAT_EQ(quaternion[0], 0.1f);
  EXPECT_FLOAT_EQ(quaternion[1], 0.2f);
  EXPECT_FLOAT_EQ(quaternion[2], 0.3f);
  EXPECT_FLOAT_EQ(quaternion[3], 0.4f);
}

TEST_F(Ros2MsgsTest, DriveTelemetryUsesDedicatedMessageTypeAndPayload)
{
  const drive_state_t state = {1234, 1.5f, -0.25f, 0.75f, 2.0f};
  ros2_msgs_send_drive_state(&messages_, 19, &state);

  const DecodedFrame decoded = Decode(response_);
  ASSERT_EQ(decoded.count, 1u);
  EXPECT_EQ(decoded.type, ROS2_MSG_TELEMETRY_DRIVE_STATE);
  EXPECT_EQ(decoded.sequence, 19);
  ASSERT_EQ(decoded.payload_length, ROS2_TELEMETRY_DRIVE_STATE_PAYLOAD_SIZE);

  drive_state_t decoded_state{};
  std::memcpy(&decoded_state.timestamp_ms, decoded.payload, sizeof(decoded_state.timestamp_ms));
  std::memcpy(&decoded_state.linear_velocity, decoded.payload + 4, sizeof(decoded_state.linear_velocity));
  std::memcpy(&decoded_state.angular_velocity, decoded.payload + 8, sizeof(decoded_state.angular_velocity));
  std::memcpy(&decoded_state.left_velocity, decoded.payload + 12, sizeof(decoded_state.left_velocity));
  std::memcpy(&decoded_state.right_velocity, decoded.payload + 16, sizeof(decoded_state.right_velocity));
  EXPECT_EQ(decoded_state.timestamp_ms, state.timestamp_ms);
  EXPECT_FLOAT_EQ(decoded_state.linear_velocity, state.linear_velocity);
  EXPECT_FLOAT_EQ(decoded_state.angular_velocity, state.angular_velocity);
  EXPECT_FLOAT_EQ(decoded_state.left_velocity, state.left_velocity);
  EXPECT_FLOAT_EQ(decoded_state.right_velocity, state.right_velocity);
}

TEST_F(Ros2MsgsTest, HeartbeatReceivesAcknowledgement)
{
  const DecodedFrame response = SendToRos(kHeartbeat, 21, nullptr, 0);

  ASSERT_EQ(response.count, 1u);
  EXPECT_EQ(response.type, kAck);
  EXPECT_EQ(response.sequence, 21);
  ASSERT_EQ(response.payload_length, 1u);
  EXPECT_EQ(response.payload[0], 21);
}

TEST_F(Ros2MsgsTest, SetTimeUpdatesUtcAndReceivesAcknowledgement)
{
  const uint64_t unix_seconds = 1788796800ULL;
  const auto response =
      SendToRos(ROS2_MSG_SET_TIME, 56, reinterpret_cast<const uint8_t *>(&unix_seconds), sizeof(unix_seconds));

  EXPECT_EQ(response.type, kAck);
  EXPECT_EQ(response.sequence, 56);
  EXPECT_EQ(ros2_host_last_set_time(), unix_seconds);
  EXPECT_EQ(ros2_msgs_get_utc_offset_ms(), unix_seconds * 1000 - ros2_host_get_uptime_ms());
}

TEST_F(Ros2MsgsTest, SetTimeRejectsInvalidLengthAndUnsupportedValues)
{
  EXPECT_EQ(SendToRos(ROS2_MSG_SET_TIME, 57, nullptr, 0).payload[1], ROS2_MSG_ERR_LEN);

  const uint64_t out_of_range = UINT64_MAX;
  auto response =
      SendToRos(ROS2_MSG_SET_TIME, 58, reinterpret_cast<const uint8_t *>(&out_of_range), sizeof(out_of_range));
  EXPECT_EQ(response.type, kNack);
  EXPECT_EQ(response.payload[1], ROS2_MSG_ERR_RANGE);

  ros2_host_faults.set_time_fails = true;
  const uint64_t valid = 1788796800ULL;
  response = SendToRos(ROS2_MSG_SET_TIME, 59, reinterpret_cast<const uint8_t *>(&valid), sizeof(valid));
  EXPECT_EQ(response.type, kNack);
  EXPECT_EQ(response.payload[1], ROS2_MSG_ERR_RANGE);
}

TEST_F(Ros2MsgsTest, ValidMotorCommandReceivesAcknowledgement)
{
  const uint8_t payload[] = {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0xBF};
  const DecodedFrame response = SendToRos(kMotorCommand, 22, payload, sizeof(payload));

  ASSERT_EQ(response.count, 1u);
  EXPECT_EQ(response.type, kAck);
  EXPECT_EQ(response.sequence, 22);
}

TEST_F(Ros2MsgsTest, MalformedMotorCommandReceivesLengthNack)
{
  const uint8_t payload[] = {0x34, 0x12, 0x78};
  const DecodedFrame response = SendToRos(kMotorCommand, 23, payload, sizeof(payload));

  ASSERT_EQ(response.count, 1u);
  EXPECT_EQ(response.type, kNack);
  EXPECT_EQ(response.sequence, 23);
  ASSERT_EQ(response.payload_length, 2u);
  EXPECT_EQ(response.payload[0], 23);
  EXPECT_EQ(response.payload[1], kErrorLength);
}

TEST_F(Ros2MsgsTest, UnknownMessageReceivesTypeNack)
{
  const DecodedFrame response = SendToRos(0xFE, 24, nullptr, 0);

  ASSERT_EQ(response.count, 1u);
  EXPECT_EQ(response.type, kNack);
  EXPECT_EQ(response.sequence, 24);
  ASSERT_EQ(response.payload_length, 2u);
  EXPECT_EQ(response.payload[0], 24);
  EXPECT_EQ(response.payload[1], kErrorType);
}

TEST_F(Ros2MsgsTest, MonitorReceivesMessageAndCanBeRemoved)
{
  g_monitored = DecodedFrame{};
  ros2_msgs_set_monitor(Monitor);
  const uint8_t payload[] = {2, 0xDC, 0x05};
  EXPECT_EQ(SendToRos(ROS2_MSG_CMD_SERVO, 41, payload, sizeof(payload)).type, kAck);
  ASSERT_EQ(g_monitored.count, 1u);
  EXPECT_EQ(g_monitored.type, ROS2_MSG_CMD_SERVO);
  EXPECT_EQ(g_monitored.sequence, 41);
  ASSERT_EQ(g_monitored.payload_length, sizeof(payload));
  EXPECT_EQ(std::memcmp(g_monitored.payload, payload, sizeof(payload)), 0);

  ros2_msgs_set_monitor(nullptr);
  SendToRos(kHeartbeat, 42, nullptr, 0);
  EXPECT_EQ(g_monitored.count, 1u);
}

TEST_F(Ros2MsgsTest, TelemetryEnableControlsTimerAndGetter)
{
  EXPECT_TRUE(ros2_msgs_get_telemetry_enabled());
  EXPECT_TRUE(ros2_host_timer_enabled());
  EXPECT_EQ(Configure(ROS2_CFG_TELEM_ENABLE, 0).type, kAck);
  EXPECT_FALSE(ros2_msgs_get_telemetry_enabled());
  EXPECT_FALSE(ros2_host_timer_enabled());
  EXPECT_EQ(Configure(ROS2_CFG_TELEM_ENABLE, 1).type, kAck);
  EXPECT_TRUE(ros2_msgs_get_telemetry_enabled());
  EXPECT_TRUE(ros2_host_timer_enabled());
}

TEST_F(Ros2MsgsTest, TelemetryRateAcceptsBoundsAndRejectsOutOfRangeValues)
{
  for (uint32_t period : {10u, 5000u}) {
    EXPECT_EQ(Configure(ROS2_CFG_TELEM_RATE_MS, period).type, kAck);
    EXPECT_EQ(ros2_host_timer_period(), period);
  }
  for (uint32_t period : {0u, 9u, 5001u, UINT32_MAX}) {
    const auto response = Configure(ROS2_CFG_TELEM_RATE_MS, period);
    EXPECT_EQ(response.type, kNack);
    ASSERT_EQ(response.payload_length, 2u);
    EXPECT_EQ(response.payload[1], ROS2_MSG_ERR_RANGE);
    EXPECT_EQ(ros2_host_timer_period(), 5000u);
  }
}

TEST_F(Ros2MsgsTest, InvalidConfigurationAndServoLengthsReceiveNacks)
{
  const auto unknown = Configure(0xFF, 0);
  EXPECT_EQ(unknown.type, kNack);
  ASSERT_EQ(unknown.payload_length, 2u);
  EXPECT_EQ(unknown.payload[1], ROS2_MSG_ERR_CFG);
  for (uint8_t type : {ROS2_MSG_CMD_CONFIG, ROS2_MSG_CMD_SERVO}) {
    const auto response = SendToRos(type, 43, nullptr, 0);
    EXPECT_EQ(response.type, kNack);
    ASSERT_EQ(response.payload_length, 2u);
    EXPECT_EQ(response.payload[1], ROS2_MSG_ERR_LEN);
  }
}

TEST_F(Ros2MsgsTest, ReceiveCallbackWakesCommandTaskAndProcessesBufferedFrame)
{
  WireCapture request;
  framed_link_t host_link{};
  g_wire_capture = &request;
  framed_link_init(&host_link, CaptureWrite, nullptr, nullptr, nullptr);
  framed_link_send_frame(&host_link, kHeartbeat, 44, nullptr, 0);

  ros2_host_receive(request.bytes, request.length);
  EXPECT_EQ(ros2_host_notifications("ros2_command"), 1u);
  EXPECT_TRUE(g_transmissions.empty());
  ros2_host_run_task("ros2_command");
  ASSERT_EQ(g_transmissions.size(), 1u);
  const auto response = Decode(response_);
  EXPECT_EQ(response.type, kAck);
  EXPECT_EQ(response.sequence, 44);
  ASSERT_EQ(response.payload_length, 1u);
  EXPECT_EQ(response.payload[0], 44);

  ros2_host_run_task("ros2_command");
  EXPECT_EQ(g_transmissions.size(), 1u);
}

TEST_F(Ros2MsgsTest, TimerQueuesBatteryAndMeasuredDriveState)
{
  ros2_host_fire_timer();
  EXPECT_EQ(ros2_host_notifications("ros2_telemetry"), 1u);
  EXPECT_TRUE(g_transmissions.empty());
  ros2_host_run_task("ros2_telemetry");
  ASSERT_EQ(g_transmissions.size(), 2u);
  const auto battery = Decode(g_transmissions[0]);
  EXPECT_EQ(battery.type, ROS2_MSG_TELEMETRY_BATTERY_STATE);
  EXPECT_EQ(battery.sequence, 0);
  const auto drive = Decode(g_transmissions[1]);
  EXPECT_EQ(drive.type, ROS2_MSG_TELEMETRY_DRIVE_STATE);
  EXPECT_EQ(drive.sequence, 1);
  ASSERT_EQ(drive.payload_length, ROS2_TELEMETRY_DRIVE_STATE_PAYLOAD_SIZE);
  uint32_t timestamp;
  float velocities[4];
  std::memcpy(&timestamp, drive.payload, sizeof(timestamp));
  std::memcpy(velocities, drive.payload + 4, sizeof(velocities));
  EXPECT_EQ(timestamp, ros2_msgs_get_total_runtime_ms());
  EXPECT_FLOAT_EQ(velocities[0], -0.125f);
  EXPECT_FLOAT_EQ(velocities[1], -0.75f);
  EXPECT_FLOAT_EQ(velocities[2], 0.25f);
  EXPECT_FLOAT_EQ(velocities[3], -0.5f);

  ros2_host_run_task("ros2_telemetry");
  EXPECT_EQ(g_transmissions.size(), 2u);
}

TEST_F(Ros2MsgsTest, ImuCallbackQueuesLatestSampleAndIgnoresNull)
{
  ros2_host_emit_imu(nullptr);
  EXPECT_EQ(ros2_host_notifications("ros2_telemetry"), 0u);
  navigation_imu_sample_t sample{};
  sample.timestamp_us = 100;
  ros2_host_emit_imu(&sample);
  sample.timestamp_us = 200;
  sample.acceleration_mps2[0] = 9.5f;
  sample.data_valid = true;
  ros2_host_emit_imu(&sample);
  EXPECT_EQ(ros2_host_notifications("ros2_telemetry"), 2u);
  ros2_host_run_task("ros2_telemetry");
  ASSERT_EQ(g_transmissions.size(), 1u);
  const auto imu = Decode(response_);
  EXPECT_EQ(imu.type, ROS2_MSG_TELEMETRY_IMU_STATE);
  EXPECT_EQ(imu.sequence, 0);
  ASSERT_EQ(imu.payload_length, ROS2_TELEMETRY_IMU_STATE_PAYLOAD_SIZE);
  EXPECT_EQ(imu.payload[0], 1);
  EXPECT_EQ(imu.payload[1], 0);
  int64_t timestamp;
  float acceleration;
  std::memcpy(&timestamp, imu.payload + 2, sizeof(timestamp));
  std::memcpy(&acceleration, imu.payload + 10, sizeof(acceleration));
  EXPECT_EQ(timestamp, 200);
  EXPECT_FLOAT_EQ(acceleration, 9.5f);
}

TEST_F(Ros2MsgsTest, DisabledTelemetryDrainsPendingSamplesWithoutSending)
{
  navigation_imu_sample_t sample{};
  ros2_host_emit_imu(&sample);
  ros2_host_fire_timer();
  ros2_msgs_set_telemetry_enabled(false);
  ros2_host_run_task("ros2_telemetry");
  EXPECT_TRUE(g_transmissions.empty());
  ros2_msgs_set_telemetry_enabled(true);
  ros2_host_run_task("ros2_telemetry");
  EXPECT_TRUE(g_transmissions.empty());
}

TEST_F(Ros2MsgsTest, TelemetryMaskSelectsEachPublisher)
{
  const uint32_t masks[] = {ROS2_TELEM_MASK_IMU_STATE, ROS2_TELEM_MASK_BATTERY_STATE, ROS2_TELEM_MASK_DRIVE_STATE};
  const uint8_t types[] = {ROS2_MSG_TELEMETRY_IMU_STATE, ROS2_MSG_TELEMETRY_BATTERY_STATE,
                           ROS2_MSG_TELEMETRY_DRIVE_STATE};
  for (size_t i = 0; i < 3; ++i) {
    ASSERT_EQ(Configure(ROS2_CFG_TELEM_MASK, masks[i]).type, kAck);
    g_transmissions.clear();
    navigation_imu_sample_t sample{};
    ros2_host_emit_imu(&sample);
    ros2_host_fire_timer();
    ros2_host_run_task("ros2_telemetry");
    ASSERT_EQ(g_transmissions.size(), 1u);
    const auto frame = Decode(g_transmissions[0]);
    EXPECT_EQ(frame.type, types[i]);
    EXPECT_EQ(frame.sequence, 0);
    if (types[i] == ROS2_MSG_TELEMETRY_IMU_STATE) {
      ASSERT_EQ(frame.payload_length, ROS2_TELEMETRY_IMU_STATE_PAYLOAD_SIZE);
      EXPECT_EQ(frame.payload[0], 0);
      EXPECT_EQ(frame.payload[1], 1);
    }
  }
}

TEST_F(Ros2MsgsTest, NullDriveStateDoesNotSendFrame)
{
  ros2_msgs_send_drive_state(&messages_, 45, nullptr);
  EXPECT_TRUE(g_transmissions.empty());
}

TEST_F(Ros2MsgsTest, BatteryFailureAndInvalidDataAreReportedInPayload)
{
  for (esp_err_t result : {ESP_OK, ESP_FAIL}) {
    ros2_host_faults.battery_result = result;
    ros2_host_faults.battery_valid = false;
    ros2_msgs_send_battery_state(&messages_, 50);
    const auto frame = Decode(response_);
    ASSERT_EQ(frame.count, 1u);
    EXPECT_EQ(frame.type, ROS2_MSG_TELEMETRY_BATTERY_STATE);
    ASSERT_EQ(frame.payload_length, 22u);
    EXPECT_EQ(frame.payload[0], 0);
    EXPECT_EQ(frame.payload[1], static_cast<uint8_t>(result));
  }
}

TEST_F(Ros2MsgsTest, MissingNavigationSnapshotSuppressesImuFrame)
{
  ros2_host_faults.navigation_result = ESP_FAIL;
  ros2_host_faults.snapshot_valid = false;
  ros2_msgs_send_imu_state(&messages_, 51);
  EXPECT_TRUE(g_transmissions.empty());
}

TEST_F(Ros2MsgsTest, NavigationErrorStillSendsAvailableSnapshot)
{
  ros2_host_faults.navigation_result = ESP_FAIL;
  ros2_host_faults.snapshot_valid = true;
  ros2_host_faults.imu_valid = false;
  ros2_msgs_send_imu_state(&messages_, 52);
  const auto frame = Decode(response_);
  ASSERT_EQ(frame.count, 1u);
  EXPECT_EQ(frame.sequence, 52);
  EXPECT_EQ(frame.type, ROS2_MSG_TELEMETRY_IMU_STATE);
  ASSERT_EQ(frame.payload_length, ROS2_TELEMETRY_IMU_STATE_PAYLOAD_SIZE);
  EXPECT_EQ(frame.payload[0], 0);
  EXPECT_EQ(frame.payload[1], 1);
}

TEST_F(Ros2MsgsTest, RejectedMotorCommandReceivesRangeNack)
{
  ros2_host_faults.motor_result = false;
  const float velocities[] = {100.0f, -100.0f};
  const auto frame = SendToRos(kMotorCommand, 53, reinterpret_cast<const uint8_t *>(velocities), sizeof(velocities));
  ASSERT_EQ(frame.count, 1u);
  EXPECT_EQ(frame.type, kNack);
  EXPECT_EQ(frame.sequence, 53);
  ASSERT_EQ(frame.payload_length, 2u);
  EXPECT_EQ(frame.payload[0], 53);
  EXPECT_EQ(frame.payload[1], ROS2_MSG_ERR_RANGE);
}

TEST_F(Ros2MsgsTest, FailedTransmissionAllowsLaterFrame)
{
  messages_.link.write = nullptr;
  ros2_msgs_send_frame(&messages_, kAck, 54, nullptr, 0);
  EXPECT_TRUE(g_transmissions.empty());
  messages_.link.write = CaptureRosWrite;
  ros2_msgs_send_frame(&messages_, kAck, 55, nullptr, 0);
  ASSERT_EQ(g_transmissions.size(), 1u);
  EXPECT_EQ(Decode(response_).sequence, 55);
}

TEST_F(Ros2MsgsTest, TimerCreationFailureStillAllowsTelemetryConfiguration)
{
  ros2_host_reset();
  ros2_host_faults.timer_create_fails = true;
  ros2_msgs_init();
  ros2_msgs_test_set_write(CaptureRosWrite);
  EXPECT_EQ(Configure(ROS2_CFG_TELEM_ENABLE, 0).type, kAck);
  EXPECT_FALSE(ros2_msgs_get_telemetry_enabled());
  EXPECT_EQ(Configure(ROS2_CFG_TELEM_ENABLE, 1).type, kAck);
  EXPECT_TRUE(ros2_msgs_get_telemetry_enabled());
  EXPECT_EQ(Configure(ROS2_CFG_TELEM_RATE_MS, 100).type, kAck);
  // No period-change call can be made without a timer handle.
  EXPECT_EQ(ros2_host_timer_period(), 20u);
}

TEST_F(Ros2MsgsTest, MissingCommandTaskIsNotNotifiedOnReceive)
{
  ros2_host_reset();
  ros2_host_faults.failed_task_name = "ros2_command";
  ros2_msgs_init();
  ros2_msgs_on_rx();
  EXPECT_EQ(ros2_host_notifications("ros2_command"), 0u);
}

TEST_F(Ros2MsgsTest, MissingTelemetryTaskIgnoresTimerAndImuCallbacks)
{
  ros2_host_reset();
  ros2_host_faults.failed_task_name = "ros2_telemetry";
  ros2_msgs_init();
  navigation_imu_sample_t sample{};
  ros2_host_emit_imu(&sample);
  ros2_host_fire_timer();
  EXPECT_EQ(ros2_host_notifications("ros2_telemetry"), 0u);
  EXPECT_TRUE(g_transmissions.empty());
}

TEST_F(Ros2MsgsTest, InitializationWithTelemetryDisabledDoesNotStartTimer)
{
  ros2_msgs_set_telemetry_enabled(false);
  ros2_host_reset();
  ros2_msgs_init();
  EXPECT_FALSE(ros2_host_timer_enabled());
  EXPECT_FALSE(ros2_msgs_get_telemetry_enabled());
  ros2_msgs_set_telemetry_enabled(true);
  EXPECT_TRUE(ros2_host_timer_enabled());
}

} // namespace
