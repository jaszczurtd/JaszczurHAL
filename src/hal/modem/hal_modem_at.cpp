#include "hal/modem/hal_modem_at.h"

#ifdef HAL_ENABLE_CELLULAR_MODEM

#include "hal/core/hal_config.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Pool sizing ─────────────────────────────────────────────────────── */

/**
 * @def HAL_MODEM_AT_MAX_INSTANCES
 * Maximum number of simultaneous AT-engine handles. One per physical
 * modem UART. Override with -DHAL_MODEM_AT_MAX_INSTANCES=N if needed.
 */
#ifndef HAL_MODEM_AT_MAX_INSTANCES
#define HAL_MODEM_AT_MAX_INSTANCES 2
#endif

/** Maximum URC handlers registered per engine handle. */
#ifndef HAL_MODEM_AT_MAX_URCS
#define HAL_MODEM_AT_MAX_URCS 8
#endif

/** Length cap for a registered URC prefix string (including NUL). */
#ifndef HAL_MODEM_AT_URC_PREFIX_MAX
#define HAL_MODEM_AT_URC_PREFIX_MAX 32
#endif

/** Length cap for a single assembled URC line (including NUL). */
#ifndef HAL_MODEM_AT_URC_LINE_MAX
#define HAL_MODEM_AT_URC_LINE_MAX 256
#endif

/** Maximum number of secrets installed via hal_modem_at_set_log_filter(). */
#ifndef HAL_MODEM_AT_MAX_SECRETS
#define HAL_MODEM_AT_MAX_SECRETS 8
#endif

/* ── Internal state ──────────────────────────────────────────────────── */

typedef struct {
  char prefix[HAL_MODEM_AT_URC_PREFIX_MAX];
  hal_modem_at_urc_cb_t cb;
  void *user;
  bool in_use;
} urc_slot_t;

struct hal_modem_at_impl_s {
  hal_modem_at_config_t cfg;
  hal_mutex_t mutex;
  size_t rx_len; /* bytes accumulated in cfg.rx_buf       */
  char line_buf[HAL_MODEM_AT_URC_LINE_MAX]; /* current in-progress line  */
  size_t line_len;
  urc_slot_t urcs[HAL_MODEM_AT_MAX_URCS];
  hal_modem_at_urc_cb_t
      line_observer; /* raw per-line tap, see set_line_observer */
  void *line_observer_user;
  hal_modem_at_tick_cb_t tick_cb; /* watchdog tick, see set_tick_callback   */
  void *tick_user;
  const char *const *secrets; /* not owned                              */
  size_t secret_count;
  bool in_use;
};

static hal_modem_at_impl_t s_pool[HAL_MODEM_AT_MAX_INSTANCES];

/* Short poll-loop sleep that also fires the application tick (watchdog
   feed / LED refresh). Used by every internal busy-wait inside the
   engine so that no blocking entry point can ever starve the app's
   watchdog. */
static inline void engine_poll_sleep(hal_modem_at_impl_t *h) {
  if (h->tick_cb)
    h->tick_cb(h->tick_user);
  hal_delay_ms(2);
}

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void reset_rx(hal_modem_at_impl_t *h) {
  h->rx_len = 0;
  if (h->cfg.rx_buf && h->cfg.rx_buf_size > 0) {
    h->cfg.rx_buf[0] = '\0';
  }
  h->line_len = 0;
  h->line_buf[0] = '\0';
}

static bool buf_contains(const char *hay, size_t hay_len, const char *needle) {
  if (!needle || !*needle)
    return false;
  size_t nlen = strlen(needle);
  if (nlen > hay_len)
    return false;
  /* memmem is non-portable; do a simple search (hay is NUL-terminated) */
  return strstr(hay, needle) != NULL;
}

