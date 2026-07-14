# Communication buses

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_spi`, `hal_i2c`, `hal_i2c_slave`, `hal_uart`, `hal_swserial`, `hal_onewire`.

## `hal_spi` - SPI bus and transfer API

```c
#include <hal/hal_spi.h>

// Fallible historical void operations now return status in place.
hal_status_t hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin,
                          uint8_t sck_pin);
hal_status_t hal_spi_begin_transaction(
    uint8_t bus, const hal_spi_settings_t *settings);
hal_status_t hal_spi_end_transaction(uint8_t bus);
hal_status_t hal_spi_transfer_buffer(uint8_t bus, uint8_t *buffer,
                                     size_t len);
hal_status_t hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx,
                                   uint8_t *rx, size_t len);
hal_status_t hal_spi_write(uint8_t bus, const uint8_t *data, size_t len);

// Value/bool compatibility APIs keep status-returning _ex companions.
hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                 uint8_t *out_received);
hal_status_t hal_spi_transfer16_ex(uint8_t bus, uint16_t data,
                                   uint16_t *out_received);
hal_status_t hal_spi_write_dma_ex(uint8_t bus, const uint8_t *data, size_t len);
hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus,
                                              const uint8_t *data, size_t len);
hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus);

// Configure pins and start the SPI bus in master mode.
// bus: 0 = SPI (default), 1 = SPI1 (second controller, RP2040)
void hal_spi_deinit(uint8_t bus);

// Optional runtime synchronization for shared SPI buses.
void hal_spi_lock(uint8_t bus);
void hal_spi_unlock(uint8_t bus);

// SPISettings-compatible transaction settings and transfer primitives.
hal_spi_settings_t settings = {4000000u, HAL_SPI_MSBFIRST, HAL_SPI_MODE0};
uint8_t  hal_spi_transfer(uint8_t bus, uint8_t data);
uint16_t hal_spi_transfer16(uint8_t bus, uint16_t data);
bool     hal_spi_write_dma(uint8_t bus, const uint8_t *data, size_t len);

// Asynchronous DMA-capable write path. Backends without async DMA complete
// the transfer before returning from _start().
bool     hal_spi_write_dma_async_start(uint8_t bus, const uint8_t *data, size_t len);
bool     hal_spi_write_dma_async_busy(uint8_t bus);
bool     hal_spi_write_dma_async_wait(uint8_t bus);
```

Only bus values 0 and 1 are supported. Status-returning operations report
`HAL_EINVAL` for other values; low-level synchronization and cleanup helpers
retain their checked-build assertions.

The API reports `HAL_EINVAL` for invalid buses, settings, output pointers and
non-empty NULL buffers; async DMA start reports `HAL_EBUSY` when a transfer is
already active. Zero-length buffer operations succeed without requiring a
buffer. STM32G474 polling paths additionally report `HAL_ETIMEOUT`, while
RP2040 blocking SDK calls report `HAL_EIO` if they transfer fewer items than
requested. Backends expose only errors they can distinguish honestly.

**DMA writes:** `hal_spi_write_dma()` is the blocking convenience wrapper: it
starts the fastest available backend write path and returns only after the
buffer has been accepted/transmitted according to that backend. The
`hal_spi_write_dma_async_*()` trio exposes the non-blocking form where the
backend supports it. After a successful `_async_start()`, the caller must keep
the `data` buffer alive and unchanged until `_async_busy()` becomes false or
`_async_wait()` returns, and must not start a second async write on the same bus
while the previous one is active. Backends without asynchronous DMA perform the
write inside `_async_start()`, report `_async_busy() == false`, and let
`_async_wait()` return immediately.

**impl/rp2040:** Native Pico SDK `hardware/spi.h` on SPI0/SPI1 plus `hardware/gpio.h` pin muxing. `hal_spi_write_dma_async_start()` uses SPI TX DMA for MSB-first byte streams and returns before the bus is idle; `hal_spi_end_transaction()` / `hal_spi_deinit()` wait for any active async TX DMA before closing the transaction or releasing the channel.
**impl/stm32g474:** register-level SPI1/SPI2 master, 8-bit full-duplex, software NSS, polling transfer, AF5 pin setup. Default pins: SPI bus 0 = PA6/PA7/PA5, bus 1 = PB14/PB15/PB13. The async DMA API currently falls back to the synchronous polling write path.
**impl/.mock:** stores init/settings, lock depth, scripted RX bytes and TX log for tests.
**Thread safety:** `hal_spi_begin_transaction()` applies bus settings but does not lock. Use `hal_spi_lock()` / `hal_spi_unlock()` around multi-step driver operations on shared buses. Treat async DMA lifetime as part of the same transaction/critical section: keep chip-select and bus ownership valid until `_async_wait()` completes.

---

## `hal_i2c` - I2C bus  *(optional - `HAL_ENABLE_I2C`)*

```c
#include <hal/hal_i2c.h>

