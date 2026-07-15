#include "hal/hal_target.h"
#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474

#include "hal/hal_config.h"

#ifdef HAL_ENABLE_SDLOGGER

#include "../sd/hal_sd_file.h"
#include "hal/hal_eeprom.h"
#include "hal/hal_sdlogger.h"
#include "hal/hal_serial.h"
#include "hal/hal_sync.h"
#include "hal/hal_system.h"
#include "hal/impl/shared/hal_mutex_once.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef HAL_SDLOGGER_SPI_BUS
#define HAL_SDLOGGER_SPI_BUS 0u
#endif

static hal_mutex_t s_sdlogger_mutex = NULL;
static bool s_log_initialized = false;
static bool s_crash_initialized = false;
static bool s_sd_started = false;
static hal_sd_file_t s_log_file = {};
static hal_sd_file_t s_crash_file = {};
static uint32_t s_last_write_time = 0;
static char s_log_buffer[HAL_SDLOGGER_LOG_BUFFER_SIZE];
static int s_log_buf_pos = 0;

static hal_status_t sdlogger_ensure_mutex(void) {
  return jh_hal_mutex_create_once(&s_sdlogger_mutex) ? HAL_OK : HAL_ENOMEM;
}

static hal_status_t ensure_sd_started_locked(int cs) {
  if (!s_sd_started) {
    s_sd_started =
        hal_sd_file_begin((uint8_t)HAL_SDLOGGER_SPI_BUS, (uint8_t)cs);
  }
  return s_sd_started ? HAL_OK : HAL_EBUS;
}

static void clear_log_buffer_locked(void) {
  s_log_buf_pos = 0;
  s_log_buffer[0] = '\0';
}

static hal_status_t flush_log_buffer_locked(void) {
  if (s_log_buf_pos <= 0) {
    return HAL_OK;
  }

  const size_t bytes_to_write = (size_t)s_log_buf_pos;
  const size_t bytes_written = hal_sd_file_print(&s_log_file, s_log_buffer);
  if (bytes_written < bytes_to_write) {
    return HAL_EIO;
  }
  if (!hal_sd_file_flush(&s_log_file)) {
    return HAL_EIO;
  }
  clear_log_buffer_locked();
  return HAL_OK;
}

static unsigned sdlogger_bounded_filename_number(int number, unsigned modulo) {
  if (number < 0) {
    return 0u;
  }
  if (modulo == 0u) {
    return 0u;
  }
  return (unsigned)number % modulo;
}

static void sdlogger_make_log_filename(char *dst, size_t dst_size,
                                       int log_number) {
  if (dst == NULL || dst_size == 0u) {
    return;
  }

  /* FF_USE_LFN is disabled, so keep log files in strict 8.3 form. */
  snprintf(dst, dst_size, "log%05u.txt",
           sdlogger_bounded_filename_number(log_number, 100000u));
}

static void sdlogger_make_crash_filename(char *dst, size_t dst_size,
                                         int crash_number) {
  if (dst == NULL || dst_size == 0u) {
    return;
  }

  /* FF_USE_LFN is disabled, so keep crash reports in strict 8.3 form. */
  snprintf(dst, dst_size, "wd%06u.txt",
           sdlogger_bounded_filename_number(crash_number, 1000000u));
}

static void sdlogger_make_crash_tag_line(char *dst, size_t dst_size,
                                         const char *tag) {
  if (dst == NULL || dst_size == 0u) {
    return;
  }

  dst[0] = '\0';
  if (tag == NULL || tag[0] == '\0') {
    return;
  }

  const char prefix[] = "crash tag: ";
  size_t pos = 0u;
  for (size_t i = 0u; prefix[i] != '\0' && pos + 1u < dst_size; ++i) {
    dst[pos++] = prefix[i];
  }
  for (size_t i = 0u; tag[i] != '\0' && pos + 1u < dst_size; ++i) {
    dst[pos++] = tag[i];
  }
  dst[pos] = '\0';
}