static void dispatch_line(hal_modem_at_impl_t *h, const char *line) {
  if (line[0] == '\0')
    return;
  /* Prefix-keyed URC handlers first. */
  for (size_t i = 0; i < HAL_MODEM_AT_MAX_URCS; i++) {
    const urc_slot_t *u = &h->urcs[i];
    if (!u->in_use || !u->cb)
      continue;
    size_t plen = strlen(u->prefix);
    if (plen == 0)
      continue;
    if (strncmp(line, u->prefix, plen) == 0) {
      u->cb(line, u->user);
    }
  }
  /* Raw line observer, if installed. Receives every assembled line
     (URC, response body, prompt echo) - used by drivers that need to
     capture multi-line payload bodies (e.g. SimCom CMQTTRX topic/payload). */
  if (h->line_observer) {
    h->line_observer(line, h->line_observer_user);
  }
}

/* Append one byte to rx_buf + line assembler. On line termination dispatch
   URC handlers (after which the line stays in rx_buf for last_response). */
static void absorb_byte(hal_modem_at_impl_t *h, uint8_t b) {
  if (h->rx_len + 1 < h->cfg.rx_buf_size) {
    h->cfg.rx_buf[h->rx_len++] = (char)b;
    h->cfg.rx_buf[h->rx_len] = '\0';
  }
  if (b == '\r' || b == '\n') {
    if (h->line_len > 0) {
      h->line_buf[h->line_len] = '\0';
      dispatch_line(h, h->line_buf);
      h->line_len = 0;
      h->line_buf[0] = '\0';
    }
  } else {
    if (h->line_len + 1 < sizeof(h->line_buf)) {
      h->line_buf[h->line_len++] = (char)b;
      h->line_buf[h->line_len] = '\0';
    }
  }
}

/* Drain any bytes currently sitting in the UART RX buffer into the
   engine, dispatching URCs as complete lines emerge. Returns number of
   bytes consumed. */
static int drain_uart(hal_modem_at_impl_t *h) {
  int consumed = 0;
  while (hal_uart_available(h->cfg.uart) > 0) {
    int b = hal_uart_read(h->cfg.uart);
    if (b < 0)
      break;
    absorb_byte(h, (uint8_t)b);
    consumed++;
  }
  return consumed;
}

/* Decide whether the current rx_buf already constitutes a final answer.

   Semantics:
     +1  -> terminator that means SUCCESS:
              * if `expected` was given: ONLY when `expected` itself is
                present in the buffer. The bare "\r\nOK\r\n" line is
                NOT a success terminator in that case - SimCom CMQTT*
                (and similar) acknowledge the command with OK first and
                emit the actual result code as an asynchronous URC
                (e.g. "+CMQTTSUB: <ci>,<err>") a moment later. Stopping
                on the early OK would race the URC and silently let the
                next command run while the modem is still busy.
              * if `expected` was NOT given: any "\r\nOK\r\n" is a
                success terminator.
     -1  -> ERROR terminator seen (always, regardless of `expected`).
      0  -> no terminator yet                                            */
static int response_terminated(const hal_modem_at_impl_t *h,
                               const char *expected) {
  if (expected) {
    if (buf_contains(h->cfg.rx_buf, h->rx_len, expected))
      return 1;
  } else {
    /* OK / ERROR are case-sensitive in the SimCom AT protocol. */
    if (buf_contains(h->cfg.rx_buf, h->rx_len, "\r\nOK\r\n"))
      return 1;
  }
  if (buf_contains(h->cfg.rx_buf, h->rx_len, "\r\nERROR\r\n"))
    return -1;
  if (buf_contains(h->cfg.rx_buf, h->rx_len, "+CME ERROR"))
    return -1;
  if (buf_contains(h->cfg.rx_buf, h->rx_len, "+CMS ERROR"))
    return -1;
  return 0;
}

/* Render a TX line into a heap-free local buffer with any registered
   secrets replaced by "***", then log via hal_deb. */
