# GPIO, ADC and PWM

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## `hal_gpio` - GPIO

```c
#include <hal/hal_gpio.h>

typedef enum {
    HAL_GPIO_INPUT        = 0,
    HAL_GPIO_OUTPUT       = 1,
    HAL_GPIO_INPUT_PULLUP = 2,
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
**Thread safety:** `hal_gpio_write` / `hal_gpio_read` are thin pass-throughs. Concurrent access to different pins from different cores is safe. Concurrent access to the same pin from two cores requires external synchronization.
**STM32G474 routing:** Pin id is `port * 16 + pin` (`PA0=0`, `PB0=16`, ...). EXTI is line-based (`line == pin_number`), so only one port source can own a given line at a time; attaching another pin with the same pin number remaps that EXTI line.
**IRQ priority:** `hal_gpio_set_irq_priority` sets GPIO interrupt priority. On RP2040 all GPIO pins share `IO_IRQ_BANK0`. On STM32G474 GPIO IRQs are split across `EXTI0..EXTI4`, `EXTI9_5`, and `EXTI15_10`; the same HAL priority is applied to all those NVIC entries.
**Interrupt detach:** `hal_gpio_detach_interrupt` removes the registered callback and masks the pin/EXTI source where the backend supports hardware interrupt masking.

---

## `hal_pwm` - PWM (simple, Arduino-level)

```c
#include <hal/hal_pwm.h>

void hal_pwm_set_resolution(uint8_t bits);
void hal_pwm_write(uint8_t pin, uint32_t value);
```

**impl/arduino:** `analogWriteResolution()`, `analogWrite()` (Arduino-pico).
**impl/stm32g474:** register-level TIM PWM output on mapped timer channels; default simple-PWM target frequency is 1 kHz best-effort from the current APB clock.
**Thread safety:** RP2040 maps pins to PWM hardware slices; STM32G474 maps pins to TIM channels. Channels sharing a timer also share frequency/resolution, and pins sharing the same TIM channel are not independent. Call `hal_pwm_set_resolution` during init, not concurrently with writes.

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

**impl/arduino:** pico SDK `hardware/pwm.h` + `hardware/clocks.h` - computes clkdiv and wrap
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

**impl/arduino:** `analogReadResolution()`, `analogRead()` (Arduino-pico).
**impl/.mock:** injectable per-pin values via `hal_mock_adc_inject(pin, value)`.
**Thread safety:** Thread-safe and multicore-safe. An internal mutex protects the RP2040 shared ADC multiplexer - concurrent `hal_adc_read()` calls from different cores are serialized automatically.

---


---

*Next: [Timers and system](06_timers_system.md)*
