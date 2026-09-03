#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/serial/hal_serial.h>
#include <hal/spi/hal_spi.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>
#include <hal/storage/hal_littlefs.h>
#include <hal/storage/hal_sdlogger.h>
#include <hal/system/hal_system.h>
#include <stdio.h>
#include <string.h>

#if HAL_TARGET_IS_RP
#define SD_MISO_PIN 16u
#define SD_MOSI_PIN 19u
#define SD_SCK_PIN 18u
#define SD_CS_PIN 17u
#elif HAL_TARGET_IS_STM32G474
#define SD_MISO_PIN 6u
#define SD_MOSI_PIN 7u
#define SD_SCK_PIN 5u
#define SD_CS_PIN 22u
#else
#define SD_MISO_PIN 6u
#define SD_MOSI_PIN 7u
#define SD_SCK_PIN 5u
#define SD_CS_PIN 4u
#endif

#if HAL_TARGET_IS_RP
#define EEPROM_SIZE_BYTES 0u
#define KV_SIZE_BYTES HAL_RP_FLASH_EEPROM_SIZE
#elif HAL_TARGET_IS_STM32G474
#define EEPROM_SIZE_BYTES 0u
#define KV_SIZE_BYTES HAL_STM32_FLASH_EEPROM_SIZE
#else
#define EEPROM_SIZE_BYTES 8192u
#define KV_SIZE_BYTES 8192u
#endif
#define KV_BASE_ADDR 0u
#define KV_KEY_BOOT_COUNT 1u
#define KV_KEY_DEVICE_NAME 2u
#define LITTLEFS_MARKER_PATH "/hal_marker.txt"
#define LITTLEFS_REPORT_PERIOD_MS 5000u
#define KV_REPORT_PERIOD_MS 10000u
#define SD_LOG_PERIOD_MS 1000u
#define SD_RETRY_PERIOD_MS 5000u
#define SD_CRASH_RETRY_PERIOD_MS 5000u

static bool s_eeprom_ready = false;
static bool s_kv_ready = false;
static bool s_littlefs_ready = false;
static bool s_spi_ready = false;
static bool s_logger_ready = false;
static bool s_boot_report_written = false;
static uint32_t s_last_littlefs_report_ms = 0u;
static uint32_t s_last_kv_report_ms = 0u;
static uint32_t s_last_sd_log_ms = 0u;
static uint32_t s_last_sd_retry_ms = 0u;
static uint32_t s_last_crash_retry_ms = 0u;
static uint32_t s_sd_sample = 0u;

static void init_eeprom_and_kv(void) {
  hal_status_t status =
      hal_eeprom_init(HAL_EEPROM_FLASH, EEPROM_SIZE_BYTES, 0u);
  s_eeprom_ready = status == HAL_OK;
  if (!s_eeprom_ready) {
    derr("Flash EEPROM unavailable: %s", hal_status_to_string(status));
    return;
  }

  status = hal_kv_init_ex(KV_BASE_ADDR, KV_SIZE_BYTES);
  s_kv_ready = status == HAL_OK;
  if (!s_kv_ready) {
    derr("KV store unavailable: %s", hal_status_to_string(status));
    return;
  }

  status = hal_kv_set_auto_commit(false);
  if (status != HAL_OK) {
    derr("KV deferred mode failed: %s", hal_status_to_string(status));
  }

  uint32_t boot_count = 0u;
  status = hal_kv_get_u32_ex(KV_KEY_BOOT_COUNT, &boot_count);
  if (status != HAL_OK && status != HAL_ENOENT) {
    derr("KV boot count read failed: %s", hal_status_to_string(status));
  }
  boot_count++;

  const char device_name[] = "JaszczurHAL storage example";
  status = hal_kv_set_u32_ex(KV_KEY_BOOT_COUNT, boot_count);
  if (status == HAL_OK) {
    status =
        hal_kv_set_blob_ex(KV_KEY_DEVICE_NAME, (const uint8_t *)device_name,
                           (uint16_t)sizeof(device_name));
  }
  if (status == HAL_OK) {
    status = hal_kv_commit_ex();
  }
  if (status == HAL_OK) {
    char name_readback[sizeof(device_name)] = {0};
    uint16_t name_length = 0u;
    status = hal_kv_get_blob_ex(KV_KEY_DEVICE_NAME, (uint8_t *)name_readback,
                                (uint16_t)sizeof(name_readback), &name_length);
    if (status == HAL_OK && name_length == sizeof(device_name) &&
        memcmp(name_readback, device_name, sizeof(device_name)) == 0) {
      deb("KV store ready, boot count=%lu name=%s", (unsigned long)boot_count,
          name_readback);
      return;
    }
    if (status == HAL_OK) {
      status = HAL_EIO;
    }
  }
  derr("KV update/readback failed: %s", hal_status_to_string(status));
}

