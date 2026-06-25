# GPIO, ADC and PWM

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## `hal_gpio` - GPIO

```c
#include <hal/hal_gpio.h>

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

typedef enum {
    HAL_IRQ_PRIORITY_HIGHEST = 0,
    HAL_IRQ_PRIORITY_HIGH    = 1,
    HAL_IRQ_PRIORITY_DEFAULT = 2,
    HAL_IRQ_PRIORITY_LOW     = 3,
} hal_irq_priority_t;

void hal_gpio_set_irq_priority(hal_irq_priority_t priority);
```

**Note:** The callback passed to `hal_gpio_attach_interrupt` runs in ISR context - avoid `printf`, `malloc`, `Serial`, or any blocking call inside it.
**Validation:** Invalid GPIO modes, invalid IRQ modes, invalid pins and NULL interrupt callbacks trigger `HAL_ASSERT` in checked builds and return without configuring hardware.
**Output initial state:** `HAL_GPIO_OUTPUT_LOW/HIGH` and `HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW/HIGH` make the intended initial latch state explicit. `HAL_GPIO_OUTPUT` remains compatible and means push-pull output with initial low.
**Open drain:** On STM32G474 this maps to hardware open-drain. On RP2040 (native pico-sdk) it is emulated by driving LOW for `false` and releasing the pin as input (high-Z) for `true`.
**Thread safety:** `hal_gpio_write` / `hal_gpio_read` are thin pass-throughs. Concurrent access to different pins from different cores is safe. Concurrent access to the same pin from two cores requires external synchronization.
**STM32G474 routing:** Pin id is `port * 16 + pin` (`PA0=0`, `PB0=16`, ...). EXTI is line-based (`line == pin_number`), so only one port source can own a given line at a time; attaching another pin with the same pin number remaps that EXTI line.
**IRQ priority:** `hal_gpio_set_irq_priority` sets GPIO interrupt priority. On RP2040 all GPIO pins share `IO_IRQ_BANK0`. On STM32G474 GPIO IRQs are split across `EXTI0..EXTI4`, `EXTI9_5`, and `EXTI15_10`; the same HAL priority is applied to all those NVIC entries.
**Interrupt detach:** `hal_gpio_detach_interrupt` removes the registered callback and masks the pin/EXTI source where the backend supports hardware interrupt masking.

---

## `hal_pwm` - PWM

```c
#include <hal/hal_pwm.h>

void hal_pwm_set_resolution(uint8_t bits);
bool hal_pwm_is_pin_supported(uint8_t pin);
void hal_pwm_write(uint8_t pin, uint32_t value);
```

`hal_pwm` is the portable, simple PWM API. It has a deliberately small
contract: resolution is 1..16 bits, `hal_pwm_write()` clamps values above the
current maximum, and unsupported pins trigger `HAL_ASSERT` in checked builds and
are ignored. Use `hal_pwm_is_pin_supported()` before dynamic pin selection.

It does not guarantee a caller-selected frequency or independent channel
allocation. Use `hal_pwm_freq` when frequency, period/wrap value and channel
lifetime matter. Default resolution is 8 bits.

**impl/rp2040:** native pico-sdk `hardware/pwm.h` (`pwm_init`, `pwm_config_set_wrap`,
`pwm_set_gpio_level`, `pwm_set_enabled`). The duty resolution sets the slice wrap
(`2^bits - 1`); the target frequency is a fixed ~1 kHz best-effort derived from
`clk_sys`, but because `clkdiv` saturates at 255 the effective frequency rises at
low resolutions (e.g. ~1.9 kHz at 8-bit, ~1.0 kHz at 16-bit). Two GPIOs on the
same hardware slice (`gpio/2 mod 8`) share one frequency/wrap but keep independent
duty. Use `hal_pwm_freq` when exact frequency matters.
**impl/stm32g474:** register-level TIM PWM output on mapped timer channels;
default simple-PWM target frequency is 1 kHz best-effort from the current APB
clock.
**Thread safety:** RP2040 maps pins to PWM hardware slices; STM32G474 maps pins
to TIM channels. Channels sharing a timer also share frequency/resolution, and
pins sharing the same TIM channel are not independent. Call
`hal_pwm_set_resolution` during init, not concurrently with writes.

---

## `hal_pwm_freq` - PWM with frequency control  *(optional - `HAL_ENABLE_PWM_FREQ`)*

Use this instead of `hal_pwm` when you need a specific PWM frequency (e.g. 160 Hz, 300 Hz).

```c
#include <hal/hal_pwm_freq.h>

// Opaque handle
typedef hal_pwm_freq_channel_impl_t *hal_pwm_freq_channel_t;

// Create a channel: pin, frequency in Hz, resolution (wrap value, e.g. 2047 for 11-bit = 2^11-1)
hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin,
                                           uint32_t frequency_hz,
                                           uint32_t resolution);

// Write value in [0, resolution] - values outside range are clamped automatically
void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value);

// Free resources
void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch);
```

**impl/rp2040:** pico SDK `hardware/pwm.h` + `hardware/clocks.h` - computes clkdiv and wrap
to achieve the exact requested frequency, with pseudo/slow-scale correction for edge cases.
**impl/stm32g474:** register-level TIM PWM on mapped TIM2/TIM3/TIM4/TIM15/TIM16/TIM17 channels. Frequency is a timer-level resource, so multiple channels on the same TIM share the same frequency and effective period.
The PWM slice is configured at `hal_pwm_freq_create()` time but **not started** - the GPIO
function / TIM channel enable are deferred until the first `hal_pwm_freq_write()` call. This
prevents a glitch on pins with inverted logic (0 % duty = actuator ON) at power-on.
**impl/.mock:** stores last written value; injectable via mock helpers.

**Mock helpers:**
```c
int      hal_mock_pwm_freq_get_value(hal_pwm_freq_channel_t ch);
uint32_t hal_mock_pwm_freq_get_frequency(hal_pwm_freq_channel_t ch);
uint8_t  hal_mock_pwm_freq_get_pin(hal_pwm_freq_channel_t ch);
```

**Thread safety:** RP2040 and STM32G474 backends protect `hal_pwm_freq_write()` with an internal mutex. `hal_pwm_freq_create()` and `hal_pwm_freq_destroy()` are not synchronized and should be called from one task/core during setup/teardown. Mock backend does not provide concurrent-access synchronization.

---

## `hal_adc` - Analog input

```c
#include <hal/hal_adc.h>

void hal_adc_set_resolution(uint8_t bits);
int  hal_adc_read(uint8_t pin);
```

Default resolution is 12 bits (consistent with the STM32G474 and mock backends).

**impl/rp2040:** native pico-sdk `hardware/adc.h` (`adc_init`, `adc_gpio_init`,
`adc_select_input`, `adc_read`). Valid ADC pins are GPIO 26-29 (channels 0-3);
the 12-bit hardware sample is rescaled to the configured resolution.
**impl/.mock:** injectable per-pin values via `hal_mock_adc_inject(pin, value)`.
**Thread safety:** Thread-safe and multicore-safe. An internal mutex protects the RP2040 shared ADC multiplexer - concurrent `hal_adc_read()` calls from different cores are serialized automatically.

---


---

*Next: [Timers and system](06_timers_system.md)*
