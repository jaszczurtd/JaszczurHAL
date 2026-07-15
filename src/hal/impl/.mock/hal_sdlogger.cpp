#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"

#ifdef HAL_ENABLE_SDLOGGER

#include "../../hal_eeprom.h"
#include "../../hal_sdlogger.h"
#include "../../hal_serial.h"
#include "../../hal_system.h"
#include "hal_mock.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MOCK_SDLOGGER_CONTENT_SIZE 4096u

static bool s_sd_begin_result = true;
static bool s_log_open_result = true;
static bool s_crash_open_result = true;

static bool s_sd_started = false;
static bool s_log_initialized = false;
static bool s_crash_initialized = false;
static bool s_log_closed = false;
static bool s_crash_closed = false;

static uint32_t s_log_flush_count = 0;
static uint32_t s_crash_flush_count = 0;
static uint32_t s_sd_begin_count = 0;

static uint32_t s_last_write_time = 0;
static char s_log_buffer[HAL_SDLOGGER_LOG_BUFFER_SIZE];
static int s_log_buf_pos = 0;

static char s_log_filename[HAL_SDLOGGER_NAME_BUFFER_SIZE];
static char s_crash_filename[HAL_SDLOGGER_NAME_BUFFER_SIZE];
static char s_log_content[MOCK_SDLOGGER_CONTENT_SIZE];
static char s_crash_content[MOCK_SDLOGGER_CONTENT_SIZE];

static hal_status_t append_to(char *dst, size_t dst_size, const char *src) {
  if (dst == NULL || dst_size == 0u || src == NULL) {
    return HAL_EINVAL;
  }
  const size_t used = strlen(dst);
  if (used >= dst_size - 1u) {
    return HAL_EOVERFLOW;
  }
  if (strlen(src) >= dst_size - used) {
    return HAL_EOVERFLOW;
  }
  snprintf(dst + used, dst_size - used, "%s", src);
  return HAL_OK;
}

static hal_status_t flush_log_buffer(void) {
  if (s_log_buf_pos <= 0) {
    return HAL_OK;
  }
  hal_status_t status =
      append_to(s_log_content, sizeof(s_log_content), s_log_buffer);
  if (status != HAL_OK) {
    return status;
  }
  s_log_buffer[0] = '\0';
  s_log_buf_pos = 0;
  s_log_flush_count++;
  return HAL_OK;
}

static hal_status_t append_line_to_log_buffer(const char *data) {
  const char *s = (data != NULL) ? data : "";
  const int slen = (int)strlen(s);
  if ((s_log_buf_pos + slen + 1) < (int)sizeof(s_log_buffer)) {
    memcpy(s_log_buffer + s_log_buf_pos, s, (size_t)slen);
    s_log_buf_pos += slen;
    s_log_buffer[s_log_buf_pos++] = '\n';
    s_log_buffer[s_log_buf_pos] = '\0';
    return HAL_OK;
  }
  return HAL_EOVERFLOW;
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

void hal_mock_sdlogger_reset(void) {
  s_sd_begin_result = true;
  s_log_open_result = true;
  s_crash_open_result = true;
  s_sd_started = false;
  s_log_initialized = false;
  s_crash_initialized = false;
  s_log_closed = false;
  s_crash_closed = false;
  s_log_flush_count = 0;
  s_crash_flush_count = 0;
  s_sd_begin_count = 0;
  s_last_write_time = 0;
  s_log_buf_pos = 0;
  s_log_buffer[0] = '\0';
  s_log_filename[0] = '\0';
  s_crash_filename[0] = '\0';
  s_log_content[0] = '\0';
  s_crash_content[0] = '\0';
}

void hal_mock_sdlogger_set_sd_begin_result(bool result) {
  s_sd_begin_result = result;
}

void hal_mock_sdlogger_set_log_open_result(bool result) {
  s_log_open_result = result;
}

void hal_mock_sdlogger_set_crash_open_result(bool result) {
  s_crash_open_result = result;
}

const char *hal_mock_sdlogger_log_filename(void) { return s_log_filename; }

const char *hal_mock_sdlogger_crash_filename(void) { return s_crash_filename; }

const char *hal_mock_sdlogger_log_content(void) { return s_log_content; }

const char *hal_mock_sdlogger_crash_content(void) { return s_crash_content; }

uint32_t hal_mock_sdlogger_log_flush_count(void) { return s_log_flush_count; }

uint32_t hal_mock_sdlogger_crash_flush_count(void) {
  return s_crash_flush_count;
}

uint32_t hal_mock_sdlogger_sd_begin_count(void) { return s_sd_begin_count; }

bool hal_mock_sdlogger_log_was_closed(void) { return s_log_closed; }

bool hal_mock_sdlogger_crash_was_closed(void) { return s_crash_closed; }

int hal_sdlogger_get_log_number(void) {
  return hal_eeprom_read_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR);
}

