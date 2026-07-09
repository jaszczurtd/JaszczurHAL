# Sensors

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_thermocouple`, `hal_ds18b20`, `hal_dht`, `hal_bh1750`, `hal_mcp3221`, `hal_tsc2007`, `hal_stmpe610`, `hal_irsmall_decoder`, `hal_rtc`, `hal_external_adc`, `hal_gps`.

## `hal_thermocouple` - Thermocouple amplifier  *(optional - `HAL_ENABLE_THERMOCOUPLE`)*

Supports MCP9600/MCP9601 (shared HAL I2C driver) and MAX6675 (shared SPI
bit-bang over HAL GPIO). Functions not available on the
selected chip return a safe default (NAN / 0 / false) and print an error.

```c
#include <hal/hal_thermocouple.h>

// Chip selector
typedef enum {
    HAL_THERMOCOUPLE_CHIP_MCP9600,  // MCP9600/MCP9601 via I2C
    HAL_THERMOCOUPLE_CHIP_MAX6675,  // MAX6675 via HAL GPIO bit-bang SPI (K-type only)
} hal_thermocouple_chip_t;

// Wire type (MCP9600 supports all; MAX6675 is fixed K-type)
typedef enum {
    HAL_THERMOCOUPLE_TYPE_K = 0, HAL_THERMOCOUPLE_TYPE_J, HAL_THERMOCOUPLE_TYPE_T,
    HAL_THERMOCOUPLE_TYPE_N,     HAL_THERMOCOUPLE_TYPE_S, HAL_THERMOCOUPLE_TYPE_E,
    HAL_THERMOCOUPLE_TYPE_B,     HAL_THERMOCOUPLE_TYPE_R,
} hal_thermocouple_type_t;

// ADC resolution (MCP9600 only)
typedef enum {
    HAL_THERMOCOUPLE_ADC_RES_18 = 0,  // 18-bit, ~320 ms/conv
    HAL_THERMOCOUPLE_ADC_RES_16,      // 16-bit, ~80 ms/conv
    HAL_THERMOCOUPLE_ADC_RES_14,      // 14-bit, ~20 ms/conv
    HAL_THERMOCOUPLE_ADC_RES_12,      // 12-bit, ~5 ms/conv
} hal_thermocouple_adc_res_t;

// Ambient (cold-junction) resolution (MCP9600 only)
typedef enum {
    HAL_THERMOCOUPLE_AMBIENT_RES_0_25    = 0,  // 0.25 °C/LSB
    HAL_THERMOCOUPLE_AMBIENT_RES_0_125,         // 0.125 °C/LSB
    HAL_THERMOCOUPLE_AMBIENT_RES_0_0625,        // 0.0625 °C/LSB
    HAL_THERMOCOUPLE_AMBIENT_RES_0_03125,       // 0.03125 °C/LSB
} hal_thermocouple_ambient_res_t;

// Config struct - fill chip, then the matching bus union member
typedef struct {
    hal_thermocouple_chip_t chip;
    union {
        struct { uint8_t sda_pin; uint8_t scl_pin; uint32_t clock_hz; uint8_t i2c_addr; } i2c;
        struct { uint8_t sclk_pin; uint8_t cs_pin; uint8_t miso_pin; } spi;
    } bus;
} hal_thermocouple_config_t;

typedef hal_thermocouple_impl_t *hal_thermocouple_t;  // opaque handle

// Lifecycle
hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg);
void               hal_thermocouple_deinit(hal_thermocouple_t h);  // NULL-safe

// Readings
float   hal_thermocouple_read(hal_thermocouple_t h);          // hot junction °C, NAN on fault
float   hal_thermocouple_read_ambient(hal_thermocouple_t h);  // cold junction °C (MCP9600 only)
int32_t hal_thermocouple_read_adc_raw(hal_thermocouple_t h);  // raw µV (MCP9600 only); 0 if unsupported

// Configuration (MCP9600 only unless noted)
void hal_thermocouple_set_type(hal_thermocouple_t h, hal_thermocouple_type_t type);
hal_thermocouple_type_t hal_thermocouple_get_type(hal_thermocouple_t h);  // MAX6675 always returns K

void hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff);    // IIR coeff [0,7]
uint8_t hal_thermocouple_get_filter(hal_thermocouple_t h);

void hal_thermocouple_set_adc_resolution(hal_thermocouple_t h, hal_thermocouple_adc_res_t res);
hal_thermocouple_adc_res_t hal_thermocouple_get_adc_resolution(hal_thermocouple_t h);

void hal_thermocouple_set_ambient_resolution(hal_thermocouple_t h, hal_thermocouple_ambient_res_t res);

void hal_thermocouple_enable(hal_thermocouple_t h, bool enable);  // false = sleep (MCP9600 only)
bool hal_thermocouple_is_enabled(hal_thermocouple_t h);           // MAX6675 always returns true

// Alert channels 1-4 (MCP9600 only)
typedef struct {
    float temperature; bool rising; bool alert_cold_junction;
    bool active_high;  bool interrupt_mode;
} hal_thermocouple_alert_cfg_t;

void  hal_thermocouple_set_alert(hal_thermocouple_t h, uint8_t alert_num,
                                  bool enabled, const hal_thermocouple_alert_cfg_t *cfg);
float hal_thermocouple_get_alert_temp(hal_thermocouple_t h, uint8_t alert_num);