static void log_filtered(hal_modem_at_impl_t *h, const char *tag,
                         const char *line) {
  if (!hal_deb_is_initialized())
    return;
  char scratch[160];
  size_t in_len = strlen(line);
  if (in_len >= sizeof(scratch))
    in_len = sizeof(scratch) - 1;
  memcpy(scratch, line, in_len);
  scratch[in_len] = '\0';
  for (size_t i = 0; i < h->secret_count; i++) {
    const char *s = h->secrets[i];
    if (!s || !*s)
      continue;
    char *p = scratch;
    size_t slen = strlen(s);
    if (slen == 0)
      continue;
    while ((p = strstr(p, s)) != NULL) {
      /* replace s with "***" in-place; if "***" longer than s,
         shift right; if shorter, shift left. */
      const char *repl = "***";
      size_t rlen = 3;
      ptrdiff_t off = p - scratch;
      ptrdiff_t cur_total = (ptrdiff_t)strlen(scratch);
      ptrdiff_t delta = (ptrdiff_t)rlen - (ptrdiff_t)slen;
      if (off + (ptrdiff_t)slen > cur_total)
        break;
      if (delta != 0) {
        if (cur_total + delta + 1 > (ptrdiff_t)sizeof(scratch))
          break;
        memmove(p + rlen, p + slen,
                (size_t)(cur_total - off - (ptrdiff_t)slen) + 1u);
      }
      memcpy(p, repl, rlen);
      p += rlen;
    }
  }
  hal_deb("modem %s: %s", tag, scratch);
}

/* ── Public API ──────────────────────────────────────────────────────── */

hal_modem_at_t hal_modem_at_create(const hal_modem_at_config_t *cfg) {
  if (!cfg || !cfg->uart || !cfg->rx_buf || cfg->rx_buf_size < 64u) {
    return NULL;
  }
  hal_critical_section_enter();
  int slot = -1;
  for (int i = 0; i < HAL_MODEM_AT_MAX_INSTANCES; i++) {
    if (!s_pool[i].in_use) {
      slot = i;
      s_pool[i].in_use = true;
      break;
    }
  }
  hal_critical_section_exit();
  if (slot < 0) {
    HAL_ASSERT(
        0,
        "hal_modem_at: pool exhausted - increase HAL_MODEM_AT_MAX_INSTANCES");
    return NULL;
  }
  hal_modem_at_impl_t *h = &s_pool[slot];
  /* Preserve in_use=true while we wipe the rest. */
  memset(&h->cfg, 0, sizeof(h->cfg));
  h->rx_len = 0;
  h->line_len = 0;
  h->line_buf[0] = '\0';
  memset(h->urcs, 0, sizeof(h->urcs));
  h->line_observer = NULL;
  h->line_observer_user = NULL;
  h->tick_cb = NULL;
  h->tick_user = NULL;
  h->secrets = NULL;
  h->secret_count = 0;
  h->cfg = *cfg;
  if (h->cfg.default_timeout_ms == 0)
    h->cfg.default_timeout_ms = 1000u;
  if (h->cfg.quiet_window_ms == 0)
    h->cfg.quiet_window_ms = 250u;
  h->mutex = hal_mutex_create();
  reset_rx(h);
  return h;
}

void hal_modem_at_destroy(hal_modem_at_t h) {
  if (!h)
    return;
  hal_mutex_t m = h->mutex;
  if (m)
    hal_mutex_lock(m);
  h->cfg.uart = NULL;
  h->cfg.rx_buf = NULL;
  h->cfg.rx_buf_size = 0;
  h->in_use = false;
  if (m) {
    hal_mutex_unlock(m);
    hal_mutex_destroy(m);
  }
  h->mutex = NULL;
}

const char *hal_modem_at_last_response(hal_modem_at_t h) {
  if (!h)
    return NULL;
  return h->cfg.rx_buf;
}

