#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef void (*usb_bridge_cb_t)(void);

void usb_bridge_init(void);
bool usb_bridge_is_connected(void);
size_t usb_bridge_write_bytes(uint8_t *data, size_t len);
size_t usb_bridge_read_bytes(uint8_t *data, size_t len);
void usb_bridge_set_callback(usb_bridge_cb_t cb);

#ifdef __cplusplus
}
#endif