uint8_t hal_thermocouple_get_status(hal_thermocouple_t h);  // raw status register
```

**impl/rp2040:** MCP9600/MCP9601 and MAX6675 delegate to shared Arduino-free drivers.
**impl/stm32g474:** MCP9600/MCP9601 and MAX6675 delegate to the same shared drivers as RP2040.
**Thread safety:** Thread-safe and multicore-safe. Each instance has its own per-instance `hal_mutex_t`. All read, configuration, and deinit operations are protected under this mutex.

---

## `hal_ds18b20` - DS18B20 digital temperature sensor  *(optional - `HAL_ENABLE_DS18B20`)*

Non-blocking sensor workflow:

1. `hal_ds18b20_request()` starts conversion.
2. `hal_ds18b20_poll()` advances the state machine.
3. `hal_ds18b20_take_latest()` reads cached sample (`fresh=true` only once per new sample).

```c
#include <hal/hal_ds18b20.h>

#ifndef HAL_DS18B20_MAX_INSTANCES
#define HAL_DS18B20_MAX_INSTANCES 4
#endif

typedef struct hal_ds18b20_impl_s *hal_ds18b20_t;

typedef enum {
    HAL_DS18B20_RES_9_BIT  = 9,
    HAL_DS18B20_RES_10_BIT = 10,
    HAL_DS18B20_RES_11_BIT = 11,
    HAL_DS18B20_RES_12_BIT = 12,
} hal_ds18b20_resolution_t;

typedef struct {
    uint8_t data_pin;
    bool    use_rom;      // false: Skip ROM (single-device bus), true: Match ROM
    uint8_t rom_code[8];  // valid when use_rom=true
    hal_ds18b20_resolution_t resolution_hint;
} hal_ds18b20_config_t;

hal_ds18b20_t hal_ds18b20_init(const hal_ds18b20_config_t *cfg);
void          hal_ds18b20_deinit(hal_ds18b20_t h);
bool          hal_ds18b20_request(hal_ds18b20_t h);
void          hal_ds18b20_poll(hal_ds18b20_t h);
bool          hal_ds18b20_is_busy(hal_ds18b20_t h);
bool          hal_ds18b20_take_latest(hal_ds18b20_t h, float *temp_c, bool *fresh);
```

**impl/rp2040 + impl/stm32g474:** Both use the shared Arduino-free
`src/hal/impl/shared/drivers/onewire/` implementation. The backend performs DS18B20
presence/address probing, scratchpad CRC verification, resolution writes,
non-blocking conversion scheduling with `hal_micros64()`, and temperature decode
over the shared 1-Wire bit-bang transport.
**impl/.mock:** deterministic conversion state machine driven by mock time (`hal_mock_set_micros` / `hal_mock_advance_micros`), with injected presence/CRC/temperature.
**Thread safety:** Hardware backends use a per-handle mutex. Create/destroy
should still follow the project-wide single-core init/deinit policy. Mock
backend is intended for single-threaded tests.

**Mock helpers:**
```c
void     hal_mock_ds18b20_set_next_temp(hal_ds18b20_t h, float temp_c);
void     hal_mock_ds18b20_set_presence(hal_ds18b20_t h, bool present);
void     hal_mock_ds18b20_set_crc_ok(hal_ds18b20_t h, bool ok);
uint32_t hal_mock_ds18b20_get_request_count(hal_ds18b20_t h);
```

---

## `hal_dht` - DHT11/DHT22 temperature and humidity sensor  *(optional - `HAL_ENABLE_DHT`)*

Blocking single-frame DHT reader over HAL GPIO.

```c
#include <hal/hal_dht.h>

#ifndef HAL_DHT_MAX_INSTANCES
#define HAL_DHT_MAX_INSTANCES 4
#endif

typedef enum {
  HAL_DHT_SENSOR_DHT11 = 0,
  HAL_DHT_SENSOR_DHT22 = 1,
} hal_dht_sensor_t;

typedef struct {
  uint8_t data_pin;
  hal_dht_sensor_t sensor;
} hal_dht_config_t;

typedef struct hal_dht_impl_s *hal_dht_t;

typedef struct {
  float temperature_c;
  float temperature_f;
  float humidity;
} hal_dht_sample_t;

hal_dht_config_t hal_dht_default_config(uint8_t data_pin);
hal_dht_t        hal_dht_init(const hal_dht_config_t *cfg);
void             hal_dht_deinit(hal_dht_t h);
bool             hal_dht_read(hal_dht_t h);
float            hal_dht_get_temperature_c(hal_dht_t h);
float            hal_dht_get_temperature_f(hal_dht_t h);
float            hal_dht_get_humidity(hal_dht_t h);
bool             hal_dht_get_sample(hal_dht_t h, hal_dht_sample_t *out);
```

`hal_dht_read()` performs the DHT start pulse and 40-bit frame read, validates
the checksum, and updates the cached sample only on success. The implementation
keeps the Bonezegei DHT timing flow: 250 ms idle-high settle, 18 ms host-low
start pulse, 40 us release, 80/80 us response timing, and a 30 us bit
discriminator. DHT11 frames are decoded as integral humidity and decimal
temperature bytes; DHT22 frames use the native 16-bit humidity and signed 16-bit
temperature fields with 0.1 unit resolution.

**impl/rp2040 + impl/stm32g474 + impl/.mock:** all use
`impl/shared/drivers/dht/hal_dht.cpp` over HAL GPIO/system/sync primitives.
**Thread safety:** handle creation uses a singleton pool mutex created with
`jh_hal_mutex_create_once`; each handle has its own mutex for read/get/deinit.
The timing-critical frame read masks interrupts only for the short DHT
bit-bang window.

---

## `hal_bh1750` - BH1750 ambient-light sensor  *(optional - `HAL_ENABLE_BH1750`)*

```c
#include <hal/hal_bh1750.h>