hal_modem_at_result_t hal_modem_at_send(hal_modem_at_t h, const char *cmd,
                                        const char *expected,
                                        uint32_t timeout_ms) {
  if (!h || !h->in_use || !cmd)
    return HAL_MODEM_AT_INVALID_ARG;
  if (timeout_ms == 0)
    timeout_ms = h->cfg.default_timeout_ms;

  hal_mutex_lock(h->mutex);

  /* Drain stale bytes (URCs etc.) before issuing a fresh command. Any
     complete URC lines are dispatched by absorb_byte(); whatever stays
     in rx_buf is harmless prefix - the OK/ERROR matcher is anchored on
     \r\n and won't false-match. */
  reset_rx(h);
  (void)drain_uart(h);

  log_filtered(h, "TX", cmd);
  (void)hal_uart_println(h->cfg.uart, cmd);

  uint32_t start = hal_millis();
  hal_modem_at_result_t res = HAL_MODEM_AT_TIMEOUT;
  for (;;) {
    (void)drain_uart(h);
    int term = response_terminated(h, expected);
    if (term > 0) {
      res = HAL_MODEM_AT_OK;
      break;
    }
    if (term < 0) {
      res = HAL_MODEM_AT_ERROR;
      break;
    }
    if ((hal_millis() - start) >= timeout_ms)
      break;
    engine_poll_sleep(h);
  }

  /* Tail-drain after an `expected` substring matched.

     Many SimCom responses arrive as "<payload>\r\n\r\nOK\r\n". When
     the caller pinned the wait to a payload substring (e.g.
     "+CCLK:"), `response_terminated` returns success the moment the
     payload appears - but the trailing "\r\nOK\r\n" (and any pending
     async URC) may still be in flight in the UART FIFO. If we returned
     now, those bytes would bleed into the next command's RX buffer
     and confuse its terminator matcher.

     So when `expected` was used and the wait succeeded, keep polling
     briefly until "\r\nOK\r\n" / "\r\nERROR\r\n" appears, capped at a
     short grace window. The result code is NOT downgraded if grace
     expires without OK - `expected` was already matched. */
  if (res == HAL_MODEM_AT_OK && expected) {
    const uint32_t TAIL_GRACE_MS = 200u;
    uint32_t tail_start = hal_millis();
    while ((hal_millis() - tail_start) < TAIL_GRACE_MS) {
      (void)drain_uart(h);
      if (buf_contains(h->cfg.rx_buf, h->rx_len, "\r\nOK\r\n") ||
          buf_contains(h->cfg.rx_buf, h->rx_len, "\r\nERROR\r\n") ||
          buf_contains(h->cfg.rx_buf, h->rx_len, "+CME ERROR") ||
          buf_contains(h->cfg.rx_buf, h->rx_len, "+CMS ERROR")) {
        break;
      }
      engine_poll_sleep(h);
    }
  }

  if (h->rx_len > 0) {
    log_filtered(h, "RX", h->cfg.rx_buf);
  }
  hal_mutex_unlock(h->mutex);
  return res;
}

hal_modem_at_result_t hal_modem_at_send_with_data(
    hal_modem_at_t h, const char *cmd, const uint8_t *data, size_t data_len,
    uint32_t prompt_timeout_ms, uint32_t resp_timeout_ms) {
  if (!h || !h->in_use || !cmd)
    return HAL_MODEM_AT_INVALID_ARG;
  if (data_len > 0 && !data)
    return HAL_MODEM_AT_INVALID_ARG;
  if (prompt_timeout_ms == 0)
    prompt_timeout_ms = 1000u;
  if (resp_timeout_ms == 0)
    resp_timeout_ms = h->cfg.default_timeout_ms;

  hal_mutex_lock(h->mutex);

  reset_rx(h);
  (void)drain_uart(h);

  log_filtered(h, "TX", cmd);
  (void)hal_uart_println(h->cfg.uart, cmd);

  /* Phase 2: wait for '>' prompt. Bytes received in the meantime are
     still absorbed (URCs get dispatched, characters end up in rx_buf).
     If the initial drain already pulled the prompt (or it arrived
     before we entered the wait loop), short-circuit immediately. */
  uint32_t start = hal_millis();
  bool got_prompt = (strchr(h->cfg.rx_buf, '>') != NULL);
  while (!got_prompt && (hal_millis() - start) < prompt_timeout_ms) {
    while (hal_uart_available(h->cfg.uart) > 0) {
      int b = hal_uart_read(h->cfg.uart);
      if (b < 0)
        break;
      absorb_byte(h, (uint8_t)b);
      if (b == '>') {
        got_prompt = true;
        break;
      }
    }
    if (got_prompt)
      break;
    engine_poll_sleep(h);
  }
  if (!got_prompt) {
    hal_mutex_unlock(h->mutex);
    return HAL_MODEM_AT_NO_PROMPT;
  }

  /* Phase 3: write payload. No CR/LF appended. */
  if (data_len > 0) {
    (void)hal_uart_write(h->cfg.uart, data, data_len);
  }

  /* Phase 4: wait for OK / ERROR. */
  start = hal_millis();
  hal_modem_at_result_t res = HAL_MODEM_AT_TIMEOUT;
  for (;;) {
    (void)drain_uart(h);
    int term = response_terminated(h, NULL);
    if (term > 0) {
      res = HAL_MODEM_AT_OK;
      break;
    }
    if (term < 0) {
      res = HAL_MODEM_AT_ERROR;
      break;
    }
    if ((hal_millis() - start) >= resp_timeout_ms)
      break;
    engine_poll_sleep(h);
  }

  if (h->rx_len > 0) {
    log_filtered(h, "RX", h->cfg.rx_buf);
  }
  hal_mutex_unlock(h->mutex);
  return res;
}

