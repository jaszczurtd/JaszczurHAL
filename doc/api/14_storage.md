# Storage

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_eeprom`, `hal_kv`, `hal_littlefs`, `hal_sdlogger`.

Internal-flash layouts reserve application, OTA, LittleFS, and EEPROM regions
at link time. RP erase/program operations share the flash transaction
coordinator, which makes the other core safe, pauses USB work, rejects active
DMA conflicts, masks local interrupts, and restores runtime state on exit.
STM32G474 uses page-aligned linker reservations and its target flash service.
See [RP memory map](../../rp_native_lib/MEMORY_MAP.md) and
[STM32G474 memory map](../../stm32_lib/MEMORY_MAP.md).

## `hal_eeprom` - Unified EEPROM  *(optional - `HAL_ENABLE_EEPROM`)*

Single API for persistent byte-addressable storage. The back-end is selected at
runtime in `hal_eeprom_init()`.

For portable application code, prefer `HAL_EEPROM_FLASH`: it means "use the
target-native internal flash EEPROM emulation". Existing RP2040 code that uses
`HAL_EEPROM_RP2040` remains valid.

| Back-end selector | RP2040/RP2350 | STM32G474 |
|---|---|---|
| `HAL_EEPROM_FLASH` | Coordinated internal flash reservation | Internal flash reservation |
| `HAL_EEPROM_RP2040` | Same as target flash; retained API name | Alias for target-native flash |
| `HAL_EEPROM_STM32_FLASH` | STM32-specific selector; use `HAL_EEPROM_FLASH` for portable code | Internal flash reservation |
| `HAL_EEPROM_AT24C256` | External AT24C256 over HAL I2C | External AT24C256 over HAL I2C |

Both RP2040 and STM32G474 can therefore use either their own internal flash or
an external AT24C256 chip through the same `hal_eeprom_*` API. `hal_kv` sits on
top of whichever EEPROM back-end was selected.

```c
#include <hal/hal_eeprom.h>

typedef enum {
    HAL_EEPROM_DEFAULT     = 0, // Target default persistent storage
    HAL_EEPROM_AT24C256    = 1, // External AT24C256 I2C EEPROM - 32 KB
    HAL_EEPROM_RP2040      = 2, // RP2040 internal flash-backed EEPROM emulation
    HAL_EEPROM_STM32_FLASH = 3, // STM32G474 internal flash-backed EEPROM emulation
    HAL_EEPROM_FLASH       = 4, // Target-native internal flash EEPROM
} hal_eeprom_type_t;

// Initialise EEPROM. Call before any other hal_eeprom_* function.
// size:     used for flash-backed EEPROM; pass 0 to use the whole target
//           reservation. Ignored for HAL_EEPROM_AT24C256 (always 32768 bytes).
// i2c_addr: 7-bit I2C address of the AT24C256 chip; ignored for flash.
//           Pass 0 to use the default EEPROM_I2C_ADDRESS (0x50 from hal_config.h).
hal_status_t hal_eeprom_init(hal_eeprom_type_t type, uint16_t size,
                             uint8_t i2c_addr);

// Byte-level access
hal_status_t hal_eeprom_write_byte(uint16_t addr, uint8_t val);
uint8_t hal_eeprom_read_byte(uint16_t addr);

// 32-bit integer access (little-endian, 4 bytes starting at addr)
hal_status_t hal_eeprom_write_int(uint16_t addr, int32_t val);
int32_t hal_eeprom_read_int(uint16_t addr);

// Batched byte access under one internal lock.
hal_status_t hal_eeprom_write_bytes(uint16_t addr, const uint8_t *data,
                                    uint16_t len);
hal_status_t hal_eeprom_read_bytes(uint16_t addr, uint8_t *out, uint16_t len);

// Flush buffered writes to non-volatile storage.
// HAL_EEPROM_FLASH / native flash: flushes the RAM mirror to flash.
// HAL_EEPROM_AT24C256: no-op.
hal_status_t hal_eeprom_commit(void);

// Zero-fill entire EEPROM (slow - do not use in time-critical code).
hal_status_t hal_eeprom_reset(void);