// Common I2C clock constants:
#define HAL_I2C_CLOCK_STANDARD_HZ    100000UL   // Standard-mode, 100 kHz
#define HAL_I2C_CLOCK_FAST_HZ        400000UL   // Fast-mode, 400 kHz
#define HAL_I2C_CLOCK_FAST_PLUS_HZ  1000000UL   // Fast-mode Plus, 1 MHz
#define HAL_I2C_CLOCK_HIGH_SPEED_HZ 3400000UL   // High-speed mode, 3.4 MHz

// Common I2C transaction result constants:
#define HAL_I2C_RESULT_OK           0u
#define HAL_I2C_ERROR_GENERIC       2u
#define HAL_I2C_ERROR_OTHER         3u
#define HAL_I2C_ERROR_TIMEOUT       4u

// Fallible historical void operations now return status in place.
hal_status_t hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin,
                          uint32_t clock_hz);
hal_status_t hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin,
                              uint8_t scl_pin, uint32_t clock_hz);
hal_status_t hal_i2c_set_clock(uint32_t clock_hz);
hal_status_t hal_i2c_set_clock_bus(uint8_t bus, uint32_t clock_hz);
hal_status_t hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin);
hal_status_t hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin);

// One bounded scan of the usable 7-bit range 0x08..0x77. The callback is
// optional and runs before every probe; hal_watchdog_feed can be passed
// directly. A NULL addresses pointer with capacity 0 performs a count-only
// scan. outFound receives the total count even when the output is too small.
typedef void (*hal_i2c_scan_callback_t)(void);
hal_status_t hal_i2c_scan(uint8_t *addresses, size_t capacity,
                          size_t *outFound,
                          hal_i2c_scan_callback_t callback);
hal_status_t hal_i2c_scan_bus(uint8_t bus, uint8_t *addresses,
                              size_t capacity, size_t *outFound,
                              hal_i2c_scan_callback_t callback);

// Status companions for historical value/bool-returning operations.
hal_status_t hal_i2c_end_transmission_ex(void);
hal_status_t hal_i2c_end_transmission_bus_ex(uint8_t bus);
hal_status_t hal_i2c_write_byte_ex(uint8_t address, uint8_t data,
                                   bool *outWriteOk);
hal_status_t hal_i2c_write_byte_bus_ex(uint8_t bus, uint8_t address,
                                       uint8_t data, bool *outWriteOk);
hal_status_t hal_i2c_read_byte_ex(uint8_t address, uint8_t *outValue);
hal_status_t hal_i2c_read_byte_bus_ex(uint8_t bus, uint8_t address,
                                      uint8_t *outValue);
hal_status_t hal_i2c_write_read_ex(uint8_t address,
                                   const uint8_t *tx, size_t tx_len,
                                   uint8_t *rx, size_t rx_len);
hal_status_t hal_i2c_write_read_bus_ex(uint8_t bus, uint8_t address,
                                       const uint8_t *tx, size_t tx_len,
                                       uint8_t *rx, size_t rx_len);
hal_status_t hal_i2c_read_bytes_ex(uint8_t address, uint8_t *rx,
                                   size_t rx_len);
hal_status_t hal_i2c_read_bytes_bus_ex(uint8_t bus, uint8_t address,
                                       uint8_t *rx, size_t rx_len);
hal_status_t hal_i2c_request_from_ex(uint8_t address, uint8_t count,
                                     uint8_t *outReceived);
hal_status_t hal_i2c_request_from_bus_ex(uint8_t bus, uint8_t address,
                                         uint8_t count, uint8_t *outReceived);
