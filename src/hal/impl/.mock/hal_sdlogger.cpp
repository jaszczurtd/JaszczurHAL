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

static void append_to(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0u || src == NULL) {
        return;
    }
    const size_t used = strlen(dst);
    if (used >= dst_size - 1u) {
        return;
    }
    snprintf(dst + used, dst_size - used, "%s", src);
}

static void flush_log_buffer(void) {
    if (s_log_buf_pos <= 0) {
        return;
    }
    append_to(s_log_content, sizeof(s_log_content), s_log_buffer);
    s_log_buffer[0] = '\0';
    s_log_buf_pos = 0;
    s_log_flush_count++;
}

static void append_line_to_log_buffer(const char *data) {
    const char *s = (data != NULL) ? data : "";
    const int slen = (int)strlen(s);
    if ((s_log_buf_pos + slen + 1) < (int)sizeof(s_log_buffer)) {
        memcpy(s_log_buffer + s_log_buf_pos, s, (size_t)slen);
        s_log_buf_pos += slen;
        s_log_buffer[s_log_buf_pos++] = '\n';
        s_log_buffer[s_log_buf_pos] = '\0';
    }
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

const char *hal_mock_sdlogger_log_filename(void) {
    return s_log_filename;
}

const char *hal_mock_sdlogger_crash_filename(void) {
    return s_crash_filename;
}

const char *hal_mock_sdlogger_log_content(void) {
    return s_log_content;
}

const char *hal_mock_sdlogger_crash_content(void) {
    return s_crash_content;
}

uint32_t hal_mock_sdlogger_log_flush_count(void) {
    return s_log_flush_count;
}

uint32_t hal_mock_sdlogger_crash_flush_count(void) {
    return s_crash_flush_count;
}

uint32_t hal_mock_sdlogger_sd_begin_count(void) {
    return s_sd_begin_count;
}

bool hal_mock_sdlogger_log_was_closed(void) {
    return s_log_closed;
}

bool hal_mock_sdlogger_crash_was_closed(void) {
    return s_crash_closed;
}

int hal_sdlogger_get_log_number(void) {
    return hal_eeprom_read_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR);
}

int hal_sdlogger_get_crash_number(void) {
    return hal_eeprom_read_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR);
}

bool hal_sdlogger_init(int cs) {
    (void)cs;

    char name[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
    int log_number = hal_sdlogger_get_log_number();
    snprintf(name, sizeof(name), "log%d.txt", log_number);
    hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, log_number + 1);
    hal_eeprom_commit();

    if (!s_sd_started) {
        s_sd_begin_count++;
        s_sd_started = s_sd_begin_result;
    }

    s_log_initialized = s_sd_started && s_log_open_result;
    if (s_log_initialized) {
        snprintf(s_log_filename, sizeof(s_log_filename), "%s", name);
        s_log_closed = false;
    } else {
        hal_serial_println("hal_sdlogger_init: SD logger open failed");
    }
    return s_log_initialized;
}

bool hal_sdlogger_is_initialized(void) {
    return s_log_initialized;
}

void hal_sdlogger_append(const char *data) {
    if (!s_log_initialized) {
        return;
    }

    append_line_to_log_buffer(data);
    uint32_t now = hal_millis();
    if (now - s_last_write_time >= HAL_SDLOGGER_WRITE_INTERVAL_MS) {
        s_last_write_time = now;
        flush_log_buffer();
    }
}

void hal_sdlogger_close(void) {
    if (!s_log_initialized) {
        return;
    }
    s_log_initialized = false;
    flush_log_buffer();
    s_log_closed = true;
}

bool hal_sdlogger_crash_init(const char *add_to_name, int cs) {
    (void)cs;

    char name[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
    int crash_number = hal_sdlogger_get_crash_number();
    if (add_to_name != NULL && strlen(add_to_name) > 0u) {
        snprintf(name, sizeof(name), "watchdog%d(%s).txt", crash_number, add_to_name);
    } else {
        snprintf(name, sizeof(name), "watchdog%d.txt", crash_number);
    }
    hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR, crash_number + 1);
    hal_eeprom_commit();

    if (!s_sd_started) {
        s_sd_begin_count++;
        s_sd_started = s_sd_begin_result;
    }

    s_crash_initialized = s_sd_started && s_crash_open_result;
    if (s_crash_initialized) {
        snprintf(s_crash_filename, sizeof(s_crash_filename), "%s", name);
        s_crash_closed = false;

        char line[HAL_SDLOGGER_NAME_BUFFER_SIZE] = {};
        snprintf(line, sizeof(line), "corresponded log file: log%d.txt",
                 hal_sdlogger_get_log_number() - 1);
        hal_sdlogger_crash_append(line);
    } else {
        hal_serial_println("hal_sdlogger_crash_init: crash logger open failed");
    }
    return s_crash_initialized;
}

bool hal_sdlogger_crash_is_initialized(void) {
    return s_crash_initialized;
}

void hal_sdlogger_crash_append(const char *data) {
    if (!s_crash_initialized) {
        return;
    }
    append_to(s_crash_content, sizeof(s_crash_content),
              (data != NULL) ? data : "");
    append_to(s_crash_content, sizeof(s_crash_content), "\n");
    s_crash_flush_count++;
}

void hal_sdlogger_crash_close(void) {
    if (!s_crash_initialized) {
        return;
    }
    s_crash_initialized = false;
    s_crash_flush_count++;
    s_crash_closed = true;
}

void hal_sdlogger_crash_report(const char *format, ...) {
    if (!s_crash_initialized || format == NULL) {
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
#endif  // HAL_TARGET_IS_MOCK