static hal_modem_at_result_t
listen_until_quiet(hal_modem_at_t h, hal_modem_at_ready_cb_t ready, void *user,
                   uint32_t total_timeout_ms, bool preserve_buffer) {
  if (!h || !h->in_use)
    return HAL_MODEM_AT_INVALID_ARG;
  if (total_timeout_ms == 0)
    total_timeout_ms = h->cfg.default_timeout_ms;

  hal_mutex_lock(h->mutex);

  if (!preserve_buffer) {
    reset_rx(h);
  }

  uint32_t start = hal_millis();
  uint32_t last_byte_ms = start;
  bool ready_seen = false;
  if (preserve_buffer && ready && h->rx_len > 0) {
    ready_seen = ready(h->cfg.rx_buf, h->rx_len, user);
  }
  hal_modem_at_result_t res = HAL_MODEM_AT_TIMEOUT;

  for (;;) {
    int consumed = drain_uart(h);
    uint32_t now = hal_millis();
    if (consumed > 0) {
      last_byte_ms = now;
      if (ready && !ready_seen) {
        if (ready(h->cfg.rx_buf, h->rx_len, user)) {
          ready_seen = true;
        }
      }
    }
    if (ready_seen && (now - last_byte_ms) >= h->cfg.quiet_window_ms) {
      res = HAL_MODEM_AT_OK;
      break;
    }
    const uint32_t quiet_reference = preserve_buffer ? last_byte_ms : start;
    if (!ready && consumed == 0 &&
        (now - quiet_reference) >= h->cfg.quiet_window_ms) {
      res = HAL_MODEM_AT_OK;
      break;
    }
    if ((now - start) >= total_timeout_ms) {
      break;
    }
    engine_poll_sleep(h);
  }

  if (h->rx_len > 0) {
    log_filtered(h, "RX", h->cfg.rx_buf);
  }
  hal_mutex_unlock(h->mutex);
  return res;
}

hal_modem_at_result_t hal_modem_at_listen_until(hal_modem_at_t h,
                                                hal_modem_at_ready_cb_t ready,
                                                void *user,
                                                uint32_t total_timeout_ms) {
  return listen_until_quiet(h, ready, user, total_timeout_ms, false);
}

hal_modem_at_result_t hal_modem_at_listen_more(hal_modem_at_t h,
                                               hal_modem_at_ready_cb_t ready,
                                               void *user,
                                               uint32_t total_timeout_ms) {
  return listen_until_quiet(h, ready, user, total_timeout_ms, true);
}

