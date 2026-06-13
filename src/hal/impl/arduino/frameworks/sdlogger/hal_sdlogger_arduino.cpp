#include "../../../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../../../hal_config.h"

#ifdef HAL_ENABLE_SDLOGGER

#include "../../../../hal_eeprom.h"
#include "../../../../hal_sdlogger.h"
#include "../../../../hal_serial.h"
#include "../../../../hal_sync.h"
#include "../../../../hal_system.h"
#include "../../../shared/hal_mutex_once.h"

#include <SD.h>
#include <SPI.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static hal_mutex_t s_sdlogger_mutex = NULL;
static bool s_log_initialized = false;
static bool s_crash_initialized = false;
static bool s_sd_started = false;
static File s_log_file;
static File s_crash_file;
static SPISettings s_spi_settings(1000000, MSBFIRST, SPI_MODE1);
static unsigned long s_last_write_time = 0;
static char s_log_buffer[HAL_SDLOGGER_LOG_BUFFER_SIZE];
static int s_log_buf_pos = 0;

static void sdlogger_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_sdlogger_mutex);
}

static bool ensure_sd_started_locked(int cs) {
  if (!s_sd_started) {
    s_sd_started = SD.begin(cs);
  }
  return s_sd_started;
}

static void clear_log_buffer_locked(void) {
  s_log_buf_pos = 0;
  s_log_buffer[0] = '\0';
}

static void flush_log_buffer_locked(void) {
  if (s_log_buf_pos <= 0) {
    return;
  }
  s_log_file.print(s_log_buffer);
  s_log_file.flush();
  clear_log_buffer_locked();
}

int hal_sdlogger_get_log_number(void) {
  return hal_eeprom_read_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR);
}

int hal_sdlogger_get_crash_number(void) {
  return hal_eeprom_read_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR);
}

bool hal_sdlogger_init(int cs) {
  char name[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
  int log_number = hal_sdlogger_get_log_number();
  snprintf(name, sizeof(name), "log%d.txt", log_number);

  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, log_number + 1);
  hal_eeprom_commit();

  sdlogger_ensure_mutex();
  hal_mutex_lock(s_sdlogger_mutex);

  SPI.beginTransaction(s_spi_settings);
  if (!ensure_sd_started_locked(cs)) {
    s_log_initialized = false;
    SPI.endTransaction();
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_init: SD card mount failed");
    return false;
  }

  s_log_file = SD.open(name, FILE_WRITE);
  s_log_initialized = (bool)s_log_file;
  if (!s_log_initialized) {
    SPI.endTransaction();
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_init: log file open failed");
    return false;
  }

  clear_log_buffer_locked();
  s_last_write_time = hal_millis();
  SPI.endTransaction();
  hal_mutex_unlock(s_sdlogger_mutex);
  return true;
}

bool hal_sdlogger_is_initialized(void) {
  sdlogger_ensure_mutex();
  hal_mutex_lock(s_sdlogger_mutex);
  const bool initialized = s_log_initialized;
  hal_mutex_unlock(s_sdlogger_mutex);
  return initialized;
}

void hal_sdlogger_append(const char *data) {
  sdlogger_ensure_mutex();
  hal_mutex_lock(s_sdlogger_mutex);

  if (!s_log_initialized) {
    hal_mutex_unlock(s_sdlogger_mutex);
    return;
  }

  const char *s = (data != NULL) ? data : "";
  const int slen = (int)strlen(s);
  if ((s_log_buf_pos + slen + 1) < (int)sizeof(s_log_buffer)) {
    memcpy(s_log_buffer + s_log_buf_pos, s, (size_t)slen);
    s_log_buf_pos += slen;
    s_log_buffer[s_log_buf_pos++] = '\n';
    s_log_buffer[s_log_buf_pos] = '\0';
  }

  const unsigned long now = hal_millis();
  if (now - s_last_write_time >= HAL_SDLOGGER_WRITE_INTERVAL_MS) {
    s_last_write_time = now;
    SPI.beginTransaction(s_spi_settings);
    flush_log_buffer_locked();
    SPI.endTransaction();
  }

  hal_mutex_unlock(s_sdlogger_mutex);
}

void hal_sdlogger_close(void) {
  sdlogger_ensure_mutex();
  hal_mutex_lock(s_sdlogger_mutex);

  if (s_log_initialized) {
    s_log_initialized = false;
    SPI.beginTransaction(s_spi_settings);
    flush_log_buffer_locked();
    s_log_file.close();
    SPI.endTransaction();
  }

  hal_mutex_unlock(s_sdlogger_mutex);
}

bool hal_sdlogger_crash_init(const char *add_to_name, int cs) {
  char name[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
  int crash_number = hal_sdlogger_get_crash_number();
  if (add_to_name != NULL && strlen(add_to_name) > 0u) {
    snprintf(name, sizeof(name), "watchdog%d(%s).txt", crash_number,
             add_to_name);
  } else {
    snprintf(name, sizeof(name), "watchdog%d.txt", crash_number);
  }

  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR, crash_number + 1);
  hal_eeprom_commit();

  sdlogger_ensure_mutex();
  hal_mutex_lock(s_sdlogger_mutex);

  SPI.beginTransaction(s_spi_settings);
  if (!ensure_sd_started_locked(cs)) {
    s_crash_initialized = false;
    SPI.endTransaction();
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_crash_init: SD card mount failed");
    return false;
  }

  s_crash_file = SD.open(name, FILE_WRITE);
  s_crash_initialized = (bool)s_crash_file;
  if (!s_crash_initialized) {
    SPI.endTransaction();
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_crash_init: crash file open failed");
    return false;
  }
  SPI.endTransaction();
  hal_mutex_unlock(s_sdlogger_mutex);

  char line[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
  snprintf(line, sizeof(line), "corresponded log file: log%d.txt",
           hal_sdlogger_get_log_number() - 1);
  hal_sdlogger_crash_append(line);
  return true;
}

bool hal_sdlogger_crash_is_initialized(void) {
  sdlogger_ensure_mutex();
  hal_mutex_lock(s_sdlogger_mutex);
  const bool initialized = s_crash_initialized;
  hal_mutex_unlock(s_sdlogger_mutex);
  return initialized;
}

void hal_sdlogger_crash_append(const char *data) {
  sdlogger_ensure_mutex();
  hal_mutex_lock(s_sdlogger_mutex);

  if (!s_crash_initialized) {
    hal_mutex_unlock(s_sdlogger_mutex);
    return;
  }

  SPI.beginTransaction(s_spi_settings);
  s_crash_file.println((data != NULL) ? data : "");
  s_crash_file.flush();
  SPI.endTransaction();

  hal_mutex_unlock(s_sdlogger_mutex);
}

void hal_sdlogger_crash_close(void) {
  sdlogger_ensure_mutex();
  hal_mutex_lock(s_sdlogger_mutex);

  if (s_crash_initialized) {
    s_crash_initialized = false;
    SPI.beginTransaction(s_spi_settings);
    s_crash_file.flush();
    s_crash_file.close();
    SPI.endTransaction();
  }

  hal_mutex_unlock(s_sdlogger_mutex);
}

void hal_sdlogger_crash_report(const char *format, ...) {
  if (format == NULL || !hal_sdlogger_crash_is_initialized()) {
    return;
  }

  va_list args;
  va_start(args, format);
  char buffer[128] = {};
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  hal_sdlogger_crash_append(buffer);
}

#endif /* HAL_ENABLE_SDLOGGER */
#endif // HAL_TARGET_IS_RP2040
