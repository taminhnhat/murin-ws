#pragma once

#include <stddef.h>
#include <stdint.h>

void diag_log_ros2(uint8_t msg_type, uint8_t seq, const uint8_t *payload, size_t payload_len);