#define HAL_BH1750_I2C_ADDR_LOW      0x23u
#define HAL_BH1750_I2C_ADDR_HIGH     0x5Cu
#define HAL_BH1750_I2C_ADDR_DEFAULT  HAL_BH1750_I2C_ADDR_HIGH

typedef struct {
  uint8_t i2c_bus;   // 0 = default controller, 1 = second controller
  uint8_t i2c_addr;  // 7-bit BH1750 address
} hal_bh1750_config_t;

typedef struct {
  hal_bh1750_config_t cfg;
  bool initialized;
  hal_mutex_t mutex;
} hal_bh1750_t;

hal_bh1750_config_t hal_bh1750_default_config(void);
hal_status_t hal_bh1750_init_ex(hal_bh1750_t *dev,
                                const hal_bh1750_config_t *cfg);
bool  hal_bh1750_init(hal_bh1750_t *dev, const hal_bh1750_config_t *cfg);
void  hal_bh1750_deinit(hal_bh1750_t *dev);
hal_status_t hal_bh1750_light_ex(hal_bh1750_t *dev, float *out_lux);
float hal_bh1750_light(hal_bh1750_t *dev);
```

`hal_bh1750_init_ex()` sends command `0x10` (continuous H-resolution mode),
waits 180 ms for the first measurement, and returns `HAL_OK` only when the
device ACKs the command. `hal_bh1750_light_ex()` reads exactly two bytes and
returns lux as `raw / 1.2f` through `out_lux`; failed reads return `HAL_EBUS`
and set the output to `-1.0f`. The legacy `hal_bh1750_init()` and
`hal_bh1750_light()` wrappers preserve the original `bool` / `-1.0f` behavior.

**impl/shared:** `impl/shared/drivers/bh1750/hal_bh1750.cpp` is used by RP2040,
STM32G474, and mock tests. The default address is `0x5C` to preserve the source
driver constructor default; boards with ADDR tied low should set `0x23`.
**Thread safety:** per-instance mutex serializes driver calls; I2C byte reads
use `hal_i2c_read_bytes_bus()` so request and sample copy stay inside the bus
mutex.

---

## `hal_adp5360` - ADP5360 PMIC  *(optional - `HAL_ENABLE_ADP5360`)*

```c
#include <hal/hal_adp5360.h>

#define HAL_ADP5360_I2C_ADDR_DEFAULT 0x46u
#define HAL_ADP5360_DEVICE_ID        0x10u

hal_adp5360_config_t hal_adp5360_default_config(void);
hal_status_t hal_adp5360_init_ex(hal_adp5360_t *dev,
                                 const hal_adp5360_config_t *cfg);
bool hal_adp5360_init(hal_adp5360_t *dev, const hal_adp5360_config_t *cfg);
void hal_adp5360_deinit(hal_adp5360_t *dev);

hal_status_t hal_adp5360_shipment_mode_enable(hal_adp5360_t *dev);
hal_status_t hal_adp5360_software_reset(hal_adp5360_t *dev);
hal_status_t hal_adp5360_hardware_reset(hal_adp5360_t *dev);

hal_status_t hal_adp5360_charger_enable(hal_adp5360_t *dev, bool enable);
hal_status_t hal_adp5360_fuel_gauge_get_soc_pct(hal_adp5360_t *dev,
                                                uint8_t *out_pct);
hal_status_t hal_adp5360_fuel_gauge_get_voltage_uv(hal_adp5360_t *dev,
                                                   uint32_t *out_uv);
hal_status_t hal_adp5360_regulator_set_voltage(hal_adp5360_t *dev,
                                               hal_adp5360_regulator_t reg,
                                               int32_t min_uv,
                                               int32_t max_uv);
```

The shared ADP5360 driver is modeled on the working Zephyr ADP5360 MFD,
charger, fuel-gauge and regulator drivers, but depends only on JaszczurHAL.
`hal_adp5360_init_ex()` probes device ID `0x10`, programs supervisory reset /
watchdog options, clears interrupt status registers, and optionally applies the
charger, fuel-gauge, BUCK and BUCK-BOOST configuration sections from
`hal_adp5360_config_t`.

Runtime APIs expose the Zephyr-derived behavior as `hal_status_t` calls:
shipment mode, software/hardware reset, charger online/status/health/current
controls, fuel-gauge SOC/voltage/capacity/alarm reads and writes, and
regulator voltage/current/mode/enable/active-discharge control. The low-level
`hal_adp5360_reg_read/write/burst/update()` helpers are public for board bring
up and diagnostics.

**impl/shared:** `impl/shared/drivers/adp5360/hal_adp5360.cpp` is used by
RP2040, STM32G474 and mock tests. It uses HAL I2C/GPIO/time primitives and a
per-device mutex created with `jh_hal_mutex_create_once()`, so the driver is
safe to call from multicore/FreeRTOS task contexts when the underlying HAL I2C
backend is initialized.

Current scope intentionally does not include Zephyr-style GPIO interrupt
callback registration for ADP5360 INT/PGOOD/reset-status pins.

---

## `hal_tsc2007` - TSC2007 resistive touch controller  *(optional - `HAL_ENABLE_TSC2007`)*

```c
#include <hal/hal_tsc2007.h>

