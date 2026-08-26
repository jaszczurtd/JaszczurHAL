# GPIO, ADC and PWM

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## `hal_gpio` - GPIO

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

`hal_pwm` is the portable, simple PWM API. It has a deliberately small
behavior: resolution is 1..16 bits, `hal_pwm_write()` clamps values above the
current maximum, and unsupported pins trigger `HAL_ASSERT` in checked builds and
are ignored. Use `hal_pwm_is_pin_supported()` before dynamic pin selection.

It does not guarantee a caller-selected frequency or independent channel
allocation. Use `hal_pwm_freq` when frequency, period/wrap value and channel
lifetime matter. Default resolution is 8 bits.

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

## `hal_dac` - True DAC output  *(optional - `HAL_ENABLE_DAC`)*

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

Use this instead of `hal_pwm` when you need a specific PWM frequency (e.g. 160 Hz, 300 Hz).

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

## `DAClessAudio` - PWM audio engine  *(optional - `HAL_ENABLE_DACLESS`)*

```cpp
#include <hal/audio/hal_dacless.h>

struct DAClessConfig {
    uint8_t  pinPWM;      // default 6
    uint16_t pwmBits;     // default 12
    uint16_t blockSize;   // default 128, capped by DACLESS_MAX_BLOCK_SIZE
    uint8_t  nAdcInputs;  // capped by DACLESS_MAX_ADC_INPUTS
    bool     useDma;      // default true; set false for cooperative service()
    uint8_t  adcPins[DACLESS_MAX_ADC_INPUTS];
};

class DAClessAudio {
public:
    using SampleCallback = uint16_t (*)(void *);
    using BlockCallback  = void (*)(void *, uint16_t *);

    explicit DAClessAudio(const DAClessConfig &cfg = DAClessConfig());
    bool begin();
    void service();
    void mute();
    void unmute();
    void setSampleCallback(SampleCallback cb, void *userdata = nullptr);
    void setBlockCallback(BlockCallback cb, void *userdata = nullptr);
    uint16_t getADC(uint8_t channel) const;
    float getSampleRate() const;
    const volatile uint16_t *getOutBufPtr() const;
    const volatile uint16_t *getAdcBuffer() const;
    bool isDmaActive() const;
};

uint16_t interpolate(uint16_t x, uint16_t y, uint16_t mu_scaled);
```

`HAL_ENABLE_DACLESS` propagates `HAL_ENABLE_DMA_PWM_AUDIO` and `HAL_ENABLE_PWM_FREQ`.
The shared driver is modeled after Brian Varren's DACless engine and preserves
the configuration, double-buffered block flow, sample/block callbacks, ADC
result buffer, compatibility globals (`audio_rate`, `out_buf_ptr`,
`adc_results_buf`) and RP2040 8-bit blend-fraction interpolation helper
semantics.

The default path (`cfg.useDma = true`) uses `hal_dma_pwm_audio` to feed PWM
from double-buffered DMA and to keep the ADC result buffer refreshed. RP2040
mirrors the original chained channel-A/channel-B PWM DMA plus ADC
sample/control DMA flow. STM32G474 uses TIM update DMA into the active CCR
register, circular half-transfer/transfer-complete callbacks for the two audio
halves, and an ADC1 circular DMA scan for the configured ADC pins.

`begin()` returns `true` when the PWM/DMA backend was created and started.
When it returns `false`, the instance remains stopped and muted; this can happen
when the target backend has exhausted a fixed hardware resource.

Set `cfg.useDma = false` to use the cooperative path: call `service()`
frequently from `app_task0()` or a FreeRTOS task. It writes due samples through
`hal_pwm_freq_write()`, refills the finished buffer through the block callback
when present, otherwise through the sample callback, otherwise with midpoint
silence. If `service()` is called late, polling playback catches up by at most
`DACLESS_MAX_POLLING_CATCHUP_SAMPLES` samples before resynchronising to current
time. Callbacks are invoked outside the instance mutex, so they may read
`getADC()` without deadlocking.

Default ADC pins are GPIO 26..29 on RP2040/mock and PA0..PA3 on STM32G474
(`port * 16 + pin`). Override `cfg.adcPins[]` for custom wiring.

**Thread safety:** Public methods serialize instance state with a per-instance
HAL mutex created via `jh_hal_mutex_create_once`. DMA buffer callbacks run from
the backend DMA interrupt and do not take the instance mutex. Callback
registration, `begin()`, `service()`, `mute()`, `unmute()` and `getADC()` are
safe to call from normal task/core context. Do not call `service()` from an ISR.

---

## `hal_adc` - Analog input

```c
#include <hal/analog/hal_adc.h>

void hal_adc_set_resolution(uint8_t bits);
int  hal_adc_read(uint8_t pin);
```

Default resolution is 12 bits (consistent across RP2040, STM32G474, ESP32-S3,
and mock backends).

**impl/rp2040:** native pico-sdk `hardware/adc.h` (`adc_init`, `adc_gpio_init`,
`adc_select_input`, `adc_read`). Valid ADC pins are GPIO 26-29 (channels 0-3);
the 12-bit hardware sample is rescaled to the configured resolution.
**impl/stm32g474:** ADC1 single-ended regular conversions with lazy regulator
startup, calibration, and pin-to-channel validation. ADC12 is synchronously
clocked from HCLK/4, or 42.5 MHz with the current 170 MHz clock tree.
**impl/esp32:** ESP-IDF ADC oneshot conversion with lazy unit/channel setup and
12 dB attenuation. Only ADC-capable pins that are exposed or soft-reserved by
the generated board profile are accepted. The 12-bit hardware result is scaled
to the configured 1..16-bit range; an invalid pin or conversion failure returns
the compatibility value `0`.
**impl/.mock:** injectable per-pin values via `hal_mock_adc_inject(pin, value)`.
**Thread safety:** Thread-safe and multicore-safe. An internal mutex protects
the shared ADC state on RP2040, STM32G474, and ESP32-S3, so concurrent reads are
serialized automatically.

---


---

*Next: [Timers and system](06_timers_system.md)*
