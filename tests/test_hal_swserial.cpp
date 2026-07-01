#include "hal/hal_swserial.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

static hal_swserial_t s_port = NULL;

void setUp(void) {
  hal_mock_critical_section_reset();
  hal_mock_gpio_trace_reset();
  hal_mock_mutex_stats_reset();
  s_port = hal_swserial_create(5, 4);
  hal_mock_swserial_reset(s_port);
}

void tearDown(void) {
  hal_swserial_destroy(s_port);
  s_port = NULL;
}

void test_swserial_reads_injected_bytes(void) {
  const uint8_t payload[] = {'O', 'K'};
  hal_mock_swserial_push(s_port, payload, (int)sizeof(payload));

  TEST_ASSERT_EQUAL_INT(2, hal_swserial_available(s_port));
  TEST_ASSERT_EQUAL_INT('O', hal_swserial_read(s_port));
  TEST_ASSERT_EQUAL_INT('K', hal_swserial_read(s_port));
  TEST_ASSERT_EQUAL_INT(-1, hal_swserial_read(s_port));
}

void test_swserial_captures_written_line(void) {
  hal_swserial_begin(s_port, 9600, HAL_UART_CFG_8N1);

  TEST_ASSERT_EQUAL_UINT32(4u, hal_swserial_println(s_port, "ATE0"));
  TEST_ASSERT_EQUAL_STRING("ATE0", hal_mock_swserial_last_write(s_port));
  TEST_ASSERT_TRUE(hal_mock_mutex_lock_count() > 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_critical_depth());
}

void test_swserial_reassigns_pins_before_begin(void) {
  TEST_ASSERT_TRUE(hal_swserial_set_rx(s_port, 7));
  TEST_ASSERT_TRUE(hal_swserial_set_tx(s_port, 6));

  hal_swserial_begin(s_port, 115200, HAL_UART_CFG_8N1);

  TEST_ASSERT_FALSE(hal_swserial_set_rx(s_port, 9));
  TEST_ASSERT_FALSE(hal_swserial_set_tx(s_port, 8));
}

void test_swserial_begin_configures_gpio_and_irq(void) {
  hal_swserial_begin(s_port, 9600, HAL_UART_CFG_7E2);

  TEST_ASSERT_EQUAL(HAL_GPIO_INPUT_PULLUP, hal_mock_gpio_get_mode(5));
  TEST_ASSERT_EQUAL(HAL_GPIO_OUTPUT_HIGH, hal_mock_gpio_get_mode(4));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(4));
  TEST_ASSERT_EQUAL(HAL_IRQ_PRIORITY_HIGH, hal_mock_gpio_get_irq_priority());
}

void test_swserial_write_bits_8n1_lsb_first(void) {
  hal_swserial_begin(s_port, 1000000, HAL_UART_CFG_8N1);
  hal_mock_gpio_trace_reset();

  const uint8_t byte = 0xA5u;
  TEST_ASSERT_EQUAL_UINT32(1u, hal_swserial_write(s_port, &byte, 1u));

  const int expected[] = {
      0,                      /* start */
      1, 0, 1, 0, 0, 1, 0, 1, /* data, LSB first */
      1,                      /* stop */
  };
  size_t write_index = 0u;
  for (size_t i = 0u; i < hal_mock_gpio_trace_count(); ++i) {
    hal_mock_gpio_event_t event;
    TEST_ASSERT_TRUE(hal_mock_gpio_trace_get(i, &event));
    if (event.type == HAL_MOCK_GPIO_EVENT_WRITE && event.pin == 4) {
      TEST_ASSERT_LESS_THAN_UINT32(sizeof(expected) / sizeof(expected[0]),
                                   write_index);
      TEST_ASSERT_EQUAL_INT(expected[write_index], event.value);
      write_index++;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected) / sizeof(expected[0]), write_index);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_critical_depth());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_swserial_reads_injected_bytes);
  RUN_TEST(test_swserial_captures_written_line);
  RUN_TEST(test_swserial_reassigns_pins_before_begin);
  RUN_TEST(test_swserial_begin_configures_gpio_and_irq);
  RUN_TEST(test_swserial_write_bits_8n1_lsb_first);
  return UNITY_END();
}
