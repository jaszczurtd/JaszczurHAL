#include "hal/gpio/hal_gpio.h"
#include "hal/gpio/hal_gpio_common.h"
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

typedef struct {
  int hits;
  uint8_t last_pin;
} gpio_ctx_probe_t;

static gpio_ctx_probe_t s_probe_a;
static gpio_ctx_probe_t s_probe_b;
static int s_gpio_null_context_hits;

static void gpio_irq_ctx_hit(uint8_t pin, void *context) {
  gpio_ctx_probe_t *probe = (gpio_ctx_probe_t *)context;
  probe->hits++;
  probe->last_pin = pin;
}

static void gpio_irq_null_context_hit(uint8_t pin, void *context) {
  (void)pin;
  if (context == nullptr) {
    s_gpio_null_context_hits++;
  }
}

void test_common_gpio_validators_match_public_enum_ranges(void) {
  for (int mode = HAL_GPIO_INPUT; mode <= HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH;
       ++mode) {
    TEST_ASSERT_TRUE(jh_hal_gpio_mode_valid((hal_gpio_mode_t)mode));
  }
  TEST_ASSERT_FALSE(jh_hal_gpio_mode_valid((hal_gpio_mode_t)-1));
  TEST_ASSERT_FALSE(jh_hal_gpio_mode_valid(
      (hal_gpio_mode_t)(HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH + 1)));

  for (int mode = HAL_GPIO_IRQ_FALLING; mode <= HAL_GPIO_IRQ_CHANGE; ++mode) {
    TEST_ASSERT_TRUE(jh_hal_gpio_irq_mode_valid((hal_gpio_irq_mode_t)mode));
  }
  TEST_ASSERT_FALSE(jh_hal_gpio_irq_mode_valid((hal_gpio_irq_mode_t)-1));
  TEST_ASSERT_FALSE(jh_hal_gpio_irq_mode_valid(
      (hal_gpio_irq_mode_t)(HAL_GPIO_IRQ_CHANGE + 1)));
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

void test_context_handler_receives_pin_and_context(void) {
  s_probe_a = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_gpio_attach_interrupt_ctx_ex(
                  22u, gpio_irq_ctx_hit, &s_probe_a, HAL_GPIO_IRQ_RISING, 0u));
  hal_mock_gpio_fire_interrupt(22u);
  TEST_ASSERT_EQUAL_INT(1, s_probe_a.hits);
  TEST_ASSERT_EQUAL_UINT8(22u, s_probe_a.last_pin);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(22u));
}

/* The reason the API exists: one handler serving several pins has to tell the
 * instances apart. */
void test_one_handler_serves_two_pins_with_separate_contexts(void) {
  s_probe_a = {};
  s_probe_b = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_gpio_attach_interrupt_ctx(23u, gpio_irq_ctx_hit, &s_probe_a,
                                            HAL_GPIO_IRQ_RISING));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_gpio_attach_interrupt_ctx(24u, gpio_irq_ctx_hit, &s_probe_b,
                                            HAL_GPIO_IRQ_FALLING));
  hal_mock_gpio_fire_interrupt(23u);
  hal_mock_gpio_fire_interrupt(24u);
  hal_mock_gpio_fire_interrupt(24u);

  TEST_ASSERT_EQUAL_INT(1, s_probe_a.hits);
  TEST_ASSERT_EQUAL_UINT8(23u, s_probe_a.last_pin);
  TEST_ASSERT_EQUAL_INT(2, s_probe_b.hits);
  TEST_ASSERT_EQUAL_UINT8(24u, s_probe_b.last_pin);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(23u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(24u));
}

