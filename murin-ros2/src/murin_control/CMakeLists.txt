cmake_minimum_required(VERSION 3.16)
project(murin_control LANGUAGES CXX)

find_package(ros2_control_cmake REQUIRED)
set_compiler_options()
export_windows_symbols()

# find dependencies
set(THIS_PACKAGE_INCLUDE_DEPENDS hardware_interface std_msgs pluginlib rclcpp
                                 rclcpp_lifecycle)

# Specify the required version of ros2_control
find_package(controller_manager 4.0.0)
# Handle the case where the required version is not found
if(NOT controller_manager_FOUND)
  message(
    FATAL_ERROR
      "ros2_control version 4.0.0 or higher is required. "
      "Install the ROS 2 Jazzy ros2_control packages.")
endif()

# find dependencies
find_package(backward_ros REQUIRED)
find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
foreach(Dependency IN ITEMS ${THIS_PACKAGE_INCLUDE_DEPENDS})
  find_package(${Dependency} REQUIRED)
endforeach()

# COMPILE
add_library(murin_control SHARED hardware/murin_system.cpp)
target_compile_features(murin_control PUBLIC cxx_std_17)
target_include_directories(
  murin_control PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/hardware/include>
                       $<INSTALL_INTERFACE:include/murin_control>)
target_link_libraries(
  murin_control
  PUBLIC hardware_interface::hardware_interface ${std_msgs_TARGETS}
         pluginlib::pluginlib rclcpp::rclcpp rclcpp_lifecycle::rclcpp_lifecycle)

# Export hardware plugins
pluginlib_export_plugin_description_file(hardware_interface murin_control.xml)

# INSTALL
ament_python_install_package(murin_control_bridge)
install(PROGRAMS scripts/robot_socket_bridge DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY hardware/include/ DESTINATION include/murin_control)
install(DIRECTORY description/launch description/ros2_control description/urdf description/rviz
        DESTINATION share/murin_control)
install(DIRECTORY bringup/launch bringup/config DESTINATION share/murin_control)
install(
  TARGETS murin_control
  EXPORT export_murin_control
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_pytest REQUIRED)
  add_executable(test_serial_link test/test_serial_link.cpp)
  target_include_directories(test_serial_link PRIVATE hardware/include)
  target_link_libraries(test_serial_link util)
  add_test(NAME serial_protocol COMMAND test_serial_link)
  ament_add_pytest_test(bridge_protocol test/test_bridge_protocol.py)
  ament_add_pytest_test(murin_urdf_xacro test/test_urdf_xacro.py)

  # Integration (launch) tests
  find_package(ament_cmake_ros REQUIRED)
  find_package(launch_testing_ament_cmake REQUIRED)
  function(add_ros_isolated_launch_test path)
    set(RUNNER "${ament_cmake_ros_DIR}/run_test_isolated.py")
    add_launch_test("${path}" RUNNER "${RUNNER}" ${ARGN})
  endfunction()
  add_ros_isolated_launch_test(test/test_view_robot_launch.py)
  add_ros_isolated_launch_test(test/test_murin_launch.py)
endif()

install(FILES README.md LICENSE requirements.txt
        DESTINATION share/${PROJECT_NAME})

# EXPORTS
ament_export_targets(export_murin_control HAS_LIBRARY_TARGET)
ament_export_dependencies(${THIS_PACKAGE_INCLUDE_DEPENDS})
ament_package()
