/*
 * Exercises the REAL bit-banging 1-Wire driver (JHOneWire / onewire_driver.cpp)
 * against the mock GPIO. The public hal_onewire HAL is faked under MOCK by
 * .mock/hal_onewire.cpp, so without this test the actual reset/read/write pin
 * sequences never run on host - which is exactly why the onewire_drive_high
 * "write-before-mode" ordering bug escaped CI.
 *
 * The load-bearing guard is behavioural: after any write_bit() the line is left
 * HIGH (the recovery/idle level driven by onewire_drive_high). The mock models
 * RP2040 faithfully - set_mode(OUTPUT) clobbers the output latch to 0 - so with
 * the buggy write-then-mode order the line reads LOW and these tests fail.
 */
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/drivers/onewire/onewire_driver.h"
#include "utils/unity.h"

#if (defined(HAL_TARGET_IS_MOCK) && HAL_TARGET_IS_MOCK) &&                     \
    defined(HAL_ENABLE_ONEWIRE)

static const uint8_t OW_PIN = 7u;

void setUp(void) {
  hal_mock_critical_section_reset();
  hal_mock_gpio_clear_read_sequence(OW_PIN);
}
void tearDown(void) {
  /* Balance invariant: every hal_critical_section_enter() the driver runs must
   * be matched by an exit(). A leaked section (enter without exit) leaves a
   * non-zero depth here - which the behavioural data assertions cannot see. */
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_critical_depth());
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());
}

/* write_bit(1) ends by driving the line HIGH (10us low slot + release-high).
 * onewire_drive_high() must set OUTPUT before writing HIGH; with the buggy
 * order set_mode(OUTPUT) clobbers the latch and the pin reads LOW. */
void test_write_bit_one_leaves_line_high(void) {
  JHOneWire ow(OW_PIN);
  ow.write_bit(1u);
  TEST_ASSERT_TRUE_MESSAGE(hal_mock_gpio_get_state(OW_PIN),
                           "write_bit(1) left the line LOW: onewire_drive_high "
                           "drove before set_mode(OUTPUT)");
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(OW_PIN));
}

/* write_bit(0) is a 65us low slot, also released HIGH at the end via
 * drive_high. */
void test_write_bit_zero_leaves_line_high(void) {
  JHOneWire ow(OW_PIN);
  ow.write_bit(0u);
  TEST_ASSERT_TRUE_MESSAGE(hal_mock_gpio_get_state(OW_PIN),
                           "write_bit(0) left the line LOW: onewire_drive_high "
                           "drove before set_mode(OUTPUT)");
}

/* A whole byte of ones must still leave the bus released HIGH after the final
 * recovery (write() releases to INPUT afterwards, so check at bit granularity).
 */
void test_write_byte_each_bit_recovers_high(void) {
  JHOneWire ow(OW_PIN);
  for (int i = 0; i < 8; ++i) {
    ow.write_bit(1u);
    TEST_ASSERT_TRUE(hal_mock_gpio_get_state(OW_PIN));
  }
}

/* A full reset slot: poll sees the line released HIGH, then a device pulls it
 * LOW during the presence window -> reset() reports presence (1). */
void test_reset_detects_presence(void) {
  JHOneWire ow(OW_PIN);
  const bool levels[] = {true /* bus high, exit poll */, false /* presence */};
  hal_mock_gpio_push_read_sequence(OW_PIN, levels, 2u);
  TEST_ASSERT_EQUAL_UINT8(1u, ow.reset());
}

/* No device on the bus: the presence window stays HIGH -> no presence (0). */
void test_reset_no_presence(void) {
  JHOneWire ow(OW_PIN);
  const bool levels[] = {true /* bus high, exit poll */, true /* still high */};
  hal_mock_gpio_push_read_sequence(OW_PIN, levels, 2u);
  TEST_ASSERT_EQUAL_UINT8(0u, ow.reset());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_write_bit_one_leaves_line_high);
  RUN_TEST(test_write_bit_zero_leaves_line_high);
  RUN_TEST(test_write_byte_each_bit_recovers_high);
  RUN_TEST(test_reset_detects_presence);
  RUN_TEST(test_reset_no_presence);
  return UNITY_END();
}

#else
int main(void) { return 0; }
#endif