#define HAL_TSC2007_I2C_ADDR_DEFAULT      0x48u
#define HAL_TSC2007_TOUCH_INVALID         4095u
#define HAL_TSC2007_STABILITY_THRESHOLD   100u

typedef struct {
  uint8_t i2c_bus;   // 0 = default controller, 1 = second controller
  uint8_t i2c_addr;  // 7-bit TSC2007 address
} hal_tsc2007_config_t;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t z;         // Z1 pressure sample
} hal_tsc2007_point_t;

typedef struct {
  hal_tsc2007_config_t cfg;
  bool initialized;
  hal_mutex_t mutex;
} hal_tsc2007_t;

hal_tsc2007_config_t hal_tsc2007_default_config(void);
hal_status_t hal_tsc2007_init_ex(hal_tsc2007_t *dev,
                                 const hal_tsc2007_config_t *cfg);
bool hal_tsc2007_init(hal_tsc2007_t *dev, const hal_tsc2007_config_t *cfg);
void hal_tsc2007_deinit(hal_tsc2007_t *dev);

hal_status_t hal_tsc2007_command_ex(hal_tsc2007_t *dev,
                                    hal_tsc2007_function_t func,
                                    hal_tsc2007_power_t pwr,
                                    hal_tsc2007_resolution_t res,
                                    uint16_t *out_value);
uint16_t hal_tsc2007_command(hal_tsc2007_t *dev,
                             hal_tsc2007_function_t func,
                             hal_tsc2007_power_t pwr,
                             hal_tsc2007_resolution_t res);

hal_status_t hal_tsc2007_read_touch_ex(hal_tsc2007_t *dev, uint16_t *x,
                                       uint16_t *y, uint16_t *z1,
                                       uint16_t *z2);
bool hal_tsc2007_read_touch(hal_tsc2007_t *dev, uint16_t *x, uint16_t *y,
                            uint16_t *z1, uint16_t *z2);
hal_tsc2007_point_t hal_tsc2007_get_point(hal_tsc2007_t *dev);
```

`hal_tsc2007_init_ex()` probes the 7-bit address and sends the same initial
`MEASURE_TEMP0` / `POWERDOWN_IRQON` / 12-bit command as the source driver.
`hal_tsc2007_command_ex()` builds the command byte as `(function << 4) |
(power << 2) | (resolution << 1)`, waits 500 us, reads exactly two bytes and
returns the 12-bit value decoded from the upper reply bits through `out_value`.
The legacy `hal_tsc2007_command()` wrapper still returns `0` on failure.

`hal_tsc2007_read_touch_ex()` performs the established sequence:
`Z1`, `Z2`, `X`, `Y`, duplicate `X`, duplicate `Y`, then `MEASURE_TEMP0` with
power-down. The X/Y sample is accepted only when the duplicate measurements are
within `HAL_TSC2007_STABILITY_THRESHOLD` and neither accepted coordinate equals
`HAL_TSC2007_TOUCH_INVALID`. Rejected samples return `HAL_ENOENT`, I2C
transaction failures return `HAL_EBUS`, and invalid arguments return
`HAL_EINVAL`. `hal_tsc2007_read_touch()` preserves the legacy `bool` shape, and
`hal_tsc2007_get_point()` returns `{x, y, z1}` or `{0, 0, 0}` when the sample
is rejected.

**impl/shared:** `impl/shared/drivers/tsc2007/tsc2007.cpp` is used by RP2040,
STM32G474, and mock tests over HAL I2C and HAL system timing.
**Thread safety:** per-instance mutex serializes public driver calls and is
created with the shared create-once helper, so first access is safe under
FreeRTOS/RP2040 multicore. `hal_tsc2007_deinit()` should not run concurrently
with other operations on the same instance.

---

## `hal_stmpe610` - STMPE610 resistive touch controller  *(optional - `HAL_ENABLE_STMPE610`)*

```c
#include <hal/hal_stmpe610.h>

#define HAL_STMPE610_I2C_ADDR_DEFAULT 0x41u
#define HAL_STMPE610_CHIP_ID          0x0811u
#define HAL_STMPE610_SPI_CLOCK_HZ     1000000ul

typedef enum {
  HAL_STMPE610_TRANSPORT_I2C,
  HAL_STMPE610_TRANSPORT_SPI,
  HAL_STMPE610_TRANSPORT_SOFT_SPI,
} hal_stmpe610_transport_t;

typedef struct {
  hal_stmpe610_transport_t transport;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t spi_bus;
  uint8_t cs_pin;
  uint8_t mosi_pin;
  uint8_t miso_pin;
  uint8_t sck_pin;
} hal_stmpe610_config_t;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t z;
} hal_stmpe610_point_t;

hal_stmpe610_config_t hal_stmpe610_default_config(void);
hal_stmpe610_config_t hal_stmpe610_i2c_config(uint8_t bus, uint8_t addr);
hal_stmpe610_config_t hal_stmpe610_spi_config(uint8_t bus, uint8_t cs_pin);
hal_stmpe610_config_t hal_stmpe610_soft_spi_config(uint8_t cs_pin,
                                                   uint8_t mosi_pin,
                                                   uint8_t miso_pin,
                                                   uint8_t sck_pin);