int hal_sdlogger_get_crash_number(void) {
  return hal_eeprom_read_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR);
}

hal_status_t hal_sdlogger_init_ex(int cs) {
  (void)cs;

  char name[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
  int log_number = hal_sdlogger_get_log_number();
  sdlogger_make_log_filename(name, sizeof(name), log_number);

  if (!s_sd_started) {
    s_sd_begin_count++;
    s_sd_started = s_sd_begin_result;
  }

  if (!s_sd_started) {
    s_log_initialized = false;
    hal_serial_println("hal_sdlogger_init: SD card mount failed");
    return HAL_EBUS;
  }

  s_log_initialized = s_log_open_result;
  if (s_log_initialized) {
    hal_status_t status =
        hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, log_number + 1);
    if (status == HAL_OK) {
      status = hal_eeprom_commit();
    }
    if (status != HAL_OK) {
      s_log_initialized = false;
      hal_serial_println("hal_sdlogger_init: EEPROM update failed");
      return status;
    }
    snprintf(s_log_filename, sizeof(s_log_filename), "%s", name);
    s_log_closed = false;
  } else {
    hal_serial_println("hal_sdlogger_init: log file open failed");
    return HAL_EIO;
  }
  return HAL_OK;
}

bool hal_sdlogger_init(int cs) {
  return hal_status_to_bool(hal_sdlogger_init_ex(cs));
}

bool hal_sdlogger_is_initialized(void) { return s_log_initialized; }

hal_status_t hal_sdlogger_append(const char *data) {
  if (!s_log_initialized) {
    return HAL_EUNINIT;
  }

  hal_status_t status = append_line_to_log_buffer(data);
  if (status != HAL_OK) {
    return status;
  }
  uint32_t now = hal_millis();
  if (now - s_last_write_time >= HAL_SDLOGGER_WRITE_INTERVAL_MS) {
    s_last_write_time = now;
    status = flush_log_buffer();
  }
  return status;
}

hal_status_t hal_sdlogger_close(void) {
  if (!s_log_initialized) {
    return HAL_EUNINIT;
  }
  hal_status_t status = flush_log_buffer();
  s_log_initialized = false;
  s_log_closed = true;
  return status;
}

hal_status_t hal_sdlogger_crash_init_ex(const char *add_to_name, int cs) {
  (void)cs;

  char name[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
  int crash_number = hal_sdlogger_get_crash_number();
  sdlogger_make_crash_filename(name, sizeof(name), crash_number);

  if (!s_sd_started) {
    s_sd_begin_count++;
    s_sd_started = s_sd_begin_result;
  }

  if (!s_sd_started) {
    s_crash_initialized = false;
    hal_serial_println("hal_sdlogger_crash_init: SD card mount failed");
    return HAL_EBUS;
  }

  s_crash_initialized = s_crash_open_result;
  if (s_crash_initialized) {
    hal_status_t status =
        hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR, crash_number + 1);
    if (status == HAL_OK) {
      status = hal_eeprom_commit();
    }
    if (status != HAL_OK) {
      s_crash_initialized = false;
      hal_serial_println("hal_sdlogger_crash_init: EEPROM update failed");
      return status;
    }
    snprintf(s_crash_filename, sizeof(s_crash_filename), "%s", name);
    s_crash_closed = false;

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
    status = hal_sdlogger_crash_append(line);
    if (status != HAL_OK) {
      return status;
    }
  } else {
    hal_serial_println("hal_sdlogger_crash_init: crash logger open failed");
    return HAL_EIO;
  }
  return HAL_OK;
}

bool hal_sdlogger_crash_init(const char *add_to_name, int cs) {
  return hal_status_to_bool(hal_sdlogger_crash_init_ex(add_to_name, cs));
}

bool hal_sdlogger_crash_is_initialized(void) { return s_crash_initialized; }

hal_status_t hal_sdlogger_crash_append(const char *data) {
  if (!s_crash_initialized) {
    return HAL_EUNINIT;
  }
  hal_status_t status = append_to(s_crash_content, sizeof(s_crash_content),
                                  (data != NULL) ? data : "");
  if (status != HAL_OK) {
    return status;
  }
  status = append_to(s_crash_content, sizeof(s_crash_content), "\n");
  if (status != HAL_OK) {
    return status;
  }
  s_crash_flush_count++;
  return HAL_OK;
}

hal_status_t hal_sdlogger_crash_close(void) {
  if (!s_crash_initialized) {
    return HAL_EUNINIT;
  }
  s_crash_initialized = false;
  s_crash_flush_count++;
  s_crash_closed = true;
  return HAL_OK;
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
#endif // HAL_TARGET_IS_MOCK
