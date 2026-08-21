# Sync, USB, serial output, framing and auth

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## `hal_sync` - Mutex

```c
#include <hal/system/hal_sync.h>

typedef hal_mutex_impl_t* hal_mutex_t;   // opaque

hal_mutex_t hal_mutex_create(void);
void        hal_mutex_lock(hal_mutex_t mutex);
bool        hal_mutex_try_lock(hal_mutex_t mutex);
void        hal_mutex_unlock(hal_mutex_t mutex);
void        hal_mutex_destroy(hal_mutex_t mutex);
```

**impl/rp2040:** pico SDK `mutex_t` in normal RP2040 builds; FreeRTOS mutex (`xSemaphoreCreateMutex`) in `HAL_ENABLE_FREERTOS + __FREERTOS` builds. Both are non-recursive and synchronize core0/core1 task callers.
**impl/stm32g474:** single-core atomic spinlock in non-FreeRTOS builds; FreeRTOS mutex (`xSemaphoreCreateMutex`) in `HAL_ENABLE_FREERTOS` builds. Both are non-recursive.
**impl/esp32:** ESP-IDF FreeRTOS mutex (`xSemaphoreCreateMutex`), non-recursive
and task-context only.
**impl/.mock:** `std::mutex`.
**FreeRTOS note:** `hal_mutex_*` is FreeRTOS-aware on RP2040/RP2350 and
STM32G474 when `HAL_ENABLE_FREERTOS` selects the pinned kernel, and on ESP32-S3
through the scheduler supplied by pinned ESP-IDF.
`hal_mutex_*` remains task-context only; it is not an ISR API.
Singleton/bus module mutexes use an internal atomic create-once helper where a
defensive lazy fallback is still needed.
`hal_mutex_try_lock()` never waits. Bare-metal interrupt workers may use it;
FreeRTOS backends return `false` when it is called from interrupt context.

### Macros (tools.h)

```c
m_mutex_def(name)            // static hal_mutex_t name = NULL
m_mutex_init(name)           // name = hal_mutex_create()
m_mutex_enter_blocking(name) // hal_mutex_lock(name)
m_mutex_exit(name)           // hal_mutex_unlock(name)
```

### Critical section (hard target interrupt section)

```c
void hal_critical_section_enter(void);  // save and disable interrupts
void hal_critical_section_exit(void);   // restore prior interrupt state
```

**impl/rp2040:** nesting-safe, per-core `save_and_disable_interrupts()` /
`restore_interrupts()` (pico SDK), including FreeRTOS builds.
**impl/stm32g474:** nesting-safe PRIMASK full interrupt mask, including
FreeRTOS builds.
**impl/esp32:** nesting-safe ESP-IDF `portMUX_TYPE` critical section shared by
both cores, with per-core depth tracking.
**impl/.mock:** no-ops.
**Note:** This uses each target's hard interrupt-critical mechanism for short
timing-sensitive or ISR-shared sections. ESP32-S3 also serializes both cores
with its shared portMUX; RP2040 masks only the calling core. It is not a
FreeRTOS scheduler lock; use `hal_mutex_t` for task mutual exclusion.

### Examples

**Example: Protect shared state with `hal_mutex_t`**
```c
#include <hal/system/hal_sync.h>

static hal_mutex_t s_stats_mutex;
static uint32_t s_success_count = 0;

void stats_init(void) {
  s_stats_mutex = hal_mutex_create();
}

void stats_note_success(void) {
  hal_mutex_lock(s_stats_mutex);
  s_success_count++;
  hal_mutex_unlock(s_stats_mutex);
}

uint32_t stats_snapshot(void) {
  uint32_t copy;

  hal_mutex_lock(s_stats_mutex);
  copy = s_success_count;
  hal_mutex_unlock(s_stats_mutex);

  return copy;
}
```

**Example: Short interrupt-masked section for ISR-shared flags**
```c
#include <hal/system/hal_sync.h>

static volatile bool s_alarm_fired = false;

void alarm_isr_hook(void) {
  hal_critical_section_enter();
  s_alarm_fired = true;
  hal_critical_section_exit();
}

bool consume_alarm_flag(void) {
  bool fired;

  hal_critical_section_enter();
  fired = s_alarm_fired;
  s_alarm_fired = false;
  hal_critical_section_exit();

  return fired;
}
```

---

## `hal_usb` - USB device lifecycle and CDC

```c
#include <hal/usb/hal_usb.h>

hal_status_t hal_usb_init(void);
hal_status_t hal_usb_deinit(void);
hal_status_t hal_usb_task(void);
hal_status_t hal_usb_cdc_is_connected(bool *out_connected);
hal_status_t hal_usb_cdc_available(size_t *out_available);
hal_status_t hal_usb_cdc_read(uint8_t *data, size_t capacity,
                              size_t *out_read);
hal_status_t hal_usb_cdc_write(const uint8_t *data, size_t length,
                               uint32_t timeout_ms, size_t *out_written);
hal_status_t hal_usb_cdc_flush(uint32_t timeout_ms);
hal_status_t hal_usb_reset_to_bootloader(void);

typedef void (*hal_usb_bootloader_reset_hook_t)(void *user);
hal_status_t hal_usb_set_bootloader_reset_hook(
    hal_usb_bootloader_reset_hook_t hook, void *user);
```

