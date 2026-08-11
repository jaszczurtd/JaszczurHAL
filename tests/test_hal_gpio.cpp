#include "hal/gpio/hal_gpio.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

void setUp(void) { hal_mock_gpio_set_current_core(0u); }
void tearDown(void) {}

static int s_gpio_irq_hits;
static uint8_t s_gpio_irq_core_seen;

static void gpio_irq_hit(void) {
  s_gpio_irq_hits++;
  s_gpio_irq_core_seen = hal_mock_gpio_get_current_core();
}

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

void test_set_mode_input_pulldown(void) {
  hal_gpio_set_mode(6, HAL_GPIO_INPUT_PULLDOWN);
  TEST_ASSERT_FALSE(hal_mock_gpio_is_output(6));
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_INPUT_PULLDOWN, hal_mock_gpio_get_mode(6));
}

void test_set_mode_output_low_initializes_low(void) {
  hal_gpio_write(14, true);
  hal_gpio_set_mode(14, HAL_GPIO_OUTPUT_LOW);

  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(14));
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT_LOW, hal_mock_gpio_get_mode(14));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(14));
}

void test_set_mode_output_high_initializes_high(void) {
  hal_gpio_set_mode(15, HAL_GPIO_OUTPUT_HIGH);

  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(15));
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT_HIGH, hal_mock_gpio_get_mode(15));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(15));
}

void test_set_mode_open_drain_initial_states(void) {
  hal_gpio_set_mode(16, HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW);
  hal_gpio_set_mode(17, HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH);

  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(16));
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(17));
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW,
                        hal_mock_gpio_get_mode(16));
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH,
                        hal_mock_gpio_get_mode(17));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(16));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(17));
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

void test_interrupt_owner_status_and_same_core_reconfiguration(void) {
  uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_gpio_attach_interrupt_ex(18u, gpio_irq_hit, HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_get_interrupt_owner_ex(18u, &owner));
  TEST_ASSERT_EQUAL_UINT8(0u, owner);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gpio_attach_interrupt_ex(18u, gpio_irq_hit,
                                                     HAL_GPIO_IRQ_FALLING, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(18u));

  owner = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_gpio_get_interrupt_owner_ex(18u, &owner));
  TEST_ASSERT_EQUAL_UINT8(HAL_GPIO_IRQ_CORE_NONE, owner);
}

void test_interrupt_owner_rejects_wrong_core_reconfigure_and_detach(void) {
  s_gpio_irq_hits = 0;
  s_gpio_irq_core_seen = HAL_GPIO_IRQ_CORE_NONE;
  hal_mock_gpio_set_current_core(1u);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_gpio_attach_interrupt_ex(19u, gpio_irq_hit, HAL_GPIO_IRQ_CHANGE, 1u));

  hal_mock_gpio_set_current_core(0u);
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE,
      hal_gpio_attach_interrupt_ex(19u, gpio_irq_hit, HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_gpio_detach_interrupt_ex(19u));

  uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_get_interrupt_owner_ex(19u, &owner));
  TEST_ASSERT_EQUAL_UINT8(1u, owner);

  hal_mock_gpio_fire_interrupt(19u);
  TEST_ASSERT_EQUAL_INT(1, s_gpio_irq_hits);
  TEST_ASSERT_EQUAL_UINT8(1u, s_gpio_irq_core_seen);
  TEST_ASSERT_EQUAL_UINT8(0u, hal_mock_gpio_get_current_core());

  hal_mock_gpio_set_current_core(1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(19u));
}

void test_interrupt_owner_validates_arguments_and_caller_core(void) {
  uint8_t owner = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_gpio_attach_interrupt_ex(64u, gpio_irq_hit, HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_gpio_attach_interrupt_ex(
                                        20u, nullptr, HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_gpio_attach_interrupt_ex(20u, gpio_irq_hit,
                                               (hal_gpio_irq_mode_t)99, 0u));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_gpio_attach_interrupt_ex(20u, gpio_irq_hit, HAL_GPIO_IRQ_RISING, 2u));
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE,
      hal_gpio_attach_interrupt_ex(20u, gpio_irq_hit, HAL_GPIO_IRQ_RISING, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_gpio_get_interrupt_owner_ex(20u, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_gpio_get_interrupt_owner_ex(20u, &owner));
  TEST_ASSERT_EQUAL_UINT8(HAL_GPIO_IRQ_CORE_NONE, owner);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_gpio_detach_interrupt_ex(20u));
}

void test_legacy_attach_records_current_core_owner(void) {
  hal_mock_gpio_set_current_core(1u);
  hal_gpio_attach_interrupt(21u, gpio_irq_hit, HAL_GPIO_IRQ_RISING);

  uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_get_interrupt_owner_ex(21u, &owner));
  TEST_ASSERT_EQUAL_UINT8(1u, owner);
  hal_gpio_detach_interrupt(21u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_set_mode_output);
  RUN_TEST(test_set_mode_input);
  RUN_TEST(test_set_mode_input_pulldown);
  RUN_TEST(test_set_mode_output_low_initializes_low);
  RUN_TEST(test_set_mode_output_high_initializes_high);
  RUN_TEST(test_set_mode_open_drain_initial_states);
  RUN_TEST(test_write_high);
  RUN_TEST(test_write_low);
  RUN_TEST(test_read_injected_high);
  RUN_TEST(test_read_injected_low);
  RUN_TEST(test_default_state_is_low);
  RUN_TEST(test_drive_high_mode_then_write_drives_high);
  RUN_TEST(test_write_then_set_output_clobbers_high_to_low);
  RUN_TEST(test_detach_interrupt_stops_mock_callback);
  RUN_TEST(test_interrupt_owner_status_and_same_core_reconfiguration);
  RUN_TEST(test_interrupt_owner_rejects_wrong_core_reconfigure_and_detach);
  RUN_TEST(test_interrupt_owner_validates_arguments_and_caller_core);
  RUN_TEST(test_legacy_attach_records_current_core_owner);
  return UNITY_END();
}