static void exercise_littlefs_marker(void) {
  const hal_status_t exists_status =
      hal_littlefs_exists_ex(LITTLEFS_MARKER_PATH);
  if (exists_status == HAL_ENOENT) {
    deb("LittleFS marker is not present");
    return;
  }
  if (exists_status != HAL_OK) {
    derr("LittleFS marker query failed: %s",
         hal_status_to_string(exists_status));
    return;
  }

  const hal_status_t remove_status =
      hal_littlefs_remove_ex(LITTLEFS_MARKER_PATH);
  if (remove_status == HAL_OK) {
    deb("LittleFS marker removed");
  } else {
    derr("LittleFS marker removal failed: %s",
         hal_status_to_string(remove_status));
  }
}

static void init_littlefs(void) {
  hal_status_t status = hal_littlefs_begin_ex();
  if (status != HAL_OK && EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT != 0) {
    derr("LittleFS mount failed: %s; explicit format opt-in is enabled",
         hal_status_to_string(status));
    const hal_status_t format_status = hal_littlefs_format_ex();
    if (format_status == HAL_OK) {
      status = hal_littlefs_begin_ex();
    } else {
      status = format_status;
    }
  }

  s_littlefs_ready = status == HAL_OK;
  if (s_littlefs_ready) {
    deb("LittleFS ready");
    exercise_littlefs_marker();
  } else {
    derr("LittleFS unavailable: %s", hal_status_to_string(status));
    if (EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT == 0) {
      derr("LittleFS was preserved; set "
           "EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT=1 to permit formatting");
    }
  }
}

static void write_boot_crash_report(uint32_t now) {
  if (s_boot_report_written) {
    return;
  }
  s_last_crash_retry_ms = now;

  hal_status_t status = hal_sdlogger_crash_init_ex("boot", (int)SD_CS_PIN);
  if (status == HAL_OK) {
    status = hal_sdlogger_crash_report("boot ms=%lu heap=%lu",
                                       (unsigned long)hal_millis(),
                                       (unsigned long)hal_get_free_heap());
  }

  if (hal_sdlogger_crash_is_initialized()) {
    const hal_status_t close_status = hal_sdlogger_crash_close();
    if (status == HAL_OK) {
      status = close_status;
    } else if (close_status != HAL_OK) {
      derr("SDLogger crash cleanup failed: %s",
           hal_status_to_string(close_status));
    }
  }

  if (status == HAL_OK) {
    s_boot_report_written = true;
    deb("SDLogger boot report closed");
  } else {
    derr("SDLogger boot report failed: %s; retrying later",
         hal_status_to_string(status));
  }
}

static void start_sdlogger(uint32_t now) {
  if (!s_eeprom_ready || !s_spi_ready) {
    return;
  }

  const int next_log_number = hal_sdlogger_get_log_number();
  const hal_status_t status = hal_sdlogger_init_ex((int)SD_CS_PIN);
  s_logger_ready = status == HAL_OK;
  if (!s_logger_ready) {
    derr("SDLogger unavailable: %s", hal_status_to_string(status));
    return;
  }

  deb("SDLogger ready, next log number was %d", next_log_number);
  write_boot_crash_report(now);
}

