#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK || HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_SDLOGGER

#include "hal/storage/hal_eeprom.h"
#include "hal/storage/hal_sdlogger.h"
#include "hal/storage/hal_sdlogger_internal.h"

#include <stdio.h>

static unsigned bounded_filename_number(int number, unsigned modulo) {
  return number < 0 || modulo == 0u ? 0u : (unsigned)number % modulo;
}

void jh_sdlogger_make_log_filename(char *dst, size_t dst_size, int log_number) {
  if (dst != nullptr && dst_size > 0u) {
    snprintf(dst, dst_size, "log%05u.txt",
             bounded_filename_number(log_number, 100000u));
  }
}

void jh_sdlogger_make_crash_filename(char *dst, size_t dst_size,
                                     int crash_number) {
  if (dst != nullptr && dst_size > 0u) {
    snprintf(dst, dst_size, "wd%06u.txt",
             bounded_filename_number(crash_number, 1000000u));
  }
}

static void make_crash_tag_line(char *dst, size_t dst_size, const char *tag) {
  if (dst == nullptr || dst_size == 0u) {
    return;
  }
  dst[0] = '\0';
  if (tag == nullptr || tag[0] == '\0') {
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

hal_status_t jh_sdlogger_append_crash_context(const char *tag) {
  hal_status_t status = HAL_OK;
  if (tag != nullptr && tag[0] != '\0') {
    char tag_line[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
    make_crash_tag_line(tag_line, sizeof(tag_line), tag);
    status = hal_sdlogger_crash_append(tag_line);
  }
  if (status != HAL_OK) {
    return status;
  }

  char log_name[sizeof("log00000.txt")] = {};
  jh_sdlogger_make_log_filename(log_name, sizeof(log_name),
                                hal_sdlogger_get_log_number() - 1);
  char line[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
  snprintf(line, sizeof(line), "corresponded log file: %s", log_name);
  return hal_sdlogger_crash_append(line);
}

#endif
#endif
