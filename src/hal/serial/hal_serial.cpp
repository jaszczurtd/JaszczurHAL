#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK || HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 ||       \
    HAL_TARGET_IS_ESP32_FAMILY
#include "hal/core/hal_config.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/debug/hal_debug_format.h"
#include "hal/debug/jh_serial_port.h"
#include "hal/network/net_console/hal_net_console.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#if HAL_TARGET_IS_MOCK
#include "hal/impl/.mock/hal_mock.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char s_prefix[HAL_DEBUG_PREFIX_SIZE] = {};
static hal_mutex_t s_deb_mutex = NULL;
static hal_mutex_t s_derr_mutex = NULL;
static hal_mutex_t s_rl_mutex = NULL;
/* One global lock protects every target's byte transport. The separate debug
 * locks serialize formatter/rate-limit state, while this mutex keeps complete
 * serial, debug, error, session, and deferred-ISR messages from interleaving
 * across tasks or RP2040 cores. */
static hal_mutex_t s_tx_mutex = NULL;
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
//                  through the regular mutex-protected serial output.
// Overflow: producer drops the new record and increments s_isr_dropped;
//           the next drain emits one summary line and clears the counter.

typedef enum {
  HAL_ISR_REC_DEB = 0,
  HAL_ISR_REC_DERR = 1,
} hal_isr_rec_level_t;

typedef struct {
  uint8_t level;
  uint32_t ts_us;
  size_t text_len;
  char text[HAL_DEBUG_ISR_TEXT_MAX];
} hal_isr_rec_t;

static hal_isr_rec_t s_isr_default_slots[HAL_DEBUG_ISR_SLOT_COUNT];
static hal_isr_rec_t *s_isr_slots = s_isr_default_slots;
static size_t s_isr_cap = HAL_DEBUG_ISR_SLOT_COUNT;
static volatile size_t s_isr_head = 0u;
static volatile size_t s_isr_tail = 0u;
static volatile uint32_t s_isr_dropped = 0u;

static inline size_t isr_ring_next(size_t idx) {
  size_t n = idx + 1u;
  if (n >= s_isr_cap)
    n = 0u;
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

static hal_debug_rate_limit_t
sanitize_rate_cfg(const hal_debug_rate_limit_t *cfg) {
  hal_debug_rate_limit_t def = hal_debug_rate_limit_defaults();
  if (!cfg)
    return def;

  hal_debug_rate_limit_t out = *cfg;
  if (out.full_logs_limit == 0u)
    out.full_logs_limit = def.full_logs_limit;
  if (out.min_gap_ms == 0u)
    out.min_gap_ms = def.min_gap_ms;
  if (out.summary_every_ms == 0u)
    out.summary_every_ms = def.summary_every_ms;
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
  if (dst_size == 0u)
    return;
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
      copy_source_label(s_overflow_slot.source, sizeof(s_overflow_slot.source),
                        "overflow");
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
 * Safe to call from multiple cores/tasks: one caller performs the lazy
 * init while others yield/spin until s_debug_initialized is published.
 */
static void hal_debug_ensure_init(void) {
  if (__atomic_load_n(&s_debug_initialized, __ATOMIC_ACQUIRE)) {
    return;
  }
  (void)jh_hal_mutex_create_once(&s_deb_mutex);
  hal_mutex_lock(s_deb_mutex);
  if (!__atomic_load_n(&s_debug_initialized, __ATOMIC_ACQUIRE)) {
    hal_debug_init(HAL_DEBUG_DEFAULT_BAUD);
  }
  hal_mutex_unlock(s_deb_mutex);
}

/* Lazy-init guard for the TX mutex. Direct callers of hal_serial_print
 * may run before hal_debug_init (e.g. very early bring-up); we don't
 * want to require an explicit init order just to lock. */
static void hal_serial_ensure_tx_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_tx_mutex);
}

void hal_serial_begin(uint32_t baud) { jh_serial_port_begin(baud); }

void hal_serial_set_flush(bool enabled) { jh_serial_port_set_flush(enabled); }

