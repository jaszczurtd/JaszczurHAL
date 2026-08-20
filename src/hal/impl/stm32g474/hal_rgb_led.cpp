#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_RGB_LED

#include "hal/gpio/hal_gpio.h"
#include "hal/gpio/hal_rgb_led_internal.h"
#include "hal/system/hal_sync.h"

#ifdef JH_STM32G474_HW
#include "port/stm32g474_regs.h"

#define STM32_DEMCR (*(volatile uint32_t *)0xE000EDFCu)
#define STM32_DWT_CTRL (*(volatile uint32_t *)0xE0001000u)
#define STM32_DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define STM32_DEMCR_TRCENA (1u << 24)
#define STM32_DWT_CYCCNTENA (1u << 0)

namespace {

void cycles_enable(void) {
  STM32_DEMCR |= STM32_DEMCR_TRCENA;
  STM32_DWT_CTRL |= STM32_DWT_CYCCNTENA;
}

void wait_until(uint32_t target) {
  while ((int32_t)(STM32_DWT_CYCCNT - target) < 0) {
  }
}

} // namespace
#endif

bool jh_hal_rgb_led_pin_valid(uint8_t pin) { return (pin >> 4u) <= 6u; }

void jh_hal_rgb_led_release_transport(void) {}

hal_status_t jh_hal_rgb_led_prepare_transport(uint8_t pin, bool is800khz) {
  if (!is800khz) {
    return HAL_EUNSUPPORTED;
  }
  hal_gpio_set_mode(pin, HAL_GPIO_OUTPUT);
  hal_gpio_write(pin, false);
  return HAL_OK;
}

bool jh_hal_rgb_led_write_pixels(const uint8_t *pixels, uint32_t num_bytes,
                                 bool is800khz, uint8_t pin, void *user) {
  (void)user;
  if (pixels == nullptr || num_bytes == 0u) {
    return true;
  }
#ifdef JH_STM32G474_HW
  if (!is800khz) {
    return false;
  }
  const uint32_t port = pin >> 4u;
  const uint32_t pin_number = pin & 0x0Fu;
  if (port > 6u) {
    return false;
  }
  volatile uint32_t *const bsrr = &GPIO_BSRR(port);
  const uint32_t set_mask = 1u << pin_number;
  const uint32_t clear_mask = 1u << (pin_number + 16u);
  cycles_enable();
  const uint32_t bit_cycles = JH_G474_CORE_CLOCK_HZ / 800000u;
  const uint32_t zero_high_cycles =
      (uint32_t)((((uint64_t)JH_G474_CORE_CLOCK_HZ * 35u) + 50000000u) /
                 100000000u);
  const uint32_t one_high_cycles =
      (uint32_t)((((uint64_t)JH_G474_CORE_CLOCK_HZ * 80u) + 50000000u) /
                 100000000u);
  if (zero_high_cycles == 0u ||
      !(zero_high_cycles < one_high_cycles && one_high_cycles < bit_cycles)) {
    return false;
  }

  hal_critical_section_enter();
  uint32_t start = STM32_DWT_CYCCNT;
  for (uint32_t i = 0u; i < num_bytes; ++i) {
    const uint8_t value = pixels[i];
    for (uint8_t mask = 0x80u; mask != 0u; mask >>= 1u) {
      *bsrr = set_mask;
      wait_until(start +
                 ((value & mask) != 0u ? one_high_cycles : zero_high_cycles));
      *bsrr = clear_mask;
      start += bit_cycles;
      wait_until(start);
    }
  }
  hal_critical_section_exit();
#else
  (void)is800khz;
  (void)pin;
#endif
  return true;
}

#endif
#endif