// Return EEPROM size in bytes.
uint16_t hal_eeprom_size(void);
```

**Integer byte order:** `hal_eeprom_write_int` / `hal_eeprom_read_int` use
**little-endian** order (LSB at the lowest address).

**Commit semantics:** For flash-backed back-ends, `hal_eeprom_write_byte`,
`hal_eeprom_write_int`, and `hal_eeprom_write_bytes` update a RAM buffer first.
Call `hal_eeprom_commit()` once after a group of writes to persist them to
flash. For `HAL_EEPROM_AT24C256`, writes are committed synchronously to the
chip in page-sized chunks; `hal_eeprom_commit()` is a no-op.

**Native RP implementation:** `HAL_EEPROM_FLASH` and `HAL_EEPROM_RP2040` use
the final `HAL_RP_FLASH_EEPROM_SIZE` bytes of physical flash (4096 bytes by
default). Writes update a RAM mirror. A dirty commit performs the complete
partition erase and program inside one `jh_rp_flash_transaction_execute()`
operation, so core 1, interrupts, DMA and TinyUSB follow the same safety policy
as every other native flash mutation. The generated linker region excludes the
reservation from firmware.

**STM32G474 implementation:** `HAL_EEPROM_FLASH`,
`HAL_EEPROM_STM32_FLASH`, and the compatibility alias `HAL_EEPROM_RP2040` use
the last pages of internal flash reserved by the STM32 linker script. The
default reservation is `HAL_STM32_FLASH_EEPROM_SIZE = 4096` bytes, with
`HAL_STM32_FLASH_PAGE_SIZE = 2048` bytes. This reduces the flash available for
application code by 4 KB. If the reservation size is changed, keep the compile
definition and linker symbol in sync, and use a multiple of the STM32 flash page
size.

The STM32 linker also supports a separate LittleFS reservation before EEPROM.
Keep `HAL_STM32_FLASH_EEPROM_SIZE` and `HAL_STM32_FLASH_LITTLEFS_SIZE`
non-overlapping; EEPROM/KV and LittleFS do not share pages.

**AT24C256 implementation:** `HAL_EEPROM_AT24C256` drives the external chip via
`hal_i2c_*` primitives with write-cycle polling. Writes are split on 64-byte
page boundaries and ACK-polled with a bounded timeout
(`HAL_AT24C256_WRITE_TIMEOUT_US`, default 20000 us). Out-of-range writes are
clipped; out-of-range reads return zero-filled bytes. The AT24C256 I2C address
is `EEPROM_I2C_ADDRESS` (default `0x50`, defined in `hal_config.h`), unless an
explicit address is passed to `hal_eeprom_init()`.

**Long operation progress:** EEPROM never feeds the watchdog implicitly. Use
`hal_eeprom_set_progress_callback()` before long writes, reset, or flash commit
operations if the application wants to feed its own watchdog or report
progress. A full AT24C256 reset touches 512 pages and can take seconds.

**impl/.mock:** in-memory byte array (`MOCK_EEPROM_BUF_SIZE`, default 32768).

**Thread safety:** Thread-safe and multicore-safe for both back-end families.
`HAL_EEPROM_AT24C256` operations are protected by the `hal_i2c` bus mutex.
Flash-backed operations are protected by a dedicated internal mutex.
`hal_eeprom_init()` must be called from one core only.

### Mock helpers

```c
#include <hal/impl/.mock/hal_mock.h>

// Read a byte directly from the mock backing store.
uint8_t           hal_mock_eeprom_get_byte(uint16_t addr);
// Return the type set by hal_eeprom_init().
hal_eeprom_type_t hal_mock_eeprom_get_type(void);
// True if hal_eeprom_commit() was called since the last reset.
bool              hal_mock_eeprom_was_committed(void);
// Clear the committed flag (re-arm the check).
void              hal_mock_eeprom_clear_committed_flag(void);
// Return number of byte writes since last reset/counter clear.
uint32_t          hal_mock_eeprom_get_write_count(void);
// Clear the byte-write counter.
void              hal_mock_eeprom_clear_write_count(void);
// Reset all mock state to defaults (zeroed memory, no type, not committed).
void              hal_mock_eeprom_reset(void);
```

**Usage example:**
```c
// Target-native internal flash EEPROM reservation.
hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);
hal_eeprom_write_int(0, my_value);
hal_eeprom_commit();

// AT24C256 at default address 0x50 (I2C must already be initialised via hal_i2c_init):
hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0);            // 0 -> use EEPROM_I2C_ADDRESS
// or with an explicit address (e.g. A0 pin tied high -> 0x51):
hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0x51);
hal_eeprom_write_byte(0, 0xAB);
// no commit needed for AT24C256
```

**Example: write and read configuration data**
```c
#include <hal/hal_eeprom.h>

