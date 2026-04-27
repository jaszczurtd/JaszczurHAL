# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] - 2026-04-27 (Fiesta R1.6 - strip SC_* literals from production code)

### Changed
- `hal_serial_session_vocabulary_default` is now an empty placeholder
  (every field NULL). The R1.0 decoupling kept Fiesta's SC_* tokens
  baked in as the per-field NULL fallback; R1.6 finishes the
  decoupling. Projects that need AUTH or REBOOT_BOOTLOADER handlers
  MUST pass a populated vocabulary via
  `hal_serial_session_init_with_vocabulary`. The legacy
  `hal_serial_session_init` still works for HELLO-only sessions.
- Dispatch path in `hal_serial_session.h` gained NULL guards on the
  three command lookups (`cmd_auth_begin`, `cmd_auth_prove`,
  `cmd_reboot_bootloader`) and the one fmt lookup
  (`reply_auth_challenge_fmt`). NULL means "this command is not
  recognised by this session" -> falls through to the unknown-line
  handler. The reply-side `println` calls were already NULL-safe.

### Tests
- `test_hal_serial_session.cpp` introduced a test-local
  `k_test_sc_vocab` fixture carrying the SC_* family verbatim, and an
  `init_session_with_test_vocab` helper. All 24 existing init call
  sites route through it so the suite still asserts on the SC_*
  wire output. JaszczurHAL `src/` now has zero SC_* literals; SC_*
  appears only in `tests/` as fixture data (acceptance criterion
  from the R1.0 pre-flight grep snapshot).
- `test_hal_serial_session_vocabulary.cpp` updated the two cases
  that encoded the R1.0 fallback contract:
  * `test_classic_init_default_vocabulary_is_empty` - verifies the
    new empty-default semantics (unknown-cmd silently dropped,
    HELLO still works structurally).
  * `test_partial_vocab_unset_fields_remain_unrecognised` - verifies
    that NULL fields fall back to NULL (= unrecognised) rather than
    to a built-in SC_* token, so SC_AUTH_BEGIN with a partial vocab
    that doesn't set `cmd_auth_begin` falls through to the unknown
    handler.
- Full HAL ctest 30/30 green.

### Migration
- Fiesta firmware (ECU/Clocks/OilAndSpeed) already passes
  `fiesta_default_vocabulary` explicitly via R1.2/R1.4/R1.5, so this
  change is silent on that side. Other consumers that relied on the
  built-in SC_* defaults need to either supply their own vocabulary
  or accept that AUTH/REBOOT commands fall through to their unknown
  handler.

## [Unreleased] - 2026-04-27 (Fiesta R1.0 - session vocabulary decoupling)

### Added
- `hal_serial_session_vocabulary.h`: optional vocabulary table that lets
  a project override the inbound command tokens (`SC_AUTH_BEGIN`,
  `SC_AUTH_PROVE`, `SC_REBOOT_BOOTLOADER`) and the outbound reply tokens
  (`SC_OK AUTH_OK`, `SC_AUTH_FAILED ...`, `SC_NOT_AUTHORIZED`,
  `SC_NOT_READY HELLO_REQUIRED`, `SC_OK REBOOT`, `SC_OK AUTH_CHALLENGE %s`,
  `SC_UNKNOWN_CMD`) used by the framed session helper. Each field is
  independently optional - NULL falls back to the matching field of the
  exposed `hal_serial_session_vocabulary_default` singleton.
- `hal_serial_session_init_with_vocabulary()`: new entry point that takes
  a vocabulary pointer alongside the existing identity arguments. The
  classic `hal_serial_session_init()` is now a thin wrapper that passes
  NULL, so existing call sites keep their wire behaviour unchanged.
- `test_hal_serial_session_vocabulary`: new ctest target with 9 cases
  covering full override, partial (per-field) NULL fallback, fall-through
  of original `SC_*` literals to the unknown handler when commands are
  renamed, and verifications for unknown / not-ready / not-authorized /
  auth-failed / reboot reply paths.

### Changed
- The framed session helper no longer hard-codes the SC token strings -
  every dispatch / reply site now looks them up via
  `HAL_SERIAL_SESSION_VOCAB(session, field)`. Default behaviour is
  byte-identical: existing `test_hal_serial_session` (24 cases) passes
  unmodified, and the wire output verified by ctest is unchanged (`OK
  HELLO`, `SC_OK AUTH_CHALLENGE …`, `SC_OK AUTH_OK`, `SC_OK REBOOT`,
  `SC_AUTH_FAILED …`, `SC_NOT_AUTHORIZED`, `SC_NOT_READY HELLO_REQUIRED`,
  `SC_UNKNOWN_CMD`).