void    hal_i2c_deinit(void);
void    hal_i2c_deinit_bus(uint8_t bus);

// Manual lock/unlock - use when wrapping a third-party library that calls the
// backend-native bus object directly.
void    hal_i2c_lock(void);
void    hal_i2c_unlock(void);
void    hal_i2c_lock_bus(uint8_t bus);
void    hal_i2c_unlock_bus(uint8_t bus);

// Transaction primitives (begin/write/end acquire/release the mutex automatically)
void    hal_i2c_begin_transmission(uint8_t address);
size_t  hal_i2c_write(uint8_t data);        // returns 1 on success, 0 on failure
uint8_t hal_i2c_end_transmission(void);     // returns 0 on success, non-zero on error
void    hal_i2c_begin_transmission_bus(uint8_t bus, uint8_t address);
size_t  hal_i2c_write_bus(uint8_t bus, uint8_t data);
uint8_t hal_i2c_end_transmission_bus(uint8_t bus);

// One-shot "begin + write one byte + end" helper (acquires/releases mutex internally).
// *outWriteOk (optional) receives the hal_i2c_write() queued-bytes status.
// Return value is the end_transmission status (0 on success).
uint8_t hal_i2c_write_byte(uint8_t address, uint8_t data, bool *outWriteOk);
uint8_t hal_i2c_write_byte_bus(uint8_t bus, uint8_t address, uint8_t data, bool *outWriteOk);

// Symmetric one-shot "request + read 1 byte" helper.
// The internal mutex is held across the full request+read sequence.
// *outReadOk (optional) receives true when exactly one byte was received.
// Returns the byte read, or 0 on failure - inspect *outReadOk to distinguish
// a genuine 0x00 from a communication error.
uint8_t hal_i2c_read_byte(uint8_t address, bool *outReadOk);
uint8_t hal_i2c_read_byte_bus(uint8_t bus, uint8_t address, bool *outReadOk);

// Combined write-then-read helper for register-pointer sensors.
// Writes tx bytes, keeps the bus active for a repeated-start read, and reads
// exactly rx_len bytes. Returns true only when both phases complete.
bool    hal_i2c_write_read(uint8_t address,
                           const uint8_t *tx, size_t tx_len,
                           uint8_t *rx, size_t rx_len);
bool    hal_i2c_write_read_bus(uint8_t bus, uint8_t address,
                               const uint8_t *tx, size_t tx_len,
                               uint8_t *rx, size_t rx_len);

// Direct read helper for sensors that expose current data without a register
// pointer phase. Holds the bus mutex across request+copy.
bool    hal_i2c_read_bytes(uint8_t address, uint8_t *rx, size_t rx_len);
bool    hal_i2c_read_bytes_bus(uint8_t bus, uint8_t address,
                               uint8_t *rx, size_t rx_len);

// Legacy buffered receive API. Not an atomic read sequence unless
// the caller wraps request+available/read in hal_i2c_lock()/hal_i2c_unlock().
// Prefer hal_i2c_read_bytes(_bus) or hal_i2c_write_read(_bus) in drivers.
uint8_t hal_i2c_request_from(uint8_t address, uint8_t count);  // returns bytes received
int     hal_i2c_available(void);    // bytes in receive buffer
int     hal_i2c_read(void);         // one byte, or -1 if empty
uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count);
int     hal_i2c_available_bus(uint8_t bus);
int     hal_i2c_read_bus(uint8_t bus);

// Transaction counter - counts completed write (end_transmission) and read
// (request_from) transactions since init. Resets on init. Wraps at UINT32_MAX.
uint32_t hal_i2c_get_transaction_count(void);
uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus);

// Device-busy probe - send address, check ACK/NACK immediately.
// Returns true if the device did NOT ACK (busy or absent).
// Typical use: poll after an AT24C256 write until the chip is ready.
bool    hal_i2c_is_busy(uint8_t address);
bool    hal_i2c_is_busy_bus(uint8_t bus, uint8_t address);