int hal_sdlogger_get_log_number(void) {
  return hal_eeprom_read_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR);
}

int hal_sdlogger_get_crash_number(void) {
  return hal_eeprom_read_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR);
}

hal_status_t hal_sdlogger_init_ex(int cs) {
  char name[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
  int log_number = hal_sdlogger_get_log_number();
  sdlogger_make_log_filename(name, sizeof(name), log_number);

  hal_status_t status = sdlogger_ensure_mutex();
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_sdlogger_mutex);

  status = ensure_sd_started_locked(cs);
  if (status != HAL_OK) {
    s_log_initialized = false;
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_init: SD card mount failed");
    return status;
  }

  s_log_initialized =
      hal_sd_file_open(&s_log_file, name, HAL_SD_FILE_WRITE_APPEND);
  if (!s_log_initialized) {
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_init: log file open failed");
    return HAL_EIO;
  }

  status =
      hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, log_number + 1);
  if (status == HAL_OK) {
    status = hal_eeprom_commit();
  }
  if (status != HAL_OK) {
    s_log_initialized = false;
    (void)hal_sd_file_close(&s_log_file);
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_init: EEPROM update failed");
    return status;
  }

  clear_log_buffer_locked();
  s_last_write_time = hal_millis();
  hal_mutex_unlock(s_sdlogger_mutex);
  return HAL_OK;
}

bool hal_sdlogger_init(int cs) {
  return hal_status_to_bool(hal_sdlogger_init_ex(cs));
}

bool hal_sdlogger_is_initialized(void) {
  if (sdlogger_ensure_mutex() != HAL_OK) {
    return false;
  }
  hal_mutex_lock(s_sdlogger_mutex);
  const bool initialized = s_log_initialized;
  hal_mutex_unlock(s_sdlogger_mutex);
  return initialized;
}

hal_status_t hal_sdlogger_append(const char *data) {
  hal_status_t status = sdlogger_ensure_mutex();
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_sdlogger_mutex);

  if (!s_log_initialized) {
    hal_mutex_unlock(s_sdlogger_mutex);
    return HAL_EUNINIT;
  }

  const char *s = (data != NULL) ? data : "";
  const int slen = (int)strlen(s);
  if ((s_log_buf_pos + slen + 1) < (int)sizeof(s_log_buffer)) {
    memcpy(s_log_buffer + s_log_buf_pos, s, (size_t)slen);
    s_log_buf_pos += slen;
    s_log_buffer[s_log_buf_pos++] = '\n';
    s_log_buffer[s_log_buf_pos] = '\0';
  } else {
    hal_mutex_unlock(s_sdlogger_mutex);
    return HAL_EOVERFLOW;
  }

  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_write_time) >=
      (uint32_t)HAL_SDLOGGER_WRITE_INTERVAL_MS) {
    s_last_write_time = now;
    status = flush_log_buffer_locked();
  }

  hal_mutex_unlock(s_sdlogger_mutex);
  return status;
}

hal_status_t hal_sdlogger_close(void) {
  hal_status_t status = sdlogger_ensure_mutex();
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_sdlogger_mutex);

  if (!s_log_initialized) {
    hal_mutex_unlock(s_sdlogger_mutex);
    return HAL_EUNINIT;
  }

  status = flush_log_buffer_locked();
  if (!hal_sd_file_close(&s_log_file) && status == HAL_OK) {
    status = HAL_EIO;
  }
  s_log_initialized = false;

  hal_mutex_unlock(s_sdlogger_mutex);
  return status;
}

