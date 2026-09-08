# GPIO, ADC and PWM

*Also available in [Polish](../pl/05_gpio_adc_pwm.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

## `hal_gpio` - GPIO

Configure digital pins, read and write their state, and register interrupt handlers. When sharing pins between cores, follow the synchronization and IRQ-affinity rules below.

```c
#include <hal/gpio/hal_gpio.h>

typedef enum {
    HAL_GPIO_INPUT                  = 0,
    HAL_GPIO_OUTPUT                 = 1,
    HAL_GPIO_INPUT_PULLUP           = 2,
    HAL_GPIO_INPUT_PULLDOWN         = 3,
    HAL_GPIO_OUTPUT_LOW             = 4,
    HAL_GPIO_OUTPUT_HIGH            = 5,
    HAL_GPIO_OUTPUT_OPEN_DRAIN      = 6,
    HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW  = 7,
    HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH = 8,
} hal_gpio_mode_t;

typedef enum {
    HAL_GPIO_IRQ_FALLING = 0,
    HAL_GPIO_IRQ_RISING  = 1,
    HAL_GPIO_IRQ_CHANGE  = 2,
} hal_gpio_irq_mode_t;

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode);
void hal_gpio_write(uint8_t pin, bool high);
bool hal_gpio_read(uint8_t pin);
void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void), hal_gpio_irq_mode_t mode);
void hal_gpio_detach_interrupt(uint8_t pin);

#define HAL_GPIO_IRQ_CORE_NONE UINT8_MAX

hal_status_t hal_gpio_attach_interrupt_ex(uint8_t pin,
                                          void (*callback)(void),
                                          hal_gpio_irq_mode_t mode,
                                          uint8_t owner_core);
hal_status_t hal_gpio_detach_interrupt_ex(uint8_t pin);
hal_status_t hal_gpio_get_interrupt_owner_ex(uint8_t pin,
                                             uint8_t *out_owner_core);

typedef enum {
    HAL_IRQ_PRIORITY_HIGHEST = 0,
    HAL_IRQ_PRIORITY_HIGH    = 1,
    HAL_IRQ_PRIORITY_DEFAULT = 2,
    HAL_IRQ_PRIORITY_LOW     = 3,
} hal_irq_priority_t;

void hal_gpio_set_irq_priority(hal_irq_priority_t priority);
```

**Note:** The callback passed to `hal_gpio_attach_interrupt` runs in ISR context - avoid `printf`, `malloc`, or any blocking call inside it.

**Validation:** Invalid arguments passed to legacy `void` operations trigger
`HAL_ASSERT` in checked builds. The status-returning IRQ operations report
`HAL_EINVAL` (or `HAL_EUNSUPPORTED` for an unsupported backend/pin) without
configuring hardware.

**Output initial state:** `HAL_GPIO_OUTPUT_LOW/HIGH` and `HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW/HIGH` make the intended initial latch state explicit. `HAL_GPIO_OUTPUT` remains compatible and means push-pull output with initial low.

**Open drain:** On STM32G474 and ESP32-S3 this maps to hardware open-drain. On
RP2040 (native pico-sdk) it is emulated by driving LOW for `false` and releasing
the pin as input (high-Z) for `true`.

**Thread safety:** `hal_gpio_write` / `hal_gpio_read` are thin pass-throughs. Concurrent access to different pins from different cores is safe. Concurrent access to the same pin from two cores requires external synchronization.

**IRQ core ownership:** Use `hal_gpio_attach_interrupt_ex` in multicore code. It
returns `HAL_ESTATE` unless the caller is currently running on `owner_core`, and
atomically records that core as the pin's interrupt owner. Reconfiguration and
`hal_gpio_detach_interrupt_ex` are accepted only from the recorded owner core;
`hal_gpio_get_interrupt_owner_ex` is a read-only diagnostic and may be called
from either core. A detached pin returns `HAL_ENOENT` and writes
`HAL_GPIO_IRQ_CORE_NONE`. The legacy attach wrapper binds the IRQ to its current
caller core, while the legacy detach wrapper asserts if ownership is violated.

On the single-core STM32G474 backend the only valid caller/owner is core 0.
ESP32-S3 allocates one ESP-IDF GPIO ISR service for all HAL GPIO callbacks on
the core that performs the first successful attach. Consequently every active
HAL GPIO interrupt must use that same owner core until the last callback is
detached and the service is released. A different owner reports `HAL_ESTATE`.
The status APIs are intended for initialization/task diagnostics, not ISR
context.

This explicit ownership API covers GPIO interrupts only. Peripheral IRQs have
their own backend requirements. In particular, the RP2040 hardware-UART RX IRQ is
currently bound implicitly to the core that calls `hal_uart_begin()`; GPS
inherits that behavior when built with `HAL_GPS_TRANSPORT_UART`. The UART API
does not expose an owner query. RP begin/reconfigure/destroy must be serialized
on the same core; ESP32-S3 additionally reports `HAL_ESTATE` for wrong-core
reconfiguration and retains the handle when its compatibility destroy operation
is called from another core. See
the [`hal_uart` bus documentation](09_buses.md) and
[`hal_gps` sensor documentation](11_sensors.md).

RP2040 SoftwareSerial instead receives through PIO/DMA and does not install a
CPU RX interrupt.

**STM32G474 routing:** Pin id is `port * 16 + pin` (`PA0=0`, `PB0=16`, ...). EXTI is line-based (`line == pin_number`), so only one port source can own a given line at a time; attaching another pin with the same pin number remaps that EXTI line.

**IRQ priority:** `hal_gpio_set_irq_priority` sets GPIO interrupt priority. On
RP2040 all GPIO pins share `IO_IRQ_BANK0`. On STM32G474 GPIO IRQs are split
across `EXTI0..EXTI4`, `EXTI9_5`, and `EXTI15_10`; the same HAL priority is
applied to all those NVIC entries. ESP32-S3 recreates its shared ISR service on
the service-owner core; highest/high/default-or-low map to ESP-IDF interrupt
levels 3/2/1.

**impl/esp32:** Pin validation consumes the generated target-valid,
input-only, board-exposed, hard-reserved, and soft-reserved masks. Hard-reserved
USB/memory pins are rejected; soft-reserved board pins remain available for an
intentional application override. GPIO callbacks run in ISR context through the
shared ESP-IDF service.

**Interrupt detach:** `hal_gpio_detach_interrupt` removes the registered callback and masks the pin/EXTI source where the backend supports hardware interrupt masking.

For example, RPM capture intended for RP2040 core 1 can fail fast during
initialization instead of silently registering on the wrong core:

```c
hal_status_t status = hal_gpio_attach_interrupt_ex(
    rpm_pin, rpm_edge_isr, HAL_GPIO_IRQ_RISING, 1u);
if (status != HAL_OK) {
    /* Abort ECU startup or report a core-affinity configuration fault. */
}
```

---

## `hal_pwm` - PWM

```c
#include <hal/gpio/hal_pwm.h>

void hal_pwm_set_resolution(uint8_t bits);
bool hal_pwm_is_pin_supported(uint8_t pin);
void hal_pwm_write(uint8_t pin, uint32_t value);
```

`hal_pwm` provides a small, portable API for setting PWM duty cycle. It supports 1-16-bit resolution, and `hal_pwm_write()` clamps values to the current maximum. Unsupported pins are ignored and trigger `HAL_ASSERT` when checks are enabled. Use `hal_pwm_is_pin_supported()` before selecting a pin at runtime.

The simple API does not guarantee an application-selected frequency or independent channel allocation. Use `hal_pwm_freq` to control frequency, period (`wrap`), and channel lifetime. The default `hal_pwm` resolution is 8 bits.

**impl/rp2040:** native pico-sdk `hardware/pwm.h` (`pwm_init`, `pwm_config_set_wrap`,
`pwm_set_gpio_level`, `pwm_set_enabled`). The public duty range stays
`0..2^bits-1`; internally the backend may increase the slice wrap at low
resolutions to preserve the approximately 1 kHz default frequency when
`clkdiv` would otherwise exceed the hardware limit. Two GPIOs on the same
hardware slice (`gpio/2 mod 8`) share one frequency/wrap but keep independent
duty. Use `hal_pwm_freq` when exact frequency matters.

**impl/stm32g474:** register-level TIM PWM output on mapped timer channels;
default simple-PWM target frequency is 1 kHz best-effort. The backend uses
explicit `JH_G474_TIMCLK1_HZ` / `JH_G474_TIMCLK2_HZ` constants. Both are
170 MHz in the current clock tree because APB1 and APB2 run without a
prescaler; future APB changes must update the timer-kernel constants according
to the STM32 timer x2 clock rule.

**impl/esp32:** ESP-IDF LEDC output at 1 kHz. The backend allocates a logical
LEDC channel lazily per output-capable pin, maps the selected 1..16-bit range to
the hardware duty range, and releases all active simple-PWM channels when the
global resolution changes. The logical maximum uses LEDC's idle-high stop state
for exact 100% output; a later lower write uses the duty-update path, which
re-enables the waveform after that stop state. A failed channel stop/deconfigure
keeps the internal channel owned and leaves the global resolution unchanged,
preventing reuse of a still-active hardware channel.

**Thread safety:** RP2040 maps pins to PWM hardware slices; STM32G474 maps pins
to TIM channels. Channels sharing a timer also share frequency/resolution, and
pins sharing the same TIM channel are not independent. Call
`hal_pwm_set_resolution` during init, not concurrently with writes.

---

<a id="hal_dac---true-dac-output--optional---hal_enable_dac"></a>

## `hal_dac` - hardware analog output *(optional - `HAL_ENABLE_DAC`)*  *(optional - `HAL_ENABLE_DAC`)*

Set an analog output through a hardware digital-to-analog converter. The module requires a supported hardware DAC; it does not substitute PWM emulation.

```c
#include <hal/analog/hal_dac.h>

bool hal_dac_is_supported(void);
uint8_t hal_dac_resolution_bits(void);
uint16_t hal_dac_max_value(void);

hal_status_t hal_dac_init_ex(uint8_t channel);
bool hal_dac_init(uint8_t channel);

hal_status_t hal_dac_write(uint8_t channel, uint16_t value);
hal_status_t hal_dac_write_millivolts(uint8_t channel, uint16_t millivolts);
```

The status APIs report `HAL_OK`, `HAL_EUNSUPPORTED` on targets without a true
DAC (RP2040), `HAL_EINVAL` for invalid channels and `HAL_EUNINIT` for writes
before channel initialization. `hal_dac_init()` remains the historical `bool`
compatibility wrapper over `hal_dac_init_ex()`. The historical write functions
now return `hal_status_t` in place; existing callers may continue to ignore the
result.

**impl/stm32g474:** real DAC1, 12-bit, channel 0 -> PA4 and channel 1 -> PA5.

**impl/rp2040:** no true DAC peripheral; status APIs return
`HAL_EUNSUPPORTED`.

**impl/.mock:** two 12-bit channels with mock read-back helpers.

---

## `hal_pcnt` - Pulse / edge counter  *(optional - `HAL_ENABLE_PCNT`)*

Count pulses or edges on a digital input. The API configures a channel, reads its count, and resets it; the counting mechanism depends on the platform.

```c
#include <hal/analog/hal_pcnt.h>

bool hal_pcnt_is_supported(void);
uint8_t hal_pcnt_channel_count(void);

hal_status_t hal_pcnt_init_ex(uint8_t channel, uint8_t pin,
                              hal_pcnt_edge_t edge);
bool hal_pcnt_init(uint8_t channel, uint8_t pin, hal_pcnt_edge_t edge);

hal_status_t hal_pcnt_read_ex(uint8_t channel, uint32_t *out_count);
uint32_t hal_pcnt_read(uint8_t channel);

hal_status_t hal_pcnt_reset(uint8_t channel);

hal_status_t hal_pcnt_read_and_reset_ex(uint8_t channel, uint32_t *out_count);
uint32_t hal_pcnt_read_and_reset(uint8_t channel);
```

The status APIs report `HAL_OK`, `HAL_EINVAL` for invalid channels, pins, edges
or output pointers, and `HAL_EUNINIT` when reading/resetting a valid channel
that has not been initialized. Historical `void hal_pcnt_reset()` now returns
`hal_status_t` directly; existing callers may keep ignoring the result. The
legacy init/read/read-and-reset wrappers retain their `bool`/`uint32_t` shapes.

**impl/rp2040:** GPIO interrupt based software counters.

**impl/stm32g474:** TIM2 external-clock mode for channel 0.

**impl/esp32:** four logical counters backed by ESP-IDF PCNT units. Each input
selects rising, falling, or both edges; accumulated-count mode and signed-limit
watch points extend the underlying counter for the public `uint32_t` result.
Reinitializing a logical channel tears down its previous PCNT unit first.

**impl/.mock:** in-memory counters with pulse injection helpers.


---

## `hal_pwm_freq` - PWM with frequency control  *(optional - `HAL_ENABLE_PWM_FREQ`)*

Choose `hal_pwm_freq` instead of `hal_pwm` when you need a specific frequency, such as 160 Hz or 300 Hz, and explicit channel management.

```c
#include <hal/gpio/hal_pwm_freq.h>

// Opaque handle
typedef hal_pwm_freq_channel_impl_t *hal_pwm_freq_channel_t;

// Create a channel: pin, frequency in Hz, resolution (wrap value, e.g. 2047 for 11-bit = 2^11-1)
hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin,
                                           uint32_t frequency_hz,
                                           uint32_t resolution);

// Write value in [0, resolution] - values outside range are clamped automatically
void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value);

// Stop output without releasing the channel; the next write starts it again
void hal_pwm_freq_stop(hal_pwm_freq_channel_t ch);

// Free resources
void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch);
```

**impl/rp2040:** pico SDK `hardware/pwm.h` + `hardware/clocks.h` - computes clkdiv and wrap
to achieve the exact requested frequency, with pseudo/slow-scale correction for edge cases.

**impl/stm32g474:** register-level TIM PWM on mapped
TIM2/TIM3/TIM4/TIM15/TIM16/TIM17 channels. Frequency is a timer-level
resource, so multiple channels on the same TIM share the same frequency and
effective period. Like `hal_pwm`, it uses the explicit 170 MHz TIMCLK constants,
which keeps DACless sample-rate reporting consistent with timer programming.
The PWM slice is configured at `hal_pwm_freq_create()` time but **not started** - the GPIO
function / TIM channel enable are deferred until the first `hal_pwm_freq_write()` call. This
prevents a glitch on pins with inverted logic (0 % duty = actuator ON) at power-on.

**impl/esp32:** ESP-IDF LEDC through the same target-local allocator used by
simple PWM. Creation reserves one bounded logical handle and a compatible LEDC
timer/channel for the requested pin, frequency, and logical maximum. Writes
clamp to that maximum; stop keeps the handle and destroy releases both logical
and LEDC resources. The shared allocator gives the logical maximum an exact
idle-high 100% state and restarts LEDC on the next partial-duty write. If
ESP-IDF rejects teardown, the logical handle and LEDC slot remain owned for a
retry; checked builds report the failed `hal_pwm_freq_destroy()` with
`HAL_ASSERT`.

**impl/.mock:** stores last written value; injectable via mock helpers.

**Mock helpers:**
```c
int      hal_mock_pwm_freq_get_value(hal_pwm_freq_channel_t ch);
uint32_t hal_mock_pwm_freq_get_frequency(hal_pwm_freq_channel_t ch);
uint8_t  hal_mock_pwm_freq_get_pin(hal_pwm_freq_channel_t ch);
bool     hal_mock_pwm_freq_is_running(hal_pwm_freq_channel_t ch);
```

**Thread safety:** RP2040, STM32G474, and ESP32-S3 backends protect
`hal_pwm_freq_create()`, `hal_pwm_freq_write()`, `hal_pwm_freq_stop()` and
`hal_pwm_freq_destroy()` with an internal mutex. Callers still own channel
handle lifetime and must not use a handle after `hal_pwm_freq_destroy()`. Mock
backend does not provide concurrent-access synchronization.

---

<a id="daclessaudio---pwm-audio-engine--optional---hal_enable_dacless"></a>

## `hal_dacless` - audio playback through PWM *(optional - `HAL_ENABLE_DACLESS`)*

Play audio samples through PWM, using DMA or periodic servicing in the
application loop. The module provides per-sample and block callbacks, sampled
ADC values, and explicit output state.

### C API

The C interface uses an opaque `hal_dacless_t` handle. Each callback receives
its own `void *` application context; a block callback also receives the
active sample count.

```c
#include <hal/audio/hal_dacless.h>

typedef struct {
    uint16_t next_sample;
} audio_source_t;

static uint16_t next_sample(void *context) {
    audio_source_t *source = (audio_source_t *)context;
    return source->next_sample++;
}

hal_status_t start_audio(hal_dacless_t *out_audio, audio_source_t *source) {
    if (out_audio == NULL || source == NULL) return HAL_EINVAL;
    *out_audio = NULL;

    hal_dacless_config_t config = hal_dacless_default_config();
    config.use_dma = false;
    config.adc_input_count = 0u;

    hal_status_t status = hal_dacless_create(&config, out_audio);
    if (status != HAL_OK) return status;

    status = hal_dacless_set_sample_callback(*out_audio, next_sample, source);
    if (status == HAL_OK) status = hal_dacless_begin(*out_audio);
    if (status != HAL_OK) {
        (void)hal_dacless_destroy(*out_audio);
        *out_audio = NULL;
    }
    return status;
}

hal_status_t service_audio(hal_dacless_t audio) {
    return hal_dacless_service(audio);
}
```

`hal_dacless_default_config()` selects 12-bit DMA output, 128 samples per
block, `DACLESS_DEFAULT_PWM_PIN`, and four default ADC inputs. The
implementation normalizes PWM resolution, block size, and ADC storage count to
their supported ranges. DMA accepts at most `DACLESS_MAX_DMA_ADC_INPUTS`
inputs. The static C API pool holds `DACLESS_MAX_INSTANCES` instances. C++
objects registered for the compatibility globals consume the same instance
budget.

Call `hal_dacless_begin()` before service, mute, or unmute operations. In
polling mode, call `hal_dacless_service()` frequently from `app_task0()` or a
FreeRTOS task. A block callback takes precedence over a sample callback; when
neither is installed, the driver outputs midpoint silence. A late polling call
catches up by at most `DACLESS_MAX_POLLING_CATCHUP_SAMPLES` samples before it
resynchronizes with the current time.

Register both callbacks before `hal_dacless_begin()`. After the first
successful start, the callback setters return `HAL_ESTATE`; recreate the
instance to use a different callback or context.

Use `hal_dacless_get_state()` to read the started, muted, running, and DMA
flags. The API also exposes the effective configuration, sample rate, an ADC
channel or driver-owned ADC buffer, and the most recently completed output
buffer. Finish with `hal_dacless_destroy()`.

The default DMA path uses `hal_dma_pwm_audio` for double-buffered PWM output
and ADC updates. On RP platforms, each instance reserves two PWM data DMA
channels and two control channels that switch between the buffers. Sampling
ADC inputs reserves two more DMA channels. STM32G474 uses timer-update DMA,
invokes callbacks after half and complete transfers, and scans ADC1 in circular
mode. Default ADC pins are GPIO 26..29 on RP and in the mock implementation,
and PA0..PA3 on STM32G474 (`port * 16 + pin`).

Creation rejects PWM and used ADC pins that the selected target does not
support. The same pin cannot be used simultaneously for PWM output and an ADC
input. On RP, a DMA ADC scan must use GPIO 26..29 in order. On RP and
STM32G474, only one DMA instance with an ADC scan may own the shared converter
at a time; a competing start returns `HAL_EBUSY`. Muting or stopping that
instance releases the ADC, and resuming it reacquires the resource. One
instance also owns each RP PWM slice; starting a separate DACless stream on
another pin mapped to that slice returns `HAL_EBUSY`. DMA-less instances and
DMA instances without ADC sampling remain independent of the ADC restriction.

**Concurrency:** Calls made by tasks serialize access to instance state.
Creation and destruction are single-owner operations. On RP platforms, configure,
start, control, and destroy all DACless DMA instances from one owner core; the
DMA interrupt is installed on that core. A DMA callback runs in interrupt
context and must not block. It may call `hal_dacless_get_adc()` for its own
handle, but no other DACless function. Never destroy the handle concurrently
with a callback. In polling mode, callbacks run synchronously inside
`hal_dacless_service()`. In either mode, a callback may call only
`hal_dacless_get_adc()` from the DACless API.

### Existing C++ compatibility API

The same header continues to expose `DAClessConfig`, `DAClessAudio`, and
`interpolate()` to C++ code. Existing applications may keep their current
class-based calls and compatibility globals (`audio_rate`, `out_buf_ptr`, and
`adc_results_buf`).

```cpp
#include <hal/audio/hal_dacless.h>

DAClessAudio audio;

bool start_audio_cpp(void) {
    return audio.begin();
}

void service_audio_cpp(void) {
    audio.service();
}
```

The C++ interface remains maintained for compatibility. New code that needs
explicit error reporting and independent callback contexts should prefer the
C interface above.

---

## `hal_adc` - Analog input

Read analog signals from supported ADC pins. Results are scaled to the selected resolution; available pins and conversion details depend on the platform.

```c
#include <hal/analog/hal_adc.h>

void hal_adc_set_resolution(uint8_t bits);
bool hal_adc_is_pin_supported(uint8_t pin);
int  hal_adc_read(uint8_t pin);
```

Use `hal_adc_is_pin_supported()` to validate a target-specific pin before a
conversion. It does not configure the pin or ADC peripheral.

Default resolution is 12 bits (consistent across RP platforms, STM32G474, ESP32-S3,
and mock backends).

On RP platforms and STM32G474, a DACless DMA stream with ADC inputs reserves
the shared converter until the stream is stopped or paused. During that time,
`hal_adc_read()` returns `0` without changing the DMA scan configuration, and
`hal_read_chip_temp_ex()` returns `HAL_EBUSY`. Resuming the audio stream
reacquires the converter.

- **impl/rp2040:** native pico-sdk `hardware/adc.h` (`adc_init`, `adc_gpio_init`,
  `adc_select_input`, `adc_read`). Valid ADC pins are GPIO 26-29 (channels 0-3);
  the 12-bit hardware sample is rescaled to the configured resolution.
- **impl/stm32g474:** ADC1 single-ended regular conversions with lazy regulator
  startup, calibration, and pin-to-channel validation. ADC12 is synchronously
  clocked from HCLK/4, or 42.5 MHz with the current 170 MHz clock tree.
- **impl/esp32:** ESP-IDF ADC oneshot conversion with lazy unit/channel setup and
  12 dB attenuation. Only ADC-capable pins that are exposed or soft-reserved by
  the generated board profile are accepted. The 12-bit hardware result is scaled
  to the configured 1..16-bit range; an invalid pin or conversion failure returns
  the compatibility value `0`.
- **impl/.mock:** injectable per-pin values via `hal_mock_adc_inject(pin, value)`.

**Thread safety:** Thread-safe and multicore-safe. An internal mutex protects
the shared ADC state on RP platforms, STM32G474, and ESP32-S3, so concurrent reads are
serialized automatically.

---


---

*Next: [Timers and system](06_timers_system.md)*