- HELLO and the structural `OK HELLO module=...` reply remain
  intentionally non-configurable: they encode protocol structure that
  every host parses, not project vocabulary.

### Notes
- HAL itself stays project-agnostic: the vocabulary header has no Fiesta
  scDefinitions dependency, the default singleton is purely a captured
  snapshot of the historical defaults, and consumers that want the old
  behaviour pass NULL (or simply call the classic init).
- This is the foundation for Fiesta refactor R1.1+, which moves the SC
  token strings into a single source of truth on the Fiesta side and
  passes the table at session init.

## [Unreleased] - 2026-04-27 (SerialConfigurator Phase 5)

### Added
- `hal_serial_session`: built-in `SC_REBOOT_BOOTLOADER` framed command,
  gated on `hal_serial_session_is_authenticated()`. Authenticated
  sessions get `SC_OK REBOOT`, a brief drain window, and a call to
  `hal_enter_bootloader()`. Unauthenticated sessions get
  `SC_NOT_AUTHORIZED` and the boot ROM is NOT entered. Like the AUTH
  handlers this surface is wrapped in `#ifdef HAL_ENABLE_CRYPTO`; with
  crypto off the command falls through to the user unknown-handler.
- 4 new `test_hal_serial_session` cases:
  reboot-without-auth-rejected, reboot-after-hello-only-rejected,
  reboot-after-auth-acks-and-enters-bootloader, and
  reboot-blocked-after-new-hello-clears-auth. Suite: 20 -> 24.

### Notes
- `hal_enter_bootloader` already existed; Phase 5 only wires it into
  the framed protocol behind the auth gate. Mock backend keeps its
  observable flag (`hal_mock_bootloader_was_requested`).

## [Unreleased] - 2026-04-26 (HAL_ENABLE_CRYPTO opt-in)

### Changed
- `hal_crypto` is now an **opt-in** module gated by `HAL_ENABLE_CRYPTO`
  (define it in `hal_project_config.h` or via `-D`). Without the flag
  `hal_crypto.h` expands to nothing and `hal_crypto.cpp` compiles to an
  empty translation unit, so projects that never touch crypto pay zero
  code/RAM cost. `hal_sc_auth.h` follows the same gate.
- `hal_serial_session` keeps working without crypto: the
  `SC_AUTH_BEGIN` / `SC_AUTH_PROVE` handlers, the auth state fields
  (`authenticated`, `challenge_pending`, `challenge[]`, `auth_counter`,
  `auth_failures`, `uid_bytes`), and the `hal_sc_auth.h` include all
  collapse to nothing. `hal_serial_session_is_authenticated()` stays
  callable and returns `false` unconditionally when crypto is off.
- `hal.h` and `tools_c.h` now include `hal_crypto.h` only inside an
  `#ifdef HAL_ENABLE_CRYPTO` guard.
- `hal_config.h` documents the new flag in its `HAL_ENABLE_*` section
  alongside the existing `HAL_ENABLE_CJSON`.
- VS Code project templates (`vscode-templates/{linux,windows}`) gained
  a commented-out `HAL_ENABLE_CRYPTO` block so users can see the flag
  next to the existing `HAL_DISABLE_*` list.

### Build / tests
- The host-test `hal_mock` library defines `HAL_ENABLE_CRYPTO` publicly
  so the existing `test_hal_crypto` and the auth cases in
  `test_hal_serial_session` keep running. STM32 and Arduino builds keep
  the flag off by default; consumer projects opt in via their own
  `hal_project_config.h`.

### Migration
- Projects that already use any `hal_*` symbol from `hal_crypto.h`
  (Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20, AEAD) or any
  `hal_sc_auth_*` helper must add `#define HAL_ENABLE_CRYPTO` to their
  `hal_project_config.h`. Without the flag the linker reports the
  helpers as undefined.

## [Unreleased] - 2026-04-26 (SerialConfigurator Phase 3)