```

Only bus values 0 and 1 are supported. Other values are programmer errors and
trigger `HAL_ASSERT` in checked builds.

The `_ex` variants return `HAL_OK` on success and `hal_status_t` diagnostics
for new code. Invalid buses or buffers return `HAL_EINVAL`; uninitialized bus
use returns `HAL_EUNINIT` where the backend can detect it; legacy
`HAL_I2C_ERROR_TIMEOUT` maps to `HAL_ETIMEOUT`, generic NACK/bus failures map
to `HAL_EBUS`, and non-specific backend failures map to `HAL_EIO`.
Existing wrappers keep their `void`, `uint8_t` and `bool` return shapes for
source compatibility.

`hal_i2c_scan()` replaces the old `tools.cpp` `i2cScanner()` helper. It scans
once rather than owning an infinite print/delay loop, skips reserved 7-bit
addresses, has no serial dependency, supports both controllers, reports
buffer truncation as `HAL_EOVERFLOW`, and keeps watchdog servicing explicit
through its optional callback. Applications own presentation and scheduling:

```c
uint8_t addresses[HAL_I2C_SCAN_ADDRESS_COUNT];
size_t found = 0;
hal_status_t status =
    hal_i2c_scan(addresses, HAL_I2C_SCAN_ADDRESS_COUNT, &found,
                 hal_watchdog_feed);
```

**Init behavior:** `hal_i2c_init*()` creates the per-bus mutex, configures
SDA/SCL, clock and starts the backend controller; it should still be called
during setup before normal I2C traffic. Runtime calls keep an atomic create-once
fallback for defensive use before init. Use
`hal_i2c_set_clock()` / `hal_i2c_set_clock_bus()` to retune an already
configured bus while keeping the change inside the HAL bus mutex.

**Clock modes:** The named clock constants map to I2C-bus specification modes:
Standard-mode (100 kHz), Fast-mode (400 kHz), Fast-mode Plus / Fm+ (1 MHz),
and High-speed mode / Hs-mode (3.4 MHz). 1 MHz and 3.4 MHz are real-world
use cases: Fm+ is common for faster local board-level peripherals and bus
buffers, while Hs-mode appears in high-rate sensors such as some Bosch
environmental sensors and ST motion sensors. Always check the controller,
board routing, pull-ups, bus capacitance, and every device datasheet before
selecting these speeds. In particular, Hs-mode has protocol/timing requirements
beyond simply writing a larger clock value, so backend/controller support must
be verified on the target platform.

**Reference:** NXP UM10204, "I2C-bus specification and user manual", defines
Standard-mode, Fast-mode, Fast-mode Plus, and High-speed mode.

**impl/rp2040:** Native Pico SDK `hardware/i2c.h` on I2C0/I2C1 plus `hardware/gpio.h` pin muxing; per-bus mutex guards all transactions. Clock requests above Fast-mode Plus are clamped to 1 MHz because RP2040 I2C does not implement Hs-mode. `hal_i2c_bus_clear()` uses GPIO-level SCL/SDA recovery before restoring the I2C pin function.
**impl/stm32g474:** Register-level I2C v2 master on I2C1/I2C2. The backend validates SDA/SCL alternate-function mappings, configures GPIO open-drain pull-ups, supports the HAL clock tiers via 16 MHz TIMINGR presets, handles write/read/write-read/is-busy paths on both buses, and performs GPIO-level bus clear before init.
**impl/.mock:** ring buffer; injectable via mock helpers. Injected RX bytes are consumed sequentially by request/read transactions, which lets tests script multi-register flows. `hal_i2c_end_transmission()` returns `HAL_I2C_ERROR_GENERIC` when the mock busy flag is set, `HAL_I2C_RESULT_OK` otherwise. `hal_i2c_bus_clear()` increments an internal counter (query via `hal_mock_i2c_get_bus_clear_count()`); counter resets on `hal_i2c_init()`.
**Thread safety:** Hardware backends serialize transfer APIs with an internal per-bus `hal_mutex_t`; use `hal_i2c_lock` / `hal_i2c_unlock` to extend critical regions around direct third-party/backend bus calls. `hal_i2c_init*()` / `hal_i2c_deinit*()` reconfigure shared bus objects and must be serialized by the application during setup/teardown. Mock backend does not synchronize concurrent access.

**Mock helpers:**
```c
void    hal_mock_i2c_inject_rx(const uint8_t *data, int len);                    // pre-load receive buffer on bus 0
void    hal_mock_i2c_inject_rx_bus(uint8_t bus, const uint8_t *data, int len);   // pre-load receive buffer on selected bus
uint8_t hal_mock_i2c_get_last_addr(void);                                         // last address on bus 0
uint8_t hal_mock_i2c_get_last_addr_bus(uint8_t bus);                              // last address on selected bus
int     hal_mock_i2c_get_lock_depth(void);                                        // current lock depth on bus 0
int     hal_mock_i2c_get_lock_depth_bus(uint8_t bus);                             // current lock depth on selected bus
int     hal_mock_i2c_get_read_byte_lock_depth(void);                              // lock depth captured at the byte-read point in hal_i2c_read_byte() on bus 0
int     hal_mock_i2c_get_read_byte_lock_depth_bus(uint8_t bus);                   // lock depth captured at the byte-read point in hal_i2c_read_byte_bus() on selected bus
bool    hal_mock_i2c_is_initialized(void);                                        // init state for bus 0
bool    hal_mock_i2c_is_initialized_bus(uint8_t bus);                             // init state for selected bus
void    hal_mock_i2c_set_busy(bool busy);                                         // control hal_i2c_is_busy() + end_transmission NACK on bus 0
void    hal_mock_i2c_set_busy_bus(uint8_t bus, bool busy);                        // control hal_i2c_is_busy() + end_transmission NACK on selected bus
void    hal_mock_i2c_set_device_present(uint8_t address, bool present);            // configure scan ACK map on bus 0
void    hal_mock_i2c_set_device_present_bus(uint8_t bus, uint8_t address, bool present); // configure scan ACK map on selected bus
uint32_t hal_mock_i2c_get_bus_clear_count(void);                                  // number of bus_clear calls on bus 0
uint32_t hal_mock_i2c_get_bus_clear_count_bus(uint8_t bus);                       // number of bus_clear calls on selected bus
```

**Example - PCF8574 8-bit I/O expander using the one-shot helpers:**

PCF8574 is addressed once and has no register layout: a single write byte
drives all 8 output latches; a single read byte returns the current port
value. Using `hal_i2c_write_byte()` and `hal_i2c_read_byte()` keeps the
driver code free of explicit begin/write/end or request/read sequences.

```c
#include <hal/hal_i2c.h>