The native RP backend is the sole TinyUSB owner. It supplies CDC descriptors,
serial identity, a mutex-protected foreground pump, a low-priority IRQ/timer
background pump, bounded transmit backpressure and 1200-bps DTR-triggered
BOOTSEL reset. `hal_usb_init()` publishes the USB runtime board capability.

`hal_usb_set_bootloader_reset_hook()` registers an optional observer invoked
immediately before a bootloader reset (both the 1200-bps DTR trigger and
`hal_usb_reset_to_bootloader()`). It is intended for application shutdown
bookkeeping and host/mock tests; the hook must not block. Passing `NULL`
clears the registration.

STM32G474 currently returns `HAL_EUNSUPPORTED`; the host mock provides
deterministic RX/TX buffers and reset observation for unit tests.
ESP32-S3 does not expose this public USB lifecycle: the target rejects
`HAL_ENABLE_USB`, while its debug console uses the startup-owned ESP-IDF USB
Serial/JTAG VFS described below.

---

## `hal_serial` - Serial & debug output

```c
#include <hal/serial/hal_serial.h>

// Configurable debug sizing knobs (define before including if needed):
// #define HAL_DEBUG_BUF_SIZE    1024   // legacy mock capture/RX helper buffer
// #define HAL_DEBUG_PREFIX_SIZE  16
// #define HAL_DEBUG_DEFAULT_BAUD 9600   // used by lazy init
// #define HAL_DEBUG_RATE_LIMIT_SOURCES_MAX 16
// #define HAL_DEBUG_RATE_LIMIT_SOURCE_NAME_MAX 24
// #define HAL_DEBUG_ISR_SLOT_COUNT 64u  // SPSC ring slots for ISR-deferred logs (>= 2)
// #define HAL_DEBUG_ISR_TEXT_MAX  160u  // per-record payload (incl. NUL terminator)

typedef struct {
    uint16_t full_logs_limit;   // default 5
    uint32_t min_gap_ms;        // default 1000 ms
    uint32_t summary_every_ms;  // default 30000 ms
} hal_debug_rate_limit_t;

void hal_serial_begin(uint32_t baud);
void hal_serial_set_flush(bool enabled);
void hal_serial_print(const char *s);
void hal_serial_println(const char *s);
int  hal_serial_available(void);   // bytes waiting in RX buffer
int  hal_serial_read(void);        // read one byte, or -1 if empty

hal_debug_rate_limit_t hal_debug_rate_limit_defaults(void);
const hal_debug_rate_limit_t *hal_debug_get_rate_limit(void);

void hal_debug_init(uint32_t baud, const hal_debug_rate_limit_t *cfg = 0);
// cfg == 0 -> defaults

bool hal_deb_is_initialized(void);        // query init state
void hal_deb_set_prefix(const char *prefix);
void hal_deb(const char *format, ...);    // printf-style, streamed, thread-safe
void hal_derr(const char *format, ...);   // same but prefixes "ERROR! "
void hal_derr_limited(const char *source, const char *format, ...);
// source is caller-defined free-form tag (e.g. "gps", "can"); 0 -> "global"
void hal_deb_hex(const char *prefix, const uint8_t *buf, int len, int maxBytes);
// logs: "<prefix> len=<n> bytes: XX XX ...", maxBytes is clamped to 1..48

void hal_debug_loop(void);  // drain ISR-deferred debug records (call from main loop)
```

### Task-context debug formatting

In task context, `hal_deb()`, `hal_derr()` and the full-message path of
`hal_derr_limited()` no longer build the whole formatted log line in a fixed
`HAL_DEBUG_BUF_SIZE` buffer. The shared serial/debug core streams output
directly into the mutex-protected transport writer:

- literal spans and `%s` payloads are emitted in chunks without a whole-line
  staging buffer
- numeric, floating-point and pointer conversions use a small per-conversion
  local buffer, with a temporary exact-size fallback only when a single
  conversion does not fit
- prefixes (`hal_deb_set_prefix()`, `ERROR!`, timestamps and rate-limit source
  tags) are emitted as separate stream fragments under the same TX mutex, so a
  logical log line still cannot interleave with another serial emitter

`HAL_DEBUG_BUF_SIZE` is therefore not a task-log length limit anymore. It
remains a compatibility sizing knob for mock capture/RX helpers. ISR-deferred
records are intentionally still bounded by `HAL_DEBUG_ISR_TEXT_MAX`, because
the ISR path must not allocate, lock or touch the serial transport.

### ISR-deferred debug logging

`hal_deb()`, `hal_derr()` and `hal_derr_limited()` may now be called from interrupt context, but still - **you should avoid that**. They detect ISR context via `hal_in_isr()` and on
the hot path do **no** mutex acquisition, **no** lazy init, **no**
timestamp hook, **no** rate-limiter table lookup, and **no** UART I/O.
The formatted payload is enqueued into the shared core's single-producer /
single-consumer (SPSC) lock-free ring (`HAL_DEBUG_ISR_SLOT_COUNT`
slots × `HAL_DEBUG_ISR_TEXT_MAX` bytes each, default 64 × 160 B) using
release/acquire atomics. For `hal_derr_limited()` the `[source]` tag
is baked into the queued text up front, since the global rate-limiter
is bypassed in ISR context.