void example_eeprom(void) {
    // Initialize target-native flash EEPROM (512 bytes)
    hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);

    // Write multiple values at different addresses
    hal_eeprom_write_int(0, 12345);           // Store int at offset 0 (4 bytes)
    hal_eeprom_write_byte(4, 0x42);           // Store byte at offset 4

    // For structured data, use byte array writes
    uint8_t config_data[16] = {
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        0xFF, 0xFE, 0xFD, 0xFC,
        0x00, 0x00, 0x00, 0x00
    };
    hal_eeprom_write_bytes(8, config_data, sizeof(config_data));

    // Commit all buffered flash writes at once
    hal_eeprom_commit();

    // Read back the values
    int32_t stored_int = hal_eeprom_read_int(0);
    uint8_t stored_byte = hal_eeprom_read_byte(4);

    uint8_t read_buffer[16];
    hal_eeprom_read_bytes(8, read_buffer, sizeof(read_buffer));

    hal_deb("Int: %ld, Byte: 0x%02x", stored_int, stored_byte);
}
```

**Status-returning API** (see [Status API](01_status_api.md)):
`hal_eeprom` is the **reference module** for the revised status migration.
The historically `void` entry points (`init`, `write_byte`, `write_int`,
`write_bytes`, `read_bytes`, `commit`, `reset`, `set_progress_callback`) now
**return `hal_status_t` directly** - this is source-compatible, so existing
callers that ignore the return value are unaffected, and new code can inspect
it. On the AT24C256 back-end this recovers real I2C failures (`HAL_EIO`) that
the old `void` API discarded. The three value-returning getters keep their
signature and gain `_ex` companions (`hal_eeprom_read_byte_ex`,
`hal_eeprom_read_int_ex`, `hal_eeprom_size_ex`) that report the value through
an output parameter. Out-of-range access still clips exactly as before; the new
status merely reports it as `HAL_EOVERFLOW`.

```c
hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);   // returns HAL_OK / HAL_EINVAL

hal_status_t st = hal_eeprom_write_byte(600, 0x42);
// HAL_EUNINIT   -> hal_eeprom_init() not called yet
// HAL_EOVERFLOW -> addr outside the device (write clipped, as before)
// HAL_EIO       -> AT24C256 I2C failure
// HAL_OK        -> buffered; persist with hal_eeprom_commit()

uint8_t value = 0;
if (hal_eeprom_read_byte_ex(10, &value) == HAL_OK) {
    use(value);              // HAL_EINVAL if the output pointer is NULL
}
```

---


## `hal_kv` - Key-value storage on EEPROM  *(optional - `HAL_ENABLE_KV`)*

Thread-safe append-only KV/record storage on top of `hal_eeprom`.
Uses dual-bank layout with CRC16-protected headers and records.
Automatic garbage-collection (GC) compacts live records into the alternate bank
when the active bank runs out of space.

```c
#include <hal/hal_kv.h>

typedef struct {
    uint32_t generation;       // bank generation counter
    uint16_t used_bytes;       // bytes used in active bank
    uint16_t capacity_bytes;   // single-bank capacity
    uint16_t key_count;        // number of live keys
    uint32_t next_sequence;    // next record sequence number
} hal_kv_stats_t;

bool hal_kv_init(uint16_t base_addr, uint16_t size_bytes);
bool hal_kv_set_u32(uint16_t key, uint32_t value);
bool hal_kv_get_u32(uint16_t key, uint32_t *out_value);
bool hal_kv_set_blob(uint16_t key, const uint8_t *data, uint16_t len);
bool hal_kv_get_blob(uint16_t key, uint8_t *out, uint16_t out_size, uint16_t *out_len);
bool hal_kv_delete(uint16_t key);
bool hal_kv_gc(void);
bool hal_kv_get_stats(hal_kv_stats_t *out_stats);
hal_status_t hal_kv_set_auto_commit(bool enabled);
bool hal_kv_commit(void);
```

**Dependencies:** `hal_eeprom`, `hal_sync`, `hal_serial`.
**Thread safety:** Thread-safe and multicore-safe. An internal singleton mutex
created with the HAL atomic create-once helper protects all operations.
`hal_kv_init()` must be called after `hal_eeprom_init()`.

**Deduplication:** `hal_kv_set_u32` / `hal_kv_set_blob` skip the EEPROM write when the
value is unchanged, avoiding unnecessary flash wear.

**Commit policy:** auto-commit is enabled by default (historical behavior).
Use `hal_kv_set_auto_commit(false)` to defer physical EEPROM/flash commit and
coalesce multiple writes, then flush once with `hal_kv_commit()`.

**Example: key-value storage with integers and blobs**
```c
#include <hal/hal_kv.h>
#include <hal/hal_eeprom.h>
#include <string.h>