#define PCF8574_ADDR 0x38   // 7-bit address (A2..A0 = 0)

static uint8_t s_portLatch;  // shadow of the 8 output bits

/** Initialize the expander to all-zero outputs. */
bool pcf8574_init(void) {
    s_portLatch = 0x00;
    bool writeOk = false;
    uint8_t endTx = hal_i2c_write_byte(PCF8574_ADDR, s_portLatch, &writeOk);
    return writeOk && (endTx == 0);
}

/** Drive one output pin (0..7). */
bool pcf8574_write_pin(uint8_t pin, bool high) {
    if (pin > 7) return false;
    if (high) s_portLatch |=  (uint8_t)(1u << pin);
    else      s_portLatch &= (uint8_t)~(1u << pin);

    bool writeOk = false;
    uint8_t endTx = hal_i2c_write_byte(PCF8574_ADDR, s_portLatch, &writeOk);
    return writeOk && (endTx == 0);
}

/** Sample one input pin (0..7). Returns false on I2C error too. */
bool pcf8574_read_pin(uint8_t pin) {
    if (pin > 7) return false;
    bool readOk = false;
    uint8_t port = hal_i2c_read_byte(PCF8574_ADDR, &readOk);
    if (!readOk) return false;
    return (port & (uint8_t)(1u << pin)) != 0;
}
```

Note: the helpers rely on the HAL's *internal* per-bus mutex, which covers
a single begin/end pair. Code that interleaves a write-then-read against
another multi-step transaction on the same bus (e.g. set register pointer
-> request N bytes) must serialize the two sequences with a caller-owned
mutex in addition, since the HAL mutex is released at each `end_transmission`.

---

## `hal_i2c_slave` - I2C slave/target with register map  *(optional - `HAL_ENABLE_I2C_SLAVE`)*

Exposes a fixed-size register map over I2C slave mode. A remote master writes
a one-byte register pointer, then reads N bytes starting from that address.
The register pointer auto-increments on each byte read.
The pointer intentionally survives STOP conditions, so a later bare read
continues from the last position unless the master first writes a new register
address. This mirrors common register-map peripherals.

This is independent of the I2C master module (`hal_i2c`) - both can be
disabled/enabled separately, but they cannot share the same bus simultaneously.

```c
#include <hal/hal_i2c_slave.h>

