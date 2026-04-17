#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include <Arduino.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "../../hal_config.h"

static char        s_prefix[HAL_DEBUG_PREFIX_SIZE] = {};
static char        s_deb_buf[HAL_DEBUG_BUF_SIZE]   = {};
static char        s_derr_buf[HAL_DEBUG_BUF_SIZE]  = {};
static hal_mutex_t s_deb_mutex  = NULL;
static hal_mutex_t s_derr_mutex = NULL;
static hal_mutex_t s_rl_mutex   = NULL;
static volatile bool s_debug_initialized = false;
static hal_debug_rate_limit_t s_rate_limit_cfg = {5u, 1000u, 30000u};
static hal_debug_timestamp_hook_t s_timestamp_hook = NULL;
static void *s_timestamp_user = NULL;

typedef struct {
    bool in_use;
    uint32_t hash;
    char source[HAL_DEBUG_RATE_LIMIT_SOURCE_NAME_MAX];
    uint16_t full_printed;
    uint32_t suppressed_since_summary;
    uint32_t last_full_ms;
    uint32_t last_summary_ms;
    bool suppression_notice_printed;
} hal_error_slot_t;

static hal_error_slot_t s_error_slots[HAL_DEBUG_RATE_LIMIT_SOURCES_MAX] = {};
static hal_error_slot_t s_overflow_slot = {};

#ifndef HAL_DEBUG_COLOR_ERRORS
#define HAL_DEBUG_COLOR_ERRORS 1
#endif

#if HAL_DEBUG_COLOR_ERRORS
#define HAL_ERR_PREFIX "\x1b[1;31mERROR!\x1b[0m "
#else
#define HAL_ERR_PREFIX "ERROR! "
#endif

static hal_debug_rate_limit_t sanitize_rate_cfg(const hal_debug_rate_limit_t *cfg) {
    hal_debug_rate_limit_t def = hal_debug_rate_limit_defaults();
    if (!cfg) return def;

    hal_debug_rate_limit_t out = *cfg;
    if (out.full_logs_limit == 0u) out.full_logs_limit = def.full_logs_limit;
    if (out.min_gap_ms == 0u) out.min_gap_ms = def.min_gap_ms;
    if (out.summary_every_ms == 0u) out.summary_every_ms = def.summary_every_ms;
    return out;
}