bool hal_stmpe610_init(hal_stmpe610_t *dev, const hal_stmpe610_config_t *cfg);
hal_status_t hal_stmpe610_init_ex(hal_stmpe610_t *dev,
                                  const hal_stmpe610_config_t *cfg);
void hal_stmpe610_deinit(hal_stmpe610_t *dev);

bool hal_stmpe610_touched(hal_stmpe610_t *dev);
bool hal_stmpe610_buffer_empty(hal_stmpe610_t *dev);
uint8_t hal_stmpe610_buffer_size(hal_stmpe610_t *dev);
hal_status_t hal_stmpe610_read_data_ex(hal_stmpe610_t *dev, uint16_t *x,
                                       uint16_t *y, uint8_t *z);
void hal_stmpe610_read_data(hal_stmpe610_t *dev, uint16_t *x, uint16_t *y,
                            uint8_t *z);
hal_stmpe610_point_t hal_stmpe610_get_point(hal_stmpe610_t *dev);

uint8_t hal_stmpe610_read_register8(hal_stmpe610_t *dev, uint8_t reg);
uint16_t hal_stmpe610_read_register16(hal_stmpe610_t *dev, uint8_t reg);
void hal_stmpe610_write_register8(hal_stmpe610_t *dev, uint8_t reg,
                                  uint8_t value);
```

`hal_stmpe610_init_ex()` probes chip ID `0x0811`, keeps the original
hardware-SPI mode-1 fallback when mode 0 does not answer, then runs the
established touch-controller setup sequence: soft reset, 10 ms wait, register
flush reads, TSC enable, touch interrupt enable, ADC/TSC timing setup, FIFO
threshold/reset, 50 mA drive current and interrupt-status clear. It reports
bad arguments/configuration as `HAL_EINVAL`, allocation failure as `HAL_ENOMEM`
and chip-ID mismatch as `HAL_ENOENT`; `hal_stmpe610_init()` remains the legacy
`bool` wrapper.

`hal_stmpe610_read_data_ex()` reads four bytes from the FIFO data port and
decodes 12-bit X/Y plus 8-bit pressure, returning `HAL_EUNINIT` for an
uninitialized instance and `HAL_EINVAL` for bad output pointers.
`hal_stmpe610_read_data()` preserves the old void-returning behavior.
`hal_stmpe610_get_point()` drains the FIFO, returns the last sample, and clears
interrupt status when the FIFO is empty. The I2C 16-bit register read path is
dispatched only through I2C; this avoids the fall-through transport bug present
in the source import.

**impl/shared:** `impl/shared/drivers/stmpe610/stmpe610.cpp` is used by RP2040,
STM32G474, and mock tests. I2C uses HAL bus-selecting transfers; hardware SPI
uses HAL SPI transactions plus a caller-provided CS pin; soft SPI bit-bangs
MSB-first over HAL GPIO.
**Thread safety:** per-instance mutex serializes public driver calls and is
created with the shared create-once helper, so first access is safe under
FreeRTOS/RP2040 multicore. Hardware SPI transactions additionally lock the HAL
SPI bus while CS is asserted. `hal_stmpe610_deinit()` should not run
concurrently with other operations on the same instance.

---

## `hal_irsmall_decoder` - IR receiver decoder  *(optional - `HAL_ENABLE_IRSMALL_DECODER`)*

```c
#include <hal/hal_irsmall_decoder.h>

typedef enum {
  HAL_IRSMALL_PROTOCOL_NEC,
  HAL_IRSMALL_PROTOCOL_NECX,
  HAL_IRSMALL_PROTOCOL_RC5,
  HAL_IRSMALL_PROTOCOL_SIRC12,
  HAL_IRSMALL_PROTOCOL_SIRC15,
  HAL_IRSMALL_PROTOCOL_SIRC20,
  HAL_IRSMALL_PROTOCOL_SIRC,
  HAL_IRSMALL_PROTOCOL_SAMSUNG,
  HAL_IRSMALL_PROTOCOL_SAMSUNG32,
} hal_irsmall_protocol_t;

typedef struct {
  hal_irsmall_protocol_t protocol;
  uint8_t input_pin;
  bool timeout_enabled;
  hal_irq_priority_t irq_priority;
} hal_irsmall_decoder_config_t;

typedef struct {
  hal_irsmall_protocol_t protocol;
  uint16_t addr;
  uint8_t cmd;
  uint8_t ext;
  bool key_held;
  uint8_t bits;
} hal_irsmall_decoder_data_t;

hal_irsmall_decoder_config_t
hal_irsmall_decoder_default_config(uint8_t input_pin,
                                   hal_irsmall_protocol_t protocol);

bool hal_irsmall_decoder_init(hal_irsmall_decoder_t *dev,
                              const hal_irsmall_decoder_config_t *cfg);
void hal_irsmall_decoder_deinit(hal_irsmall_decoder_t *dev);
void hal_irsmall_decoder_enable(hal_irsmall_decoder_t *dev);
void hal_irsmall_decoder_disable(hal_irsmall_decoder_t *dev);
void hal_irsmall_decoder_reset(hal_irsmall_decoder_t *dev);
bool hal_irsmall_decoder_data_available(hal_irsmall_decoder_t *dev,
                                        hal_irsmall_decoder_data_t *out);