// Default register map size (override in hal_project_config.h)
#ifndef HAL_I2C_SLAVE_REG_MAP_SIZE
#define HAL_I2C_SLAVE_REG_MAP_SIZE 32U
#endif

// Init / deinit
void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address);
void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin, uint8_t address);
void hal_i2c_slave_deinit(void);
void hal_i2c_slave_deinit_bus(uint8_t bus);

// Write to register map (application -> slave buffer).
// Out-of-range registers are silently ignored.
void hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value);
void hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value);
void hal_i2c_slave_reg_write16(uint8_t reg, uint16_t value);   // big-endian: MSB at reg, LSB at reg+1
void hal_i2c_slave_reg_write16_bus(uint8_t bus, uint8_t reg, uint16_t value);

// Read from register map
uint8_t  hal_i2c_slave_reg_read8(uint8_t reg);
uint8_t  hal_i2c_slave_reg_read8_bus(uint8_t bus, uint8_t reg);
uint16_t hal_i2c_slave_reg_read16(uint8_t reg);
uint16_t hal_i2c_slave_reg_read16_bus(uint8_t bus, uint8_t reg);

// Query address
uint8_t hal_i2c_slave_get_address(void);
uint8_t hal_i2c_slave_get_address_bus(uint8_t bus);

// Transaction counter - counts completed master reads and writes since init.
// Useful for detecting live bus activity without polling reg_write return values.
// Resets on init. Wraps at UINT32_MAX. Thread-safe (atomic).
uint32_t hal_i2c_slave_get_transaction_count(void);
uint32_t hal_i2c_slave_get_transaction_count_bus(uint8_t bus);
```

Only bus values 0 and 1 are supported. Other values are programmer errors and
trigger `HAL_ASSERT` in checked builds.

**Register map protocol (I2C):**
1. Master writes: `[reg_address]` - sets the register pointer
2. Master reads N bytes - slave responds with `regs[ptr], regs[ptr+1], ...`
3. Master writes: `[reg_address, data0, data1, ...]` - sets pointer, then writes data sequentially

**impl/rp2040:** Native Pico SDK `hardware/i2c.h` peripheral mode on I2C0/I2C1 plus `hardware/irq.h` event handling. RX FIFO, read-request, START and STOP/TX-abort interrupts drive the register-map protocol directly.
**impl/stm32g474:** Register-level I2C v2 target mode on I2C1/I2C2. The backend configures SDA/SCL alternate functions, own-address match, conservative `TIMINGR`, RX/TX/ADDR/STOP/NACK/error interrupts, TXDR flush on NACK/STOP, and serves the same register-map protocol from I2C EV/ER IRQ handlers.
**impl/.mock:** direct register-map access; simulation helpers for master write/read.
**Thread safety:** `reg_write*` / `reg_read*` are thread-safe for normal task/core callers on hardware backends. The register map is protected by a short backend-local lock shared with bus callbacks/ISRs, so handlers do not take HAL mutexes in FreeRTOS builds. `init` / `deinit` must be serialized by the application during setup/teardown. Mock backend does not synchronize concurrent access.

**Mock helpers:**
```c
bool    hal_mock_i2c_slave_is_initialized(void);                                       // init state for bus 0
bool    hal_mock_i2c_slave_is_initialized_bus(uint8_t bus);
uint8_t hal_mock_i2c_slave_get_reg(uint8_t reg);                                       // read register directly (bus 0)
uint8_t hal_mock_i2c_slave_get_reg_bus(uint8_t bus, uint8_t reg);
void    hal_mock_i2c_slave_set_reg(uint8_t reg, uint8_t value);                         // write register directly (bus 0)
void    hal_mock_i2c_slave_set_reg_bus(uint8_t bus, uint8_t reg, uint8_t value);
uint8_t hal_mock_i2c_slave_get_reg_ptr(void);                                          // current pointer (bus 0)
uint8_t hal_mock_i2c_slave_get_reg_ptr_bus(uint8_t bus);
void    hal_mock_i2c_slave_simulate_receive(const uint8_t *data, int len);              // simulate master-write
void    hal_mock_i2c_slave_simulate_receive_bus(uint8_t bus, const uint8_t *data, int len);
int     hal_mock_i2c_slave_simulate_request(uint8_t *out_buf, int max_len);             // simulate master-read
int     hal_mock_i2c_slave_simulate_request_bus(uint8_t bus, uint8_t *out_buf, int max_len);
```

---

## `hal_swserial` - Software UART  *(optional - `HAL_ENABLE_SWSERIAL`)*

UART frame-format constants for `config` are defined in `hal/hal_uart_config.h`.

```c
#include <hal/hal_uart_config.h>

