#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "esp_err.h"
#include "flash_storage.h"
#include "flash_storage_host_stubs.h"
}

namespace {

constexpr const char *kStorageFile = "flash_storage_gtest.txt";

class FlashStorageTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    std::remove(kStorageFile);
    flash_storage_host_reset();
    flash_storage_init();
  }

  void TearDown() override { std::remove(kStorageFile); }
};

TEST_F(FlashStorageTest, UsesDefaultsWhenValuesAreMissing)
{
  EXPECT_TRUE(flash_storage_get_telemetry_enabled(true));
  EXPECT_FALSE(flash_storage_get_telemetry_enabled(false));
  EXPECT_EQ(flash_storage_get_total_runtime_ms(), 0u);
  EXPECT_EQ(flash_storage_get_utc_offset_ms(), 0u);
}

TEST_F(FlashStorageTest, StoresAndLoadsTypedValues)
{
  EXPECT_EQ(flash_storage_set_telemetry_enabled(false), ESP_OK);
  EXPECT_FALSE(flash_storage_get_telemetry_enabled(true));

  EXPECT_EQ(flash_storage_set_total_runtime_ms(9876543210123ULL), ESP_OK);
  EXPECT_EQ(flash_storage_get_total_runtime_ms(), 9876543210123ULL);

  EXPECT_EQ(flash_storage_set_utc_offset_ms(1788796798766ULL), ESP_OK);
  EXPECT_EQ(flash_storage_get_utc_offset_ms(), 1788796798766ULL);

  uint8_t enabled = 0;
  size_t enabled_size = sizeof(enabled);
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, &enabled, &enabled_size), ESP_OK);
  EXPECT_EQ(enabled, 0u);
  EXPECT_EQ(enabled_size, sizeof(enabled));
}

TEST_F(FlashStorageTest, StoresAndLoadsBlobAndReportsItsSize)
{
  const uint8_t expected[] = {0x00, 0xAA, 0x1B, 0xFF};
  ASSERT_EQ(flash_storage_set(FLASH_STORAGE_ITEM_SHELL_HISTORY, expected, sizeof(expected)), ESP_OK);

  uint8_t actual[sizeof(expected)]{};
  size_t actual_size = sizeof(actual);
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_SHELL_HISTORY, actual, &actual_size), ESP_OK);
  EXPECT_EQ(actual_size, sizeof(expected));
  EXPECT_EQ(std::memcmp(actual, expected, sizeof(expected)), 0);
}

TEST_F(FlashStorageTest, RejectsInvalidArguments)
{
  uint8_t value = 0;
  size_t size = sizeof(value);
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, &value, nullptr), ESP_ERR_INVALID_ARG);
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_COUNT, &value, &size), ESP_ERR_INVALID_SIZE);
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, nullptr, &size), ESP_ERR_INVALID_SIZE);
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, &value, &size), ESP_ERR_NVS_NOT_FOUND);

  EXPECT_EQ(flash_storage_set(FLASH_STORAGE_ITEM_COUNT, &value, sizeof(value)), ESP_ERR_INVALID_ARG);
  EXPECT_EQ(flash_storage_set(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, nullptr, sizeof(value)), ESP_ERR_INVALID_ARG);
  EXPECT_EQ(flash_storage_set(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, &value, 0), ESP_ERR_INVALID_ARG);
  EXPECT_EQ(flash_storage_set(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, &value, sizeof(uint16_t)), ESP_ERR_INVALID_SIZE);
}

TEST_F(FlashStorageTest, PersistsValuesInTextFile)
{
  const uint64_t runtime = 42;
  ASSERT_EQ(flash_storage_set_total_runtime_ms(runtime), ESP_OK);

  FILE *file = std::fopen(kStorageFile, "r");
  ASSERT_NE(file, nullptr);
  char contents[256]{};
  ASSERT_NE(std::fgets(contents, sizeof(contents), file), nullptr);
  std::fclose(file);
  EXPECT_NE(std::strstr(contents, "runtime_ms|q|"), nullptr);

  EXPECT_EQ(flash_storage_get_total_runtime_ms(), runtime);
}

TEST_F(FlashStorageTest, RecoversFromFullOrOutdatedNvsByErasingAndRetrying)
{
  for (esp_err_t error : {ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND}) {
    ASSERT_EQ(flash_storage_set_total_runtime_ms(123), ESP_OK);
    flash_storage_host_reset();
    flash_storage_host.init_results[0] = error;
    flash_storage_init();
    EXPECT_EQ(flash_storage_host.init_calls, 2u);
    EXPECT_EQ(flash_storage_host.erase_calls, 1u);
    EXPECT_EQ(flash_storage_get_total_runtime_ms(), 0u);
    EXPECT_EQ(flash_storage_set_total_runtime_ms(456), ESP_OK);
    EXPECT_EQ(flash_storage_get_total_runtime_ms(), 456u);
  }
}