static uint32_t hash_source(const char *source) {
    const unsigned char *p = (const unsigned char *)(source ? source : "global");
    uint32_t h = 2166136261u;
    while (*p) {
        h ^= (uint32_t)(*p++);
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static void copy_source_label(char *dst, size_t dst_size, const char *src) {
    const char *safe = src ? src : "global";
    if (dst_size == 0u) return;
    strncpy(dst, safe, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
}

static hal_error_slot_t *get_error_slot(const char *source) {
    const char *safe_source = source ? source : "global";
    const uint32_t h = hash_source(safe_source);
    int free_idx = -1;

    for (int i = 0; i < HAL_DEBUG_RATE_LIMIT_SOURCES_MAX; i++) {
        if (s_error_slots[i].in_use) {
            if (s_error_slots[i].hash == h &&
                strncmp(s_error_slots[i].source, safe_source,
                        HAL_DEBUG_RATE_LIMIT_SOURCE_NAME_MAX) == 0) {
                return &s_error_slots[i];
            }
        } else if (free_idx < 0) {
            free_idx = i;
        }
    }

    if (free_idx < 0) {
        if (!s_overflow_slot.in_use) {
            memset(&s_overflow_slot, 0, sizeof(s_overflow_slot));
            s_overflow_slot.in_use = true;
            copy_source_label(s_overflow_slot.source, sizeof(s_overflow_slot.source), "overflow");
            s_overflow_slot.hash = hash_source("overflow");
        }
        return &s_overflow_slot;
    }

    hal_error_slot_t *slot = &s_error_slots[free_idx];
    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    slot->hash = h;
    copy_source_label(slot->source, sizeof(slot->source), source);
    return slot;
}

/**
 * @brief Ensure the debug subsystem is initialised (lazy init).
 *
 * Safe to call from multiple cores — worst case is a harmless
 * double-init because hal_debug_init() overwrites the same statics.
 */
static void hal_debug_ensure_init(void) {
    if (s_debug_initialized) return;
    hal_critical_section_enter();
    if (!s_debug_initialized) {
        hal_debug_init(HAL_DEBUG_DEFAULT_BAUD);
    }
    hal_critical_section_exit();
}

void hal_serial_begin(uint32_t baud) {
    Serial.begin(baud);
}

void hal_serial_print(const char *s) {
    Serial.print(s);
}

void hal_serial_println(const char *s) {
    Serial.println(s);
}

int hal_serial_available(void) {
    return Serial.available();
}

int hal_serial_read(void) {
    return Serial.read();
}

hal_debug_rate_limit_t hal_debug_rate_limit_defaults(void) {
    hal_debug_rate_limit_t cfg = {5u, 1000u, 30000u};
    return cfg;
}

const hal_debug_rate_limit_t *hal_debug_get_rate_limit(void) {
    return &s_rate_limit_cfg;
}

void hal_debug_set_timestamp_hook(hal_debug_timestamp_hook_t hook, void *user) {
    s_timestamp_hook = hook;
    s_timestamp_user = user;
}

void hal_debug_init(uint32_t baud, const hal_debug_rate_limit_t *cfg) {
    memset(s_prefix, 0, sizeof(s_prefix));
    s_rate_limit_cfg = sanitize_rate_cfg(cfg);
    memset(s_error_slots, 0, sizeof(s_error_slots));
    memset(&s_overflow_slot, 0, sizeof(s_overflow_slot));
    s_deb_mutex  = hal_mutex_create();
    s_derr_mutex = hal_mutex_create();
    s_rl_mutex   = hal_mutex_create();
    hal_serial_begin(baud);
    s_debug_initialized = true;
}

bool hal_deb_is_initialized(void) {
    return s_debug_initialized;
}

void hal_deb_set_prefix(const char *prefix) {
    if (prefix != NULL && strlen(prefix) > 0 &&
        strlen(prefix) < HAL_DEBUG_PREFIX_SIZE) {
        strncpy(s_prefix, prefix, HAL_DEBUG_PREFIX_SIZE - 1);
    }
}

void hal_deb(const char *format, ...) {
    hal_debug_ensure_init();
    hal_mutex_lock(s_deb_mutex);
    va_list args;
    va_start(args, format);
    memset(s_deb_buf, 0, sizeof(s_deb_buf));
    size_t used = 0;
    if (strlen(s_prefix) > 0) {
        int n = snprintf(s_deb_buf, sizeof(s_deb_buf), "%s ", s_prefix);
        if (n > 0) {
            used = (size_t)n;
            if (used >= sizeof(s_deb_buf)) used = sizeof(s_deb_buf) - 1;
        }
    }
    vsnprintf(s_deb_buf + used, sizeof(s_deb_buf) - used, format, args);
    va_end(args);
    hal_serial_println(s_deb_buf);
    hal_mutex_unlock(s_deb_mutex);
}

void hal_derr(const char *format, ...) {
    hal_debug_ensure_init();
    hal_mutex_lock(s_derr_mutex);
    va_list args;
    va_start(args, format);
    memset(s_derr_buf, 0, sizeof(s_derr_buf));
    const char *error = HAL_ERR_PREFIX;
    size_t len = 0;
    if (s_timestamp_hook != NULL) {
        char ts[32] = {};
        if (s_timestamp_hook(ts, sizeof(ts), s_timestamp_user) && ts[0] != '\0') {
            int n = snprintf(s_derr_buf, sizeof(s_derr_buf), "[%s] %s", ts, error);
            if (n > 0) {
                len = (size_t)n;
                if (len >= sizeof(s_derr_buf)) {
                    len = sizeof(s_derr_buf) - 1;
                }
            }
        }
    }
    if (len == 0) {
        strcpy(s_derr_buf, error);
        len = strlen(error);
    }
    vsnprintf(s_derr_buf + len, sizeof(s_derr_buf) - 1 - len, format, args);
    va_end(args);
    hal_serial_println(s_derr_buf);
    hal_mutex_unlock(s_derr_mutex);
}

void hal_derr_limited(const char *source, const char *format, ...) {
    hal_debug_ensure_init();

    HAL_ASSERT(s_rl_mutex != NULL, "hal_derr_limited: rate-limit mutex is NULL");
    hal_mutex_lock(s_rl_mutex);

    hal_error_slot_t *slot = get_error_slot(source);
    const uint32_t now = hal_millis();
    const bool can_emit_full = (slot->full_printed < s_rate_limit_cfg.full_logs_limit) &&
                               (slot->full_printed == 0u ||
                                (now - slot->last_full_ms) >= s_rate_limit_cfg.min_gap_ms);

    if (can_emit_full) {
        char msg[HAL_DEBUG_BUF_SIZE] = {};
        va_list args;
        va_start(args, format);
        vsnprintf(msg, sizeof(msg), format, args);
        va_end(args);

        hal_derr("[%s] %s", slot->source[0] ? slot->source : "global", msg);
        slot->full_printed++;
        slot->last_full_ms = now;
        hal_mutex_unlock(s_rl_mutex);
        return;
    }

    slot->suppressed_since_summary++;

    if (!slot->suppression_notice_printed) {
        hal_derr("[%s] repeated errors are being suppressed (if they continue)",
                 slot->source[0] ? slot->source : "global");
        slot->suppression_notice_printed = true;
        slot->last_summary_ms = now;
        hal_mutex_unlock(s_rl_mutex);
        return;
    }

    if (s_rate_limit_cfg.summary_every_ms > 0u &&
        (now - slot->last_summary_ms) >= s_rate_limit_cfg.summary_every_ms &&
        slot->suppressed_since_summary > 0u) {
        hal_derr("[%s] suppressed %lu repeated errors in last %lu ms",
                 slot->source[0] ? slot->source : "global",
                 (unsigned long)slot->suppressed_since_summary,
                 (unsigned long)(now - slot->last_summary_ms));
        slot->suppressed_since_summary = 0u;
        slot->last_summary_ms = now;
    }

    hal_mutex_unlock(s_rl_mutex);
}

void hal_deb_hex(const char *prefix, const uint8_t *buf, int len, int maxBytes) {
    if(prefix == NULL) return;

    if(buf == NULL || len <= 0) {
        hal_deb("%s len=%d", prefix, len);
        return;
    }

    if(maxBytes < 1) maxBytes = 1;
    if(maxBytes > 48) maxBytes = 48;
    int shown = (len < maxBytes) ? len : maxBytes;

    char line[256];
    int pos = snprintf(line, sizeof(line), "%s len=%d bytes:", prefix, len);
    if(pos < 0 || pos >= (int)sizeof(line)) {
        hal_deb("%s len=%d", prefix, len);
        return;
    }

    for(int i = 0; i < shown; i++) {
        int n = snprintf(&line[pos], sizeof(line) - (size_t)pos, " %02X", buf[i]);
        if(n < 0) break;
        pos += n;
        if(pos >= (int)sizeof(line) - 1) break;
    }

    if(shown < len && pos < (int)sizeof(line) - 5) {
        snprintf(&line[pos], sizeof(line) - (size_t)pos, " ...");
    }

    hal_deb("%s", line);
}
