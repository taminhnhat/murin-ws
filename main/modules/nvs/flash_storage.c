#include "flash_storage.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define FLASH_STORAGE_NAMESPACE "ros2"
static const char *TAG = "flash_storage";

const flash_storage_item_t flash_storage_items[FLASH_STORAGE_ITEM_COUNT] = {
    [FLASH_STORAGE_ITEM_TELEMETRY_ENABLED] =
        {
            .id = FLASH_STORAGE_ITEM_TELEMETRY_ENABLED,
            .key = "telemetry",
            .type = FLASH_STORAGE_TYPE_U8,
            .size = sizeof(uint8_t),
        },
    [FLASH_STORAGE_ITEM_TOTAL_RUNTIME_MS] =
        {
            .id = FLASH_STORAGE_ITEM_TOTAL_RUNTIME_MS,
            .key = "runtime_ms",
            .type = FLASH_STORAGE_TYPE_U64,
            .size = sizeof(uint64_t),
        },
    [FLASH_STORAGE_ITEM_UTC_OFFSET_MS] =
        {
            .id = FLASH_STORAGE_ITEM_UTC_OFFSET_MS,
            .key = "utc_offset_ms",
            .type = FLASH_STORAGE_TYPE_U64,
            .size = sizeof(uint64_t),
        },
    [FLASH_STORAGE_ITEM_ROBOT_STATUS] =
        {
            .id = FLASH_STORAGE_ITEM_ROBOT_STATUS,
            .key = "robot_status",
            .type = FLASH_STORAGE_TYPE_BLOB,
            .size = 0,
        },
    [FLASH_STORAGE_ITEM_BATTERY_MONITOR] =
        {
            .id = FLASH_STORAGE_ITEM_BATTERY_MONITOR,
            .key = "battery_mon",
            .type = FLASH_STORAGE_TYPE_BLOB,
            .size = 0,
        },
    [FLASH_STORAGE_ITEM_SHELL_HISTORY] =
        {
            .id = FLASH_STORAGE_ITEM_SHELL_HISTORY,
            .key = "shell_history",
            .type = FLASH_STORAGE_TYPE_BLOB,
            .size = 0,
        },
};

void flash_storage_init(void)
{
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    err = nvs_flash_erase();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
      return;
    }
    err = nvs_flash_init();
  }
  if (err != ESP_OK)
    ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
}

esp_err_t flash_storage_get(flash_storage_item_id_t item, void *data, size_t *size)
{
  if (size == NULL)
    return ESP_ERR_INVALID_ARG;

  if (item >= FLASH_STORAGE_ITEM_COUNT ||
      (flash_storage_items[item].size != 0 && *size < flash_storage_items[item].size) ||
      (flash_storage_items[item].size != 0 && data == NULL))
    return ESP_ERR_INVALID_SIZE;

  const flash_storage_item_t *entry = &flash_storage_items[item];

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(FLASH_STORAGE_NAMESPACE, NVS_READONLY, &nvs);
  if (err != ESP_OK)
    return err;

  switch (entry->type) {
  case FLASH_STORAGE_TYPE_U8:
    err = nvs_get_u8(nvs, entry->key, data);
    break;
  case FLASH_STORAGE_TYPE_U64:
    err = nvs_get_u64(nvs, entry->key, data);
    break;
  case FLASH_STORAGE_TYPE_BLOB:
    err = nvs_get_blob(nvs, entry->key, data, size);
    break;
  default:
    err = ESP_ERR_NVS_TYPE_MISMATCH;
    break;
  }
  nvs_close(nvs);
  if (err == ESP_OK && entry->size != 0)
    *size = entry->size;
  return err;
}

esp_err_t flash_storage_set(flash_storage_item_id_t item, const void *data, size_t size)
{
  if (item >= FLASH_STORAGE_ITEM_COUNT || data == NULL || size == 0)
    return ESP_ERR_INVALID_ARG;

  const flash_storage_item_t *entry = &flash_storage_items[item];
  if (entry->size != 0 && size != entry->size)
    return ESP_ERR_INVALID_SIZE;

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(FLASH_STORAGE_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK)
    return err;

  switch (entry->type) {
  case FLASH_STORAGE_TYPE_U8:
    err = nvs_set_u8(nvs, entry->key, *(const uint8_t *)data);
    break;
  case FLASH_STORAGE_TYPE_U64:
    err = nvs_set_u64(nvs, entry->key, *(const uint64_t *)data);
    break;
  case FLASH_STORAGE_TYPE_BLOB:
    err = nvs_set_blob(nvs, entry->key, data, size);
    break;
  default:
    err = ESP_ERR_NVS_TYPE_MISMATCH;
    break;
  }
  if (err == ESP_OK)
    err = nvs_commit(nvs);
  nvs_close(nvs);
  return err;
}

bool flash_storage_get_telemetry_enabled(bool default_enabled)
{
  uint8_t enabled;
  size_t size = sizeof(enabled);
  return flash_storage_get(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, &enabled, &size) == ESP_OK ? enabled != 0
                                                                                            : default_enabled;
}

esp_err_t flash_storage_set_telemetry_enabled(bool enabled)
{
  const uint8_t value = enabled ? 1 : 0;
  return flash_storage_set(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, &value, sizeof(value));
}

uint64_t flash_storage_get_total_runtime_ms(void)
{
  uint64_t runtime_ms;
  size_t size = sizeof(runtime_ms);
  return flash_storage_get(FLASH_STORAGE_ITEM_TOTAL_RUNTIME_MS, &runtime_ms, &size) == ESP_OK ? runtime_ms : 0;
}

esp_err_t flash_storage_set_total_runtime_ms(uint64_t runtime_ms)
{
  return flash_storage_set(FLASH_STORAGE_ITEM_TOTAL_RUNTIME_MS, &runtime_ms, sizeof(runtime_ms));
}

uint64_t flash_storage_get_utc_offset_ms(void)
{
  uint64_t offset_ms;
  size_t size = sizeof(offset_ms);
  return flash_storage_get(FLASH_STORAGE_ITEM_UTC_OFFSET_MS, &offset_ms, &size) == ESP_OK ? offset_ms : 0;
}

esp_err_t flash_storage_set_utc_offset_ms(uint64_t offset_ms)
{
  return flash_storage_set(FLASH_STORAGE_ITEM_UTC_OFFSET_MS, &offset_ms, sizeof(offset_ms));
}
