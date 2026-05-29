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
/* Single global lock around the underlying Serial TX path. Without this,
 * `hal_deb` (lock = s_deb_mutex), `hal_derr` (lock = s_derr_mutex) and
 * `hal_serial_session_println` (no lock) can interleave their bytes on
 * dual-core RP2040: each only protects its own format buffer, not the
 * actual `Serial.println(s)` call that runs as two separate `write()`s
 * (the string, then "\r\n"). The single-byte CDC drops observed in the
 * field were that interleave. */
static hal_mutex_t s_tx_mutex   = NULL;
static volatile bool s_debug_initialized = false;
static volatile bool s_debug_muted = false;
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

// ── ISR-deferred debug ring (SPSC) ───────────────────────────────────────────
//
// Producer (ISR): hal_deb / hal_derr / hal_derr_limited when hal_in_isr()
//                 is true. Format into a stack buffer, push to ring, return.
// Consumer (task): hal_debug_loop() drains records, applies the same prefix
//                  / ERROR! markup the non-ISR path would, and emits them
//                  through the regular mutex-protected Serial output.
// Overflow: producer drops the new record and increments s_isr_dropped;
//           the next drain emits one summary line and clears the counter.

typedef enum {
    HAL_ISR_REC_DEB  = 0,
    HAL_ISR_REC_DERR = 1,
} hal_isr_rec_level_t;

typedef struct {
    uint8_t  level;
    uint32_t ts_us;
    size_t   text_len;
    char     text[HAL_DEBUG_ISR_TEXT_MAX];
} hal_isr_rec_t;

static hal_isr_rec_t   s_isr_slots[HAL_DEBUG_ISR_SLOT_COUNT];
static volatile size_t   s_isr_head    = 0u;
static volatile size_t   s_isr_tail    = 0u;
static volatile uint32_t s_isr_dropped = 0u;

static inline size_t isr_ring_next(size_t idx) {
    size_t n = idx + 1u;
    if (n >= HAL_DEBUG_ISR_SLOT_COUNT) n = 0u;
    return n;
}

static bool isr_ring_push(uint8_t level, uint32_t ts_us, const char *text) {
    const size_t head = __atomic_load_n(&s_isr_head, __ATOMIC_RELAXED);
    const size_t tail = __atomic_load_n(&s_isr_tail, __ATOMIC_ACQUIRE);
    const size_t next = isr_ring_next(head);
    if (next == tail) {
        __atomic_fetch_add(&s_isr_dropped, 1u, __ATOMIC_RELAXED);
        return false;
    }
    hal_isr_rec_t *slot = &s_isr_slots[head];
    slot->level = level;
    slot->ts_us = ts_us;
    size_t n = 0u;
    if (text != NULL) {
        while (n < (HAL_DEBUG_ISR_TEXT_MAX - 1u) && text[n] != '\0') {
            ++n;
        }
        if (n > 0u) {
            memcpy(slot->text, text, n);
        }
    }
    slot->text[n] = '\0';
    slot->text_len = n;
    __atomic_store_n(&s_isr_head, next, __ATOMIC_RELEASE);
    return true;
}

