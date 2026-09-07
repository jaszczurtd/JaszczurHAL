<a id="status-api-hal_status_t"></a>

# Operation results and error handling (`hal_status_t`)

*Also available in [Polish](../pl/01_status_api.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

Use `hal_status_t` to check an operation's result and identify the cause of a failure. This chapter covers status codes, helpers in [`hal_status.h`](../../../src/hal/core/hal_status.h), and migration from older APIs to status-returning functions, including `_ex` variants.

<a id="why-it-exists"></a>

## Why use status codes

Older HAL functions return `bool`, an `int` or `size_t` count, a handle, or `void`. These interfaces do not provide a consistent way to identify failures and their causes. `hal_status_t` distinguishes invalid arguments, missing initialization, bus errors, missing objects, and overflow without requiring a separate error convention for each module.

The status-returning function validates arguments, performs the operation, and maps failures to error codes. Compatibility wrappers call that function, not the other way around.

## Status codes

In this API, success values are positive and error values are negative. Use `status == HAL_OK` to check successful completion and `status < 0` or `hal_status_is_error()` to detect an error. The value `0` means `HAL_NONE`, not `HAL_OK`.

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

All are `static inline` in [`hal_status.h`](../../../src/hal/core/hal_status.h) and are
available from both C and C++:

```c
bool        hal_status_is_ok(hal_status_t status);        // status == HAL_OK
bool        hal_status_is_error(hal_status_t status);     // status < HAL_NONE
hal_status_t hal_status_from_bool(bool ok, hal_status_t error_status);
bool        hal_status_to_bool(hal_status_t status);      // legacy bool shape
const char *hal_status_to_string(hal_status_t status);    // e.g. "HAL_EINVAL"
```

For logging, `hal_status_to_string()` returns a stable symbolic name or `"HAL_STATUS_UNKNOWN"`:

```c
hal_status_t st = hal_spi_init(0, rx, tx, sck);
if (hal_status_is_error(st)) {
    hal_derr("SPI init failed: %s", hal_status_to_string(st));
}
```

<a id="status-naming-and-migration-convention"></a>

## Function names and backward compatibility

New functions that can fail return `hal_status_t`. When changing an existing function, its previous return type determines how compatibility is preserved:

- A function that previously returned `void` can return `hal_status_t` under the same name. Existing callers may continue to ignore the result, so a separate `_ex` variant is unnecessary. Examples include `hal_eeprom_commit()`, `hal_display_init()`, `hal_dac_write()`, and `hal_i2c_init()`.

- A function returning `bool` remains a compatibility wrapper. Replacing its result directly with a status would be unsafe because a negative error code evaluates to true in C. The corresponding `_ex` function validates arguments and performs the operation.

- A function returning data moves that data to an **output parameter** and returns the status:

  ```c
  int  w = hal_display_get_width();              // legacy: 0 if unconfigured
  hal_status_t st = hal_display_get_width_ex(&w); // _ex: status + value in *w
  ```

- A function creating a handle writes it to an output parameter. An error code replaces the `NULL` result previously used to indicate failure:

  ```c
  hal_rtc_t rtc = NULL;
  hal_status_t st = hal_rtc_init_ex(&cfg, &rtc);  // HAL_OK, or a precise failure status
  ```

- If `hal_foo_bar_ex()` already names a legacy function, the status variant inserts `_status` before `_ex`. Examples are `hal_wifi_ping_status_ex()` alongside the int-returning `hal_wifi_ping_ex()`, and `hal_display_init_ssd1306_i2c_status_ex()` alongside the bus-selecting `hal_display_init_ssd1306_i2c_ex()`.

- State queries that cannot fail do not need an `_ex` variant. Examples include `hal_littlefs_is_mounted()` and `hal_spi_write_dma_async_busy()`.

<a id="where-status-variants-are-documented"></a>

## Finding individual function references

Each module reference documents status-returning functions alongside their compatibility wrappers and examples:

| Area | Section |
|---|---|
| Buses (`hal_spi`, `hal_i2c`, `hal_swserial`) | [Communication buses](09_buses.md) |
| GPIO/peripherals (`hal_dac`, `hal_pcnt`) | [GPIO, ADC and PWM](05_gpio_adc_pwm.md) |
| Display (`hal_display`) | [CAN bus and display](10_can_display.md) |
| Output devices (`hal_dac`, `hal_rgb_led`, `hal_pga2311`) | [Output devices](13_output_devices.md) |
| RTC (`hal_rtc`) | [Sensors](11_sensors.md) |
| Storage (`hal_eeprom`, `hal_kv`, `hal_littlefs`) | [Storage](14_storage.md) |
| Networking (`hal_wifi`, `hal_tcp`, `hal_udp`, `hal_mqtt`, `hal_notify`, `hal_wireguard`) | [Network connectivity](15_connectivity.md) |

<a id="migration-guidance"></a>

## Migrating to status-returning APIs

- In new code, use the function returning `hal_status_t`: either the primary function or its `_ex` variant when a legacy data, handle, or `bool` interface must remain available.
- Use `hal_status_is_error(st)` for general error detection. Check individual codes when the application needs to react differently to them.
- Failures that a legacy `bool` function could not distinguish are assigned module-specific codes documented in the header and module reference. The status-returning function must validate arguments, perform the operation, and map the error; it must not simply call a legacy wrapper.
