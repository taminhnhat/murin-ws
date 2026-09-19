#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void monitor_init(void);
void monitor_ros2_enable(bool enabled);
bool monitor_ros2_is_enable(void);
void monitor_rp3_enable(bool enabled);
bool monitor_rp3_is_enable(void);
void monitor_diff_drive_enable(bool enabled);
bool monitor_diff_drive_is_enable(void);
void monitor_navigation_enable(bool enabled);
bool monitor_navigation_is_enable(void);
void monitor_system_enable(bool enabled);
bool monitor_system_is_enable(void);

#ifdef __cplusplus
}
#endif