### Added
- `hal_crypto`: SHA-256 and HMAC-SHA256 helpers (`hal_sha256`, `hal_sha256_hex`,
  `hal_hmac_sha256`, `hal_hmac_sha256_hex`). Portable C++ implementation
  validated against FIPS 180-2 and RFC 4231 vectors. Bit-stable with the
  host-side copy in SerialConfigurator's `sc_sha256.c`.
- `hal_sc_auth.h` - new header-only helper for the SerialConfigurator
  authentication handshake. Defines the compile-time salt
  (`FIESTA-SC-AUTH-v1`), per-device key derivation
  (`K_device = HMAC-SHA256(salt, uid_bytes)`), challenge/response
  computation (`HMAC-SHA256(K_device, challenge || session_id_be32)`),
  and a constant-time MAC comparison helper. Salt and constants must
  stay byte-for-byte in sync with `src/SerialConfigurator/src/core/sc_auth.h`
  in the Fiesta repo.
- `hal_serial_session_is_authenticated(session)` - public reader for the
  new auth state.
- Built-in framed commands `SC_AUTH_BEGIN` and `SC_AUTH_PROVE <hex>` in
  `hal_serial_session_poll`. Modules consume them automatically through
  the existing wrapper - no per-module rollout work required.

### Changed
- `hal_serial_session_t` gained five auth fields (`authenticated`,
  `challenge_pending`, `challenge[16]`, `auth_counter`, `auth_failures`)
  plus a cached binary `uid_bytes` used for in-RAM key derivation. A new
  HELLO mints a new `session_id` and clears `authenticated` /
  `challenge_pending`.

### Tests
- 7 new cases in `test_hal_serial_session` covering AUTH_BEGIN gating,
  challenge issuance, fresh-challenge-per-BEGIN, correct-MAC success,
  bad-MAC rejection with one-shot challenge consumption, malformed
  payload rejection, and auth-clear-on-new-HELLO. Suite size: 13 -> 20.

## [Unreleased] - 2026-04-26 (SerialConfigurator Phase 2 - framed protocol)

### Added
- `hal_serial_frame.h` - new header-only wire-framing helpers shared with
  the SerialConfigurator host. Exposes `hal_serial_frame_encode`,
  `hal_serial_frame_decode`, and `hal_serial_frame_crc8` along with the
  constants `HAL_SERIAL_FRAME_PREFIX` (`"$SC,"`),
  `HAL_SERIAL_FRAME_PREFIX_LEN`, `HAL_SERIAL_FRAME_PAYLOAD_MAX` (256) and
  `HAL_SERIAL_FRAME_LINE_MAX`. Frame format:
  `$SC,<seq>,<payload>*<crc8>\n`. CRC-8/CCITT (poly `0x07`, init `0x00`,
  no reflect, no xor-out) over the bytes between the leading `$` and the
  `*` separator. The `"123456789" -> 0xF4` reference vector is asserted by
  the host-test suite.
- `hal_serial_session_set_unknown_handler(session, cb, user)` - register
  a per-module sink for unrecognised inner payloads (used by ECU/Clocks/
  OilAndSpeed to implement their `SC_*` command sets).
- `hal_serial_session_println(session, payload)` - emit one inner payload
  as a framed reply that echoes the in-flight request's `<seq>`. No-op
  outside the request-dispatch window.

### Changed
- **BREAKING:** `hal_serial_session.h` is now framed-only. Lines that do
  not start with the `$SC,` sentinel are silently discarded by
  `hal_serial_session_poll`. The previous plain-text fall-through (which
  replied `ERR UNKNOWN`) has been removed; the SerialConfigurator host
  always frames its requests, and dropping the legacy path eliminates a
  class of bugs (substring matches against debug-log lines, unframed
  bytes corrupting the stream, modules accidentally responding to noise).
- **BREAKING:** Default reply for unrecognised framed payloads changed
  from `ERR UNKNOWN` (plain text) to `SC_UNKNOWN_CMD` (inside the frame
  envelope, echoing the inbound `<seq>`).
- `HAL_SERIAL_SESSION_MAX_LINE` widened from 48 to 128 bytes to
  accommodate the framing overhead and longer SC payloads.
- `hal_serial_session_t` gained two new fields:
  - `bool in_request` - gates `hal_serial_session_println` so modules
    cannot inject unsolicited bytes,
  - `uint16_t request_seq` - the seq carried in the in-flight request,
    automatically used by `hal_serial_session_println` to correlate
    replies.
