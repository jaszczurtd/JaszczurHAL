#include "hal/gpio/hal_gpio.h"
#include "utils/unity.h"

namespace {
void gpio_irq_hit(void) {}
} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_stm32_gpio_irq_owner_is_core0(void) {
  uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_attach_interrupt_ex(
                                    5u, gpio_irq_hit, HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_get_interrupt_owner_ex(5u, &owner));
  TEST_ASSERT_EQUAL_UINT8(0u, owner);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(5u));

  owner = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_gpio_get_interrupt_owner_ex(5u, &owner));
  TEST_ASSERT_EQUAL_UINT8(HAL_GPIO_IRQ_CORE_NONE, owner);
}

void test_stm32_gpio_irq_owner_rejects_nonzero_caller_core(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE,
      hal_gpio_attach_interrupt_ex(6u, gpio_irq_hit, HAL_GPIO_IRQ_FALLING, 1u));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_gpio_attach_interrupt_ex(6u, gpio_irq_hit, HAL_GPIO_IRQ_FALLING, 2u));
}

void test_stm32_exti_reroute_moves_owner_to_new_pin(void) {
  constexpr uint8_t pa5 = 5u;
  constexpr uint8_t pb5 = 21u;
  uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;

  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_gpio_attach_interrupt_ex(pa5, gpio_irq_hit, HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_gpio_attach_interrupt_ex(pb5, gpio_irq_hit, HAL_GPIO_IRQ_CHANGE, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_gpio_get_interrupt_owner_ex(pa5, &owner));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_gpio_detach_interrupt_ex(pa5));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_get_interrupt_owner_ex(pb5, &owner));
  TEST_ASSERT_EQUAL_UINT8(0u, owner);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(pb5));
}

void test_stm32_gpio_irq_owner_validates_arguments(void) {
  uint8_t owner = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_gpio_attach_interrupt_ex(128u, gpio_irq_hit,
                                                     HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_gpio_attach_interrupt_ex(
                                        7u, nullptr, HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_gpio_attach_interrupt_ex(7u, gpio_irq_hit,
                                               (hal_gpio_irq_mode_t)99, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_gpio_get_interrupt_owner_ex(7u, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_gpio_get_interrupt_owner_ex(7u, &owner));
  TEST_ASSERT_EQUAL_UINT8(HAL_GPIO_IRQ_CORE_NONE, owner);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stm32_gpio_irq_owner_is_core0);
  RUN_TEST(test_stm32_gpio_irq_owner_rejects_nonzero_caller_core);
  RUN_TEST(test_stm32_exti_reroute_moves_owner_to_new_pin);
  RUN_TEST(test_stm32_gpio_irq_owner_validates_arguments);
  return UNITY_END();
}
