cmake_minimum_required(VERSION 3.14)
project(ros2_msgs_unittest C CXX)

include(FetchContent)
set(FETCHCONTENT_BASE_DIR
    ${CMAKE_BINARY_DIR}/_deps
    CACHE PATH "Directory used by FetchContent to store downloaded content")
message(STATUS "FetchContent base dir: ${FETCHCONTENT_BASE_DIR}")
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt
    ON
    CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

set(ROS2_MOCK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mocks/ros2)
set(FLASH_STORAGE_MOCK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mocks/flash_storage)
set(COMMON_MOCK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include)

# Build the ros2_msgs C library (compiled as C)
add_library(
  ros2_msgs STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/ros2/ros2_msgs.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/link/framed_link.c
  ${ROS2_MOCK_DIR}/ros2_msgs_host_stubs.c)
target_include_directories(
  ros2_msgs
  PRIVATE ${ROS2_MOCK_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/ros2
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/link
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/usb
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/diag
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/motor)
target_compile_definitions(ros2_msgs PRIVATE UNIT_TEST)
set_target_properties(ros2_msgs PROPERTIES LINKER_LANGUAGE C)

# Test executable (C++ using GoogleTest)
add_executable(ros2_msgs_unittest
               ${CMAKE_CURRENT_SOURCE_DIR}/test_ros2_msgs.cpp)
target_include_directories(
  ros2_msgs_unittest
  PRIVATE ${ROS2_MOCK_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/ros2
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/link
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/usb
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/motor)
target_compile_definitions(ros2_msgs_unittest PRIVATE UNIT_TEST)
target_compile_features(ros2_msgs_unittest PRIVATE cxx_std_11)
target_link_libraries(ros2_msgs_unittest PRIVATE ros2_msgs gtest_main)

add_test(NAME ros2_msgs_unittest COMMAND ros2_msgs_unittest)

add_library(framed_link STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/link/framed_link.c)
target_include_directories(
  framed_link PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/link)
set_target_properties(framed_link PROPERTIES LINKER_LANGUAGE C)

add_executable(framed_link_unittest
               ${CMAKE_CURRENT_SOURCE_DIR}/test_framed_link.cpp)
target_include_directories(
  framed_link_unittest
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/link)
target_compile_features(framed_link_unittest PRIVATE cxx_std_11)
target_link_libraries(framed_link_unittest PRIVATE framed_link gtest_main)

add_test(NAME framed_link_unittest COMMAND framed_link_unittest)

add_library(
  diff_drive STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/motor/diff_drive.c
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks/diff_drive/diff_drive_host_stubs.c)
target_include_directories(
  diff_drive
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks/diff_drive
          ${COMMON_MOCK_INCLUDE_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/config
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/motor)
target_compile_definitions(diff_drive PUBLIC UNIT_TEST)
set_target_properties(diff_drive PROPERTIES LINKER_LANGUAGE C)

add_executable(diff_drive_unittest
               ${CMAKE_CURRENT_SOURCE_DIR}/test_diff_drive.cpp)
target_include_directories(
  diff_drive_unittest
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks/diff_drive
          ${COMMON_MOCK_INCLUDE_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/config
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/motor)
target_compile_features(diff_drive_unittest PRIVATE cxx_std_11)
target_link_libraries(diff_drive_unittest PRIVATE diff_drive gtest_main)

add_test(NAME diff_drive_unittest COMMAND diff_drive_unittest)

add_library(
  diag
  ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/diag/diag.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/system_log/system_log.c
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks/diag/diag_host_stubs.c)
target_include_directories(
  diag
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks/diag
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/diag
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/battery
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/link
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/rp3
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/system_log
          ${COMMON_MOCK_INCLUDE_DIR})
set_target_properties(diag PROPERTIES LINKER_LANGUAGE C)

add_executable(diag_unittest ${CMAKE_CURRENT_SOURCE_DIR}/test_diag.cpp)
target_include_directories(
  diag_unittest
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks/diag
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/diag
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/battery
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/link
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/rp3
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/system_log
          ${COMMON_MOCK_INCLUDE_DIR})
target_compile_features(diag_unittest PRIVATE cxx_std_11)
target_link_libraries(diag_unittest PRIVATE diag gtest_main)

add_test(NAME diag_unittest COMMAND diag_unittest)

# Build flash_storage against a text-file-backed host NVS implementation.
add_library(
  flash_storage STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/nvs/flash_storage.c
  ${FLASH_STORAGE_MOCK_DIR}/flash_storage_host_stubs.c)
target_include_directories(
  flash_storage PRIVATE ${COMMON_MOCK_INCLUDE_DIR}
                        ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/nvs)
set_target_properties(flash_storage PROPERTIES LINKER_LANGUAGE C)
if(WIN32)
  target_compile_definitions(flash_storage PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(flash_storage_unittest
               ${CMAKE_CURRENT_SOURCE_DIR}/test_flash_storage.cpp)
target_include_directories(
  flash_storage_unittest
  PRIVATE ${FLASH_STORAGE_MOCK_DIR} ${COMMON_MOCK_INCLUDE_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/nvs)
target_compile_features(flash_storage_unittest PRIVATE cxx_std_11)
target_link_libraries(flash_storage_unittest PRIVATE flash_storage gtest_main)
if(WIN32)
  target_compile_definitions(flash_storage_unittest
                             PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_test(NAME flash_storage_unittest COMMAND flash_storage_unittest)
