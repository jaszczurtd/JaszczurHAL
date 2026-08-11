#ifndef JH_HAL_SDLOGGER_INTERNAL_H
#define JH_HAL_SDLOGGER_INTERNAL_H

#include "hal/core/hal_status.h"

#include <stddef.h>

void jh_sdlogger_make_log_filename(char *dst, size_t dst_size, int log_number);
void jh_sdlogger_make_crash_filename(char *dst, size_t dst_size,
                                     int crash_number);
hal_status_t jh_sdlogger_append_crash_context(const char *tag);

#endif