static void schedule_sdlogger_retry(uint32_t now) {
  if (hal_sdlogger_is_initialized()) {
    const hal_status_t close_status = hal_sdlogger_close();
    if (close_status != HAL_OK) {
      derr("SDLogger close during recovery failed: %s",
           hal_status_to_string(close_status));
    }
  }
  s_logger_ready = false;
  s_last_sd_retry_ms = now;
}

static void service_littlefs(uint32_t now) {
  if (!s_littlefs_ready ||
      (uint32_t)(now - s_last_littlefs_report_ms) < LITTLEFS_REPORT_PERIOD_MS) {
    return;
  }
  s_last_littlefs_report_ms = now;

  size_t total_bytes = 0u;
  size_t used_bytes = 0u;
  hal_status_t status = hal_littlefs_total_bytes_ex(&total_bytes);
  if (status == HAL_OK) {
    status = hal_littlefs_used_bytes_ex(&used_bytes);
  }
  if (status == HAL_OK) {
    deb("LittleFS: used=%lu total=%lu", (unsigned long)used_bytes,
        (unsigned long)total_bytes);
  } else {
    derr("LittleFS query failed: %s", hal_status_to_string(status));
  }
}

static void service_kv(uint32_t now) {
  if (!s_kv_ready ||
      (uint32_t)(now - s_last_kv_report_ms) < KV_REPORT_PERIOD_MS) {
    return;
  }
  s_last_kv_report_ms = now;

  hal_kv_stats_t stats = {0};
  const hal_status_t status = hal_kv_get_stats_ex(&stats);
  if (status == HAL_OK) {
    deb("KV: keys=%u used=%u/%u generation=%lu", (unsigned)stats.key_count,
        (unsigned)stats.used_bytes, (unsigned)stats.capacity_bytes,
        (unsigned long)stats.generation);
  } else {
    derr("KV stats failed: %s", hal_status_to_string(status));
  }
}

static void service_sdlogger(uint32_t now) {
  if (!s_logger_ready) {
    if ((uint32_t)(now - s_last_sd_retry_ms) >= SD_RETRY_PERIOD_MS) {
      s_last_sd_retry_ms = now;
      start_sdlogger(now);
    }
    return;
  }

  if (!s_boot_report_written &&
      (uint32_t)(now - s_last_crash_retry_ms) >= SD_CRASH_RETRY_PERIOD_MS) {
    write_boot_crash_report(now);
  }

  if ((uint32_t)(now - s_last_sd_log_ms) < SD_LOG_PERIOD_MS) {
    return;
  }
  s_last_sd_log_ms = now;

  char line[96] = {0};
  snprintf(line, sizeof(line), "sample=%lu ms=%lu heap=%lu",
           (unsigned long)s_sd_sample++, (unsigned long)now,
           (unsigned long)hal_get_free_heap());
  const hal_status_t status = hal_sdlogger_append(line);
  if (status == HAL_OK) {
    deb("SDLogger append: %s", line);
  } else {
    derr("SDLogger append failed: %s", hal_status_to_string(status));
    schedule_sdlogger_retry(now);
  }
}

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL storage ===");

  init_eeprom_and_kv();
  init_littlefs();

  const hal_status_t spi_status =
      hal_spi_init(0u, SD_MISO_PIN, SD_MOSI_PIN, SD_SCK_PIN);
  s_spi_ready = spi_status == HAL_OK;
  if (!s_spi_ready) {
    derr("SD SPI unavailable: %s", hal_status_to_string(spi_status));
  }

  s_last_sd_retry_ms = hal_millis();
  start_sdlogger(s_last_sd_retry_ms);
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  service_kv(now);
  service_littlefs(now);
  service_sdlogger(now);
  hal_delay_ms(20u);
}