bool hal_modem_at_urc_register(hal_modem_at_t h, const char *prefix,
                               hal_modem_at_urc_cb_t cb, void *user) {
  if (!h || !prefix || !*prefix)
    return false;
  size_t plen = strlen(prefix);
  if (plen >= HAL_MODEM_AT_URC_PREFIX_MAX)
    return false;

  hal_mutex_lock(h->mutex);
  /* If unregistering: find and clear matching slot. */
  if (!cb) {
    for (size_t i = 0; i < HAL_MODEM_AT_MAX_URCS; i++) {
      if (h->urcs[i].in_use && strcmp(h->urcs[i].prefix, prefix) == 0) {
        memset(&h->urcs[i], 0, sizeof(h->urcs[i]));
      }
    }
    hal_mutex_unlock(h->mutex);
    return true;
  }
  /* If already registered for this prefix: update in place. */
  for (size_t i = 0; i < HAL_MODEM_AT_MAX_URCS; i++) {
    if (h->urcs[i].in_use && strcmp(h->urcs[i].prefix, prefix) == 0) {
      h->urcs[i].cb = cb;
      h->urcs[i].user = user;
      hal_mutex_unlock(h->mutex);
      return true;
    }
  }
  /* Otherwise allocate a new slot. */
  for (size_t i = 0; i < HAL_MODEM_AT_MAX_URCS; i++) {
    if (!h->urcs[i].in_use) {
      memcpy(h->urcs[i].prefix, prefix, plen + 1u);
      h->urcs[i].cb = cb;
      h->urcs[i].user = user;
      h->urcs[i].in_use = true;
      hal_mutex_unlock(h->mutex);
      return true;
    }
  }
  hal_mutex_unlock(h->mutex);
  return false;
}

int hal_modem_at_urc_poll(hal_modem_at_t h) {
  if (!h || !h->in_use)
    return 0;
  hal_mutex_lock(h->mutex);
  int before = (int)h->rx_len;
  int consumed = drain_uart(h);
  (void)before;
  /* Count complete lines processed: rough proxy via newline count in
     the consumed window. A more accurate count would require book-
     keeping in dispatch_line(); the wrapping behaviour is good enough
     for the public contract ("number of URC lines processed"). */
  int lines = 0;
  if (consumed > 0) {
    size_t start =
        h->rx_len > (size_t)consumed ? h->rx_len - (size_t)consumed : 0u;
    for (size_t i = start; i < h->rx_len; i++) {
      if (h->cfg.rx_buf[i] == '\n')
        lines++;
    }
  }
  hal_mutex_unlock(h->mutex);
  return lines;
}

void hal_modem_at_set_log_filter(hal_modem_at_t h, const char *const *secrets,
                                 size_t count) {
  if (!h)
    return;
  hal_mutex_lock(h->mutex);
  if (count == 0 || !secrets) {
    h->secrets = NULL;
    h->secret_count = 0;
  } else {
    if (count > HAL_MODEM_AT_MAX_SECRETS)
      count = HAL_MODEM_AT_MAX_SECRETS;
    h->secrets = secrets;
    h->secret_count = count;
  }
  hal_mutex_unlock(h->mutex);
}

void hal_modem_at_set_line_observer(hal_modem_at_t h, hal_modem_at_urc_cb_t cb,
                                    void *user) {
  if (!h)
    return;
  hal_mutex_lock(h->mutex);
  h->line_observer = cb;
  h->line_observer_user = user;
  hal_mutex_unlock(h->mutex);
}

void hal_modem_at_set_tick_callback(hal_modem_at_t h, hal_modem_at_tick_cb_t cb,
                                    void *user) {
  if (!h)
    return;
  hal_mutex_lock(h->mutex);
  h->tick_cb = cb;
  h->tick_user = user;
  hal_mutex_unlock(h->mutex);
}

void hal_modem_at_sleep_ms(hal_modem_at_t h, uint32_t ms) {
  if (!h || !h->tick_cb) {
    hal_delay_ms(ms);
    return;
  }
  uint32_t start = hal_millis();
  for (;;) {
    h->tick_cb(h->tick_user);
    uint32_t elapsed = hal_millis() - start;
    if (elapsed >= ms)
      break;
    uint32_t remain = ms - elapsed;
    hal_delay_ms(remain > 20u ? 20u : remain);
  }
}

#endif /* HAL_ENABLE_CELLULAR_MODEM */