hal_status_t hal_sdlogger_crash_init_ex(const char *add_to_name, int cs) {
  char name[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
  int crash_number = hal_sdlogger_get_crash_number();
  sdlogger_make_crash_filename(name, sizeof(name), crash_number);

  hal_status_t status = sdlogger_ensure_mutex();
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_sdlogger_mutex);

  status = ensure_sd_started_locked(cs);
  if (status != HAL_OK) {
    s_crash_initialized = false;
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_crash_init: SD card mount failed");
    return status;
  }

  s_crash_initialized =
      hal_sd_file_open(&s_crash_file, name, HAL_SD_FILE_WRITE_APPEND);
  if (!s_crash_initialized) {
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_crash_init: crash file open failed");
    return HAL_EIO;
  }

  status =
      hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR, crash_number + 1);
  if (status == HAL_OK) {
    status = hal_eeprom_commit();
  }
  if (status != HAL_OK) {
    s_crash_initialized = false;
    (void)hal_sd_file_close(&s_crash_file);
    hal_mutex_unlock(s_sdlogger_mutex);
    hal_serial_println("hal_sdlogger_crash_init: EEPROM update failed");
    return status;
  }
  hal_mutex_unlock(s_sdlogger_mutex);

  if (add_to_name != NULL && add_to_name[0] != '\0') {
    char tag_line[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
    sdlogger_make_crash_tag_line(tag_line, sizeof(tag_line), add_to_name);
    status = hal_sdlogger_crash_append(tag_line);
    if (status != HAL_OK) {
      return status;
    }
  }

  char log_name[sizeof("log00000.txt")] = {};
  sdlogger_make_log_filename(log_name, sizeof(log_name),
                             hal_sdlogger_get_log_number() - 1);
  char line[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
  snprintf(line, sizeof(line), "corresponded log file: %s", log_name);
  return hal_sdlogger_crash_append(line);
}

bool hal_sdlogger_crash_init(const char *add_to_name, int cs) {
  return hal_status_to_bool(hal_sdlogger_crash_init_ex(add_to_name, cs));
}

bool hal_sdlogger_crash_is_initialized(void) {
  if (sdlogger_ensure_mutex() != HAL_OK) {
    return false;
  }
  hal_mutex_lock(s_sdlogger_mutex);
  const bool initialized = s_crash_initialized;
  hal_mutex_unlock(s_sdlogger_mutex);
  return initialized;
}

hal_status_t hal_sdlogger_crash_append(const char *data) {
  hal_status_t status = sdlogger_ensure_mutex();
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_sdlogger_mutex);

  if (!s_crash_initialized) {
    hal_mutex_unlock(s_sdlogger_mutex);
    return HAL_EUNINIT;
  }

  const char *s = (data != NULL) ? data : "";
  const size_t bytes_to_write = strlen(s) + 1u;
  const size_t bytes_written = hal_sd_file_println(&s_crash_file, s);
  if (bytes_written < bytes_to_write) {
    status = HAL_EIO;
  } else if (!hal_sd_file_flush(&s_crash_file)) {
    status = HAL_EIO;
  }

  hal_mutex_unlock(s_sdlogger_mutex);
  return status;
}

hal_status_t hal_sdlogger_crash_close(void) {
  hal_status_t status = sdlogger_ensure_mutex();
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_sdlogger_mutex);

  if (!s_crash_initialized) {
    hal_mutex_unlock(s_sdlogger_mutex);
    return HAL_EUNINIT;
  }

  if (!hal_sd_file_flush(&s_crash_file)) {
    status = HAL_EIO;
  }
  if (!hal_sd_file_close(&s_crash_file) && status == HAL_OK) {
    status = HAL_EIO;
  }
  s_crash_initialized = false;

  hal_mutex_unlock(s_sdlogger_mutex);
  return status;
}

hal_status_t hal_sdlogger_crash_report(const char *format, ...) {
  if (format == NULL) {
    return HAL_EINVAL;
  }

  va_list args;
  va_start(args, format);
  char buffer[128] = {};
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  return hal_sdlogger_crash_append(buffer);
}

#endif /* HAL_ENABLE_SDLOGGER */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
