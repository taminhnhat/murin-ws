// Copyright 2021 ros2_control Development Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MURIN_CONTROL__MURIN_SYSTEM_HPP_
#define MURIN_CONTROL__MURIN_SYSTEM_HPP_

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "murin_control/serial_link.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

#include "hardware_interface/handle.hpp"
#include "hardware_interface/hardware_info.hpp"
#include "hardware_interface/system_interface.hpp"
#include "hardware_interface/types/hardware_interface_return_values.hpp"
#include "rclcpp/clock.hpp"
#include "rclcpp/duration.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/time.hpp"
#include "rclcpp_lifecycle/node_interfaces/lifecycle_node_interface.hpp"
#include "rclcpp_lifecycle/state.hpp"

namespace murin_control
{
class MurinSystemHardware : public hardware_interface::SystemInterface
{
public:
  RCLCPP_SHARED_PTR_DEFINITIONS(MurinSystemHardware)
  ~MurinSystemHardware() override;
  hardware_interface::CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

  hardware_interface::CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params) override;

  hardware_interface::CallbackReturn on_configure(
    const rclcpp_lifecycle::State & previous_state) override;

  hardware_interface::CallbackReturn on_activate(
    const rclcpp_lifecycle::State & previous_state) override;

  hardware_interface::CallbackReturn on_deactivate(
    const rclcpp_lifecycle::State & previous_state) override;

  hardware_interface::return_type read(
    const rclcpp::Time & time, const rclcpp::Duration & period) override;

  hardware_interface::return_type write(
    const rclcpp::Time & time, const rclcpp::Duration & period) override;

private:
  void stop_serial();
  void publish_status(bool connected);
  bool serial_mode_ = false;
  bool require_bridge_ = false;
  double wheel_radius_ = 0.015;
  std::string port_;
  int baud_ = 2000000;
  SerialLink serial_;
  bool have_drive_ = false;
  bool active_ = false;
  uint32_t drive_stamp_ = 0;
  std::chrono::steady_clock::time_point drive_received_;
  std::atomic<bool> drive_enabled_{false};
  std::atomic<int64_t> bridge_seen_{0};
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr status_pub_;
  rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr telemetry_pub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr enable_sub_;
};

}  // namespace murin_control

#endif  // MURIN_CONTROL__MURIN_SYSTEM_HPP_