- `hal_serial_session.h` documentation rewritten to describe the framed
  protocol (see updated `JaszczurHAL_API.md` §`hal_serial_session`).

### Tests
- `test_hal_serial_session` rewritten for the framed protocol: round-trip
  HELLO with CRC validation, request<->response seq echo, custom unknown
  handler dispatch and reply seq inheritance, default `SC_UNKNOWN_CMD`
  reply when no handler is registered, and silent drop of non-framed
  input. New independent CRC reference vector locks the wire format on
  the firmware side.

## [Unreleased] - 2026-04-24

### Added
- `hal_get_device_uid(uid[8])` and `hal_get_device_uid_hex(buf, buflen)` -
  read the RP2040 64-bit flash unique id, either as a raw 8-byte buffer or
  as an uppercase 16-character hex string. Arduino backend wraps
  `pico_get_unique_board_id()`; mock backend returns a deterministic default
  (`E661A4D1234567AB`), overridable via `hal_mock_set_device_uid()` and
  resettable via `hal_mock_reset_device_uid()`. New constants
  `HAL_DEVICE_UID_BYTES` (8) and `HAL_DEVICE_UID_HEX_BUF_SIZE` (17) exposed
  for buffer sizing.
- `hal_serial_session_init()` now takes three additional parameters:
  `module_tag`, `fw_version`, `build_id`. These identity fields are captured
  once at init time, cached in `hal_serial_session_t`, and reported on every
  accepted HELLO. `hal_serial_session_poll()` correspondingly drops its
  `module_tag` parameter. The session context also caches the device UID hex
  string at init time via `hal_get_device_uid_hex()`.
- Extended HELLO response format (still plain text, still one line):
  `OK HELLO module=<name> proto=1 session=<id> fw=<ver> build=<id> uid=<hex>`.
  When `fw_version` or `build_id` is NULL/empty at init, the field reports
  `unknown`. The response buffer was widened from 96 B to 192 B.
- `hal_serial_session.h` constant `HAL_SERIAL_SESSION_UNKNOWN` ("unknown")
  - fallback used by the HELLO response when identity fields are NULL/empty.
- Tests: 7 new tests in `test_hal_system` covering
  `hal_get_device_uid*` (default pattern, injected value, NULL-safety,
  hex formatting uppercase, small-buffer rejection, NULL-buffer safety);
  `test_hal_serial_session` rewritten to cover the new init signature,
  the `unknown` fallback for NULL identity, the `uid=` field (default and
  injected), and full HELLO field assertions. Total HAL host-test count
  unchanged at 28 suites; net new assertions.
- `setDebugPrefixWithColon(moduleName)` - utility helper in `tools_api.h` /
  `tools.cpp` that appends `:` to a module tag and forwards the result to
  `hal_deb_set_prefix()`. Intended to replace repetitive local-buffer +
  `concatStrings(..., MODULE_NAME, ":")` setup code in clients.
- `hal_enter_bootloader()` - HAL entry point for RP2040 BOOTSEL/UF2 reboot.
  Arduino backend calls `reset_usb_boot(0, 0)` and does not return;
  mock backend sets an observable flag. Exposed via `hal_system.h`.
- Mock helpers for bootloader observability:
  `hal_mock_bootloader_was_requested()` and
  `hal_mock_bootloader_reset_flag()`.
- `hal_serial_session.h` - header-only text session helper for desktop
  configurator bootstrap. Parses line-oriented input (`\r`/`\n`), handles
  the `HELLO` command, returns `ERR UNKNOWN` for unsupported text.
  Session state (`hal_serial_session_t`) carries active flag, session id,
  hello counter, last-activity timestamp, RX line buffer, and identity
  pointers (module_tag / fw_version / build_id) + cached UID hex.
- `concatStrings(dst, dst_size, src_a, src_b)` - bounded string concat
  helper in `tools.cpp` with full unit-test coverage (zero-length src,
  exact-fit, truncation, NULL args).
- `hal_i2c_write_byte(address, data, *outWriteOk)` /
  `hal_i2c_write_byte_bus(bus, address, data, *outWriteOk)` - one-shot
  convenience helper that performs the common "beginTransmission +
  write one byte + endTransmission" sequence. The internal I2C mutex
  is acquired and released automatically; the optional `outWriteOk`
  pointer receives the queue-byte status, and the return value is the
  `endTransmission` error code. Extracted from ECU PCF8574/Adjustometer
  call sites that previously open-coded the three-step pattern.