TEST_F(FlashStorageTest, FailedEraseStopsRecoveryAndPreservesStorage)
{
  ASSERT_EQ(flash_storage_set_total_runtime_ms(123), ESP_OK);
  flash_storage_host_reset();
  flash_storage_host.init_results[0] = ESP_ERR_NVS_NO_FREE_PAGES;
  flash_storage_host.erase_result = ESP_FAIL;
  flash_storage_init();
  EXPECT_EQ(flash_storage_host.init_calls, 1u);
  EXPECT_EQ(flash_storage_host.erase_calls, 1u);
  EXPECT_EQ(flash_storage_get_total_runtime_ms(), 123u);
}

TEST_F(FlashStorageTest, UnrecoverableInitErrorDoesNotEraseStorage)
{
  ASSERT_EQ(flash_storage_set_total_runtime_ms(123), ESP_OK);
  flash_storage_host_reset();
  flash_storage_host.init_results[0] = ESP_FAIL;
  flash_storage_init();
  EXPECT_EQ(flash_storage_host.init_calls, 1u);
  EXPECT_EQ(flash_storage_host.erase_calls, 0u);
  EXPECT_EQ(flash_storage_get_total_runtime_ms(), 123u);
}

TEST_F(FlashStorageTest, FailedRetryDoesNotRepeatEraseOrInit)
{
  flash_storage_host_reset();
  flash_storage_host.init_results[0] = ESP_ERR_NVS_NEW_VERSION_FOUND;
  flash_storage_host.init_results[1] = ESP_FAIL;
  flash_storage_init();
  EXPECT_EQ(flash_storage_host.init_calls, 2u);
  EXPECT_EQ(flash_storage_host.erase_calls, 1u);
}

TEST_F(FlashStorageTest, RejectsUndersizedTypedReadBeforeOpeningNvs)
{
  uint64_t value = 0;
  size_t size = sizeof(value) - 1;
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_TOTAL_RUNTIME_MS, &value, &size), ESP_ERR_INVALID_SIZE);
  EXPECT_EQ(size, sizeof(value) - 1);
  EXPECT_EQ(flash_storage_host.open_calls, 0u);
}

TEST_F(FlashStorageTest, OpenFailuresPropagateWithoutClosingInvalidHandles)
{
  flash_storage_host.open_result = ESP_FAIL;
  uint64_t value = 123;
  size_t size = sizeof(value);
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_TOTAL_RUNTIME_MS, &value, &size), ESP_FAIL);
  EXPECT_EQ(value, 123u);
  EXPECT_EQ(flash_storage_set_total_runtime_ms(456), ESP_FAIL);
  EXPECT_EQ(flash_storage_host.open_calls, 2u);
  EXPECT_EQ(flash_storage_host.close_calls, 0u);
  EXPECT_EQ(flash_storage_host.commit_calls, 0u);
}

TEST_F(FlashStorageTest, WriteFailuresSkipCommitAndCloseHandlesForEveryType)
{
  flash_storage_host.set_result = ESP_FAIL;
  const uint8_t blob[] = {1, 2};
  EXPECT_EQ(flash_storage_set_telemetry_enabled(true), ESP_FAIL);
  EXPECT_EQ(flash_storage_set_total_runtime_ms(123), ESP_FAIL);
  EXPECT_EQ(flash_storage_set(FLASH_STORAGE_ITEM_ROBOT_STATUS, blob, sizeof(blob)), ESP_FAIL);
  EXPECT_EQ(flash_storage_host.open_calls, 3u);
  EXPECT_EQ(flash_storage_host.close_calls, 3u);
  EXPECT_EQ(flash_storage_host.commit_calls, 0u);
}

TEST_F(FlashStorageTest, CommitFailurePropagatesAndClosesHandle)
{
  flash_storage_host.commit_result = ESP_FAIL;
  EXPECT_EQ(flash_storage_set_total_runtime_ms(123), ESP_FAIL);
  EXPECT_EQ(flash_storage_host.commit_calls, 1u);
  EXPECT_EQ(flash_storage_host.close_calls, 1u);
}

TEST_F(FlashStorageTest, EnabledTelemetryAndOversizedTypedReadRoundTrip)
{
  ASSERT_EQ(flash_storage_set_telemetry_enabled(true), ESP_OK);
  EXPECT_TRUE(flash_storage_get_telemetry_enabled(false));
  uint8_t data[8]{};
  size_t size = sizeof(data);
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_TELEMETRY_ENABLED, data, &size), ESP_OK);
  EXPECT_EQ(size, 1u);
  EXPECT_EQ(data[0], 1u);
}

TEST_F(FlashStorageTest, FailedBlobReadClosesHandleAndPreservesBuffer)
{
  const uint8_t blob[] = {1, 2, 3};
  ASSERT_EQ(flash_storage_set(FLASH_STORAGE_ITEM_BATTERY_MONITOR, blob, sizeof(blob)), ESP_OK);
  flash_storage_host_reset();
  uint8_t value = 99;
  size_t size = sizeof(value);
  EXPECT_EQ(flash_storage_get(FLASH_STORAGE_ITEM_BATTERY_MONITOR, &value, &size), ESP_ERR_INVALID_SIZE);
  EXPECT_EQ(value, 99u);
  EXPECT_EQ(flash_storage_host.close_calls, 1u);
}

} // namespace