static bool isr_ring_pop(hal_isr_rec_t *out) {
    const size_t tail = __atomic_load_n(&s_isr_tail, __ATOMIC_RELAXED);
    const size_t head = __atomic_load_n(&s_isr_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    *out = s_isr_slots[tail];
    __atomic_store_n(&s_isr_tail, isr_ring_next(tail), __ATOMIC_RELEASE);
    return true;
}

static uint32_t isr_ring_consume_dropped(void) {
    return __atomic_exchange_n(&s_isr_dropped, 0u, __ATOMIC_ACQ_REL);
}

static bool isr_enqueue_vformat(uint8_t level, uint32_t ts_us,
                                const char *format, va_list args) {
    char buf[HAL_DEBUG_ISR_TEXT_MAX];
    if (format == NULL) {
        buf[0] = '\0';
    } else {
        int n = vsnprintf(buf, sizeof buf, format, args);
        if (n < 0) {
            buf[0] = '\0';
        }
    }
    return isr_ring_push(level, ts_us, buf);
}

static bool isr_enqueue_derr_limited(uint32_t ts_us, const char *source,
                                     const char *format, va_list args) {
    char buf[HAL_DEBUG_ISR_TEXT_MAX];
    const char *src = (source && source[0]) ? source : "global";
    int prefix_n = snprintf(buf, sizeof buf, "[%s] ", src);
    if (prefix_n < 0) {
        prefix_n = 0;
        buf[0] = '\0';
    }
    size_t used = (size_t)prefix_n;
    if (used >= sizeof buf) {
        used = sizeof buf - 1u;
        buf[used] = '\0';
    }
    if (format != NULL && used < sizeof buf - 1u) {
        int n = vsnprintf(buf + used, sizeof buf - used, format, args);
        if (n < 0) {
            buf[used] = '\0';
        }
    }
    return isr_ring_push(HAL_ISR_REC_DERR, ts_us, buf);
}

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
 * Safe to call from multiple cores - worst case is a harmless
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

/* Lazy-init guard for the TX mutex. Direct callers of hal_serial_print
 * may run before hal_debug_init (e.g. very early bring-up); we don't
 * want to require an explicit init order just to lock. */
static void hal_serial_ensure_tx_mutex(void) {
    if (s_tx_mutex != NULL) return;
    hal_critical_section_enter();
    if (s_tx_mutex == NULL) {
        s_tx_mutex = hal_mutex_create();
    }
    hal_critical_section_exit();
}

void hal_serial_begin(uint32_t baud) {
    Serial.begin(baud);
}

/* `Serial.flush()` waits until the TX FIFO has actually been drained
 * to the USB host. We need this **inside** the TX mutex window: on
 * RP2040 + TinyUSB CDC, `Serial.println(s)` only copies bytes into the
 * USB CDC ring buffer and returns immediately. Without flush, a
 * follow-up `hal_serial_println` (from the session helper or from
 * `hal_deb` on the other core) takes the mutex and starts writing
 * fresh bytes into a FIFO that still contains tail bytes of the
 * previous frame. The TinyUSB ring-pointer race in that overlap is
 * what produced the single-byte drops observed in the field
 * (`buid` / `defaut` / `efault` etc.). Flushing before unlock keeps
 * exactly one frame in flight at a time. */
void hal_serial_print(const char *s) {
    hal_serial_ensure_tx_mutex();
    hal_mutex_lock(s_tx_mutex);
    Serial.print(s);
    Serial.flush();
    hal_mutex_unlock(s_tx_mutex);
}

void hal_serial_println(const char *s) {
    hal_serial_ensure_tx_mutex();
    hal_mutex_lock(s_tx_mutex);
    Serial.println(s);
    Serial.flush();
    hal_mutex_unlock(s_tx_mutex);
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
    hal_serial_ensure_tx_mutex();
    hal_serial_begin(baud);
    s_debug_muted = false;
    s_debug_initialized = true;
}

bool hal_deb_is_initialized(void) {
    return s_debug_initialized;
}

void hal_debug_set_muted(bool muted) {
    hal_critical_section_enter();
    s_debug_muted = muted;
    hal_critical_section_exit();
}

bool hal_debug_is_muted(void) {
    return s_debug_muted;
}

void hal_deb_set_prefix(const char *prefix) {
    if (prefix != NULL && strlen(prefix) > 0 &&
        strlen(prefix) < HAL_DEBUG_PREFIX_SIZE) {
        strncpy(s_prefix, prefix, HAL_DEBUG_PREFIX_SIZE - 1);
    }
}

void hal_deb(const char *format, ...) {
    if (hal_debug_is_muted()) {
        return;
    }

    if (hal_in_isr()) {
        /* ISR fast path: no lazy init, no mutex, no prefix application.
         * Prefix/timestamp markup is applied later by hal_debug_loop()
         * from a safe context. */
        va_list args;
        va_start(args, format);
        (void)isr_enqueue_vformat(HAL_ISR_REC_DEB, hal_micros(),
                                      format, args);
        va_end(args);
        return;
    }

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
    if (hal_debug_is_muted()) {
        return;
    }

    if (hal_in_isr()) {
        /* ISR fast path: skip timestamp hook (it may take locks) and
         * skip the ERROR! marker - both are applied at drain time. */
        va_list args;
        va_start(args, format);
        (void)isr_enqueue_vformat(HAL_ISR_REC_DERR, hal_micros(),
                                      format, args);
        va_end(args);
        return;
    }

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
    if (hal_debug_is_muted()) {
        return;
    }

    if (hal_in_isr()) {
        /* ISR fast path: rate limiter relies on a shared slot table
         * protected by a mutex, so we bypass it entirely and enqueue
         * the message with the source tag baked into the text. */
        va_list args;
        va_start(args, format);
        (void)isr_enqueue_derr_limited(hal_micros(), source, format, args);
        va_end(args);
        return;
    }

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

// ── ISR-deferred debug ring ───────────────────────────────────────────────────

void hal_debug_loop(void) {
    /* Calling drain from ISR is unsupported: the consumer side takes
     * the TX mutex and blocks on Serial.flush(), so it must run from
     * task context. Silently bail out so the caller cannot deadlock. */
    if (hal_in_isr()) {
        return;
    }

    /* When muted, drop pending records (and the dropped-counter)
     * silently. Tracking would be misleading because the records were
     * never going to be emitted anyway. */
    if (hal_debug_is_muted()) {
        hal_isr_rec_t rec;
        while (isr_ring_pop(&rec)) {
            /* discard */
        }
        (void)isr_ring_consume_dropped();
        return;
    }

    hal_debug_ensure_init();

    hal_isr_rec_t rec;
    char line[HAL_DEBUG_BUF_SIZE];
    while (isr_ring_pop(&rec)) {
        if (rec.level == HAL_ISR_REC_DERR) {
            snprintf(line, sizeof line, "%s[ISR ts=%lu] %s",
                     HAL_ERR_PREFIX,
                     (unsigned long)rec.ts_us,
                     rec.text);
        } else if (s_prefix[0] != '\0') {
            snprintf(line, sizeof line, "%s [ISR ts=%lu] %s",
                     s_prefix,
                     (unsigned long)rec.ts_us,
                     rec.text);
        } else {
            snprintf(line, sizeof line, "[ISR ts=%lu] %s",
                     (unsigned long)rec.ts_us,
                     rec.text);
        }
        hal_serial_println(line);
    }

    const uint32_t dropped = isr_ring_consume_dropped();
    if (dropped > 0u) {
        char drop_line[96];
        snprintf(drop_line, sizeof drop_line,
                 "%s[ISR] dropped %lu debug message(s)",
                 HAL_ERR_PREFIX,
                 (unsigned long)dropped);
        hal_serial_println(drop_line);
    }
}
