#include "hal/hal_swserial.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

static hal_swserial_t s_port = NULL;

void setUp(void) {
  hal_mock_critical_section_reset();
  hal_mock_gpio_trace_reset();
  hal_mock_mutex_stats_reset();
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_create_ex(5, 4, &s_port));
  TEST_ASSERT_NOT_NULL(s_port);
  hal_mock_swserial_reset(s_port);
}

void tearDown(void) {
  hal_swserial_destroy(s_port);
  s_port = NULL;
}

void test_swserial_reads_injected_bytes(void) {
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_begin(s_port, 9600, HAL_UART_CFG_8N1));
  const uint8_t payload[] = {'O', 'K'};
  hal_mock_swserial_push(s_port, payload, (int)sizeof(payload));

  TEST_ASSERT_EQUAL_INT(2, hal_swserial_available(s_port));
  uint8_t value = 0u;
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_read_ex(s_port, &value));
  TEST_ASSERT_EQUAL_UINT8('O', value);
  TEST_ASSERT_EQUAL_INT('K', hal_swserial_read(s_port));
  value = 0xFFu;
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_swserial_read_ex(s_port, &value));
  TEST_ASSERT_EQUAL_UINT8(0u, value);
  TEST_ASSERT_EQUAL_INT(-1, hal_swserial_read(s_port));
}

void test_swserial_captures_written_line(void) {
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_begin(s_port, 9600, HAL_UART_CFG_8N1));

  size_t written = 99u;
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_println_ex(s_port, "ATE0", &written));
  TEST_ASSERT_EQUAL_UINT32(4u, written);
  TEST_ASSERT_EQUAL_STRING("ATE0", hal_mock_swserial_last_write(s_port));
  TEST_ASSERT_EQUAL_UINT32(4u, hal_swserial_println(s_port, "ATZ0"));
  TEST_ASSERT_EQUAL_STRING("ATZ0", hal_mock_swserial_last_write(s_port));
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_flush(s_port));
  TEST_ASSERT_TRUE(hal_mock_mutex_lock_count() > 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_critical_depth());
}

void test_swserial_reassigns_pins_before_begin(void) {
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_set_rx_ex(s_port, 7));
  TEST_ASSERT_TRUE(hal_swserial_set_tx(s_port, 6));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_set_rx_ex(s_port, 6));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_set_tx_ex(s_port, 7));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_set_rx_ex(s_port, 64));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_set_tx_ex(s_port, 64));

  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_swserial_begin(s_port, 115200, HAL_UART_CFG_8N1));

  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_set_rx_ex(s_port, 7));
  TEST_ASSERT_TRUE(hal_swserial_set_tx(s_port, 6));
  TEST_ASSERT_EQUAL(HAL_ESTATE, hal_swserial_set_rx_ex(s_port, 9));
  TEST_ASSERT_EQUAL(HAL_ESTATE, hal_swserial_set_tx_ex(s_port, 8));
  TEST_ASSERT_FALSE(hal_swserial_set_rx(s_port, 9));
  TEST_ASSERT_FALSE(hal_swserial_set_tx(s_port, 8));
}

void test_swserial_begin_configures_gpio_and_irq(void) {
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_begin(s_port, 9600, HAL_UART_CFG_7E2));

  TEST_ASSERT_EQUAL(HAL_GPIO_INPUT_PULLUP, hal_mock_gpio_get_mode(5));
  TEST_ASSERT_EQUAL(HAL_GPIO_OUTPUT_HIGH, hal_mock_gpio_get_mode(4));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(4));
  TEST_ASSERT_EQUAL(HAL_IRQ_PRIORITY_HIGH, hal_mock_gpio_get_irq_priority());
}

void test_swserial_write_bits_8n1_lsb_first(void) {
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_swserial_begin(s_port, 1000000, HAL_UART_CFG_8N1));
  hal_mock_gpio_trace_reset();

  const uint8_t byte = 0xA5u;
  size_t written = 0u;
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_write_ex(s_port, &byte, 1u, &written));
  TEST_ASSERT_EQUAL_UINT32(1u, written);

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