void example_kv(void) {
    // Initialize EEPROM first, then KV store
    hal_eeprom_init(HAL_EEPROM_FLASH, 4096, 0);

    // KV storage uses dual-bank layout starting at address 0, 2KB per bank
    hal_kv_init(0, 4096);

    // Store a 32-bit unsigned integer with key 1
    hal_kv_set_u32(1, 42);
    hal_deb("Stored: key=1, value=42");

    // Store multiple integers
    hal_kv_set_u32(2, 1000);
    hal_kv_set_u32(3, 999999);

    // Retrieve a value
    uint32_t retrieved = 0;
    if (hal_kv_get_u32(1, &retrieved)) {
        hal_deb("Retrieved: key=1, value=%lu", retrieved);
    }

    // Store binary data (blob) - e.g., device MAC address or config
    uint8_t mac_address[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    hal_kv_set_blob(10, mac_address, sizeof(mac_address));

    // Retrieve blob data
    uint8_t retrieved_mac[6];
    uint16_t retrieved_len = 0;
    if (hal_kv_get_blob(10, retrieved_mac, sizeof(retrieved_mac), &retrieved_len)) {
        hal_deb("Retrieved MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                retrieved_mac[0], retrieved_mac[1], retrieved_mac[2],
                retrieved_mac[3], retrieved_mac[4], retrieved_mac[5]);
    }

    // Store configuration string as blob
    const char *config = "ssid=MyNetwork&pass=pwd123";
    hal_kv_set_blob(11, (const uint8_t *)config, strlen(config));

    // Retrieve configuration string
    char config_buf[128];
    uint16_t config_len = 0;
    if (hal_kv_get_blob(11, (uint8_t *)config_buf, sizeof(config_buf), &config_len)) {
        config_buf[config_len] = '\0';  // null-terminate
        hal_deb("Retrieved config: %s", config_buf);
    }

    // Delete a key
    hal_kv_delete(2);

    // Get statistics
    hal_kv_stats_t stats;
    if (hal_kv_get_stats(&stats)) {
        hal_deb("KV stats: %d keys, %d/%d bytes used, gen=%lu",
                stats.key_count, stats.used_bytes, stats.capacity_bytes,
                stats.generation);
    }

    // Manual commit (if auto-commit was disabled)
    hal_kv_set_auto_commit(false);
    hal_kv_set_u32(100, 111);
    hal_kv_set_u32(101, 222);
    hal_kv_commit();  // Flush both writes at once
    hal_kv_set_auto_commit(true);
}
```

**Status API:** the `_ex` operations own validation and EEPROM I/O; the
historical bool functions above are thin compatibility wrappers. The
historically `void` `hal_kv_set_auto_commit()` now returns `hal_status_t`
directly. A read miss maps to `HAL_ENOENT`, use before successful init to
`HAL_EUNINIT`, an invalid EEPROM range to `HAL_EOVERFLOW`, insufficient bank
capacity to `HAL_ENOMEM`, and underlying EEPROM failures are propagated.
`hal_kv_get_blob_ex()` reports a too-small caller buffer as `HAL_EOVERFLOW`
with the required length in `*out_len`.

```c
uint8_t  buf[64];
uint16_t len = 0;
hal_status_t st = hal_kv_get_blob_ex(KEY_PROFILE, buf, sizeof(buf), &len);
switch (st) {
case HAL_OK:        use(buf, len);                       break;
case HAL_ENOENT:    /* key absent */                     break;
case HAL_EUNINIT:   /* store not initialized */          break;
case HAL_EOVERFLOW: /* buf too small; *len = needed */    break;
default:            break;
}
```

---


## `hal_littlefs` - LittleFS lifecycle helpers  *(opt-in - `HAL_ENABLE_LITTLEFS`)*

Thread-safe wrapper for LittleFS mount/format and lightweight path helpers.

```c
#include <hal/hal_littlefs.h>

hal_status_t hal_littlefs_set_progress_callback(
    hal_littlefs_progress_callback_t callback, void *ctx);
bool         hal_littlefs_begin(void);
hal_status_t hal_littlefs_end(void);
bool         hal_littlefs_format(void);
bool         hal_littlefs_is_mounted(void);
bool         hal_littlefs_exists(const char *path);
bool         hal_littlefs_remove(const char *path);
size_t       hal_littlefs_total_bytes(void);
size_t       hal_littlefs_used_bytes(void);
```

**Behavior notes:**
- Module is available only when `HAL_ENABLE_LITTLEFS` is defined.
- `hal_littlefs_begin()` mounts the filesystem; `hal_littlefs_end()` unmounts it.
- `hal_littlefs_format()` formats the LittleFS partition.
- When `hal_littlefs_format()` fails, mounted/path/stat state is left unchanged.
- Path helpers require mounted filesystem and validate non-empty paths.
- The public HAL API currently exposes lifecycle, path removal/existence and
  size stats only. It does not provide portable file open/read/write wrappers.

**Native RP implementation:** uses the pinned upstream littlefs v2.11.3
checkout under `third_party/littlefs/` and an internal flash partition
controlled by `HAL_RP_FLASH_LITTLEFS_SIZE`. The native CMake recipe reserves
64 KiB when `HAL_ENABLE_LITTLEFS` is enabled without an explicit size. The
partition sits immediately before the final 4 KiB EEPROM sector. Every
256-byte program and 4096-byte erase callback goes through the native RP flash
transaction coordinator; reads use the XIP mapping. The linker prevents the
firmware image from overlapping either partition.

**STM32G474 implementation:** uses the same managed littlefs checkout under
`third_party/littlefs/` and the internal STM32 flash reservation exposed by the
linker script. `HAL_STM32_FLASH_LITTLEFS_SIZE` controls the reservation size
and must be a multiple of `HAL_STM32_FLASH_PAGE_SIZE` (2048 bytes). The size
may be zero when the backend is compiled but not used; mounting then fails
safely. The STM32 CMake helpers reserve 64 KB automatically when
`HAL_ENABLE_LITTLEFS` is passed through their define lists and no explicit
size is provided.

LittleFS erase block size is one STM32 flash page; program granularity is one
STM32 doubleword (8 bytes). `hal_littlefs_total_bytes()` reports the reserved
partition size after mount. `hal_littlefs_used_bytes()` reports allocated
littlefs blocks multiplied by the flash page size.

LittleFS never feeds the watchdog implicitly. Use
`hal_littlefs_set_progress_callback()` before long operations such as format or
large garbage-collection/write bursts if the application wants to feed its own
watchdog or report progress.

**Example: mount, format-on-first-use, inspect and remove a path**
```c
#include <hal/hal_littlefs.h>
#include <tools_c.h>

void example_littlefs(void) {
    if (!hal_littlefs_begin()) {
        derr("LittleFS mount failed; formatting");
        if (!hal_littlefs_format() || !hal_littlefs_begin()) {
            derr("LittleFS unavailable");
            return;
        }
    }

    deb("LittleFS mounted: %lu/%lu bytes used",
        (unsigned long)hal_littlefs_used_bytes(),
        (unsigned long)hal_littlefs_total_bytes());

    if (hal_littlefs_exists("/data.txt")) {
        (void)hal_littlefs_remove("/data.txt");
    }

    hal_littlefs_end();
}
```

---
**impl/.mock:** deterministic test double with injectable mount/format result,
path presence, and volume size stats.
**Thread safety:** RP2040 and STM32G474 backends are thread-safe for public
APIs. A singleton `hal_mutex_t` serializes all wrapper calls.

**Mock helpers:**
```c
void hal_mock_littlefs_reset(void);
void hal_mock_littlefs_set_begin_result(bool result);
void hal_mock_littlefs_set_format_result(bool result);
void hal_mock_littlefs_set_total_bytes(size_t total_bytes);
void hal_mock_littlefs_set_used_bytes(size_t used_bytes);
void hal_mock_littlefs_set_exists(const char *path, bool exists);
```

**Status API:** lifecycle, path and size `_ex` operations own validation and
backend I/O; historical bool/value functions are thin compatibility wrappers.
The historically `void` callback setter and unmount function now return
`hal_status_t` directly. The plain state query
`hal_littlefs_is_mounted()` has no `_ex` form. An invalid path/output maps to
`HAL_EINVAL`, use while unmounted to `HAL_EUNINIT`, a missing path to
`HAL_ENOENT`, an unconfigured STM32 partition to `HAL_ECONFIG`, and native
mount/format/stat/unmount failures to `HAL_EIO`.

```c
if (hal_littlefs_begin_ex() != HAL_OK) {
    return;                 // HAL_EIO: mount failed
}
hal_status_t st = hal_littlefs_exists_ex("/config.json");
// HAL_OK -> present, HAL_ENOENT -> absent,
// HAL_EUNINIT -> not mounted, HAL_EINVAL -> NULL/empty path

size_t used = 0;
hal_littlefs_used_bytes_ex(&used);   // HAL_EUNINIT (used=0) while unmounted
```

---

## `hal_sdlogger` - SD-card logger  *(opt-in - `HAL_ENABLE_SDLOGGER`)*

Periodic SD-card logger plus crash-report logger. The module stores log/crash
file counters in `hal_eeprom` and writes files through the shared FatFs
SD-over-SPI layer, so enabling it propagates `HAL_ENABLE_FAT`,
`HAL_ENABLE_EEPROM`, and `HAL_ENABLE_SPI`.

```c
#include <hal/hal_sdlogger.h>

int  hal_sdlogger_get_log_number(void);
int  hal_sdlogger_get_crash_number(void);
hal_status_t hal_sdlogger_init_ex(int cs);
bool hal_sdlogger_init(int cs);
hal_status_t hal_sdlogger_crash_init_ex(const char *add_to_name, int cs);
bool hal_sdlogger_crash_init(const char *add_to_name, int cs);
bool hal_sdlogger_is_initialized(void);
bool hal_sdlogger_crash_is_initialized(void);
hal_status_t hal_sdlogger_append(const char *data);
hal_status_t hal_sdlogger_crash_append(const char *data);
hal_status_t hal_sdlogger_close(void);
hal_status_t hal_sdlogger_crash_close(void);
hal_status_t hal_sdlogger_crash_report(const char *format, ...);
```

**Configuration defaults:**

```c
HAL_SDLOGGER_WRITE_INTERVAL_MS  2000u
HAL_SDLOGGER_EEPROM_LOGGER_ADDR 0u
HAL_SDLOGGER_EEPROM_CRASH_ADDR  4u
HAL_SDLOGGER_EEPROM_FIRST_ADDR  8u
HAL_SDLOGGER_LOG_BUFFER_SIZE    2048u
HAL_SDLOGGER_NAME_BUFFER_SIZE   128u
HAL_SDLOGGER_SPI_BUS            0u
```

**Behavior notes:**
- The application must initialise the selected SPI bus pins with
  `hal_spi_init()` before calling `hal_sdlogger_init()` or
  `hal_sdlogger_crash_init()`.
- `hal_sdlogger_init(cs)` opens `logNNNNN.txt` and increments the EEPROM log
  counter; `hal_sdlogger_init_ex(cs)` is the status-returning form and the
  legacy `bool` function is a thin wrapper.
- `hal_sdlogger_append()` buffers lines and flushes every
  `HAL_SDLOGGER_WRITE_INTERVAL_MS`; `hal_sdlogger_close()` flushes leftovers.
  These functions now return `hal_status_t`, so old callers may still ignore
  the result while new code can check failures.
- `hal_sdlogger_crash_init(add_to_name, cs)` opens `wdNNNNNN.txt` and writes
  the optional crash tag plus the corresponding log filename into the report.
  Generated filenames intentionally stay in FatFs 8.3 form because LFN is
  disabled.
- SD logger counters are advanced only after the SD card is mounted and the
  target file is opened successfully.
- `hal_sdlogger_crash_append()` and `hal_sdlogger_crash_report()` flush crash
  entries immediately.
- Status mapping: SD mount failure returns `HAL_EBUS`; file writes, flushes,
  closes, and EEPROM update failures return the backend status or `HAL_EIO`;
  append/close before init return `HAL_EUNINIT`; an oversized buffered log line
  returns `HAL_EOVERFLOW`; `hal_sdlogger_crash_report(NULL)` returns
  `HAL_EINVAL`.

Buildable example: `examples/39_sdlogger`.

**Example: SD card periodic logging**
```c
#include <hal/hal_sdlogger.h>
#include <hal/hal_eeprom.h>
#include <hal/hal_spi.h>

void setup_sd_logging(void) {
    // Initialize EEPROM (SD logger stores counters there)
    hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);

    // Initialize SPI bus 0 and SD card logger with CS pin 17
    hal_spi_init(0, 16, 19, 18);
    int cs_pin = 17;
    if (hal_sdlogger_init(cs_pin)) {
        hal_deb("SD logger initialized, log number: %d", hal_sdlogger_get_log_number());
    } else {
        hal_derr("SD logger init failed!");
        return;
    }
}

