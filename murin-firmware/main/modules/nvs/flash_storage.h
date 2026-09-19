#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FLASH_STORAGE_TYPE_U8,
  FLASH_STORAGE_TYPE_U64,
  FLASH_STORAGE_TYPE_BLOB,
} flash_storage_type_t;

typedef enum {
  FLASH_STORAGE_ITEM_TELEMETRY_ENABLED,
  FLASH_STORAGE_ITEM_TOTAL_RUNTIME_MS,
  FLASH_STORAGE_ITEM_UTC_OFFSET_MS,
  FLASH_STORAGE_ITEM_ROBOT_STATUS,
  FLASH_STORAGE_ITEM_BATTERY_MONITOR,
  FLASH_STORAGE_ITEM_SHELL_HISTORY,
  FLASH_STORAGE_ITEM_COUNT,
} flash_storage_item_id_t;

typedef struct {
  flash_storage_item_id_t id;
  const char *key;
  flash_storage_type_t type;
  size_t size;
} flash_storage_item_t;

extern const flash_storage_item_t flash_storage_items[FLASH_STORAGE_ITEM_COUNT];

void flash_storage_init(void);
esp_err_t flash_storage_get(flash_storage_item_id_t item, void *data, size_t *size);
esp_err_t flash_storage_set(flash_storage_item_id_t item, const void *data, size_t size);

bool flash_storage_get_telemetry_enabled(bool default_enabled);
esp_err_t flash_storage_set_telemetry_enabled(bool enabled);
uint64_t flash_storage_get_total_runtime_ms(void);
esp_err_t flash_storage_set_total_runtime_ms(uint64_t runtime_ms);
uint64_t flash_storage_get_utc_offset_ms(void);
esp_err_t flash_storage_set_utc_offset_ms(uint64_t offset_ms);

#ifdef __cplusplus
}
#endif