void test_swserial_status_validates_create_and_pool_exhaustion(void) {
  hal_swserial_t handle = (hal_swserial_t)(uintptr_t)1u;
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_create_ex(5, 4, NULL));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_create_ex(64, 4, &handle));
  TEST_ASSERT_NULL(handle);
  handle = (hal_swserial_t)(uintptr_t)1u;
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_create_ex(5, 5, &handle));
  TEST_ASSERT_NULL(handle);
  TEST_ASSERT_NULL(hal_swserial_create(64, 4));

  hal_swserial_t extra[HAL_SWSERIAL_MAX_INSTANCES] = {};
  hal_status_t statuses[HAL_SWSERIAL_MAX_INSTANCES] = {};
  const int extra_count = hal_get_config()->swserial_max_instances - 1;
  for (int i = 0; i < extra_count; ++i) {
    statuses[i] = hal_swserial_create_ex(5, 4, &extra[i]);
  }
  handle = (hal_swserial_t)(uintptr_t)1u;
  const hal_status_t exhausted = hal_swserial_create_ex(5, 4, &handle);
  for (int i = 0; i < extra_count; ++i) {
    hal_swserial_destroy(extra[i]);
  }

  for (int i = 0; i < extra_count; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, statuses[i]);
  }
  TEST_ASSERT_EQUAL(HAL_ENOMEM, exhausted);
  TEST_ASSERT_NULL(handle);
}

void test_swserial_status_reports_invalid_state_and_arguments(void) {
  uint8_t value = 0xFFu;
  size_t written = 99u;

  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_read_ex(NULL, &value));
  TEST_ASSERT_EQUAL_UINT8(0u, value);
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_read_ex(s_port, NULL));
  TEST_ASSERT_EQUAL(HAL_EUNINIT, hal_swserial_read_ex(s_port, &value));
  TEST_ASSERT_EQUAL(HAL_EUNINIT,
                    hal_swserial_write_ex(s_port, NULL, 0u, &written));
  TEST_ASSERT_EQUAL_UINT32(0u, written);
  written = 99u;
  TEST_ASSERT_EQUAL(HAL_EUNINIT,
                    hal_swserial_println_ex(s_port, "AT", &written));
  TEST_ASSERT_EQUAL_UINT32(0u, written);
  TEST_ASSERT_EQUAL(HAL_EUNINIT, hal_swserial_flush(s_port));
  TEST_ASSERT_EQUAL_INT(0, hal_swserial_available(s_port));

  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_swserial_begin(NULL, 9600, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_swserial_begin(s_port, 0, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_swserial_begin(s_port, 1000001, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_begin(s_port, 9600, 0u));
  TEST_ASSERT_EQUAL(HAL_EUNINIT, hal_swserial_flush(s_port));

  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_begin(s_port, 9600, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_begin(s_port, 9600, 0u));
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_flush(s_port));

  written = 99u;
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_write_ex(s_port, NULL, 0u, &written));
  TEST_ASSERT_EQUAL_UINT32(0u, written);
  written = 99u;
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_swserial_write_ex(s_port, NULL, 1u, &written));
  TEST_ASSERT_EQUAL_UINT32(0u, written);
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_swserial_write_ex(NULL, &value, 1u, &written));
  TEST_ASSERT_EQUAL_UINT32(0u, written);
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_swserial_flush(NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_swserial_reads_injected_bytes);
  RUN_TEST(test_swserial_captures_written_line);
  RUN_TEST(test_swserial_reassigns_pins_before_begin);
  RUN_TEST(test_swserial_begin_configures_gpio_and_irq);
  RUN_TEST(test_swserial_write_bits_8n1_lsb_first);
  RUN_TEST(test_swserial_status_validates_create_and_pool_exhaustion);
  RUN_TEST(test_swserial_status_reports_invalid_state_and_arguments);
  return UNITY_END();
}