void loop_with_logging(void) {
    static uint32_t last_log_ms = 0;
    uint32_t now_ms = hal_millis();

    // Log every 2 seconds (HAL_SDLOGGER_WRITE_INTERVAL_MS)
    if (now_ms - last_log_ms > 2000) {
        last_log_ms = now_ms;

        // Read some sensor data
        float temperature = read_temperature();
        int humidity = read_humidity();

        // Append to log file (buffered, flushed periodically)
        static char log_line[128];
        snprintf(log_line, sizeof(log_line), "[%lu] T=%.1f°C, H=%d%%\n",
                 now_ms, temperature, humidity);
        hal_sdlogger_append(log_line);
    }
}

void shutdown_logging(void) {
    // Flush any remaining buffered data to SD card
    hal_sdlogger_close();
    hal_deb("Log file closed");
}
```

**Example: SD card crash logger**
```c
#include <hal/hal_sdlogger.h>
#include <hal/hal_eeprom.h>
#include <hal/hal_spi.h>

void setup_crash_logging(void) {
    // Initialize EEPROM and crash logger
    hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);

    hal_spi_init(0, 16, 19, 18);
    int cs_pin = 17;
    // Create wdNNNNNN.txt and write "boot" as a crash tag inside it.
    if (hal_sdlogger_crash_init("boot", cs_pin)) {
        hal_deb("Crash logger initialized, crash log number: %d",
                hal_sdlogger_get_crash_number());
    }
}

