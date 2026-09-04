# Utilities

*Also available in [Polish](../pl/16_utilities.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

Covers: thematic HAL helpers, the compatibility utility headers,
`hal_soft_timer`, `hal_pid_controller`, `SmartTimers`, `pidController`,
`multicoreWatchdog`, and `draw7Segment`.

## `hal_soft_timer` - C wrapper over `SmartTimers`

```c
#include <hal/timers/hal_soft_timer.h>

typedef struct hal_soft_timer_impl_s *hal_soft_timer_t;
typedef void (*hal_soft_timer_callback_t)(void);

hal_soft_timer_t hal_soft_timer_create(void);
void             hal_soft_timer_destroy(hal_soft_timer_t timer);

bool     hal_soft_timer_begin(hal_soft_timer_t timer, hal_soft_timer_callback_t callback, uint32_t interval_ms);
void     hal_soft_timer_restart(hal_soft_timer_t timer);
bool     hal_soft_timer_available(hal_soft_timer_t timer);
uint32_t hal_soft_timer_time_left(hal_soft_timer_t timer);
void     hal_soft_timer_set_interval(hal_soft_timer_t timer, uint32_t interval_ms);
void     hal_soft_timer_tick(hal_soft_timer_t timer);
void     hal_soft_timer_abort(hal_soft_timer_t timer);

typedef struct {
    hal_soft_timer_t         *timer;
    hal_soft_timer_callback_t callback;
    uint32_t                  intervalMs;
} hal_soft_timer_table_entry_t;

bool hal_soft_timer_setup_table(const hal_soft_timer_table_entry_t *table,
                                uint32_t count,
                                void (*idle_cb)(void),
                                uint32_t delay_ms);
bool hal_soft_timer_tick_table(const hal_soft_timer_table_entry_t *table,
                               uint32_t count);
```

- **Purpose:** lets C-style modules consume timer functionality without direct `SmartTimers` class coupling.
- **Implementation:** delegates to `SmartTimers` internally (same runtime semantics).

**Thread safety:** Thread-safe and multicore-safe (inherits `SmartTimers` per-instance mutex protection).

- **Table helpers:** `hal_soft_timer_setup_table(...)` creates/configures timers from a descriptor array and optionally calls `idle_cb` + inter-entry delay. `hal_soft_timer_tick_table(...)` ticks all entries from the same array.
- **Validation rules:** table helpers validate `table != NULL` and `count > 0`. For invalid input they log via `hal_derr(...)` and return `false`.

**Example: periodic callback with C wrapper**
```c
#include <hal/timers/hal_soft_timer.h>

static hal_soft_timer_t status_timer;

static void check_connection(void) {
    if (!hal_wifi_is_connected()) {
        hal_deb("WiFi disconnected, reconnecting...");
        hal_wifi_begin_station("MySSID", "password", true);
    }
}

void app_start(void) {
    hal_debug_init(115200);
    hal_wifi_begin_station("MySSID", "password", false);

    status_timer = hal_soft_timer_create();
    hal_soft_timer_begin(status_timer, check_connection, 5000);
}

void app_task0(void) {
    hal_soft_timer_tick(status_timer);
}
```

---

## `hal_pid_controller` - C wrapper over `pidController`

```c
#include <hal/control/hal_pid_controller.h>

typedef struct hal_pid_controller_impl_s *hal_pid_controller_t;
typedef enum {
  HAL_PID_DIRECTION_FORWARD = 0,
  HAL_PID_DIRECTION_BACKWARD = 1
} hal_pid_direction_t;

hal_pid_controller_t hal_pid_controller_create(void);
hal_pid_controller_t hal_pid_controller_create_with_gains(float kp, float ki, float kd, float max_integral);
void                 hal_pid_controller_destroy(hal_pid_controller_t controller);

void  hal_pid_controller_set_kp(hal_pid_controller_t controller, float kp);
void  hal_pid_controller_set_ki(hal_pid_controller_t controller, float ki);
void  hal_pid_controller_set_kd(hal_pid_controller_t controller, float kd);
void  hal_pid_controller_set_tf(hal_pid_controller_t controller, float tf);
void  hal_pid_controller_set_max_integral(hal_pid_controller_t controller, float max_integral);

float hal_pid_controller_get_kp(hal_pid_controller_t controller);
float hal_pid_controller_get_ki(hal_pid_controller_t controller);
float hal_pid_controller_get_kd(hal_pid_controller_t controller);
float hal_pid_controller_get_tf(hal_pid_controller_t controller);

void  hal_pid_controller_update_time(hal_pid_controller_t controller, float time_divider);
float hal_pid_controller_update(hal_pid_controller_t controller, float error);
void  hal_pid_controller_set_output_limits(hal_pid_controller_t controller, float min_output, float max_output);
void  hal_pid_controller_reset(hal_pid_controller_t controller);
void  hal_pid_controller_set_direction(hal_pid_controller_t controller, hal_pid_direction_t direction);
bool  hal_pid_controller_is_error_stable(hal_pid_controller_t controller, float error, float tolerance, int stability_threshold);
bool  hal_pid_controller_is_oscillating(hal_pid_controller_t controller, float current_error, int window_size);
```

- **Purpose:** exposes PID control through C functions and opaque handles, enabling incremental migration from class-based usage.
- **Implementation:** delegates to `PIDController` internally (same runtime semantics).

**Thread safety:** Not thread-safe. Use one controller instance per control loop or serialize externally.

**Anti-windup:** two complementary mechanisms:
1. **Integral hard clamp** - `setMaxIntegral()` / `hal_pid_controller_set_max_integral()` limits `|integral|`.
2. **Clamping anti-windup** - integral accumulation is skipped when the output
   is saturated in the direction of the error (i.e. output ≥ max and error > 0,
   or output ≤ min and error < 0). This prevents integral windup at output
   limits without requiring manual tuning of the integral cap.

**Example: motor speed control**
```c
#include <hal/control/hal_pid_controller.h>

static hal_pid_controller_t speed_pid;
static float target_rpm = 1000.0f;
static float current_rpm = 0.0f;

void app_start(void) {
    speed_pid = hal_pid_controller_create_with_gains(
        50.0f,    // Kp
        10.0f,    // Ki
        5.0f,     // Kd
        200.0f    // max_integral
    );
    hal_pid_controller_set_output_limits(speed_pid, 0.0f, 255.0f);
    hal_pid_controller_update_time(speed_pid, 1.0f / 10.0f);
}

void app_task0(void) {
    current_rpm = read_encoder_rpm();
    float error = target_rpm - current_rpm;
    float pwm = hal_pid_controller_update(speed_pid, error);

    hal_pwm_write(MOTOR_PWM_PIN, (uint32_t)pwm);

    if (hal_pid_controller_is_error_stable(speed_pid, error, 10.0f, 5)) {
        hal_deb("Speed stable at %.1f RPM", current_rpm);
    }

    hal_delay_ms(10);
}
```

---

## Thematic utility modules

The former miscellaneous implementation has been split by responsibility. New
code includes the owning HAL header and uses the prefixed API. `tools.h`,
`tools_c.h`, and `utils/tools_api.h` remain include aggregators without their
own function declarations. There is no `tools.cpp` implementation unit.

Recommended include options:

- `#include <JaszczurHAL.h>` for the stable public aggregate;
- a direct domain header for narrow dependencies;
- `#include <tools.h>` or `#include <tools_c.h>` only while maintaining legacy
  code.

| Domain | Header | Primary API |
|---|---|---|
| Arrays | `hal/core/hal_array.h` | `COUNTOF()` |
| Integer serialization/endian | `hal/core/jh_endian.h` | `jh_load_*`, `jh_store_*`, `jh_bswap*`, `MSB()`, `LSB()` |
| Numeric/signal helpers | `hal/core/hal_math.h` | `hal_math_*` |
| Bounded text helpers | `hal/core/hal_text.h` | `hal_text_*` |
| NMEA scalar parsing | `hal/gps/hal_gps_nmea_utils.h` | `hal_gps_nmea_*` |
| ADC sampling/conversion | `hal/analog/hal_adc_utils.h` | `hal_adc_*_ex` |
| NTC conversion | `hal/temperature/hal_ntc.h` | `hal_ntc_*_ex` |
| Pixels | `hal/display/hal_pixel.h` | `hal_pixel_*` |
| PNG/JPEG memory adapters | `hal/codecs/hal_image.h` | `hal_image_*` |
| MAC/WiFi scan helpers | `hal/network/hal_network_utils.h` | `hal_network_*`, `hal_wifi_scan_*` |
| Calendar and elapsed time | `hal/time/hal_time.h` | `hal_time_*`, `hal_get_seconds()` |
| Periodically refreshed random values | `hal/system/hal_periodic_random.h` | `hal_periodic_random_*` |

The `_ex` functions return `hal_status_t`, use explicit output buffers/state,
and validate arguments. ADC behavior such as sample count, dummy read, delay,
and transfer correction is selected through `hal_adc_average_config_t` rather
than hidden project-wide behavior.

### Bit-manipulation helpers (`hal_bits`)

Bit aliases are defined with `#ifndef` guards so an application may provide
equivalent definitions before including the header.

```c
#include <hal/core/hal_bits.h>

#define is_set(x, mask)      // true when any bit in mask is set in x
#define set_bit(var, mask)   // OR mask into var
#define clr_bit(var, mask)   // AND ~mask into var

// Bit-index variants (established public names)
#define bitSet(var, bit)     // set bit number 'bit' in var
#define bitClear(var, bit)   // clear bit number 'bit' in var
#define bitRead(var, bit)    // read bit number 'bit' from var (returns 0 or 1)

// Volatile register helpers (macro pointer form)
#define set_bit_v(reg, mask) // set mask in *reg
#define clr_bit_v(reg, mask) // clear mask in *reg

```

Avoid passing expressions with side effects (for example `i++` or function calls
that modify state), because macro arguments may be evaluated more than once.

### Public utility functions

Utility function names follow their owning HAL domain. `tools.h`, `tools_c.h`,
and `tools_api.h` only aggregate these headers; they do not publish a second
set of function names.

```c
void hal_debug_init_default(void);
void hal_debug_set_module_prefix(const char *module_name);

static inline float hal_math_round_to_n(float value, int decimals);
void  hal_math_split_decimal_tenths(float value, int *whole, int *tenths);
float hal_math_join_decimal_tenths(int whole, int tenths);
int   hal_math_percent_to_value(float percent, int maximum);
int   hal_math_percent_from_value(int value, int maximum);
float hal_math_low_pass(float alpha, float input, float previous_output);
float hal_math_blend(float current_value, float new_value, float alpha);
hal_status_t hal_math_rolling_average_f32_ex(size_t *index, size_t *count,
                                              float value, float *table,
                                              size_t table_count,
                                              float *out_average);
hal_status_t hal_math_average_i32_ex(const int *values, size_t count,
                                      int *out_average);
hal_status_t hal_math_min_i32_ex(const int *values, size_t count,
                                  int *out_minimum);
hal_status_t hal_math_midpoint_min_max_i32_ex(const int *values, size_t count,
                                               int *out_midpoint);
float hal_math_rolling_average_default_f32(int *index, int *count,
                                            float value, float *table);
int   hal_math_nonnegative_average_i32(const int *values, int count);
int   hal_math_min_i32(const int *values, int count);
int   hal_math_midpoint_min_max_i32(const int *values, int count);
float hal_math_map_f32(float value, float input_min, float input_max,
                       float output_min, float output_max);
float hal_math_round_tenth(float value);
float hal_math_round_precision(float value, int precision);
static inline uint32_t hal_math_float_to_u32(float value);
static inline float    hal_math_u32_to_float(uint32_t value);

hal_status_t hal_adc_raw_to_voltage_ex(int raw, float reference_voltage,
                                        uint8_t resolution_bits,
                                        float high_side_resistance,
                                        float low_side_resistance,
                                        float *out_voltage);
int hal_adc_compensate_rp2040_12bit(int sample);
hal_status_t hal_adc_read_average_ex(const hal_adc_average_config_t *config,
                                      float *out_average);
float hal_adc_raw_to_voltage(int raw, float high_side_resistance,
                             float low_side_resistance);
float hal_adc_read_average(uint8_t pin);
hal_status_t hal_ntc_temperature_from_adc_ex(
    float adc_average, float adc_full_scale,
    const hal_ntc_beta_config_t *config, float *out_celsius);
hal_status_t hal_ntc_read_temperature_ex(
    const hal_adc_average_config_t *adc_config, float adc_full_scale,
    const hal_ntc_beta_config_t *ntc_config, float *out_celsius);
float hal_ntc_steinhart(float divider_ratio, float nominal_resistance,
                        int resistance, bool characteristic);
float hal_ntc_read_temperature(uint8_t pin, int nominal_resistance,
                               int series_resistance);
unsigned long hal_get_seconds(void);
bool  hal_time_is_daylight_saving_time(int year, int month, int day);
void  hal_time_adjust_cet_cest(int *year, int *month, int *day, int *hour, int *minute);
bool  hal_time_is_in_range(long now, long start, long end);
void  hal_time_extract_minutes(long time_in_minutes, int *hours, int *minutes);
uint16_t jh_load_le16(const uint8_t *input);
uint32_t jh_load_le32(const uint8_t *input);
uint64_t jh_load_le64(const uint8_t *input);
void jh_store_le16(uint8_t *output, uint16_t value);
void jh_store_le32(uint8_t *output, uint32_t value);
void jh_store_le64(uint8_t *output, uint64_t value);
uint16_t jh_load_be16(const uint8_t *input);
uint32_t jh_load_be32(const uint8_t *input);
uint64_t jh_load_be64(const uint8_t *input);
void jh_store_be16(uint8_t *output, uint16_t value);
void jh_store_be32(uint8_t *output, uint32_t value);
void jh_store_be64(uint8_t *output, uint64_t value);
uint16_t jh_bswap16(uint16_t value);
uint32_t jh_bswap32(uint32_t value);
uint64_t jh_bswap64(uint64_t value);
uint8_t  jh_u16_msb(uint16_t value);
uint8_t  jh_u16_lsb(uint16_t value);
uint8_t  MSB(unsigned short value);
uint8_t  LSB(unsigned short value);
uint16_t jh_u16_from_bytes(uint8_t msb, uint8_t lsb);
char *hal_text_format_binary_int(int value, char *buffer, size_t buffer_size);
hal_status_t hal_text_concat_ex(char *destination, size_t destination_size,
                                 const char *first, const char *second);
bool hal_text_concat(char *destination, size_t destination_size,
                     const char *first, const char *second);
bool hal_text_is_printable(const char *text, size_t maximum_size);
hal_status_t hal_text_hex_pair_to_byte_ex(char high, char low,
                                           uint8_t *out_value);
uint8_t hal_text_hex_pair_to_byte(char high, char low);
hal_status_t hal_text_url_decode_ex(const char *source, char *destination,
                                     size_t destination_size,
                                     size_t *out_length);
void  hal_text_url_decode(const char *source, char *destination);
void  hal_text_remove_whitespace(char *text);
bool  hal_text_starts_with(const char *text, const char *prefix);
int   hal_text_parse_number(const char **text);
hal_status_t hal_text_transliterate_ascii_ex(const char *input, char *output,
                                              size_t output_size);
void hal_text_transliterate_ascii(const char *input, char *output,
                                  size_t output_size);
hal_status_t hal_text_pack_field_ex(uint8_t *buffer, size_t width,
                                     const char *text, uint8_t padding);
void hal_text_pack_field_pad(uint8_t *buffer, const char *text, int width,
                             uint8_t padding);
void hal_text_pack_field(uint8_t *buffer, const char *text, int width);
uint16_t hal_pixel_rgb888_to_rgb565(uint8_t red, uint8_t green, uint8_t blue);
hal_status_t hal_pixel_rgb888_buffer_to_rgb565_ex(const uint8_t *rgb,
                                                   uint16_t *rgb565,
                                                   size_t pixel_count);
hal_status_t hal_pixel_rgba8888_buffer_to_rgb565_ex(const uint8_t *rgba,
                                                     uint16_t *rgb565,
                                                     size_t pixel_count);
bool hal_image_png_base64_decoded_size(const char *base64, size_t base64_len,
                                        size_t *png_size);
bool hal_image_png_base64_decode_rgba8888(
    uint8_t **rgba, unsigned *width, unsigned *height, const char *base64,
    size_t base64_len, uint8_t *png_work, size_t png_work_size,
    unsigned *png_error);
bool hal_image_png_base64_decode_rgb565(
    const char *base64, size_t base64_len, uint8_t *png_work,
    size_t png_work_size, uint16_t *rgb565, size_t rgb565_pixels,
    unsigned *width, unsigned *height, unsigned *png_error);
bool hal_image_jpeg_decode_rgb565(const uint8_t *jpeg, size_t jpeg_size,
                                  uint16_t *rgb565, size_t rgb565_pixels,
                                  unsigned *width, unsigned *height);
bool hal_image_jpeg_base64_decoded_size(const char *base64, size_t base64_len,
                                        size_t *jpeg_size);
bool hal_image_jpeg_base64_decode_rgb565(const char *base64, size_t base64_len,
                                         uint8_t *jpeg_work,
                                         size_t jpeg_work_size,
                                         uint16_t *rgb565, size_t rgb565_pixels,
                                         unsigned *width, unsigned *height);
int     hal_gps_nmea_hex_value(char value);
int32_t hal_gps_nmea_decimal_x100(const char *text);
void    hal_gps_nmea_degrees(const char *text, int16_t *degrees,
                             uint32_t *billionths);
hal_status_t hal_network_format_mac_ex(const uint8_t mac[6], char *buffer,
                                        size_t buffer_size);
const char *hal_wifi_encryption_to_string(hal_wifi_encryption_t encryption);
hal_status_t hal_wifi_scan_for_ssid_ex(const char *ssid_prefix,
                                        bool log_results, bool *out_found);
bool hal_wifi_scan_for_ssid(const char *ssid_prefix);
void hal_periodic_random_int_init(hal_periodic_random_int_t *random,
                                  uint32_t seed);
void hal_periodic_random_float_init(hal_periodic_random_float_t *random,
                                    uint32_t seed);
hal_status_t hal_periodic_random_int_get_ex(hal_periodic_random_int_t *random,
                                             uint32_t now_ms,
                                             uint32_t interval_ms,
                                             int maximum, int *out_value);
hal_status_t hal_periodic_random_float_get_ex(
    hal_periodic_random_float_t *random, uint32_t now_ms, uint32_t interval_ms,
    float maximum, float *out_value);
int   hal_periodic_random_int_get(uint32_t interval_ms, int maximum);
float hal_periodic_random_float_get(uint32_t interval_ms, float maximum);
```

The domain headers document each function's parameters, units, nullable
outputs, boundary behavior, and error result. Prefer the `_ex` form when a
detailed status or explicit state is available.

The time helpers are declared in `<hal/time/hal_time.h>` and implemented in
`hal_time.cpp`: `hal_get_seconds()`,
`hal_time_is_daylight_saving_time()`, `hal_time_adjust_cet_cest()`,
`hal_time_is_in_range()`, and `hal_time_extract_minutes()`. See
[Calendar helpers and optional system time/NTP](15_connectivity.md)
for boundary, validation, and rollover semantics.

---

## `hal_crc` - CRC checksums

```c
#include <hal/security/hal_crc.h>
```

Backend-agnostic, table-free CRC checksums for data integrity (**not**
cryptography - for hashing/HMAC see [`hal_crypto`](07_crypto.md)). Opt-in via
`HAL_ENABLE_CRC`; enabling `HAL_ENABLE_ONEWIRE` (or `HAL_ENABLE_DS18B20`) also
turns it on, since the 1-Wire path needs CRC for ROM/scratchpad validation. CRC
is a family parameterized by width, polynomial, init, reflection and final XOR,
so every routine is named after the concrete variant it computes - there is
deliberately no unqualified `hal_crc16` that would silently commit to one
polynomial.

### Functions

```c
uint8_t  hal_crc8_maxim(const uint8_t *data, size_t len);   // CRC-8/MAXIM-DOW (1-Wire)
uint16_t hal_crc16_maxim(const uint8_t *data, size_t len, uint16_t crc); // Maxim 1-Wire CRC-16
bool     hal_crc16_maxim_check(const uint8_t *data, size_t len,
                               const uint8_t inverted_crc[2], uint16_t crc);
uint16_t hal_crc16_ccitt(const uint8_t *data, size_t len, uint16_t crc); // CRC-16/CCITT-FALSE, poly 0x1021
uint32_t hal_crc32(const uint8_t *data, size_t len);        // CRC-32/ISO-HDLC (zlib/Ethernet)
```

- The `crc` seed lets `hal_crc16_*` continue over a split buffer; pass `0` for
  the Maxim variant and `HAL_CRC16_CCITT_INIT` (`0xFFFF`) for CCITT to start
  fresh. `hal_crc8_maxim` and `hal_crc32` are one-shot.
- `hal_crc16_maxim_check` reproduces the 1-Wire bus convention where devices
  transmit the CRC-16 bit-inverted (the old `hal_onewire_check_crc16`).
- Catalog check values over `"123456789"`: CRC-8/MAXIM `0xA1`,
  CRC-16/CCITT-FALSE `0x29B1`, CRC-32 `0xCBF43926` - pinned by
  `tests/test_hal_crc.cpp`.

Adding a new variant (e.g. `hal_crc16_ccitt`, `hal_crc32c`) is a self-contained
addition here; it never disturbs the existing entries.

---

## SmartTimers

```c
#include <hal/timers/smart_timers/SmartTimers.h>

SmartTimers timer;

// Configure and start - callback signature: void f(void)
timer.begin(callback, interval_ms);

// Advance timer; fires callback when interval elapsed, then restarts automatically.
// Call in your main loop.
timer.tick();

// true if the interval has elapsed since the last restart (non-blocking)
bool ready = timer.available();

// Remaining milliseconds until the next callback.
// Returns 0 when already elapsed or stopped. Does NOT return the configured interval.
uint32_t remaining = timer.time();

// Change the interval without resetting the timestamp.
// Call restart() afterwards if you want counting to begin from now.
timer.time(new_interval_ms);

// Reset the internal timestamp to now (interval starts counting from this moment)
timer.restart();

// Stop the timer
timer.abort();
```

> **Note:** `SECOND`, `SECS()`, `MINS()`, `HOURS()` macros are defined in `hal/system/hal_system.h`
> (included automatically by `hal/timers/smart_timers/SmartTimers.h`).

**Thread safety:** Thread-safe and multicore-safe after construction. Each instance eagerly creates a per-instance `hal_mutex_t` that serializes all method calls. Callbacks passed to `begin()` are invoked outside the mutex to prevent deadlock.

**Example: multi-timer table**
```c
#include <hal/timers/smart_timers/SmartTimers.h>

SmartTimers heartbeat_timer;
SmartTimers status_timer;
SmartTimers led_timer;

static void on_heartbeat(void) {
    hal_deb("[heartbeat] alive");
}

static void on_status(void) {
    hal_deb("[status] free_heap=%lu bytes", hal_get_free_heap());
}

static void on_led_blink(void) {
    hal_gpio_write(LED_PIN, !hal_gpio_read(LED_PIN));
}

void app_start(void) {
    hal_debug_init(115200);
    hal_gpio_set_mode(LED_PIN, HAL_GPIO_OUTPUT);

    // Start three independent timers
    heartbeat_timer.begin(on_heartbeat, 1000);  // 1 second
    status_timer.begin(on_status, 5000);        // 5 seconds
    led_timer.begin(on_led_blink, 500);         // 500 ms
}

void app_task0(void) {
    // Tick all timers
    heartbeat_timer.tick();
    status_timer.tick();
    led_timer.tick();

    if (heartbeat_timer.available()) {
        hal_deb("Heartbeat ready");
    }

    hal_delay_ms(10);
}
```

---

## pidController

```c
#include <utils/pidController.h>

// Tuning parameters struct
typedef struct { float kP; float kI; float kD; float Tf; } PIDValues;

enum Direction { FORWARD, BACKWARD };

// Construct with initial gains and anti-windup integral limit
PIDController pid(float kp, float ki, float kd, float mi);
PIDController pid;  // default constructor - all gains 0, limits uninitialised

// Tuning setters / getters
pid.setKp(kp);  pid.getKp();
pid.setKi(ki);  pid.getKi();
pid.setKd(kd);  pid.getKd();
pid.setTf(tf);  pid.getTf();   // derivative low-pass filter time constant
pid.setMaxIntegral(mi);        // anti-windup: maximum |integral| value

// Output clamping
pid.setOutputLimits(float min, float max);

// Time step - call this when dt changes (timeDivider = 1/dt in ms)
pid.updatePIDtime(float timeDivider);

// Run one PID iteration; error = setpoint - measurement
float out = pid.updatePIDcontroller(float error);

// Direction
pid.setDirection(FORWARD);   // or BACKWARD

// Analysis
bool stable = pid.isErrorStable(error, tolerance, stabilityThreshold);
bool osc    = pid.isOscillating(currentError, windowSize);  // default windowSize = 20

// Reset integral, derivative, history
pid.reset();

// Backward-compat clamp alias (use hal_constrain() in new code)
template<typename T> T pid_clamp(T v, T lo, T hi);
```

**Thread safety:** Not thread-safe. Each `PIDController` instance should be used from one core/thread.

**Example: temperature control (C++ class style)**
```cpp
#include <utils/pidController.h>

class ThermostatController {
    PIDController pid_ctrl;
    float target_temp;
    float current_temp;

public:
    ThermostatController() : target_temp(25.0f) {
        // Tuning for temperature: slow changes (P-heavy)
        pid_ctrl.setKp(2.0f);
        pid_ctrl.setKi(0.1f);
        pid_ctrl.setKd(0.5f);
        pid_ctrl.setMaxIntegral(50.0f);
        pid_ctrl.setOutputLimits(-100.0f, 100.0f);  // -100 to +100 %
        pid_ctrl.updatePIDtime(1.0f / 30.0f);  // 30 ms control loop
    }

    float compute(float measured_temp) {
        current_temp = measured_temp;
        float error = target_temp - current_temp;
        return pid_ctrl.updatePIDcontroller(error);
    }

    void set_target(float temp) { target_temp = temp; }
    float get_current(void) const { return current_temp; }
};

ThermostatController thermo;

extern "C" void app_start(void) {
    thermo.set_target(22.5f);
}

extern "C" void app_task0(void) {
    float adc_reading = (float)hal_adc_read(TEMP_SENSOR_PIN);
    float temp_c = (adc_reading * 3.3f / 4096.0f - 0.5f) * 100.0f;

    float heater_pwm = thermo.compute(temp_c);
    hal_pwm_write(HEATER_PIN, (uint32_t)(127 + heater_pwm * 127 / 100));

    hal_delay_ms(30);
}
```

---

## multicoreWatchdog

```c
#include <utils/multicoreWatchdog.h>

// Initialise hardware watchdog (timeout = time ms; liveness check every time/10 ms).
// Returns true if the system was rebooted by the watchdog (false = clean boot).
// 'function' callback is invoked once on watchdog-reboot with WATCHDOG_VALUES_AMOUNT
// diagnostic integers (NOINIT core-alive flags). May be NULL.
bool setupWatchdog(void(*function)(int *values, int size), unsigned int time);

// Call from core 0 main loop to signal liveness + tick the internal timer
void updateWatchdogCore0(void);

// Call from core 1 main loop to signal liveness + tick the internal timer
void updateWatchdogCore1(void);

// Call once at the end of each core's setup function
void setStartedCore0(void);
void setStartedCore1(void);

// Returns true when both setStartedCore0() and setStartedCore1() have been called
bool isEnvironmentStarted(void);

// Schedule an immediate hardware reset
void triggerSystemReset(void);

// Feed the hardware watchdog (wraps hal_watchdog_feed; safe to call before setupWatchdog)
void watchdog_feed(void);
```

- **impl:** `hal_watchdog_enable` / `hal_watchdog_feed` / `hal_watchdog_caused_reboot`.

> **Note:** The internal timer uses `SmartTimers` and is guarded by a HAL mutex to prevent
> double-fire from concurrent `updateWatchdogCore0/1` calls.

**Example: dual-core watchdog**
```c
#include <utils/multicoreWatchdog.h>

void on_watchdog_reboot(int *values, int size) {
    hal_derr("Watchdog triggered! Core flags: %d %d", values[0], values[1]);
}

void app_start(void) {
    hal_debug_init(115200);

    bool was_watchdog = setupWatchdog(on_watchdog_reboot, 10000);  // 10 sec
    if (was_watchdog) {
        hal_derr("REBOOT: Previous boot triggered watchdog!");
    }

    setStartedCore0();
}

void app_task0(void) {
    // Perform core 0 work
    do_core0_tasks();

    // Signal liveness to watchdog
    updateWatchdogCore0();

    if (isEnvironmentStarted()) {
        hal_deb("Both cores running");
    }
}

char pad0[128];  // prevent core 1 in same stack frame

void app_task1(void) {
    // Core 1 setup
    setStartedCore1();

    while (true) {
        // Perform core 1 work
        do_core1_tasks();

        // Signal liveness from core 1
        updateWatchdogCore1();
    }
}
```

---

## draw7Segment - 7-segment style display rendering

```c
#include <utils/draw7Segment.h>

// Draw a 7-segment string at (x, y).
// str          - null-terminated string to render.
// digitWidth   - width of a single digit cell in pixels.
// digitHeight  - height of a single digit cell in pixels.
// thickness    - segment thickness in pixels.
// color        - RGB565 colour.
void draw7SegString(const char* str, int x, int y, int digitWidth, int digitHeight, float thickness, uint16_t color);

// Calculate the pixel width of a 7-segment string without drawing it.
// Returns the total width in pixels.
int get7SegStringWidth(const char* str, int digitWidth, float thickness);
```

**Supported characters:** `0`-`9`, hex `A`-`F`, space, `+`, `-`, `.`, `:`, `%`, `^`.

Characters have proportional widths: `1` and space are narrower, `^` slightly wider.

**Dependencies:** `hal_display.h` only. The interface uses no platform-specific types; all text parameters are `const char*`.

**Thread safety:** Thread-safe when `hal_display` is thread-safe (RP-family backend). Delegates all drawing to `hal_display_*` functions which are mutex-protected.

**Example: digital clock display**
```c
#include <utils/draw7Segment.h>
#include <hal/display/hal_display.h>

void app_start(void) {
    // Assume display is already initialized via hal_display_init(...)
    hal_display_fill_screen(HAL_COLOR_BLACK);
}

void draw_time_display(int hours, int minutes, int seconds) {
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hours, minutes, seconds);

    // Calculate width of the string
    int width = get7SegStringWidth(time_str, 32, 3.0f);  // 32-pixel digits
    int x = (320 - width) / 2;  // center horizontally

    // Draw 7-segment display in red color
    draw7SegString(time_str, x, 50, 32, 48, 3.0f, HAL_COLOR_RED);
}

void app_task0(void) {
    uint32_t ms = hal_millis();
    uint32_t total_secs = ms / 1000;

    int hours = (total_secs / 3600) % 24;
    int minutes = (total_secs / 60) % 60;
    int seconds = total_secs % 60;

    draw_time_display(hours, minutes, seconds);

    hal_delay_ms(100);
}
```

**Example: status LED counter**
```c
#include <utils/draw7Segment.h>
#include <hal/gpio/hal_gpio.h>

void show_counter_7seg(int value) {
    char counter_str[16];
    snprintf(counter_str, sizeof(counter_str), "%d", value);

    // Draw on display (2x size, white, at position 10,100)
    draw7SegString(counter_str, 10, 100, 24, 36, 2.0f, HAL_COLOR_WHITE);
}

void app_task0(void) {
    static int counter = 0;

    if (hal_gpio_read(BUTTON_PIN)) {  // button pressed
        counter++;
        if (counter > 9999) counter = 0;
        show_counter_7seg(counter);
        hal_delay_ms(500);
    }
}
```

---


---

*Back to [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)*