bool hal_irsmall_decoder_has_data(hal_irsmall_decoder_t *dev);
```

`hal_irsmall_decoder_init()` configures the input as pull-up, attaches a GPIO
interrupt with the edge mode used by the selected protocol, and uses
`hal_micros()` intervals to decode NEC, NEC extended, RC5, Sony SIRC
12/15/20-bit, Sony SIRC triple-frame, Samsung 20-bit, and Samsung 32-bit
frames. `hal_irsmall_decoder_data_available()` copies and clears one decoded
frame; `hal_irsmall_decoder_has_data()` clears pending data without copying it.

**impl/shared:** `impl/shared/frameworks/irsmall_decoder/irsmall_decoder.cpp` is used by
RP2040, STM32G474, and mock tests over HAL GPIO interrupts and HAL system
timing. The shared implementation keeps the source timing thresholds and repeat
suppression behavior; NEC extended address bytes are assembled explicitly to
avoid type-punned reads. The RC5 frame decoder uses the transition-table state
machine from the existing RP2040-tested `RC5` driver, with shared
`key_held` reporting applied after a valid frame is decoded.
**Thread safety:** public calls are serialized by an instance mutex created
with the shared create-once helper. ISR-shared timestamp/state reads use short
critical sections for timeout/reset paths. Up to
`HAL_IRSMALL_DECODER_MAX_INSTANCES` instances can be attached at once.

---

## `hal_rtc` - Real-time clock  *(optional - `HAL_ENABLE_RTC`)*

Handle-based RTC abstraction. Current backends are PCF8563 and DS3231 over I2C.
The API is vendor-neutral and already exposes generic alarm/timer/clock-output
and event/IRQ controls.

```c
#include <hal/hal_rtc.h>

#ifndef HAL_RTC_MAX_INSTANCES
#define HAL_RTC_MAX_INSTANCES 4
#endif

typedef struct hal_rtc_impl_s *hal_rtc_t;

typedef enum {
  HAL_RTC_CHIP_PCF8563 = 0,
  HAL_RTC_CHIP_DS3231,
} hal_rtc_chip_t;

typedef struct {
  uint8_t  sda_pin;
  uint8_t  scl_pin;
  uint32_t clock_hz;
  uint8_t  i2c_bus;   // 0 = default, 1 = second controller
  uint8_t  i2c_addr;  // 0 = backend default (0x51 PCF8563, 0x68 DS3231)
} hal_rtc_i2c_cfg_t;

typedef struct {
  hal_rtc_chip_t chip;
  union {
    hal_rtc_i2c_cfg_t i2c;
  } bus;
} hal_rtc_config_t;

typedef struct {
  uint8_t  second;    // 0..59
  uint8_t  minute;    // 0..59
  uint8_t  hour;      // 0..23
  uint8_t  day;       // 1..31
  uint8_t  weekday;   // 0..6
  uint8_t  month;     // 1..12
  uint16_t year;      // 1900..2099
  bool     clock_integrity;
} hal_rtc_datetime_t;

#define HAL_RTC_FLAG_ALARM (1u << 0)
#define HAL_RTC_FLAG_TIMER (1u << 1)

#define HAL_RTC_IRQ_ALARM  (1u << 0)
#define HAL_RTC_IRQ_TIMER  (1u << 1)

typedef enum {
  HAL_RTC_CLKOUT_DISABLED = 0,
  HAL_RTC_CLKOUT_1_HZ,
  HAL_RTC_CLKOUT_32_HZ,
  HAL_RTC_CLKOUT_1024_HZ,
  HAL_RTC_CLKOUT_32768_HZ,
} hal_rtc_clkout_mode_t;

typedef enum {
  HAL_RTC_TIMER_DISABLED = 0,
  HAL_RTC_TIMER_1_60_HZ,
  HAL_RTC_TIMER_1_HZ,
  HAL_RTC_TIMER_64_HZ,
  HAL_RTC_TIMER_4096_HZ,
} hal_rtc_timer_clock_t;

typedef struct {
  bool    minute_enabled;
  uint8_t minute;
  bool    hour_enabled;
  uint8_t hour;
  bool    day_enabled;
  uint8_t day;
  bool    weekday_enabled;
  uint8_t weekday;
} hal_rtc_alarm_t;

hal_rtc_t hal_rtc_init(const hal_rtc_config_t *cfg);
void      hal_rtc_deinit(hal_rtc_t h);

bool hal_rtc_get_datetime(hal_rtc_t h, hal_rtc_datetime_t *out_dt);
bool hal_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt);
bool hal_rtc_get_clock_integrity(hal_rtc_t h, bool *out_ok);

bool hal_rtc_set_interrupt_enable(hal_rtc_t h, uint8_t irq_mask);
bool hal_rtc_get_interrupt_enable(hal_rtc_t h, uint8_t *out_irq_mask);
bool hal_rtc_get_and_clear_flags(hal_rtc_t h, uint8_t *out_flags);

bool hal_rtc_set_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t mode);
bool hal_rtc_get_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t *out_mode);

bool hal_rtc_set_timer(hal_rtc_t h, hal_rtc_timer_clock_t timer_clock, uint8_t count);
bool hal_rtc_get_timer(hal_rtc_t h, hal_rtc_timer_clock_t *out_timer_clock, uint8_t *out_count);

