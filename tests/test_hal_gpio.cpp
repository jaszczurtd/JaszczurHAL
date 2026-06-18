#include "hal/hal_gpio.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

void setUp(void) {}
void tearDown(void) {}

static int s_gpio_irq_hits;

static void gpio_irq_hit(void) { s_gpio_irq_hits++; }

void test_set_mode_output(void) {
  hal_gpio_set_mode(5, HAL_GPIO_OUTPUT);
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(5));
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(5));
}

void test_set_mode_input(void) {
  hal_gpio_set_mode(3, HAL_GPIO_INPUT);
  TEST_ASSERT_FALSE(hal_mock_gpio_is_output(3));
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_INPUT, hal_mock_gpio_get_mode(3));
}

void test_write_high(void) {
  hal_gpio_set_mode(10, HAL_GPIO_OUTPUT);
  hal_gpio_write(10, true);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10));
}

void test_write_low(void) {
  hal_gpio_set_mode(10, HAL_GPIO_OUTPUT);
  hal_gpio_write(10, true);
  hal_gpio_write(10, false);
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(10));
}

void test_read_injected_high(void) {
  hal_gpio_set_mode(7, HAL_GPIO_INPUT);
  hal_mock_gpio_inject_level(7, true);
  TEST_ASSERT_TRUE(hal_gpio_read(7));
}

void test_read_injected_low(void) {
  hal_gpio_set_mode(7, HAL_GPIO_INPUT);
  hal_mock_gpio_inject_level(7, false);
  TEST_ASSERT_FALSE(hal_gpio_read(7));
}

void test_default_state_is_low(void) { TEST_ASSERT_FALSE(hal_gpio_read(63)); }

/* ── write-before-mode antipattern (RP2040 latch-clobber semantics)
 * ───────────*/

/* set_mode(OUTPUT) mirrors gpio_init(): it resets the output latch to 0. So the
 * correct "drive high" is mode-then-write; mode-then-write leaves the pin HIGH.
 */
void test_drive_high_mode_then_write_drives_high(void) {
  hal_gpio_set_mode(12, HAL_GPIO_OUTPUT);
  hal_gpio_write(12, true);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(12));
}

/* The bug: write HIGH, then set OUTPUT. set_mode(OUTPUT) clobbers the latch,
 * so the pin ends up LOW - even though a HIGH was written. This is observable
 * purely from the driven level, which is how driver tests catch the
 * antipattern. */
void test_write_then_set_output_clobbers_high_to_low(void) {
  hal_gpio_set_mode(13, HAL_GPIO_OUTPUT);
  hal_gpio_write(13, true);
  hal_gpio_set_mode(13,
                    HAL_GPIO_OUTPUT); /* re-entering OUTPUT clobbers latch */
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(13));
}

void test_detach_interrupt_stops_mock_callback(void) {
  s_gpio_irq_hits = 0;
  hal_gpio_attach_interrupt(4, gpio_irq_hit, HAL_GPIO_IRQ_RISING);
  hal_mock_gpio_fire_interrupt(4);
  hal_gpio_detach_interrupt(4);
  hal_mock_gpio_fire_interrupt(4);
  TEST_ASSERT_EQUAL_INT(1, s_gpio_irq_hits);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_set_mode_output);
  RUN_TEST(test_set_mode_input);
  RUN_TEST(test_write_high);
  RUN_TEST(test_write_low);
  RUN_TEST(test_read_injected_high);
  RUN_TEST(test_read_injected_low);
  RUN_TEST(test_default_state_is_low);
  RUN_TEST(test_drive_high_mode_then_write_drives_high);
  RUN_TEST(test_write_then_set_output_clobbers_high_to_low);
  RUN_TEST(test_detach_interrupt_stops_mock_callback);
  return UNITY_END();
}