void on_critical_error(const char *error_msg) {
    // Log crash report immediately (not buffered)
    hal_sdlogger_crash_report("[CRITICAL] Error: %s, Free heap: %lu\n",
                              error_msg, hal_get_free_heap());

    // Append additional debug info
    hal_sdlogger_crash_append("[CRITICAL] Stack trace would go here\n");

    // Flush and close the crash file
    hal_sdlogger_crash_close();
}

void watchdog_reboot_handler(void) {
    hal_sdlogger_crash_report("[WATCHDOG] System reboot triggered\n");
    hal_sdlogger_crash_close();
}
```

---
**impl/shared/frameworks/filesystem:** SD file helpers and the portable SD logger
implementation used by RP2040 and STM32G474. The unchanged FatFs R0.16 core is
loaded from the SHA-256-pinned official archive in `third_party/FatFs`; tracked
wrappers provide the feature gate and the project-owned `ffconf.h`.
**impl/.mock:** deterministic test double with injectable SD/open results,
captured filenames/content, flush counts, and close flags.
**Thread safety:** shared backend serializes public calls with a singleton
`hal_mutex_t`; init/close should still be treated as single-core lifecycle work.

**Mock helpers:**
```c
void        hal_mock_sdlogger_reset(void);
void        hal_mock_sdlogger_set_sd_begin_result(bool result);
void        hal_mock_sdlogger_set_log_open_result(bool result);
void        hal_mock_sdlogger_set_crash_open_result(bool result);
const char *hal_mock_sdlogger_log_filename(void);
const char *hal_mock_sdlogger_crash_filename(void);
const char *hal_mock_sdlogger_log_content(void);
const char *hal_mock_sdlogger_crash_content(void);
uint32_t    hal_mock_sdlogger_log_flush_count(void);
uint32_t    hal_mock_sdlogger_crash_flush_count(void);
uint32_t    hal_mock_sdlogger_sd_begin_count(void);
bool        hal_mock_sdlogger_log_was_closed(void);
bool        hal_mock_sdlogger_crash_was_closed(void);
```

---


---

*Next: [Network connectivity](15_connectivity.md)*
