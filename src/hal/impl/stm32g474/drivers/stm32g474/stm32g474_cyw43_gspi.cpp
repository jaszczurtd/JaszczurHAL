#include "hal/core/hal_compiler.h"
#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474

#include "stm32g474_cyw43_gspi.h"

#include "../../port/stm32g474_regs.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/system/hal_system.h"

#include <string.h>

namespace {

constexpr uint32_t kGpioPortCount = 7u;
constexpr uint32_t kHostWakeSelfTestSpins = 100000u;

struct stm32g474_gspi_context_t {
  jh_stm32g474_cyw43_gspi_config_t config;
  uint32_t chip_select_port;
  uint32_t chip_select_mask;
  uint32_t clock_port;
  uint32_t clock_mask;
  uint32_t clock_mode_shift;
  uint32_t data_port;
  uint32_t data_mask;
  uint32_t data_mode_shift;
  jh_cyw43_gspi_host_wake_callback_t host_wake_callback;
  void *host_wake_callback_context;
};

stm32g474_gspi_context_t s_context{};
jh_cyw43_gspi_transport_t s_transport{};

uint32_t pin_port(uint8_t pin) { return (uint32_t)(pin >> 4u); }
uint32_t pin_number(uint8_t pin) { return (uint32_t)(pin & 0x0Fu); }
uint32_t pin_mask(uint8_t pin) { return 1u << pin_number(pin); }

bool pin_valid(uint8_t pin) { return pin_port(pin) < kGpioPortCount; }

bool config_valid(const jh_stm32g474_cyw43_gspi_config_t *config) {
  if (config == nullptr || !pin_valid(config->pin_chip_select) ||
      !pin_valid(config->pin_clock) || !pin_valid(config->pin_wl_on) ||
      !pin_valid(config->pin_data) || config->max_transaction_bytes < 8u ||
      (config->max_transaction_bytes & 3u) != 0u) {
    return false;
  }
  return config->pin_chip_select != config->pin_clock &&
         config->pin_chip_select != config->pin_wl_on &&
         config->pin_chip_select != config->pin_data &&
         config->pin_clock != config->pin_wl_on &&
         config->pin_clock != config->pin_data &&
         config->pin_wl_on != config->pin_data;
}

#ifdef JH_STM32G474_HW

HAL_FORCE_INLINE void fast_write_pin(uint32_t port, uint32_t mask, bool high) {
  GPIO_BSRR(port) = high ? mask : (mask << 16u);
}

HAL_FORCE_INLINE void gspi_half_period(void) {
  constexpr uint32_t kHalfPeriodCycles =
      (JH_G474_CORE_CLOCK_HZ + 3999999u) / 4000000u;
  const uint32_t start = DWT_CYCCNT;
  while ((uint32_t)(DWT_CYCCNT - start) < kHalfPeriodCycles) {
    __asm volatile("nop" ::: "memory");
  }
}

HAL_FORCE_INLINE void set_data_mode(stm32g474_gspi_context_t *context,
                                    uint32_t mode) {
  GPIO_MODER(context->data_port) =
      (GPIO_MODER(context->data_port) & ~(0x3u << context->data_mode_shift)) |
      (mode << context->data_mode_shift);
}

void prepare_bus_pins(stm32g474_gspi_context_t *context) {
  RCC_AHB2ENR |= (1u << context->clock_port) | (1u << context->data_port);
  GPIO_OTYPER(context->clock_port) &= ~context->clock_mask;
  GPIO_OTYPER(context->data_port) &= ~context->data_mask;
  GPIO_OSPEEDR(context->clock_port) |= 0x3u << context->clock_mode_shift;
  GPIO_OSPEEDR(context->data_port) |= 0x3u << context->data_mode_shift;
  GPIO_PUPDR(context->clock_port) =
      (GPIO_PUPDR(context->clock_port) & ~(0x3u << context->clock_mode_shift)) |
      (GPIO_PUPD_DOWN << context->clock_mode_shift);
  GPIO_PUPDR(context->data_port) =
      (GPIO_PUPDR(context->data_port) & ~(0x3u << context->data_mode_shift)) |
      (GPIO_PUPD_DOWN << context->data_mode_shift);
  GPIO_MODER(context->clock_port) =
      (GPIO_MODER(context->clock_port) & ~(0x3u << context->clock_mode_shift)) |
      (GPIO_MODE_OUTPUT << context->clock_mode_shift);
  set_data_mode(context, GPIO_MODE_OUTPUT);
}

#endif

hal_status_t platform_initialize(void *opaque_context) {
  auto *context = static_cast<stm32g474_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  hal_gpio_set_mode(context->config.pin_wl_on, HAL_GPIO_OUTPUT_LOW);
  hal_gpio_set_mode(context->config.pin_chip_select, HAL_GPIO_OUTPUT_HIGH);
  hal_gpio_set_mode(context->config.pin_clock, HAL_GPIO_OUTPUT_LOW);
  hal_gpio_set_mode(context->config.pin_data, HAL_GPIO_OUTPUT_LOW);
  return HAL_OK;
}

hal_status_t platform_deinitialize(void *opaque_context) {
  auto *context = static_cast<stm32g474_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  context->host_wake_callback = nullptr;
  context->host_wake_callback_context = nullptr;
  return HAL_OK;
}

hal_status_t platform_set_power(void *opaque_context, bool enabled) {
  auto *context = static_cast<stm32g474_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  if (!enabled) {
    hal_gpio_write(context->config.pin_chip_select, true);
    hal_gpio_write(context->config.pin_clock, false);
    hal_gpio_set_mode(context->config.pin_data, HAL_GPIO_OUTPUT_LOW);
  }
  hal_gpio_write(context->config.pin_wl_on, enabled);
  return HAL_OK;
}

hal_status_t platform_release_data(void *opaque_context) {
  auto *context = static_cast<stm32g474_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  hal_gpio_set_mode(context->config.pin_data, HAL_GPIO_INPUT_PULLDOWN);
  return HAL_OK;
}

__attribute__((optimize("O3"))) hal_status_t
platform_transfer(void *opaque_context, const uint8_t *tx, size_t tx_length,
                  uint8_t *rx, size_t rx_length) {
  auto *context = static_cast<stm32g474_gspi_context_t *>(opaque_context);
  if (context == nullptr || tx == nullptr || tx_length == 0u ||
      (tx_length & 3u) != 0u || (rx_length & 3u) != 0u ||
      (rx_length != 0u && rx == nullptr) ||
      tx_length > context->config.max_transaction_bytes ||
      rx_length > context->config.max_transaction_bytes - tx_length) {
    return HAL_EINVAL;
  }

#ifndef JH_STM32G474_HW
  (void)rx;
  return HAL_EUNSUPPORTED;
#else
  prepare_bus_pins(context);
  fast_write_pin(context->clock_port, context->clock_mask, false);
  fast_write_pin(context->chip_select_port, context->chip_select_mask, false);

  for (size_t index = 0u; index < tx_length; ++index) {
    for (uint32_t mask = 0x80u; mask != 0u; mask >>= 1u) {
      const bool last_bit = index == tx_length - 1u && mask == 1u;
      fast_write_pin(context->data_port, context->data_mask,
                     (tx[index] & mask) != 0u);
      gspi_half_period();
      fast_write_pin(context->clock_port, context->clock_mask, true);
      gspi_half_period();
      if (!last_bit) {
        fast_write_pin(context->clock_port, context->clock_mask, false);
      }
    }
  }

  /* DAT is released while CLK is high, before its next falling edge. */
  set_data_mode(context, GPIO_MODE_INPUT);
  if (rx_length != 0u) {
    fast_write_pin(context->clock_port, context->clock_mask, false);
    gspi_half_period();
    for (size_t index = 0u; index < rx_length; ++index) {
      uint8_t value = 0u;
      for (uint32_t bit = 0u; bit < 8u; ++bit) {
        value = (uint8_t)((value << 1u) | ((GPIO_IDR(context->data_port) &
                                            context->data_mask) != 0u));
        fast_write_pin(context->clock_port, context->clock_mask, true);
        gspi_half_period();
        fast_write_pin(context->clock_port, context->clock_mask, false);
        gspi_half_period();
      }
      rx[index] = value;
    }
    /* Match the verified Pico oracle edge count without shifting response. */
    fast_write_pin(context->clock_port, context->clock_mask, true);
    gspi_half_period();
  }

  fast_write_pin(context->chip_select_port, context->chip_select_mask, true);
  fast_write_pin(context->clock_port, context->clock_mask, false);
  return HAL_OK;
#endif
}

void host_wake_isr(void) {
  stm32g474_gspi_context_t *context = &s_context;
#ifdef JH_STM32G474_HW
  EXTI_IMR1 &= ~context->data_mask;
#endif
  if (context->host_wake_callback != nullptr) {
    context->host_wake_callback(context->host_wake_callback_context);
  }
}

hal_status_t
platform_host_wake_attach(void *opaque_context,
                          jh_cyw43_gspi_host_wake_callback_t callback,
                          void *callback_context) {
  auto *context = static_cast<stm32g474_gspi_context_t *>(opaque_context);
  if (context == nullptr || callback == nullptr ||
      context->host_wake_callback != nullptr) {
    return context != nullptr && callback != nullptr ? HAL_EBUSY : HAL_EINVAL;
  }
  context->host_wake_callback = callback;
  context->host_wake_callback_context = callback_context;
  const hal_status_t status = hal_gpio_attach_interrupt_ex(
      context->config.pin_data, host_wake_isr, HAL_GPIO_IRQ_RISING, 0u);
  if (status != HAL_OK) {
    context->host_wake_callback = nullptr;
    context->host_wake_callback_context = nullptr;
    return status;
  }
  hal_gpio_set_irq_priority(HAL_IRQ_PRIORITY_HIGH);
  return HAL_OK;
}

hal_status_t platform_host_wake_detach(void *opaque_context) {
  auto *context = static_cast<stm32g474_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t status =
      hal_gpio_detach_interrupt_ex(context->config.pin_data);
  if (status == HAL_OK || status == HAL_ENOENT) {
    context->host_wake_callback = nullptr;
    context->host_wake_callback_context = nullptr;
    return HAL_OK;
  }
  return status;
}

void platform_host_wake_mask(void *opaque_context) {
#ifdef JH_STM32G474_HW
  auto *context = static_cast<stm32g474_gspi_context_t *>(opaque_context);
  if (context != nullptr) {
    EXTI_IMR1 &= ~context->data_mask;
  }
#else
  (void)opaque_context;
#endif
}

hal_status_t platform_host_wake_rearm(void *opaque_context, bool *asserted) {
  auto *context = static_cast<stm32g474_gspi_context_t *>(opaque_context);
  if (context == nullptr || asserted == nullptr) {
    return HAL_EINVAL;
  }
#ifndef JH_STM32G474_HW
  *asserted = false;
  return HAL_EUNSUPPORTED;
#else
  EXTI_PR1 = context->data_mask;
  EXTI_IMR1 |= context->data_mask;
  *asserted = (GPIO_IDR(context->data_port) & context->data_mask) != 0u;
  return HAL_OK;
#endif
}

void platform_delay_ms(void *, uint32_t delay_ms) { hal_delay_ms(delay_ms); }

const jh_cyw43_gspi_platform_ops_t kPlatformOps = {
    platform_initialize,       platform_deinitialize,
    platform_set_power,        platform_release_data,
    platform_transfer,         platform_host_wake_attach,
    platform_host_wake_detach, platform_host_wake_mask,
    platform_host_wake_rearm,  platform_delay_ms,
};

} // namespace