void test_handler_kinds_replace_each_other_on_one_pin(void) {
  s_probe_a = {};
  s_gpio_irq_hits = 0;

  hal_gpio_attach_interrupt(25u, gpio_irq_hit, HAL_GPIO_IRQ_RISING);
  hal_mock_gpio_fire_interrupt(25u);
  TEST_ASSERT_EQUAL_INT(1, s_gpio_irq_hits);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_gpio_attach_interrupt_ctx(25u, gpio_irq_ctx_hit, &s_probe_a,
                                            HAL_GPIO_IRQ_RISING));
  hal_mock_gpio_fire_interrupt(25u);
  TEST_ASSERT_EQUAL_INT(1, s_gpio_irq_hits);
  TEST_ASSERT_EQUAL_INT(1, s_probe_a.hits);

  hal_gpio_attach_interrupt(25u, gpio_irq_hit, HAL_GPIO_IRQ_RISING);
  hal_mock_gpio_fire_interrupt(25u);
  TEST_ASSERT_EQUAL_INT(2, s_gpio_irq_hits);
  TEST_ASSERT_EQUAL_INT(1, s_probe_a.hits);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(25u));
}

void test_detach_clears_context_handler(void) {
  s_probe_a = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_gpio_attach_interrupt_ctx(26u, gpio_irq_ctx_hit, &s_probe_a,
                                            HAL_GPIO_IRQ_CHANGE));
  hal_mock_gpio_fire_interrupt(26u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(26u));
  hal_mock_gpio_fire_interrupt(26u);
  TEST_ASSERT_EQUAL_INT(1, s_probe_a.hits);

  uint8_t owner = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_gpio_get_interrupt_owner_ex(26u, &owner));
  TEST_ASSERT_EQUAL_UINT8(HAL_GPIO_IRQ_CORE_NONE, owner);
}

void test_context_handler_accepts_null_context(void) {
  s_gpio_null_context_hits = 0;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_gpio_attach_interrupt_ctx(27u, gpio_irq_null_context_hit,
                                            nullptr, HAL_GPIO_IRQ_RISING));
  hal_mock_gpio_fire_interrupt(27u);
  TEST_ASSERT_EQUAL_INT(1, s_gpio_null_context_hits);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(27u));
}

void test_context_attach_validates_arguments_and_owner_core(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_gpio_attach_interrupt_ctx_ex(
                                        64u, gpio_irq_ctx_hit, &s_probe_a,
                                        HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_gpio_attach_interrupt_ctx_ex(28u, nullptr, &s_probe_a,
                                                   HAL_GPIO_IRQ_RISING, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_gpio_attach_interrupt_ctx_ex(
                                        28u, gpio_irq_ctx_hit, &s_probe_a,
                                        (hal_gpio_irq_mode_t)99, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_gpio_attach_interrupt_ctx_ex(
                                        28u, gpio_irq_ctx_hit, &s_probe_a,
                                        HAL_GPIO_IRQ_RISING, 2u));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_gpio_attach_interrupt_ctx_ex(
                                        28u, gpio_irq_ctx_hit, &s_probe_a,
                                        HAL_GPIO_IRQ_RISING, 1u));
}

void test_context_attach_binds_to_current_core(void) {
  s_probe_a = {};
  hal_mock_gpio_set_current_core(1u);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_gpio_attach_interrupt_ctx(29u, gpio_irq_ctx_hit, &s_probe_a,
                                            HAL_GPIO_IRQ_RISING));
  uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_get_interrupt_owner_ex(29u, &owner));
  TEST_ASSERT_EQUAL_UINT8(1u, owner);

  hal_mock_gpio_set_current_core(0u);
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE, hal_gpio_attach_interrupt_ctx(
                      29u, gpio_irq_ctx_hit, &s_probe_b, HAL_GPIO_IRQ_RISING));
  hal_mock_gpio_fire_interrupt(29u);
  TEST_ASSERT_EQUAL_INT(1, s_probe_a.hits);

  hal_mock_gpio_set_current_core(1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gpio_detach_interrupt_ex(29u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_common_gpio_validators_match_public_enum_ranges);
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
  RUN_TEST(test_context_handler_receives_pin_and_context);
  RUN_TEST(test_one_handler_serves_two_pins_with_separate_contexts);
  RUN_TEST(test_handler_kinds_replace_each_other_on_one_pin);
  RUN_TEST(test_detach_clears_context_handler);
  RUN_TEST(test_context_handler_accepts_null_context);
  RUN_TEST(test_context_attach_validates_arguments_and_owner_core);
  RUN_TEST(test_context_attach_binds_to_current_core);
  return UNITY_END();
}
