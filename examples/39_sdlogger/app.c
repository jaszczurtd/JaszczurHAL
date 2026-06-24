/**
 * @file app.c
 * @brief Portable SDLogger example over JaszczurHAL SPI + FatFs.
 */

#include <hal/hal_app.h>
#include <hal/hal_eeprom.h>
#include <hal/hal_sdlogger.h>
#include <hal/hal_spi.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#include <stdio.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_SD_MISO 16u
#define EXAMPLE_SD_MOSI 19u
#define EXAMPLE_SD_SCK 18u
#define EXAMPLE_SD_CS 17u
#else
#define EXAMPLE_SD_MISO 6u
#define EXAMPLE_SD_MOSI 7u
#define EXAMPLE_SD_SCK 5u
#define EXAMPLE_SD_CS 4u
#endif

#define EXAMPLE_EEPROM_SIZE 512u
#define EXAMPLE_LOG_PERIOD_MS 1000u
#define EXAMPLE_RETRY_PERIOD_MS 5000u

static bool s_logger_ready = false;
static bool s_boot_report_written = false;
static uint32_t s_last_log_ms = 0u;
static uint32_t s_last_retry_ms = 0u;
static uint32_t s_sample = 0u;

static bool start_sdlogger(void) {
  const int next_log_number = hal_sdlogger_get_log_number();
  if (!hal_sdlogger_init((int)EXAMPLE_SD_CS)) {
    derr("SDLogger init failed, retrying");
    return false;
  }

  deb("SDLogger ready, opened log%05lu.txt",
      (unsigned long)((next_log_number < 0)
                          ? 0u
                          : ((uint32_t)next_log_number % 100000u)));
  return true;
}

static void write_boot_crash_report(void) {
  if (s_boot_report_written) {
    return;
  }
  s_boot_report_written = true;

  if (!hal_sdlogger_crash_init("boot", (int)EXAMPLE_SD_CS)) {
    derr("SDLogger crash report init failed");
    return;
  }

  hal_sdlogger_crash_report("boot ms=%lu heap=%lu", (unsigned long)hal_millis(),
                            (unsigned long)hal_get_free_heap());
  hal_sdlogger_crash_close();
  deb("SDLogger boot crash report closed");
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL SDLogger example ===");
  deb("SPI0 pins: MISO=%u MOSI=%u SCK=%u CS=%u", (unsigned)EXAMPLE_SD_MISO,
      (unsigned)EXAMPLE_SD_MOSI, (unsigned)EXAMPLE_SD_SCK,
      (unsigned)EXAMPLE_SD_CS);

  hal_eeprom_init(HAL_EEPROM_FLASH, EXAMPLE_EEPROM_SIZE, 0u);
  hal_spi_init(0u, EXAMPLE_SD_MISO, EXAMPLE_SD_MOSI, EXAMPLE_SD_SCK);

  s_last_retry_ms = hal_millis();
  s_logger_ready = start_sdlogger();
  if (s_logger_ready) {
    write_boot_crash_report();
  }
}

void app_task0(void) {
  const uint32_t now = hal_millis();

  if (!s_logger_ready) {
    if ((uint32_t)(now - s_last_retry_ms) >= EXAMPLE_RETRY_PERIOD_MS) {
      s_last_retry_ms = now;
      s_logger_ready = start_sdlogger();
      if (s_logger_ready) {
        write_boot_crash_report();
      }
    }
    hal_delay_ms(50u);
    return;
  }

  if ((uint32_t)(now - s_last_log_ms) >= EXAMPLE_LOG_PERIOD_MS) {
    s_last_log_ms = now;

    char line[96] = {};
    snprintf(line, sizeof(line), "sample=%lu ms=%lu heap=%lu",
             (unsigned long)s_sample, (unsigned long)now,
             (unsigned long)hal_get_free_heap());
    hal_sdlogger_append(line);
    deb("SDLogger append: %s", line);
    s_sample++;
  }

  hal_delay_ms(20u);
}