extern "C" hal_status_t
jh_stm32g474_cyw43_gspi_init(const jh_stm32g474_cyw43_gspi_config_t *config) {
  if (!config_valid(config)) {
    return config == nullptr ? HAL_EINVAL : HAL_ECONFIG;
  }
  if (s_transport.initialized) {
    return HAL_EEXIST;
  }
  memset(&s_context, 0, sizeof(s_context));
  s_context.config = *config;
  s_context.chip_select_port = pin_port(config->pin_chip_select);
  s_context.chip_select_mask = pin_mask(config->pin_chip_select);
  s_context.clock_port = pin_port(config->pin_clock);
  s_context.clock_mask = pin_mask(config->pin_clock);
  s_context.clock_mode_shift = pin_number(config->pin_clock) * 2u;
  s_context.data_port = pin_port(config->pin_data);
  s_context.data_mask = pin_mask(config->pin_data);
  s_context.data_mode_shift = pin_number(config->pin_data) * 2u;
  return jh_cyw43_gspi_transport_init(&s_transport, &kPlatformOps, &s_context,
                                      s_context.config.max_transaction_bytes);
}

extern "C" hal_status_t jh_stm32g474_cyw43_gspi_deinit(void) {
  return jh_cyw43_gspi_transport_deinit(&s_transport);
}