`hal_debug_loop()` drains the ring from task context and emits each
record using the normal mutex-protected serial path. Every drained
line is annotated with `[ISR ts=<micros>]` (the original event time,
not "now") and respects the current `hal_deb_set_prefix()` for
debug records and the standard `ERROR! ` marker for error records.
When the producer overruns the ring it bumps an internal counter and
the next drain emits one summary line
`"ERROR! [ISR] dropped N debug message(s)"` before resetting the
counter. When `hal_debug_set_muted(true)` is active, both the
producer (silently drops, no ring write, no drop-counter bump) and
the consumer (discards pending records and clears the drop counter)
behave as no-ops.

`hal_debug_loop()` is **safe to call from the very first iteration**
of the main loop, even when no `hal_debug_init()` / `hal_deb()` /
`hal_derr()` has been called yet: the emit path performs the same
lazy init as `hal_deb()`, and the in-ISR / muted short-circuits use
only zero-initialised statics. Calling it from ISR context is itself
a no-op (prevents drain re-entry via the underlying UART mutex).

**Mock-only ring introspection helpers** (declared in `hal_mock.h`):

```c
size_t   hal_mock_debug_isr_used_slots(void);            // pending records
size_t   hal_mock_debug_isr_capacity(void);              // current ring capacity
uint32_t hal_mock_debug_isr_dropped(void);               // overflow counter (peek)
void     hal_mock_debug_isr_reset(void);                 // clear head/tail/dropped
void     hal_mock_debug_isr_set_test_capacity(size_t);   // swap to a small test ring
void     hal_mock_debug_isr_restore_default_ring(void);  // restore production ring
```

### Lazy initialisation

`hal_deb()` and `hal_derr()` use **lazy init** - if `hal_debug_init()` has not been called
before the first debug print, it is invoked automatically with `HAL_DEBUG_DEFAULT_BAUD`
(default 9600, overridable via `-D`). The lazy init path and singleton mutex
publication are atomically gated on RP2040/RP2350, STM32G474, ESP32-S3, and
mock, so two tasks or cores do not concurrently reset the debug state or leak
competing mutex allocations. Calling `debugInit()` is no longer mandatory.

`hal_derr_limited()` reuses the same lazy init and applies global rate-limit config per
error source tag (`source`) so errors from different modules do not suppress each other.

### TX serialization (R1.8)

`hal_serial_print()` and `hal_serial_println()` take the shared core's single
global TX mutex around the underlying debug-console write path. The linked RP
port writes through `hal_usb` CDC, while ESP32-S3, STM32, and mock select their
respective transport ports. This serialises every emitter that reaches the
wire - the debug helpers (`hal_deb`, `hal_derr`,
`hal_derr_limited`), the framed session helper
(`hal_serial_session_println`), and any direct caller - against each
other.

Without this lock, on dual-core RP2040 a `hal_deb` from core 1 could
interleave its bytes mid-frame with a session reply being emitted by
core 0, producing single-byte CDC drops that broke `$SC,...*<crc>`
CRCs and forced the host to retry every command. The per-function
mutexes (`s_deb_mutex`, `s_derr_mutex`) serialize debug helper state, but
they do not by themselves stop unrelated callers from racing the actual
transport writes.

The TX mutex is created through the same atomic create-once helper used by
other singleton module locks (or eagerly in `hal_debug_init()` when called
explicitly), so callers that emit during very early bring-up still see a valid
lock. It is strictly
nested **inside** `s_deb_mutex` / `s_derr_mutex` / `s_rl_mutex`, never
the other way around, so deadlock is impossible.

On RP USB-CDC backends, the mutex window can additionally include an
extra `hal_usb_cdc_flush()` after every `hal_serial_print()` /
`hal_serial_println()`. This is disabled by default and can be changed
at runtime with `hal_serial_set_flush(bool enabled)`. The RP2040 write
loop still kicks the CDC FIFO internally so short packets are actually
started; the optional flush is the compatibility knob for applications
that want an extra transport poll before the TX mutex is released.

Leaving `hal_serial_set_flush(false)` keeps the RP backend on its default
path and avoids the optional extra poll/flush while preserving the TX mutex.
It does not bypass the write loop's bounded retry when the CDC FIFO is full.
On ESP32-S3 the option maps to `fsync(stdout)` on the startup console VFS. On
STM32G474, enabling it waits for USART2's transmission-complete flag after each
message. This is useful before STOP changes the peripheral clock or an
application disables the console. The mock backend accepts the setter without
target timing semantics.

### Shared core and link-time transport ports

`src/hal/serial/hal_serial.cpp` is the only serial/debug core. It owns public serial
and debug entry points, streamed formatting, prefixes, timestamp hooks, mute
state, rate-limit slots, the ISR SPSC ring, net-console mirroring, lazy init and
all common mutexes. The internal `jh_serial_port.h` contract is resolved at
link time and deliberately exposes only transport operations: begin/configure,
logical message boundary, byte write, target line ending/flush and byte RX.

The production ports are intentionally small:

- `impl/rp2040/hal_serial.cpp` owns USB CDC begin, TX/RX and optional flush;
- `impl/esp32/hal_serial.cpp` reuses the ESP-IDF startup-owned USB Serial/JTAG
  console VFS, adopts the official buffered `usb_serial_jtag_driver` or installs
  that single driver if absent, and never registers a second VFS owner. The
  baud argument is informational, RX is non-blocking with a 256-byte HAL
  buffer, and optional flush maps to `fsync(stdout)`;
- `impl/stm32g474/hal_serial.cpp` owns USART2 on hardware, host stdout for
  target sanity builds, and the currently unsupported RX result;
- `impl/.mock/hal_serial.cpp` owns deterministic last-message capture, stdout
  observation and injectable binary RX.

Line endings remain transport-specific: RP and STM32 hardware emit `\r\n`,
while ESP32-S3, host-style STM32, and mock output use `\n`. Mock capture
intentionally stores the message without its line ending, matching the
historical test API.
The RP assertion path uses the same raw transport and net-console mirror but
does not acquire the TX mutex, so fault output cannot block on a lock held by
the failing context.

Limiter implementation details:
- source matching uses `hash + source string` (collision-safe lookup)
- limiter state is protected by an internal mutex (thread-safe)
- when `HAL_DEBUG_RATE_LIMIT_SOURCES_MAX` is exhausted, new sources are grouped into
    an internal `overflow` bucket instead of reusing unrelated source state

**Debug helpers in `tools.h` / `tools_c.h`:**
```c
void  debugInit(void);                          // wrapper around hal_debug_init(HAL_DEBUG_DEFAULT_BAUD)
void  setDebugPrefixWithColon(const char *moduleName); // appends ':' and forwards to hal_deb_set_prefix()

#define deb            hal_deb
#define derr           hal_derr
#define derr_limited   hal_derr_limited
#define setDebugPrefix hal_deb_set_prefix
```

`setDebugPrefixWithColon(...)` truncates the module name if needed so the
generated `<module>:` prefix always fits inside `HAL_DEBUG_PREFIX_SIZE`.

The architecture and concurrency contract is covered by
`test_serial_architecture`, `test_hal_serial`, and the FreeRTOS POSIX runtime
test. They prevent target-local debug cores from returning and exercise lazy
mutex publication, complete message boundaries, ISR FIFO/overflow summaries,
mute behavior, target line boundaries and mock binary RX.

### Error Handling Policy

- `HAL_ASSERT(...)` is used for critical programming errors in core primitives
    (e.g. NULL mutex in sync paths).
- Soft validation + error log is used for noncritical runtime misuse in peripheral
    APIs where continuing execution is acceptable.
- `hal_derr(...)` prints every error (no suppression).
- `hal_derr_limited(source, ...)` should be preferred for potentially repetitive
    noncritical errors to avoid log flooding.

### Examples

**Example: Initialize debug output with a custom rate limit**
```c
#include <hal/serial/hal_serial.h>

void debug_setup(void) {
  hal_debug_rate_limit_t cfg = hal_debug_rate_limit_defaults();

  cfg.full_logs_limit = 3;
  cfg.min_gap_ms = 2000;
  cfg.summary_every_ms = 10000;

  hal_debug_init(115200, &cfg);
  hal_deb_set_prefix("net");
  // Optional on RP2040 when an extra USB CDC flush/task poll is desired:
  // hal_serial_set_flush(true);
  hal_deb("debug channel ready");
}
```

**Example: Main loop with RX polling and deferred ISR log drain**
```c
#include <hal/serial/hal_serial.h>

void app_loop(void) {
  hal_debug_loop();

  while (hal_serial_available() > 0) {
    int ch = hal_serial_read();
    if (ch == 'r') {
      hal_deb("received restart request");
    }
  }
}
```

---

## `hal_serial_session` - Framed serial session helper

```c
#include <hal/serial/hal_serial_session.h>

#define HAL_SERIAL_SESSION_PROTOCOL_VERSION 1u
#define HAL_SERIAL_SESSION_MAX_LINE         128u
#define HAL_SERIAL_SESSION_UNKNOWN          "unknown"

typedef void (*hal_serial_session_unknown_cb_t)(const char *line, void *user);

typedef struct {
    bool        active;
    uint32_t    session_id;
    uint32_t    hello_counter;
    uint32_t    last_activity_ms;
    uint8_t     line_len;
    char        line[HAL_SERIAL_SESSION_MAX_LINE + 1u];
    const char *module_tag;   // bound at init
    const char *fw_version;   // bound at init (may be NULL -> "unknown")
    const char *build_id;     // bound at init (may be NULL -> "unknown")
    uint8_t     uid_bytes[HAL_DEVICE_UID_BYTES];       // captured at init (auth)
    char        uid_hex[HAL_DEVICE_UID_HEX_BUF_SIZE];  // captured at init
    hal_serial_session_unknown_cb_t unknown_handler;   // optional sink
    void       *unknown_user;
    bool        in_request;   // gates `hal_serial_session_println`
    uint16_t    request_seq;  // seq echoed in framed replies
    const hal_serial_session_vocabulary_t *vocab;
    // Authentication state (Phase 3)
    bool        authenticated;
    bool        challenge_pending;
    uint8_t     challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
    uint32_t    auth_counter; // successfully issued random challenges
    uint32_t    auth_failures;
} hal_serial_session_t;

void     hal_serial_session_init(hal_serial_session_t *session,
                                 const char *module_tag,
                                 const char *fw_version,
                                 const char *build_id);
void     hal_serial_session_init_with_vocabulary(
                                 hal_serial_session_t *session,
                                 const char *module_tag,
                                 const char *fw_version,
                                 const char *build_id,
                                 const hal_serial_session_vocabulary_t *vocab);
void     hal_serial_session_set_unknown_handler(hal_serial_session_t *s,
                                                hal_serial_session_unknown_cb_t cb,
                                                void *user);
bool     hal_serial_session_is_active(const hal_serial_session_t *session);
bool     hal_serial_session_is_authenticated(const hal_serial_session_t *session);
uint32_t hal_serial_session_id(const hal_serial_session_t *session);
void     hal_serial_session_poll(hal_serial_session_t *session);
void     hal_serial_session_println(hal_serial_session_t *session,
                                    const char *payload);
```