- `hal_i2c_read_byte(address, *outReadOk)` /
  `hal_i2c_read_byte_bus(bus, address, *outReadOk)` - symmetric read
  counterpart: requests and returns a single byte from a slave. The
  optional `outReadOk` pointer lets callers distinguish a valid `0x00`
  value from a failed transaction (request_from short read or
  `hal_i2c_read()` returning -1). Extracted from ECU PCF8574 read path.
- Tests: 6 new `hal_i2c_write_byte` tests (begin/write/end sequence,
  busy-bus NACK path, NULL out-flag, lock-depth balance, per-bus
  routing, transaction-count increment) and 6 new `hal_i2c_read_byte`
  tests (injected-byte round-trip, zero value vs. failure disambiguation,
  NULL out-flag, lock-depth balance, per-bus routing, transaction-count
  increment), plus 1 test verifying that `hal_i2c_read_byte()` holds
  the mutex during the actual byte read. Total I2C test count: 30.
- `hal_i2c_bus_clear(sda_pin, scl_pin)` /
  `hal_i2c_bus_clear_bus(bus, sda_pin, scl_pin)` - I2C bus clear
  procedure per I2C specification §3.1.16: toggles SCL up to 9 times
  at GPIO level to release a slave holding SDA low, then generates a
  STOP condition. Must be called before `hal_i2c_init()`.
  Arduino implementation uses native Arduino GPIO primitives.
- Mock: `hal_mock_i2c_get_bus_clear_count()` /
  `hal_mock_i2c_get_bus_clear_count_bus(bus)` - return the number of
  `hal_i2c_bus_clear()` calls since the last `hal_i2c_init()`.
- Tests: 3 new I2C bus clear tests (count, reset-on-init, bus
  independence). Total I2C test count: 17.
- `hal_serial_available()` - return the number of bytes available for
  reading from the serial port (wraps `Serial.available()`).
- `hal_serial_read()` - read one byte from the serial port; returns
  0–255 or -1 when empty (wraps `Serial.read()`).
- `float_to_u32()` / `u32_to_float()` - `static inline` bitcast helpers
  (float ↔ uint32_t via memcpy) in `tools_api.h`.
- Mock: `hal_mock_serial_inject_rx(data, len)` - inject bytes into the
  mock serial RX buffer for testing `hal_serial_available/read`.
- Tests: `test_tools` now verifies that `setDebugPrefixWithColon("ECU")`
  produces the expected `ECU:` debug prefix.
- `hal_i2c_slave_get_transaction_count()` and
  `hal_i2c_slave_get_transaction_count_bus(uint8_t bus)` - return the
  number of completed I2C bus transactions (master reads and writes)
  since initialisation. Incremented inside the Wire `onReceive` /
  `onRequest` callbacks, so the counter reflects genuine bus activity.
  Resets to 0 on `hal_i2c_slave_init*()`. Wraps at `UINT32_MAX`.
  Thread-safe (atomic access).
- `hal_i2c_get_transaction_count()` and
  `hal_i2c_get_transaction_count_bus(uint8_t bus)` - symmetric API
  for the I2C master (controller) side. Incremented on every
  `hal_i2c_end_transmission*()` (write) and `hal_i2c_request_from*()`
  (read). Resets to 0 on `hal_i2c_init*()`. Wraps at `UINT32_MAX`.
- `hal_gpio_set_irq_priority(priority)` and `hal_irq_priority_t`
  (`HAL_IRQ_PRIORITY_HIGHEST` / `HIGH` / `DEFAULT` / `LOW`) -
  configurable NVIC priority for the GPIO interrupt bank. On RP2040 all
  GPIO pins share `IO_IRQ_BANK0`; call after `hal_gpio_attach_interrupt()`.
  Raising priority above other peripherals (e.g. I2C) prevents their
  ISRs from blocking edge counting. No-op on platforms without
  configurable IRQ priorities. Mock backend exposes the configured
  value for testing.

### Changed
- **Breaking API**: `hal_serial_session_init()` signature changed from
  `(session)` to `(session, module_tag, fw_version, build_id)`; identity is
  now bound once at init rather than re-passed on every poll.
  `hal_serial_session_poll()` signature changed from
  `(session, module_tag)` to `(session)`. Callers in downstream projects
  (ECU / Clocks / OilAndSpeed wrappers in Fiesta) must be updated together.