extern "C" jh_cyw43_gspi_transport_t *jh_stm32g474_cyw43_gspi_transport(void) {
  return s_transport.initialized ? &s_transport : nullptr;
}

extern "C" hal_status_t jh_stm32g474_cyw43_gspi_host_wake_self_test(void) {
#ifndef JH_STM32G474_HW
  return HAL_EUNSUPPORTED;
#else
  if (!s_transport.initialized || !s_transport.host_wake_attached) {
    return HAL_EUNINIT;
  }
  hal_status_t status = jh_cyw43_gspi_host_wake_suspend(&s_transport);
  if (status != HAL_OK) {
    return status;
  }
  status = jh_cyw43_gspi_power_off(&s_transport);
  if (status != HAL_OK) {
    (void)jh_cyw43_gspi_host_wake_resume(&s_transport);
    return status;
  }
  hal_delay_ms(20u);
  hal_gpio_set_mode(s_context.config.pin_data, HAL_GPIO_OUTPUT_LOW);
  s_transport.host_wake_pending = false;
  const uint32_t irq_before = s_transport.stats.host_wake_irqs;
  status = jh_cyw43_gspi_host_wake_resume(&s_transport);
  if (status == HAL_OK) {
    hal_gpio_write(s_context.config.pin_data, true);
    for (uint32_t spins = 0u;
         spins < kHostWakeSelfTestSpins && !s_transport.host_wake_pending;
         ++spins) {
      __asm volatile("nop" ::: "memory");
    }
  }
  const bool passed = status == HAL_OK && s_transport.host_wake_pending &&
                      s_transport.stats.host_wake_irqs == irq_before + 1u;
  (void)jh_cyw43_gspi_host_wake_suspend(&s_transport);
  hal_gpio_write(s_context.config.pin_data, false);
  hal_gpio_set_mode(s_context.config.pin_data, HAL_GPIO_INPUT_PULLDOWN);
  (void)jh_cyw43_gspi_host_wake_resume(&s_transport);
  return passed ? HAL_OK : HAL_EHW;
#endif
}

#endif
