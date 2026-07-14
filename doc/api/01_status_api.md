# Status API (`hal_status_t`)

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_status_t`, the status helper functions in
[`hal_status.h`](../../src/hal/hal_status.h), in-place status migration and
status-returning `_ex` companions used by migrated modules.

## Why it exists

The historical HAL API reports outcomes as `bool` (success/failure), an
`int`/`size_t` count, a handle (`NULL` on failure) or plain `void`. Those shapes
tell you *that* something failed, not *why*. `hal_status_t` adds a single,
uniform result type so callers can branch on the cause - invalid argument,
uninitialised backend, bus error, not-found, overflow - without inventing a
per-module error convention.

The migration is **status-first**. The status-returning function owns
validation, backend execution and error mapping. A historical `bool`, value or
handle API remains as a thin compatibility wrapper and gains an `_ex` companion
where needed. A fallible historical `void` function changes in place to return
`hal_status_t`, which remains source-compatible with callers that ignore the
result; redundant `_ex` adapters are removed. This avoids status functions that
can only guess after calling an information-losing legacy wrapper.

## Status codes

`hal_status_t` values are positive on success and negative on failure, so
`status == HAL_OK` checks success and `status < 0` (or `hal_status_is_error()`)
checks generic failure.

| Code | Meaning |
|---|---|
| `HAL_OK` | Operation completed successfully. |
| `HAL_NONE` | No status / uninitialised / abnormal (value `0`). |
| `HAL_EINVAL` | Invalid argument or unsupported parameter value. |
| `HAL_EBUSY` | Resource or bus is busy. |
| `HAL_ETIMEOUT` | Operation timed out. |
| `HAL_EIO` | Generic device, bus or backend I/O error. |
| `HAL_EUNSUPPORTED` | Operation not supported by this target/backend. |
| `HAL_ENOENT` | Requested object, device or entry was not found. |
| `HAL_EAGAIN` | Try again later / nonblocking op would block. |
| `HAL_EOVERFLOW` | Operation would overflow a buffer or resource. |
| `HAL_ENOMEM` | Out of memory or resource slots. |
| `HAL_IGNORED` | Operation was ignored (non-critical). |
| `HAL_EEXIST` | Object already exists. |
| `HAL_EPERM` | Operation not permitted. |
| `HAL_EINTERNAL` | Internal error / unexpected state. |
| `HAL_ECANCELED` | Operation was canceled. |
| `HAL_EPROTO` | Protocol error (unexpected response). |
| `HAL_EAUTH` | Authentication/authorization failure. |
| `HAL_EBUS` | Bus error (I2C/SPI transaction failure). |
| `HAL_EHW` | Hardware error (peripheral fault/misconfiguration). |
| `HAL_ECONFIG` | Configuration error (invalid setup/missing dependency). |
| `HAL_ESTATE` | Invalid state for the requested operation. |
| `HAL_EUNINIT` | Operation on an uninitialised object/subsystem. |
| `HAL_EDEPRECATED` | Operation is deprecated. |
| `HAL_EUNKNOWN` | Unknown error. |

Names use the `HAL_` prefix instead of POSIX `errno` names to avoid collisions
with `errno.h` and the BSD-sockets compatibility layer.

## Helper functions

All are `static inline` in [`hal_status.h`](../../src/hal/hal_status.h) and are
available from both C and C++:

```c
bool        hal_status_is_ok(hal_status_t status);        // status == HAL_OK
bool        hal_status_is_error(hal_status_t status);     // status < HAL_NONE
hal_status_t hal_status_from_bool(bool ok, hal_status_t error_status);
bool        hal_status_to_bool(hal_status_t status);      // legacy bool shape
const char *hal_status_to_string(hal_status_t status);    // e.g. "HAL_EINVAL"
```

`hal_status_to_string()` returns a stable symbolic name (or
`"HAL_STATUS_UNKNOWN"`), which is handy for logging:

```c
hal_status_t st = hal_spi_init(0, rx, tx, sck);
if (hal_status_is_error(st)) {
    hal_derr("SPI init failed: %s", hal_status_to_string(st));
}
```

## Status naming and migration convention

New fallible APIs return `hal_status_t` directly. During migration, the
historical return shape decides whether the original name can become the
status API or needs an `_ex` companion:

- A fallible historical `void` function changes in place to `hal_status_t`.
  Existing callers may keep ignoring the return value, and a redundant `_ex`
  adapter is not retained. Examples include `hal_eeprom_commit()`,
  `hal_display_init()`, `hal_dac_write()` and `hal_i2c_init()`.

- A historical `bool` function remains a thin compatibility wrapper because
  negative `hal_status_t` errors are truthy in C. Its adjacent `_ex` function
  owns validation and execution.

- **Value-returning** helpers expose their result through an **output
  parameter**, keeping the return value free for the status:

  ```c
  int  w = hal_display_get_width();              // legacy: 0 if unconfigured
  hal_status_t st = hal_display_get_width_ex(&w); // _ex: status + value in *w
  ```

- **Handle-returning** initialisers produce the handle through an output
  parameter and map a `NULL` result to a failure code:

  ```c
  hal_rtc_t rtc = NULL;
  hal_status_t st = hal_rtc_init_ex(&cfg, &rtc);  // HAL_OK, or HAL_EIO on failure
  ```

- **Collision fallback:** when `hal_foo_bar_ex()` already exists as a legacy
  entry point (a different meaning of "ex"), the status variant inserts
  `_status` before `_ex`. Current cases:
  `hal_wifi_ping_status_ex()` (legacy int-returning `hal_wifi_ping_ex()`) and
  `hal_display_init_ssd1306_i2c_status_ex()` (legacy bus-selecting
  `hal_display_init_ssd1306_i2c_ex()`).

- **Pure state queries** that cannot fail (for example
  `hal_littlefs_is_mounted()`, `hal_spi_write_dma_async_busy()`) report state,
  not the outcome of a fallible operation, so they keep no `_ex` form.

## Where status variants are documented

Status functions and their compatibility wrappers are documented **inline
next to each other** in the module reference, with worked examples:

| Area | Section |
|---|---|
| Buses (`hal_spi`, `hal_i2c`, `hal_swserial`) | [Communication buses](09_buses.md) |
| GPIO/peripherals (`hal_dac`, `hal_pcnt`) | [GPIO, ADC and PWM](05_gpio_adc_pwm.md) |
| Display (`hal_display`) | [CAN bus and display](10_can_display.md) |
| Output devices (`hal_dac`, `hal_rgb_led`, `hal_pga2311`) | [Output devices](13_output_devices.md) |
| RTC (`hal_rtc`) | [Sensors](11_sensors.md) |
| Storage (`hal_eeprom`, `hal_kv`, `hal_littlefs`) | [Storage](14_storage.md) |
| Networking (`hal_wifi`, `hal_tcp`, `hal_udp`, `hal_mqtt`, `hal_wireguard`) | [Network connectivity](15_connectivity.md) |

## Migration guidance

- New code should prefer the status-returning function: either the primary
  function returning `hal_status_t` or its `_ex` companion when a legacy
  value/handle/`bool` signature must remain available.
- Treat `hal_status_is_error(st)` as the generic failure gate; branch on
  specific codes only where you act on them.
- A residual failure that the legacy `bool` cannot disambiguate is mapped to
  the most representative code for that module (documented in each module's
  header and section). Status paths own validation, backend execution and
  error mapping; they must not be adapters that call a legacy wrapper.