bool hal_rtc_set_alarm(hal_rtc_t h, const hal_rtc_alarm_t *alarm);
bool hal_rtc_get_alarm(hal_rtc_t h, hal_rtc_alarm_t *out_alarm);

// Unix epoch helpers (seconds since 1970-01-01 UTC)
bool hal_rtc_get_epoch(hal_rtc_t h, uint64_t *out_epoch);
bool hal_rtc_set_epoch(hal_rtc_t h, uint64_t epoch);

// On-die temperature (DS3231 only; PCF8563 returns false)
bool hal_rtc_get_temperature(hal_rtc_t h, float *out_temperature_c);
```

**impl/rp2040 + impl/stm32g474:**
- PCF8563 backend: direct register access over `hal_i2c` (date-time,
  clock integrity/VL bit, alarm fields, timer mode+count, CLKOUT mode,
  interrupt enable mask and read-clear event flags).
- DS3231 backend: shared portable DS3231 driver over `hal_i2c` with date-time,
  clock integrity via OSF/`oscillatorCheck()`, alarm/IRQ mapping using Alarm2,
  and partial CLKOUT mapping (`1 Hz`, `1.024 kHz`, `32.768 kHz`).
  Timer functions and `HAL_RTC_CLKOUT_32_HZ` are not supported and return `false`.
**impl/.mock:** in-memory state model with deterministic behavior for unit tests.
**Thread safety:** Hardware backends: per-handle mutex serializes runtime API calls;
I2C traffic is additionally protected by the `hal_i2c` bus mutex. Create/destroy
should follow the project-wide single-core init/deinit policy. Mock backend is
for deterministic single-threaded tests.

**Mock helpers:**
```c
void hal_mock_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt);
void hal_mock_rtc_set_clock_integrity(hal_rtc_t h, bool ok);
void hal_mock_rtc_set_flags(hal_rtc_t h, uint8_t flags);
```

---


## `hal_external_adc` - ADS1115 external ADC  *(optional - `HAL_ENABLE_EXTERNAL_ADC`)*

```c
#include <hal/hal_external_adc.h>

// Init ADS1115 at the given 7-bit I2C address.
// adc_range: LSB size in millivolts (e.g. 0.1875 for ±6.144 V full-scale).
//            Stored internally for hal_ext_adc_read_scaled().
void    hal_ext_adc_init(uint8_t address, float adc_range);
void    hal_ext_adc_init_bus(uint8_t i2c_bus, uint8_t address, float adc_range); // i2c_bus: 0=default, 1=second controller

// Read raw signed 16-bit value from channel 0-3.
// Sets gain to 0 (±6.144 V) before each conversion; blocks until result ready.
int16_t hal_ext_adc_read(uint8_t channel);

// Read channel and return (raw * adc_range) / 1000.0f.
// Apply further project-specific corrections (voltage divider etc.) on top.
float   hal_ext_adc_read_scaled(uint8_t channel);
```

**impl/shared:** Arduino-free ADS1X15/ADS1115 driver over HAL I2C, used by RP2040 and STM32G474.
**Thread safety:** RP2040/STM32G474: thread-safe and multicore-safe where the backend mutex implementation provides it. A dedicated internal `hal_mutex_t` serializes ADC channel selection and range access; HAL I2C transactions protect the bus. `hal_ext_adc_init()` / `hal_ext_adc_init_bus()` modify global singleton state and should be called during init. Mock backend does not synchronize concurrent access.

**Mock helpers:**
```c
void  hal_mock_ext_adc_inject_raw(uint8_t channel, int16_t value);   // inject raw 16-bit result for channel 0-3
void  hal_mock_ext_adc_inject_scaled(uint8_t channel, float value);  // inject pre-scaled float for channel 0-3
float hal_mock_ext_adc_get_range(void);                               // return adc_range set by hal_ext_adc_init()
```

---

## `hal_gps` - GPS NMEA receiver  *(optional - `HAL_ENABLE_GPS`)*

Singleton GPS subsystem. A portable in-tree NMEA parser behind a platform-independent API.
The real implementation feeds the parser from a PIO-based SoftwareSerial port;
the mock lets tests inject position, speed, date and time directly.

**Auto-detect framing:** After ~500 received characters, if every NMEA sentence
failed its checksum, the implementation automatically toggles between 8N1 and
7N1 and re-initialises the serial port.  This handles both genuine u-blox modules
(8N1) and clone NEO-6M boards that ship as 7N1 - no user intervention required.

```c
#include <hal/hal_gps.h>

// Initialise the GPS subsystem (only first call has effect - singleton guard).
// config: UART frame format - HAL_UART_CFG_8N1 (recommended default) or
//         HAL_UART_CFG_7N1.  Auto-detect will try the other if checksums fail.
void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud, uint16_t config);

// Drain available serial bytes into the parser.
// Must be called frequently (every main-loop iteration) to prevent the
// PIO SoftwareSerial 32-byte FIFO from overflowing at 9600 baud.
// No-op in the mock build - use inject helpers directly.
void hal_gps_update(void);

// Feed one raw NMEA byte into the parser manually (alternative to hal_gps_update).
// In mock builds this is a no-op.
void hal_gps_encode(char c);