Wire protocol (both directions):

    $SC,<seq>,<inner>*<crc8>\n

See [`hal_serial_frame`](#halserialframe-wire-framing-helpers) for the
frame codec.

Built-in command (always recognised, structural):
- `HELLO` - activates the session, mints a fresh `session_id`, and emits
  the identity response.

The HELLO response is the only structurally-fixed reply (its
`module=... proto=... session=... fw=... build=... uid=...` shape is
parsed by every host):

    OK HELLO module=<name> proto=1 session=<id> fw=<ver> build=<id> uid=<hex>

Vocabulary-driven commands (R1.0 + R1.6 + R1.7):
- `cmd_bye` - closes the framed session. Replies with `reply_bye_ok` (when
  set), drops `active`, and clears auth state (when CRYPTO). Always
  succeeds; an inactive session simply repeats the OK reply. BYE lives
  outside `HAL_ENABLE_CRYPTO` so any session can be closed cleanly,
  regardless of whether the AUTH path is compiled in.
- `cmd_auth_begin` - mints a fresh 16-byte challenge for the active session;
  the helper formats the challenge through `reply_auth_challenge_fmt` (must
  contain a `%s` for the hex bytes).
- `cmd_auth_prove <64 hex chars>` - proves the host knows `K_device` for
  this UID. Outcomes go through the vocabulary's reply tokens
  (`reply_auth_ok` on success, one of the `reply_auth_failed_*` family on
  failure, `reply_not_ready_hello_required` if HELLO has not been seen).
- `cmd_reboot_bootloader` - auth-gated. Successful path emits
  `reply_reboot_ok`, drains for ~50 ms, then jumps to the boot ROM
  (BOOTSEL/UF2 mass-storage mode); unauthenticated path emits
  `reply_not_authorized`.

After R1.6 these tokens are NOT hard-coded in JaszczurHAL. They come from
the `hal_serial_session_vocabulary_t` instance the project hands to
`hal_serial_session_init_with_vocabulary`. Any field left NULL (or any
session initialised with the classic `hal_serial_session_init`) makes
the corresponding command unrecognised - the inner payload falls
through to the unknown-line handler. The Fiesta dialect lives in
`Fiesta/src/common/scDefinitions/sc_session_vocabulary.h`
(`fiesta_default_vocabulary`); see the Vocabulary configuration section
below.

Unrecognised inner payloads:
- if a user callback is registered via
  `hal_serial_session_set_unknown_handler`, it receives the unwrapped
  inner line and is responsible for any reply (use
  `hal_serial_session_println` so the reply inherits the request's `<seq>`).
- otherwise the helper emits the vocabulary's `reply_unknown_cmd` (still
  framed). With the classic init this field is NULL, so the unknown line
  is silently dropped - register the callback to observe it.

Non-framed input is silently dropped - there is no plain-text
fall-through. This is intentional: host-side tools are expected to
frame requests, and removing the legacy path eliminates substring
mismatches against debug-log lines.

Identity binding model:
- `module_tag` must not be NULL and must reference a string with static
  lifetime (typically the module's compile-time `MODULE_NAME`).
- `fw_version` and `build_id` may be NULL or empty at init; both are reported
  as `unknown` in that case. When non-NULL, they are captured by pointer and
  must likewise remain valid for the lifetime of the session.
- The device UID hex string is captured by value at init via
  `hal_get_device_uid_hex()` and stored inside the session struct.
- All identity is immutable after init; `hal_serial_session_poll()` takes no
  identity arguments.

Reply gating:
- `hal_serial_session_println` is a no-op outside the request-dispatch
  window (`session->in_request == false`). Modules cannot accidentally
  inject unsolicited bytes into the framed stream; if you need to send
  state asynchronously, do it from the unknown-handler callback in
  response to a request.

Authentication (Phase 3) - opt-in:
- The whole AUTH path is compiled in only when `HAL_ENABLE_CRYPTO` is
  defined. Without it the session struct loses the auth fields, the
  AUTH handlers are not dispatched, and
  `hal_serial_session_is_authenticated()` always returns false. The
  rest of the framed session (HELLO + project-defined commands routed
  through the unknown-line handler) is unaffected.
- The actual command tokens (`cmd_auth_begin`, `cmd_auth_prove`) come
  from the vocabulary instance - Fiesta supplies `"SC_AUTH_BEGIN"` /
  `"SC_AUTH_PROVE"`; a different project can supply different names. A
  NULL token field disables that command and routes the inner line to
  the unknown handler.
- See [`hal_sc_auth`](#halscauth-auth-handshake-helper-opt-in-halenablecrypto)
  for the salt + key-derivation primitives.
- The AUTH_BEGIN handler requires an active (HELLO-acknowledged) session
  and obtains each fresh 16-byte challenge exclusively from the target's
  `jh_secure_random_bytes()` provider. If secure entropy is unavailable, the
  handshake fails closed, any previous authenticated state and pending
  challenge are cleared, and `reply_auth_failed_entropy` is emitted when the
  vocabulary supplies it. There is no deterministic fallback.
- The AUTH_PROVE handler is one-shot per challenge: success or failure
  both consume the pending challenge, so a captured valid response
  cannot be replayed against the same challenge.
- A new HELLO mints a new `session_id` and clears `authenticated` /
  `challenge_pending`. Module code that gates sensitive operations must
  re-check `hal_serial_session_is_authenticated(session)` after every
  command, not just once.
- `auth_failures` counts failed `SC_AUTH_PROVE` attempts; rate-limit and
  lockout policies on top of it are deferred to Phase 7.

Vocabulary configuration (R1.0 + R1.6 + R1.7):
- The inbound command tokens (`cmd_bye`, `cmd_auth_begin`, `cmd_auth_prove`,
  `cmd_reboot_bootloader`) and outbound reply payloads are captured by
  `hal_serial_session_vocabulary_t`. Pass a populated instance to
  `hal_serial_session_init_with_vocabulary()` to enable BYE, AUTH and
  REBOOT_BOOTLOADER handling in the project's preferred dialect.
- R1.6 stripped the historical SC_* defaults from JaszczurHAL.
  `hal_serial_session_vocabulary_default` is now an empty placeholder
  (every field NULL). The classic `hal_serial_session_init()` keeps
  working for HELLO-only sessions: HELLO is structural and not
  vocabulary-driven, but AUTH and REBOOT commands fall through to the
  unknown-line handler when no vocabulary is supplied.
- Per-field NULL means "this command is not recognised" / "this reply
  is not emitted". Callers that want partial AUTH support can leave
  command fields NULL; the helper will skip those branches and keep
  the rest of the dialect intact.
- HELLO and the `OK HELLO module=... proto=... session=... fw=... build=...
  uid=...` reply are intentionally NOT configurable: their structure is
  parsed by every host and is treated as part of the protocol contract.
- Reply strings ending in `_fmt` (currently only `reply_auth_challenge_fmt`)
  are passed to `printf`-family formatters; overrides MUST preserve the
  `%s` placeholder for the hex challenge.
- `reply_auth_failed_entropy` is additive and describes failure before a
  challenge is issued. Existing command and success-response wire formats are
  unchanged.

Notes:
- parser is line-based (`\r` / `\n` terminate a frame),
- public types, configuration and declarations live in
  `hal_serial_session.h`; parsing, dispatch and authentication are compiled
  once in `hal_serial_session.cpp`,
- session id is non-cryptographic and intended for bootstrap tracking only,
- the HELLO inner-payload buffer is sized for the six mandatory fields plus
  reasonable slack; the implementation uses a 192-byte internal buffer.

Typical wiring (firmware module, HELLO + project-specific commands via
unknown-handler - no AUTH/REBOOT):
```c
#include <hal/serial/hal_serial_session.h>

static hal_serial_session_t s_session;

static void on_unknown(const char *inner, void *user) {
    (void)user;
    if (strcmp(inner, "SC_GET_META") == 0) {
        hal_serial_session_println(&s_session, "SC_OK META ...");
    }
}

void configSessionInit(void) {
    hal_serial_session_init(&s_session, MODULE_NAME, FW_VERSION, BUILD_ID);
    hal_serial_session_set_unknown_handler(&s_session, on_unknown, NULL);
}

void configSessionTick(void) {
    hal_serial_session_poll(&s_session);
}
```

For AUTH/REBOOT-capable modules, swap the init for
`hal_serial_session_init_with_vocabulary(&s_session, MODULE_NAME,
FW_VERSION, BUILD_ID, &my_vocab)` where `my_vocab` is the project's
populated `hal_serial_session_vocabulary_t` instance. See the
"Vocabulary configuration" section below.

Test observability (mock backend):
- Build a framed request with `hal_serial_frame_encode(seq, "HELLO", buf,
  sizeof(buf), NULL)`, append `\n`, and feed it via
  `hal_mock_serial_inject_rx(buf, -1)`.
- Inspect `hal_mock_serial_last_line()` and decode it with
  `hal_serial_frame_decode(...)` to assert HELLO response fields
  (`module=`, `proto=`, `session=`, `fw=`, `build=`, `uid=`) and that the
  reply seq matches the request seq.
- Use `hal_mock_set_device_uid(...)` to simulate a different physical board
  when asserting `uid=` values.
- Use `hal_mock_secure_random_set_seed(...)` with status `HAL_OK` for stable
  challenge vectors, or set an error status to exercise entropy failure and
  verify that the session remains unauthenticated with a zeroed challenge.

### Examples

**Example: HELLO + custom command handler**
```c
#include <hal/serial/hal_serial_session.h>
#include <string.h>

static hal_serial_session_t s_session;

static void on_unknown(const char *inner, void *user) {
  (void)user;

  if (strcmp(inner, "SC_GET_STATUS") == 0) {
    hal_serial_session_println(&s_session, "SC_OK STATUS ready=1");
    return;
  }

  hal_serial_session_println(&s_session, "SC_ERR unknown");
}

void sc_init(void) {
  hal_serial_session_init(&s_session, "cfg", "1.2.3", "dev-build");
  hal_serial_session_set_unknown_handler(&s_session, on_unknown, NULL);
}

void sc_tick(void) {
  hal_serial_session_poll(&s_session);
}
```

**Example: Enable vocabulary-driven AUTH / BYE / REBOOT handling**
```c
#include <hal/serial/hal_serial_session.h>
#include <hal/serial/hal_serial_session_vocabulary.h>

static const hal_serial_session_vocabulary_t s_vocab = {
  .cmd_bye = "SC_BYE",
  .reply_bye_ok = "SC_OK BYE",
  .cmd_auth_begin = "SC_AUTH_BEGIN",
  .reply_auth_challenge_fmt = "SC_OK AUTH_CHALLENGE %s",
  .cmd_auth_prove = "SC_AUTH_PROVE",
  .reply_auth_ok = "SC_OK AUTH",
  .reply_auth_failed_no_challenge = "SC_ERR AUTH_NO_CHALLENGE",
  .reply_auth_failed_bad_length = "SC_ERR AUTH_BAD_LENGTH",
  .reply_auth_failed_bad_hex = "SC_ERR AUTH_BAD_HEX",
  .reply_auth_failed_key_derivation = "SC_ERR AUTH_KEY_DERIVATION",
  .reply_auth_failed_mac_compute = "SC_ERR AUTH_MAC_COMPUTE",
  .reply_auth_failed_bad_mac = "SC_ERR AUTH_BAD_MAC",
  .reply_auth_failed_entropy = "SC_ERR AUTH_ENTROPY",
  .cmd_reboot_bootloader = "SC_REBOOT_BOOTLOADER",
  .reply_reboot_ok = "SC_OK REBOOT",
  .reply_not_ready_hello_required = "SC_ERR HELLO_REQUIRED",
  .reply_not_authorized = "SC_ERR NOT_AUTHORIZED",
  .reply_unknown_cmd = "SC_ERR unknown",
};

static hal_serial_session_t s_secure_session;

void secure_sc_init(void) {
  hal_serial_session_init_with_vocabulary(&s_secure_session,
                      "boot",
                      "1.2.3",
                      "dev-build",
                      &s_vocab);
}
```

---

## `hal_serial_frame` - Wire framing helpers

```c
#include <hal/serial/hal_serial_frame.h>

#define HAL_SERIAL_FRAME_PREFIX        "$SC,"
#define HAL_SERIAL_FRAME_PREFIX_LEN    4u
#define HAL_SERIAL_FRAME_PAYLOAD_MAX   256u
#define HAL_SERIAL_FRAME_LINE_MAX      (HAL_SERIAL_FRAME_PAYLOAD_MAX + 32u)

uint8_t hal_serial_frame_crc8(const uint8_t *data, size_t len);

bool    hal_serial_frame_encode(uint16_t seq,
                                const char *payload,
                                char *out, size_t out_size,
                                size_t *out_len);

bool    hal_serial_frame_decode(const char *line,
                                uint16_t *seq_out,
                                char *payload_out,
                                size_t payload_out_size);
```

Frame format (both directions):

    $SC,<seq>,<payload>*<crc8>\n

- Literal start sentinel `$SC,`.
- `<seq>`: ASCII unsigned decimal in `[0..65535]`. The response always
  echoes the request's seq so the host can correlate.
- `<payload>`: free-form ASCII text. Must not contain `*`, `\r` or `\n`.
- `<crc8>`: two uppercase hex digits. CRC-8/CCITT (poly `0x07`, init
  `0x00`, no reflect, no xor-out) over the bytes between (but excluding)
  the leading `$` and the `*` separator. Reference vector:
  `"123456789" -> 0xF4`.
- `\n` line terminator (encode helpers do **not** append it; use
  `hal_serial_println()` which already does).

This header can be mirrored byte-for-byte on the host side. If your
host stack carries a stand-alone copy, keep both sides synchronized;
both sides should assert the same CRC reference vector in test suites.

### Examples

**Example: Encode a framed request**
```c
#include <hal/serial/hal_serial_frame.h>
#include <hal/serial/hal_serial.h>

void send_hello(void) {
  char line[HAL_SERIAL_FRAME_LINE_MAX];
  size_t line_len = 0;

  if (hal_serial_frame_encode(7, "HELLO", line, sizeof(line), &line_len)) {
    hal_serial_println(line);
  }
}
```

**Example: Decode a received frame**
```c
#include <hal/serial/hal_serial_frame.h>
#include <hal/serial/hal_serial.h>

void inspect_line(const char *line) {
  uint16_t seq = 0;
  char payload[HAL_SERIAL_FRAME_PAYLOAD_MAX + 1u];

  if (hal_serial_frame_decode(line, &seq, payload, sizeof(payload))) {
    hal_deb("frame seq=%u payload=%s", (unsigned)seq, payload);
  } else {
    hal_derr("invalid frame: %s", line);
  }
}
```

---

## `hal_sc_auth` - Auth handshake helper  *(opt-in - `HAL_ENABLE_CRYPTO`)*

Pulled in by the same `HAL_ENABLE_CRYPTO` flag as `hal_crypto`. The
module depends on `hal_hmac_sha256`, so enabling auth without crypto
is not a meaningful configuration. When the flag is off
`hal_serial_session` keeps working - the AUTH / REBOOT handlers are
compiled out (no command tokens are recognised regardless of what the
vocabulary supplies) and `hal_serial_session_is_authenticated()`
returns `false`.

```c
#include <hal/security/hal_sc_auth.h>

#define HAL_SC_AUTH_SCHEME_TAG          "FIESTA-SC-AUTH-v1"
#define HAL_SC_AUTH_SCHEME_TAG_LEN      17u
#define HAL_SC_AUTH_SALT                ((const uint8_t *)HAL_SC_AUTH_SCHEME_TAG)
#define HAL_SC_AUTH_SALT_LEN            HAL_SC_AUTH_SCHEME_TAG_LEN
#define HAL_SC_AUTH_KEY_BYTES           HAL_SHA256_DIGEST_BYTES   // 32
#define HAL_SC_AUTH_CHALLENGE_BYTES     16u
#define HAL_SC_AUTH_CHALLENGE_HEX_BUF_SIZE  33u                   // 32 hex + NUL
#define HAL_SC_AUTH_RESPONSE_BYTES      HAL_SHA256_DIGEST_BYTES   // 32
#define HAL_SC_AUTH_RESPONSE_HEX_BUF_SIZE   HAL_SHA256_HEX_BUF_SIZE

bool hal_sc_auth_derive_device_key(
    const uint8_t *uid, size_t uid_len,
    uint8_t out_key[HAL_SC_AUTH_KEY_BYTES]);

bool hal_sc_auth_compute_response(
    const uint8_t device_key[HAL_SC_AUTH_KEY_BYTES],
    const uint8_t *challenge, size_t challenge_len,
    uint32_t session_id,
    uint8_t out_response[HAL_SC_AUTH_RESPONSE_BYTES]);

bool hal_sc_auth_macs_equal(const uint8_t *a, const uint8_t *b, size_t len);
```

Constructions:

- `K_device  = HMAC-SHA256(key=salt, message=uid_bytes)`
- `response  = HMAC-SHA256(key=K_device, message=challenge || session_id_be32)`

The session id is serialised big-endian via `hal_u32_to_bytes_be` so the
firmware and host MAC the exact same byte sequence regardless of host
endianness.

`hal_sc_auth_macs_equal` delegates to the single internal
`jh_constant_time_compare` implementation. Authentication message buffers and
failed outputs are erased through `jh_secure_zeroize` before return.

The salt is a public, project-wide compile-time constant. Secrecy of the
scheme rests on HMAC-SHA256 + the per-device UID, **not** on salt
secrecy. Treating the salt as confidential would only obscure design
intent.

If your host stack carries a mirror copy of this helper, keep both
sides synchronized and test key derivation + response MAC vectors on
both sides. Cross-vector checks catch divergence early and avoid
runtime AUTH_FAILED mismatches during integration.

The handshake itself is wired in
[`hal_serial_session`](#halserialsession-framed-serial-session-helper)
behind the vocabulary's `cmd_auth_begin` / `cmd_auth_prove` slots
(Fiesta names them `SC_AUTH_BEGIN` / `SC_AUTH_PROVE`). Modules consume
authentication state through `hal_serial_session_is_authenticated(...)`
and do not need to call the helpers below directly.

## Examples

**Example: Derive device key and compute AUTH response**
```c
#include <hal/security/hal_sc_auth.h>

bool build_auth_response(const uint8_t *uid,
             size_t uid_len,
             const uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES],
             uint32_t session_id,
             uint8_t out_mac[HAL_SC_AUTH_RESPONSE_BYTES]) {
  uint8_t device_key[HAL_SC_AUTH_KEY_BYTES];

  if (!hal_sc_auth_derive_device_key(uid, uid_len, device_key)) {
    return false;
  }

  return hal_sc_auth_compute_response(device_key,
                    challenge,
                    HAL_SC_AUTH_CHALLENGE_BYTES,
                    session_id,
                    out_mac);
}
```

**Example: Constant-time verification of a host response**
```c
#include <hal/security/hal_sc_auth.h>

bool auth_response_matches(const uint8_t expected[HAL_SC_AUTH_RESPONSE_BYTES],
               const uint8_t actual[HAL_SC_AUTH_RESPONSE_BYTES]) {
  return hal_sc_auth_macs_equal(expected,
                  actual,
                  HAL_SC_AUTH_RESPONSE_BYTES);
}
```

---


---

*Next: [Communication buses](09_buses.md)*
