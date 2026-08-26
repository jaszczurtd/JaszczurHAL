# Utilities

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_soft_timer`, `hal_pid_controller`, `tools.h/cpp`, `SmartTimers`, `pidController`, `multicoreWatchdog`, `draw7Segment`.

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

**Purpose:** lets C-style modules consume timer functionality without direct `SmartTimers` class coupling.
**Implementation:** delegates to `SmartTimers` internally (same runtime semantics).
**Thread safety:** Thread-safe and multicore-safe (inherits `SmartTimers` per-instance mutex protection).
**Table helpers:** `hal_soft_timer_setup_table(...)` creates/configures timers from a descriptor array and optionally calls `idle_cb` + inter-entry delay. `hal_soft_timer_tick_table(...)` ticks all entries from the same array.
**Validation rules:** table helpers validate `table != NULL` and `count > 0`. For invalid input they log via `hal_derr(...)` and return `false`.

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

**Purpose:** exposes PID control through C functions and opaque handles, enabling incremental migration from class-based usage.
**Implementation:** delegates to `PIDController` internally (same runtime semantics).
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

## Higher-level utilities (tools.h / tools.cpp)

Physical location: core utility helpers live in `src/utils/*`; shared framework
utilities such as SmartTimers live with their domain under `src/hal/timers/*`.

Recommended include options:
- `#include <tools.h>` (aggregator include in `src/`)
- `#include <tools_c.h>` (C-compatible utility declarations from `src/`)
- direct include from the component path, for example
  `#include <hal/timers/smart_timers/SmartTimers.h>`

Utilities depend on HAL internally.

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

### Functions

```c
void  debugInit(void);                  // optional - hal_deb() lazy-inits automatically
void  setDebugPrefixWithColon(const char *moduleName); // builds "<module>:" within HAL_DEBUG_PREFIX_SIZE and forwards to hal_deb_set_prefix
void  floatToDec(float val, int *hi, int *lo);
float decToFloat(int hi, int lo);
float adcToVolt(int adc, float r1, float r2);
float ntcToTemp(int tpin, int thermistor, int r);
float steinhart(float val, float thermistor, int r, bool characteristic);
int   percentToGivenVal(float percent, int maxWidth);
int   percentFrom(int givenVal, int maxVal);
float getAverageValueFrom(int tpin);  // includes dummy read (RP2040 ADC mux cross-talk fix)
float filter(float alpha, float input, float previous_output);
float filterValue(float currentValue, float newValue, float alpha);
int   adcCompe(int x);
float getAverageForTable(int *idx, int *overall, float val, float *table);
int   getAverageFrom(int *table, int size);
int   getMinimumFrom(int *table, int size);
int   getHalfwayBetweenMinMax(int *array, int n);
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max);
static inline uint32_t float_to_u32(float f);   // bitcast float -> uint32_t (memcpy)
static inline float    u32_to_float(uint32_t u); // bitcast uint32_t -> float (memcpy)
unsigned long getSeconds(void);
bool  isDaylightSavingTime(int year, int month, int day);
void  adjustTime(int *year, int *month, int *day, int *hour, int *minute);
bool  is_time_in_range(long now, long start, long end);
void  extract_time(long timeInMinutes, int *hours, int *minutes);
unsigned short byteArrayToWord(unsigned char *bytes);
void     wordToByteArray(unsigned short word, unsigned char *bytes);
uint8_t  MSB(unsigned short value);
uint8_t  LSB(unsigned short value);
int      MsbLsbToInt(uint8_t msb, uint8_t lsb);
float rroundf(float val);
float roundfWithPrecisionTo(float value, int precision);
char *printBinaryAndSize(int number, char *buf, size_t bufSize);  // writes binary string into buf
bool  concatStrings(char *dest, size_t destSize, const char *src1, const char *src2); // false on NULL args or too-small dest
bool  isValidString(const char *s, int maxBufSize);
char  hexToChar(char high, char low);
void  urlDecode(const char *src, char *dst);
void  removeSpaces(char *str);
bool  startsWith(const char *str, const char *prefix);
void  remove_non_ascii(const char *input, char *output, size_t outputSize);  // replaces UTF-8 with ASCII
void  hal_pack_field_pad(uint8_t *buf, const char *str, int width, uint8_t pad);
void  hal_pack_field(uint8_t *buf, const char *str, int width); // zero padding
unsigned short rgbToRgb565(unsigned char r, unsigned char g, unsigned char b);
bool rgb888ToRgb565(const unsigned char *rgb, unsigned short *rgb565, size_t pixelCount);
bool rgba8888ToRgb565(const unsigned char *rgba, unsigned short *rgb565, size_t pixelCount); // ignores alpha
bool pngBase64DecodedSize(const char *base64, size_t base64Len,
                          size_t *pngSize); // requires HAL_ENABLE_PNG_AS_BASE64
bool pngBase64Decode32(unsigned char **rgba, unsigned *width, unsigned *height,
                       const char *base64, size_t base64Len,
                       uint8_t *pngWork, size_t pngWorkSize,
                       unsigned *pngError); // requires HAL_ENABLE_PNG_AS_BASE64
bool pngBase64DecodeRgb565(const char *base64, size_t base64Len,
                           uint8_t *pngWork, size_t pngWorkSize,
                           unsigned short *rgb565, size_t rgb565Pixels,
                           unsigned *width, unsigned *height,
                           unsigned *pngError); // requires HAL_ENABLE_PNG_AS_BASE64
const char *macToString(uint8_t mac[6], char *buf, size_t bufSize);
const char *encToString(uint8_t enc);       // HAL WiFi encryption label
bool  scanNetworks(const char *networkToFind);  // requires HAL_ENABLE_WIFI
int   getRandomEverySomeMillis(uint32_t time, int maxValue);
float getRandomFloatEverySomeMillis(uint32_t time, float maxValue);
```

The four legacy time helpers above are compatibility wrappers only. Their
implementations delegate directly to
`hal_time_is_daylight_saving_time()`, `hal_time_adjust_cet_cest()`,
`hal_time_is_in_range()`, and `hal_time_extract_minutes()` respectively. New
code should include `<hal/time/hal_time.h>` and call the HAL API directly; see
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

**Note:** `SECOND`, `SECS()`, `MINS()`, `HOURS()` macros are defined in `hal/system/hal_system.h`
(included automatically by `hal/timers/smart_timers/SmartTimers.h`).
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

**impl:** `hal_watchdog_enable` / `hal_watchdog_feed` / `hal_watchdog_caused_reboot`.
**Note:** The internal timer uses `SmartTimers` and is guarded by a HAL mutex to prevent
double-fire from concurrent `updateWatchdogCore0/1` calls.

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

*Back to [JaszczurHAL API Reference](../JaszczurHAL_API.md)*