static void hal_serial_write_locked(const char *data, size_t len) {
  if (data == NULL || len == 0u) {
    return;
  }
  hal_net_console_write_from_serial(data, len);
  jh_serial_port_write(data, len);
}

static void hal_serial_finish_line_locked(void) {
  char line_ending[2] = {};
  size_t line_ending_len = jh_serial_port_finish_line(line_ending);
  if (line_ending_len > sizeof(line_ending)) {
    line_ending_len = sizeof(line_ending);
  }
  hal_net_console_write_from_serial(line_ending, line_ending_len);
  jh_serial_port_flush();
}

#if HAL_TARGET_IS_RP
extern "C" void hal_rp2040_serial_write_assert_fail(const char *text) {
  const char *safe = text ? text : "(null)";
  jh_serial_port_message_begin(JH_SERIAL_PORT_MESSAGE_ERROR);
  hal_serial_write_locked("HAL ASSERT FAIL: ", strlen("HAL ASSERT FAIL: "));
  hal_serial_write_locked(safe, strlen(safe));
  hal_serial_finish_line_locked();
}
#endif

static void hal_debug_stream_write(void *ctx, const char *data, size_t len) {
  (void)ctx;
  hal_serial_write_locked(data, len);
}

/* Every emitter shares this TX window, so a transport port always observes one
 * complete message at a time. The RP port optionally flushes USB CDC before the
 * shared core releases this boundary mutex. */
void hal_serial_print(const char *s) {
  const char *text = s ? s : "";
  hal_serial_ensure_tx_mutex();
  hal_mutex_lock(s_tx_mutex);
  jh_serial_port_message_begin(JH_SERIAL_PORT_MESSAGE_APPEND);
  hal_serial_write_locked(text, strlen(text));
  jh_serial_port_flush();
  hal_mutex_unlock(s_tx_mutex);
}

void hal_serial_println(const char *s) {
  const char *text = s ? s : "";
  hal_serial_ensure_tx_mutex();
  hal_mutex_lock(s_tx_mutex);
  jh_serial_port_message_begin(JH_SERIAL_PORT_MESSAGE_LINE);
  hal_serial_write_locked(text, strlen(text));
  hal_serial_finish_line_locked();
  hal_mutex_unlock(s_tx_mutex);
}

int hal_serial_available(void) { return jh_serial_port_available(); }

int hal_serial_read(void) { return jh_serial_port_read(); }

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
  (void)jh_hal_mutex_create_once(&s_deb_mutex);
  (void)jh_hal_mutex_create_once(&s_derr_mutex);
  (void)jh_hal_mutex_create_once(&s_rl_mutex);
  hal_serial_ensure_tx_mutex();
  hal_serial_begin(baud);
  __atomic_store_n(&s_debug_muted, false, __ATOMIC_RELEASE);
  __atomic_store_n(&s_debug_initialized, true, __ATOMIC_RELEASE);
}

bool hal_deb_is_initialized(void) {
  return __atomic_load_n(&s_debug_initialized, __ATOMIC_ACQUIRE);
}

void hal_debug_set_muted(bool muted) {
  __atomic_store_n(&s_debug_muted, muted, __ATOMIC_RELEASE);
}