// Fix state
bool     hal_gps_location_is_valid(void);    // true when a valid fix is available
bool     hal_gps_location_is_updated(void);  // true when new data arrived since last query
uint32_t hal_gps_location_age(void);         // ms since last valid fix; UINT32_MAX if no fix

// Position
double hal_gps_latitude(void);   // degrees, negative = south
double hal_gps_longitude(void);  // degrees, negative = west

// Speed
double hal_gps_speed_kmph(void); // ground speed in km/h; 0.0 when no fix

// Extended fix data (from GGA/GSA/GSV sentences)
double   hal_gps_altitude_m(void);              // altitude above MSL, metres
double   hal_gps_course_deg(void);              // course over ground, degrees
uint32_t hal_gps_satellites_used(void);         // satellites used in the fix
uint8_t  hal_gps_satellites_in_view(void);      // satellites in view
double   hal_gps_hdop(void);                    // horizontal dilution of precision
double   hal_gps_vdop(void);                    // vertical dilution of precision
double   hal_gps_pdop(void);                    // position dilution of precision
uint8_t  hal_gps_fix_quality(void);             // GGA fix-quality indicator
uint8_t  hal_gps_fix_mode(void);                // GSA fix mode (1=none,2=2D,3=3D)
double   hal_gps_horizontal_accuracy_m(void);   // estimated horizontal accuracy, metres

// UTC date
int hal_gps_date_year(void);   // four-digit year
int hal_gps_date_month(void);  // 1-12
int hal_gps_date_day(void);    // 1-31

// UTC time
int hal_gps_time_hour(void);   // 0-23
int hal_gps_time_minute(void); // 0-59
int hal_gps_time_second(void); // 0-59

// Diagnostics
uint32_t hal_gps_chars_processed(void);    // total bytes fed into the parser
uint32_t hal_gps_passed_checksum(void);    // NMEA sentences that passed checksum
uint32_t hal_gps_failed_checksum(void);    // NMEA sentences that failed checksum
uint32_t hal_gps_sentences_with_fix(void); // valid sentences containing a location fix
int      hal_gps_serial_available(void);   // bytes waiting in the serial RX buffer
```

**Engine:** the portable NMEA parser (`impl/shared/frameworks/gps/gps_nmea_parser.cpp`) wrapped
by a shared facade (`impl/shared/frameworks/gps/hal_gps_core.cpp`) - used by both hardware
backends; parsing logic ported from TinyGPS++ (LGPL), GSA/GSV/GST from the minmea parser.
**impl/rp2040 (RP2040):** transport only - SoftwareSerial (default) or UART,
selected at compile time. `hal_gps_update()` must be polled every loop iteration.
**impl/stm32g474:** transport only - hardware UART (USART1 by default).
**impl/.mock:** internal state struct; inject helpers set values directly.
**Thread safety:** the shared engine is thread-safe and multicore-safe - an
internal `hal_mutex_t` protects the parser state, the byte feed and all accessor
calls. Mock backend is unsynchronized and intended for single-threaded tests.

**UART config default:**

`HAL_GPS_DEFAULT_UART_CONFIG` (defined in `hal/hal_config.h`) defaults to
`HAL_UART_CFG_8N1` (the NMEA 0183 standard).  The auto-detect mechanism makes
this safe for clone modules too - it will switch to 7N1 automatically if needed.

```c
// hal/hal_config.h default (can be overridden in build flags):
#define HAL_GPS_DEFAULT_UART_CONFIG  HAL_UART_CFG_8N1

// Usage:
hal_gps_init(GPS_RX_PIN, GPS_TX_PIN, 9600, HAL_UART_CFG_8N1);
```

**Mock helpers:**
```c
void hal_mock_gps_set_location(double lat, double lng);        // inject latitude and longitude
void hal_mock_gps_set_valid(bool valid);                       // control hal_gps_location_is_valid()
void hal_mock_gps_set_updated(bool updated);                   // control hal_gps_location_is_updated()
void hal_mock_gps_set_age(uint32_t age_ms);                    // control hal_gps_location_age()
void hal_mock_gps_set_speed(double kmph);                      // control hal_gps_speed_kmph()
void hal_mock_gps_set_date(int year, int month, int day);      // control date accessors
void hal_mock_gps_set_time(int hour, int minute, int second);  // control time accessors
void hal_mock_gps_reset(void);                                 // zero all state
```

---


---

## `hal_mcp3221` - MCP3221 12-bit ADC  *(optional - `HAL_ENABLE_MCP3221`)*

```c
#include <hal/hal_mcp3221.h>

hal_i2c_init(sda_pin, scl_pin, HAL_I2C_CLOCK_STANDARD_HZ);

hal_mcp3221_t adc = {0};
hal_status_t status = hal_mcp3221_init_ex(&adc, NULL);
if (status == HAL_OK) {
  uint16_t raw = 0;
  status = hal_mcp3221_read_ex(&adc, &raw);
}
```

Default config uses bus 0 and address `HAL_MCP3221_I2C_ADDR_DEFAULT` (`0x4D`,
matching the grblHAL plugin default `(0x9A >> 1)`). `hal_mcp3221_read_ex()`
requests exactly two bytes and decodes them as a big-endian raw value, preserving
the source driver's behavior.

**Thread safety:** per-instance mutex serializes reads; I2C transactions use
the HAL I2C bus lock. Lifecycle calls remain single-owner.

Example: `examples/53_simple_io_chips`.

---

*Next: [Cellular modem](12_modem.md)*