// 5/6/7/8 data bits, N/E/O parity, 1/2 stop bits
HAL_UART_CFG_5N1  HAL_UART_CFG_6N1  HAL_UART_CFG_7N1  HAL_UART_CFG_8N1
HAL_UART_CFG_5N2  HAL_UART_CFG_6N2  HAL_UART_CFG_7N2  HAL_UART_CFG_8N2
HAL_UART_CFG_5E1  HAL_UART_CFG_6E1  HAL_UART_CFG_7E1  HAL_UART_CFG_8E1
HAL_UART_CFG_5E2  HAL_UART_CFG_6E2  HAL_UART_CFG_7E2  HAL_UART_CFG_8E2
HAL_UART_CFG_5O1  HAL_UART_CFG_6O1  HAL_UART_CFG_7O1  HAL_UART_CFG_8O1
HAL_UART_CFG_5O2  HAL_UART_CFG_6O2  HAL_UART_CFG_7O2  HAL_UART_CFG_8O2
```

All values are Arduino-compatible. On Arduino targets HAL maps directly to
core `SERIAL_*` constants; on host/mock builds HAL uses fallback values that
match ArduinoCore-API.

```c
#include <hal/hal_swserial.h>

typedef hal_swserial_impl_t *hal_swserial_t;  // opaque handle

hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin);
bool hal_swserial_set_rx(hal_swserial_t h, uint8_t rx_pin);
bool hal_swserial_set_tx(hal_swserial_t h, uint8_t tx_pin);
void hal_swserial_begin(hal_swserial_t h, uint32_t baud, uint16_t config);  // e.g. HAL_UART_CFG_8N1
int  hal_swserial_available(hal_swserial_t h);
int  hal_swserial_read(hal_swserial_t h);     // returns byte (0-255) or -1 if empty
size_t hal_swserial_write(hal_swserial_t h, const uint8_t *data, size_t len);
size_t hal_swserial_println(hal_swserial_t h, const char *s);
void hal_swserial_flush(hal_swserial_t h);       // block until TX complete
void hal_swserial_destroy(hal_swserial_t h);
```

The implementation is shared across RP2040, STM32G474 and mock builds. It uses
HAL GPIO interrupts for RX start-bit detection, HAL microsecond timing for
sampling/transmit bit periods, HAL critical sections for timing-sensitive bit
streams and per-instance HAL mutexes for public API calls.

**Mock helpers:**
```c
void        hal_mock_swserial_push(hal_swserial_t h, const uint8_t *data, int len);
void        hal_mock_swserial_reset(hal_swserial_t h);
const char *hal_mock_swserial_last_write(hal_swserial_t h);
```

---

## `hal_uart` - Hardware UART  *(optional - `HAL_ENABLE_UART`)*

```c
#include <hal/hal_uart.h>

typedef enum {
    HAL_UART_PORT_1 = 1,
    HAL_UART_PORT_2 = 2,
} hal_uart_port_t;

typedef hal_uart_impl_t *hal_uart_t;

typedef struct {
    uint32_t rx_overrun;
    // Framing errors; STM32 noise errors are counted here too.
    uint32_t rx_framing;
    uint32_t rx_parity;
    // Explicit break condition when the backend exposes a break flag.
    uint32_t rx_break;
    uint32_t rx_buffer_overflow;
} hal_uart_error_counters_t;

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin, uint8_t tx_pin);
bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin);
bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin);
void hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config);
int  hal_uart_available(hal_uart_t h);
int  hal_uart_read(hal_uart_t h);
size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len);
size_t hal_uart_println(hal_uart_t h, const char *s);
void hal_uart_flush(hal_uart_t h);       // block until TX complete
bool hal_uart_get_error_counters(hal_uart_t h,
                                 hal_uart_error_counters_t *counters);