bool hal_debug_is_muted(void) {
  return __atomic_load_n(&s_debug_muted, __ATOMIC_ACQUIRE);
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
    (void)isr_enqueue_vformat(HAL_ISR_REC_DEB, hal_micros(), format, args);
    va_end(args);
    return;
  }

  hal_debug_ensure_init();
  hal_mutex_lock(s_deb_mutex);

  va_list args;
  va_start(args, format);
  hal_serial_ensure_tx_mutex();
  hal_mutex_lock(s_tx_mutex);
  jh_serial_port_message_begin(JH_SERIAL_PORT_MESSAGE_DEBUG);
  hal_debug_format_write_deb_prefix(hal_debug_stream_write, NULL, s_prefix);
  hal_debug_vformat(hal_debug_stream_write, NULL, format, args);
  hal_serial_finish_line_locked();
  hal_mutex_unlock(s_tx_mutex);
  va_end(args);

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
    (void)isr_enqueue_vformat(HAL_ISR_REC_DERR, hal_micros(), format, args);
    va_end(args);
    return;
  }

  hal_debug_ensure_init();
  hal_mutex_lock(s_derr_mutex);

  char ts[32] = {};
  const char *timestamp = NULL;
  if (s_timestamp_hook != NULL &&
      s_timestamp_hook(ts, sizeof(ts), s_timestamp_user) && ts[0] != '\0') {
    timestamp = ts;
  }

  va_list args;
  va_start(args, format);
  hal_serial_ensure_tx_mutex();
  hal_mutex_lock(s_tx_mutex);
  jh_serial_port_message_begin(JH_SERIAL_PORT_MESSAGE_ERROR);
  hal_debug_format_write_error_prefix(hal_debug_stream_write, NULL, timestamp);
  hal_debug_vformat(hal_debug_stream_write, NULL, format, args);
  hal_serial_finish_line_locked();
  hal_mutex_unlock(s_tx_mutex);
  va_end(args);

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
  const bool can_emit_full =
      (slot->full_printed < s_rate_limit_cfg.full_logs_limit) &&
      (slot->full_printed == 0u ||
       (now - slot->last_full_ms) >= s_rate_limit_cfg.min_gap_ms);

  if (can_emit_full) {
    hal_mutex_lock(s_derr_mutex);

    char ts[32] = {};
    const char *timestamp = NULL;
    if (s_timestamp_hook != NULL &&
        s_timestamp_hook(ts, sizeof(ts), s_timestamp_user) && ts[0] != '\0') {
      timestamp = ts;
    }

    va_list args;
    va_start(args, format);
    hal_serial_ensure_tx_mutex();
    hal_mutex_lock(s_tx_mutex);
    jh_serial_port_message_begin(JH_SERIAL_PORT_MESSAGE_ERROR);
    hal_debug_format_write_error_prefix(hal_debug_stream_write, NULL,
                                        timestamp);
    hal_debug_format_write_source_prefix(hal_debug_stream_write, NULL,
                                         slot->source);
    hal_debug_vformat(hal_debug_stream_write, NULL, format, args);
    hal_serial_finish_line_locked();
    hal_mutex_unlock(s_tx_mutex);
    va_end(args);

    hal_mutex_unlock(s_derr_mutex);
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

void hal_deb_hex(const char *prefix, const uint8_t *buf, int len,
                 int maxBytes) {
  if (prefix == NULL)
    return;

  if (buf == NULL || len <= 0) {
    hal_deb("%s len=%d", prefix, len);
    return;
  }

  if (maxBytes < 1)
    maxBytes = 1;
  if (maxBytes > 48)
    maxBytes = 48;
  int shown = (len < maxBytes) ? len : maxBytes;

  char line[256];
  int pos = snprintf(line, sizeof(line), "%s len=%d bytes:", prefix, len);
  if (pos < 0 || pos >= (int)sizeof(line)) {
    hal_deb("%s len=%d", prefix, len);
    return;
  }

  for (int i = 0; i < shown; i++) {
    int n = snprintf(&line[pos], sizeof(line) - (size_t)pos, " %02X", buf[i]);
    if (n < 0)
      break;
    pos += n;
    if (pos >= (int)sizeof(line) - 1)
      break;
  }

  if (shown < len && pos < (int)sizeof(line) - 5) {
    snprintf(&line[pos], sizeof(line) - (size_t)pos, " ...");
  }

  hal_deb("%s", line);
}

#if HAL_TARGET_IS_MOCK
/* A constrained ring lets host tests force overflow without changing the
 * production capacity. The SPSC ownership and atomic indices stay identical. */
#define HAL_MOCK_DEBUG_ISR_TEST_SLOT_POOL 8u
static hal_isr_rec_t s_mock_isr_test_slots[HAL_MOCK_DEBUG_ISR_TEST_SLOT_POOL];

size_t hal_mock_debug_isr_used_slots(void) {
  const size_t head = __atomic_load_n(&s_isr_head, __ATOMIC_RELAXED);
  const size_t tail = __atomic_load_n(&s_isr_tail, __ATOMIC_RELAXED);
  if (head >= tail) {
    return head - tail;
  }
  return (s_isr_cap - tail) + head;
}

size_t hal_mock_debug_isr_capacity(void) { return s_isr_cap; }

uint32_t hal_mock_debug_isr_dropped(void) {
  return __atomic_load_n(&s_isr_dropped, __ATOMIC_RELAXED);
}

void hal_mock_debug_isr_reset(void) {
  __atomic_store_n(&s_isr_head, 0u, __ATOMIC_RELAXED);
  __atomic_store_n(&s_isr_tail, 0u, __ATOMIC_RELAXED);
  __atomic_store_n(&s_isr_dropped, 0u, __ATOMIC_RELAXED);
}

void hal_mock_debug_isr_set_test_capacity(size_t cap) {
  if (cap < 2u) {
    cap = 2u;
  }
  if (cap > HAL_MOCK_DEBUG_ISR_TEST_SLOT_POOL) {
    cap = HAL_MOCK_DEBUG_ISR_TEST_SLOT_POOL;
  }
  s_isr_slots = s_mock_isr_test_slots;
  s_isr_cap = cap;
  hal_mock_debug_isr_reset();
}

void hal_mock_debug_isr_restore_default_ring(void) {
  s_isr_slots = s_isr_default_slots;
  s_isr_cap = HAL_DEBUG_ISR_SLOT_COUNT;
  hal_mock_debug_isr_reset();
}
#endif

// ── ISR-deferred debug ring ──────────────────────────────────────────────────

void hal_debug_loop(void) {
  /* Calling drain from ISR is unsupported: the consumer side takes
   * the TX mutex and may block while pushing transport output, so it must run
   * from task context. Silently bail out so the caller cannot deadlock. */
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
  while (isr_ring_pop(&rec)) {
    char ts[16];
    snprintf(ts, sizeof ts, "%lu", (unsigned long)rec.ts_us);

    hal_serial_ensure_tx_mutex();
    hal_mutex_lock(s_tx_mutex);
    jh_serial_port_message_begin(rec.level == HAL_ISR_REC_DERR
                                     ? JH_SERIAL_PORT_MESSAGE_ERROR
                                     : JH_SERIAL_PORT_MESSAGE_DEBUG);
    hal_debug_format_write_isr_prefix(hal_debug_stream_write, NULL,
                                      rec.level == HAL_ISR_REC_DERR, s_prefix,
                                      ts);
    hal_debug_format_write_cstr(hal_debug_stream_write, NULL, rec.text);
    hal_serial_finish_line_locked();
    hal_mutex_unlock(s_tx_mutex);
  }

  const uint32_t dropped = isr_ring_consume_dropped();
  if (dropped > 0u) {
    char drop_line[96];
    snprintf(drop_line, sizeof drop_line,
             "%s[ISR] dropped %lu debug message(s)", HAL_DEBUG_ERROR_PREFIX,
             (unsigned long)dropped);
    hal_serial_println(drop_line);
  }
}

#if HAL_TARGET_IS_MOCK
/* Test-only: force every debug/serial singleton mutex through a real
 * destroy so Helgrind/DRD can observe the teardown path, then clear the
 * lazy-init flag so the next hal_debug_init()/hal_debug_ensure_init() call
 * recreates them from scratch. Firmware never calls this. */
void hal_mock_debug_serial_full_reset(void) {
  if (s_deb_mutex != NULL) {
    hal_mutex_destroy(s_deb_mutex);
    s_deb_mutex = NULL;
  }
  if (s_derr_mutex != NULL) {
    hal_mutex_destroy(s_derr_mutex);
    s_derr_mutex = NULL;
  }
  if (s_rl_mutex != NULL) {
    hal_mutex_destroy(s_rl_mutex);
    s_rl_mutex = NULL;
  }
  if (s_tx_mutex != NULL) {
    hal_mutex_destroy(s_tx_mutex);
    s_tx_mutex = NULL;
  }
  __atomic_store_n(&s_debug_initialized, false, __ATOMIC_RELEASE);
}
#endif /* HAL_TARGET_IS_MOCK */

#endif // supported target