- HELLO response format extended (additive): now always carries `fw=`,
  `build=`, `uid=` fields in addition to `module=`, `proto=`, `session=`.
  Existing parsers that only looked for `module=` / `session=` prefixes keep
  working; parsers that checked the full line end-of-string must be updated.
- Documentation updated to cover `setDebugPrefixWithColon(...)`, fix the
  published `src/` / `hal/impl/` layout in the API reference, and align the
  utility include guidance (`tools.h` vs `tools_c.h`) with the actual tree.
- `hal_pwm_freq_create()` no longer starts the PWM slice immediately.
  The GPIO function and slice enable are deferred until the first
  `hal_pwm_freq_write()` call, preventing a power-on glitch on pins
  with inverted logic (0 % duty = actuator ON).
- `hal_i2c_slave_reg_write8[_bus]()` and
  `hal_i2c_slave_reg_write16[_bus]()` return `void` again (reverts the
  short-lived return-value approach: writes always target the local
  register-map buffer and therefore always succeed, making return values
  meaningless for detecting real bus activity).

### Fixed
- stm32g474 backend source files now compile as symbol-empty units on
  non-STM32 Arduino targets. This avoids duplicate-definition link failures
  when platform build systems compile all backend directories under `src/`.
- stm32g474 mock serial RX path (`hal_serial.cpp`) now guards index ranges and
  normalizes requested lengths before buffer access, removing host-compiler
  array-bounds warnings and preventing potential out-of-bounds reads.
- Mock `hal_i2c_end_transmission_bus()` now returns 2 (NACK on address)
  when the mock busy flag is set. Previously it always returned 0,
  making it impossible to test I2C error paths.
- `hal_i2c_read_byte()` / `hal_i2c_read_byte_bus()` now hold the internal
  I2C mutex across the full one-shot `request + read` sequence, making
  the helper atomic on a given bus (symmetric with `hal_i2c_write_byte()`).
- Mock: added `hal_mock_i2c_get_read_byte_lock_depth()` /
  `hal_mock_i2c_get_read_byte_lock_depth_bus()` to expose lock-depth
  captured at the byte-read point inside one-shot `read_byte` helpers
  for mutex-behavior tests.

## [1.4.0] - 2026-04-14

### Added
- New `HAL_TOOLS_ADC_MAXVALUE` macro in `tools_sensor_config.h`, derived from
  `HAL_TOOLS_ADC_BITS` (default 12).

### Changed
- `getAverageValueFrom()` now performs a dummy read before the sampling loop
  to avoid RP2040 ADC mux cross-talk (residual charge from the previous channel).
- `ntcToTemp()` uses `HAL_TOOLS_ADC_MAXVALUE` instead of hardcoded `4095.0`,
  enabling correct operation on ADC systems with non-12-bit resolution.
- PID controller: clamping anti-windup - integral accumulation is now skipped
  when the output is saturated in the direction of the error, preventing
  windup at output limits. Existing hard clamp via `setMaxIntegral()` retained
  as secondary safeguard.

## [1.3.0] - 2026-04-10

### Added
- New table-based soft timer APIs in hal_soft_timer:
  - hal_soft_timer_table_entry_t
  - hal_soft_timer_setup_table(...)
  - hal_soft_timer_tick_table(...)
- New CAN payload helper in hal_can:
  - hal_can_encode_temp_i8(...)
- New host test suite tests/test_hal_soft_timer.cpp covering:
  - Core hal_soft_timer wrapper behavior
  - Table setup/tick behavior
  - Idle callback and inter-entry delay behavior
  - Invalid input handling
- New test_hal_can coverage for hal_can_encode_temp_i8(...) positive/negative values,
  range saturation, and fractional truncation.
- New README quick example for table-based soft timer setup/tick usage.

### Changed
- hal_soft_timer_setup_table(...) and hal_soft_timer_tick_table(...) now return bool.
- Both table helpers now validate input arguments:
  - Return false when table is NULL
  - Return false when count is 0
  - Emit hal_derr(...) log on invalid input
- JaszczurHAL API docs updated to include table APIs, return contract, and validation behavior.
- JaszczurHAL docs updated to include the shared CAN temperature-byte helper.
- Test build configuration updated:
  - Added src/hal/hal_soft_timer.cpp to host test utility sources
  - Registered test_hal_soft_timer in tests/CMakeLists.txt