void hal_uart_destroy(hal_uart_t h);
```

**impl/rp2040:** RP2040 SDK UART (`uart0` / `uart1`) with interrupt-driven RX.
**impl/stm32g474:** register-level USART1/USART2, polled RX drain; counts ORE, PE, FE, NE, and explicit LIN-break flags when reported by USART_ISR.
**impl/.mock:** ring buffer plus last-write capture; injectable via mock helpers.
**Error counters:** cumulative since `hal_uart_begin()`; mock reset also clears them.
**Thread safety:** Not thread-safe. All calls must be serialized by the caller.

**Mock helpers:**
```c
void        hal_mock_uart_push(hal_uart_t h, const uint8_t *data, int len);
void        hal_mock_uart_reset(hal_uart_t h);
const char *hal_mock_uart_last_write(hal_uart_t h);
uint8_t     hal_mock_uart_get_rx_pin(hal_uart_t h);
uint8_t     hal_mock_uart_get_tx_pin(hal_uart_t h);

typedef void (*hal_mock_uart_write_cb_t)(hal_uart_t h, const char *text, void *user);
void        hal_mock_uart_set_write_callback(hal_uart_t h,
                                             hal_mock_uart_write_cb_t cb,
                                             void *user);
```

---

## `hal_onewire` - 1-Wire bus  *(optional - `HAL_ENABLE_ONEWIRE`)*

Thread-safe wrapper for one 1-Wire bus bound to a single GPIO pin. Hardware
builds use the shared Arduino-free bit-bang driver in
`src/hal/impl/shared/drivers/onewire/`; the mock backend keeps deterministic scripted
responses for host tests.

```c
#include <hal/hal_onewire.h>

typedef struct hal_onewire_impl_s *hal_onewire_t;

hal_onewire_t hal_onewire_init(uint8_t data_pin);
void          hal_onewire_deinit(hal_onewire_t h);

bool    hal_onewire_reset(hal_onewire_t h);
void    hal_onewire_select(hal_onewire_t h, const uint8_t rom[8]);
void    hal_onewire_skip(hal_onewire_t h);
void    hal_onewire_write(hal_onewire_t h, uint8_t value, bool power);
size_t  hal_onewire_write_bytes(hal_onewire_t h, const uint8_t *data,
                                uint16_t len, bool power);
uint8_t hal_onewire_read(hal_onewire_t h);
size_t  hal_onewire_read_bytes(hal_onewire_t h, uint8_t *out, uint16_t len);
void    hal_onewire_write_bit(hal_onewire_t h, uint8_t bit);
uint8_t hal_onewire_read_bit(hal_onewire_t h);
void    hal_onewire_depower(hal_onewire_t h);

void    hal_onewire_reset_search(hal_onewire_t h);
void    hal_onewire_target_search(hal_onewire_t h, uint8_t family_code);
bool    hal_onewire_search(hal_onewire_t h, uint8_t out_rom[8],
                           bool search_mode);

uint8_t  hal_onewire_crc8(const uint8_t *data, uint8_t len);
bool     hal_onewire_check_crc16(const uint8_t *data, uint16_t len,
                                 const uint8_t inverted_crc[2],
                                 uint16_t crc);
uint16_t hal_onewire_crc16(const uint8_t *data, uint16_t len, uint16_t crc);
```

**impl/rp2040 + impl/stm32g474:** Both delegate to the same shared driver. The
driver uses HAL GPIO input/output switching, `hal_delay_us()` slot timing and
HAL critical sections around timing-sensitive sub-slots. An external 1-Wire
pull-up is still expected, matching the original OneWire electrical model.
**impl/.mock:** Scripted presence/read/search responses plus CRC helpers.
**Thread safety:** Hardware builds use a per-handle mutex and a shared bus
mutex around public operations. DS18B20 uses its own low-level driver instance
so multi-step scratchpad transactions remain atomic under the DS18B20 mutex.

---


---

*Next: [CAN and display](10_can_display.md)*
