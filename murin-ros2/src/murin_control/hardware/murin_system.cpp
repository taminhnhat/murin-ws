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

#include "murin_control/murin_system.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>

#include "hardware_interface/lexical_casts.hpp"
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "rclcpp/rclcpp.hpp"

namespace murin_control
{
hardware_interface::CallbackReturn MurinSystemHardware::on_init(
  const hardware_interface::HardwareComponentInterfaceParams & params)
{
  if (
    hardware_interface::SystemInterface::on_init(params) !=
    hardware_interface::CallbackReturn::SUCCESS)
  {
    return hardware_interface::CallbackReturn::ERROR;
  }

  try
  {
    const auto & config = info_.hardware_parameters;
    const auto mode = config.at("transport");
    if (mode != "simulation" && mode != "serial")
    {
      throw std::invalid_argument("transport must be simulation or serial");
    }
    serial_mode_ = mode == "serial";
    port_ = config.count("serial_port") ? config.at("serial_port") : "";
    baud_ = std::stoi(config.at("baud_rate"));
    wheel_radius_ = std::stod(config.at("wheel_radius"));
    const auto bridge = config.at("require_bridge");
    if (bridge != "true" && bridge != "True" && bridge != "false" && bridge != "False")
    {
      throw std::invalid_argument("require_bridge must be a boolean");
    }
    require_bridge_ = bridge == "true" || bridge == "True";
    if (!std::isfinite(wheel_radius_) || wheel_radius_ <= 0 || (serial_mode_ && port_.empty()))
    {
      throw std::invalid_argument("Positive wheel_radius and explicit serial_port are required");
    }
  }
  catch (const std::exception & error)
  {
    RCLCPP_ERROR(get_logger(), "%s", error.what());
    return hardware_interface::CallbackReturn::ERROR;
  }

  if (info_.joints.size() != 2)
  {
    RCLCPP_ERROR(get_logger(), "Murin requires exactly two wheel joints.");
    return hardware_interface::CallbackReturn::ERROR;
  }

  for (const hardware_interface::ComponentInfo & joint : info_.joints)
  {
    // Murin has exactly two states and one command interface on each joint
    if (joint.command_interfaces.size() != 1)
    {
      RCLCPP_FATAL(
        get_logger(), "Joint '%s' has %zu command interfaces found. 1 expected.",
        joint.name.c_str(), joint.command_interfaces.size());
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.command_interfaces[0].name != hardware_interface::HW_IF_VELOCITY)
    {
      RCLCPP_FATAL(
        get_logger(), "Joint '%s' have %s command interfaces found. '%s' expected.",
        joint.name.c_str(), joint.command_interfaces[0].name.c_str(),
        hardware_interface::HW_IF_VELOCITY);
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces.size() != 2)
    {
      RCLCPP_FATAL(
        get_logger(), "Joint '%s' has %zu state interface. 2 expected.", joint.name.c_str(),
        joint.state_interfaces.size());
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces[0].name != hardware_interface::HW_IF_POSITION)
    {
      RCLCPP_FATAL(
        get_logger(), "Joint '%s' have '%s' as first state interface. '%s' expected.",
        joint.name.c_str(), joint.state_interfaces[0].name.c_str(),
        hardware_interface::HW_IF_POSITION);
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces[1].name != hardware_interface::HW_IF_VELOCITY)
    {
      RCLCPP_FATAL(
        get_logger(), "Joint '%s' have '%s' as second state interface. '%s' expected.",
        joint.name.c_str(), joint.state_interfaces[1].name.c_str(),
        hardware_interface::HW_IF_VELOCITY);
      return hardware_interface::CallbackReturn::ERROR;
    }
  }

  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn MurinSystemHardware::on_configure(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  if (!get_node())
  {
    return hardware_interface::CallbackReturn::ERROR;
  }
  status_pub_ = get_node()->create_publisher<std_msgs::msg::Bool>(
    "/murin/hardware_connected", rclcpp::QoS(1).transient_local());
  telemetry_pub_ =
    get_node()->create_publisher<std_msgs::msg::UInt8MultiArray>("/murin/serial_frames", 10);
  enable_sub_ = get_node()->create_subscription<std_msgs::msg::Bool>(
    "/murin/drive_enabled", 1,
    [this](const std_msgs::msg::Bool & msg)
    {
      drive_enabled_ = msg.data;
      bridge_seen_ = std::chrono::steady_clock::now().time_since_epoch().count();
    });
  if (serial_mode_)
  {
    try
    {
      serial_.open(port_, baud_);
    }
    catch (const std::exception & error)
    {
      RCLCPP_ERROR(get_logger(), "%s", error.what());
      publish_status(false);
      return hardware_interface::CallbackReturn::ERROR;
    }
    if (!serial_.motor(0, 0))
    {
      stop_serial();
      return hardware_interface::CallbackReturn::ERROR;
    }
    RCLCPP_WARN(
      get_logger(),
      "Serial connected; using firmware drive-state "
      "feedback (currently applied motor velocity).");
  }
  publish_status(false);
  // reset values always when configuring hardware
  for (const auto & [name, descr] : joint_state_interfaces_)
  {
    set_state(name, 0.0);
  }
  for (const auto & [name, descr] : joint_command_interfaces_)
  {
    set_command(name, 0.0);
  }
  RCLCPP_INFO(get_logger(), "Successfully configured!");

  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn MurinSystemHardware::on_activate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  // command and state should be equal when starting
  for (const auto & [name, descr] : joint_command_interfaces_)
  {
    set_command(name, 0.0);
    set_state(name, 0.0);
  }

  if (serial_mode_ && !serial_.motor(0, 0))
  {
    stop_serial();
    return hardware_interface::CallbackReturn::ERROR;
  }
  active_ = true;
  have_drive_ = false;
  drive_received_ = std::chrono::steady_clock::now();
  publish_status(!serial_mode_);
  RCLCPP_INFO(get_logger(), "Successfully activated!");

  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn MurinSystemHardware::on_deactivate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  for (const auto & [name, descr] : joint_command_interfaces_)
  {
    set_command(name, 0.0);
    set_state(name, 0.0);
  }
  if (serial_mode_)
  {
    serial_.motor(0, 0);
  }
  publish_status(false);
  active_ = false;
  RCLCPP_INFO(get_logger(), "Successfully deactivated!");

  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::return_type MurinSystemHardware::read(
  const rclcpp::Time & /*time*/, const rclcpp::Duration & period)
{
  if (serial_mode_)
  {
    std::vector<Bytes> frames;
    if (!serial_.receive(frames))
    {
      stop_serial();
      return hardware_interface::return_type::ERROR;
    }
    for (const auto & data : frames)
    {
      std_msgs::msg::UInt8MultiArray msg;
      msg.data = data;
      telemetry_pub_->publish(msg);
      if (data[0] == 0x05 && data.size() == 22)
      {
        const auto u32 = [&data](size_t offset)
        {
          return uint32_t(data[offset]) | (uint32_t(data[offset + 1]) << 8) |
                 (uint32_t(data[offset + 2]) << 16) | (uint32_t(data[offset + 3]) << 24);
        };
        float wheels[2];
        for (size_t i = 0; i < 2; ++i)
        {
          const auto bits = u32(14 + 4 * i);
          std::memcpy(&wheels[i], &bits, 4);
        }
        if (!std::isfinite(wheels[0]) || !std::isfinite(wheels[1]))
        {
          stop_serial();
          return hardware_interface::return_type::ERROR;
        }
        const uint32_t stamp = u32(2);
        const uint32_t elapsed = stamp - drive_stamp_;  // Handles uint32 timestamp wrap.
        for (size_t i = 0; i < 2; ++i)
        {
          const auto name = info_.joints[i].name;
          if (have_drive_ && elapsed <= 1000)
          {
            set_state(
              name + "/position",
              get_state(name + "/position") + get_state(name + "/velocity") * elapsed / 1000.0);
          }
          set_state(name + "/velocity", wheels[i] / wheel_radius_);
        }
        drive_stamp_ = stamp;
        drive_received_ = std::chrono::steady_clock::now();
        if (!have_drive_ && active_)
        {
          publish_status(true);
        }
        have_drive_ = true;
      }
      if (data[0] == 0x7f)
      {
        RCLCPP_ERROR(get_logger(), "Firmware rejected a command (NACK)");
        stop_serial();
        return hardware_interface::return_type::ERROR;
      }
    }
  }
  if (serial_mode_)
  {
    if (active_ && std::chrono::steady_clock::now() - drive_received_ > std::chrono::seconds(1))
    {
      RCLCPP_ERROR(get_logger(), "Drive-state telemetry timed out");
      stop_serial();
      return hardware_interface::return_type::ERROR;
    }
    return hardware_interface::return_type::OK;
  }
  // Simulation integrates the previous applied command.
  for (const auto & joint : info_.joints)
  {
    const auto position = joint.name + "/position";
    set_state(
      position, get_state(position) + period.seconds() * get_state(joint.name + "/velocity"));
  }

  return hardware_interface::return_type::OK;
}

hardware_interface::return_type murin_control::MurinSystemHardware::write(
  const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
{
  double velocities[2];
  for (size_t i = 0; i < 2; ++i)
  {
    velocities[i] = get_command(info_.joints[i].name + "/velocity");
    if (!std::isfinite(velocities[i]))
    {
      stop_serial();
      return hardware_interface::return_type::ERROR;
    }
  }
  if (!active_ || (serial_mode_ && !have_drive_))
  {
    velocities[0] = velocities[1] = 0.0;
  }
  if (require_bridge_)
  {
    const auto seen = std::chrono::steady_clock::time_point(
      std::chrono::steady_clock::duration(bridge_seen_.load()));
    if (!drive_enabled_ || std::chrono::steady_clock::now() - seen > std::chrono::milliseconds(500))
    {
      velocities[0] = velocities[1] = 0.0;
    }
  }
  // Preserve curvature while bounding both firmware commands to +/-0.5 m/s.
  const double scale =
    std::max(1.0, std::max(std::abs(velocities[0]), std::abs(velocities[1])) * wheel_radius_ / 0.5);
  for (auto & velocity : velocities)
  {
    velocity /= scale;
  }
  if (serial_mode_ && !serial_.motor(velocities[0] * wheel_radius_, velocities[1] * wheel_radius_))
  {
    stop_serial();
    return hardware_interface::return_type::ERROR;
  }
  if (!serial_mode_)
  {
    for (size_t i = 0; i < 2; ++i)
    {
      set_state(info_.joints[i].name + "/velocity", velocities[i]);
    }
  }

  return hardware_interface::return_type::OK;
}

void MurinSystemHardware::publish_status(bool connected)
{
  if (status_pub_)
  {
    std_msgs::msg::Bool msg;
    msg.data = connected;
    status_pub_->publish(msg);
  }
}
void MurinSystemHardware::stop_serial()
{
  if (serial_.is_open())
  {
    serial_.motor(0, 0);
    serial_.close();
  }
  active_ = false;
  publish_status(false);
}
MurinSystemHardware::~MurinSystemHardware()
{
  try
  {
    stop_serial();
  }
  catch (...)
  {
    serial_.close();
  }
}
hardware_interface::CallbackReturn MurinSystemHardware::on_cleanup(const rclcpp_lifecycle::State &)
{
  stop_serial();
  return hardware_interface::CallbackReturn::SUCCESS;
}
hardware_interface::CallbackReturn MurinSystemHardware::on_shutdown(const rclcpp_lifecycle::State &)
{
  stop_serial();
  return hardware_interface::CallbackReturn::SUCCESS;
}
hardware_interface::CallbackReturn MurinSystemHardware::on_error(const rclcpp_lifecycle::State &)
{
  stop_serial();
  return hardware_interface::CallbackReturn::SUCCESS;
}

}  // namespace murin_control

#include "pluginlib/class_list_macros.hpp"
PLUGINLIB_EXPORT_CLASS(murin_control::MurinSystemHardware, hardware_interface::SystemInterface)
