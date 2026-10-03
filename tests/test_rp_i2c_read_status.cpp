// Real RP master and compatibility wrappers, with injected SDK/FIFO errors.
#include "hal/i2c/hal_i2c.h"
#include "utils/unity.h"

#include <hardware/i2c.h>
#include <pico/error.h>
#include <pico/time.h>

#include <cstdio>
#include <cstdlib>
#include <thread>

i2c_inst_t jh_test_i2c[2];
thread_local unsigned int jh_test_core_num = 0u;

extern "C" void hal_assert_fail(const char *msg) {
  std::fprintf(stderr, "HAL assert: %s\n", msg != nullptr ? msg : "");
  std::abort();
}

static uint8_t s_bus;
static bool s_10bit;
static hal_i2c_address_t s_address;

static void reply(int rc) {
  i2c_inst_t *sdk = &jh_test_i2c[s_bus];
  sdk->scripted_read = true;
  sdk->read_result = rc;
  sdk->received = 0xA5u;
  sdk->rx_ready = true;
  sdk->hw.raw_intr_stat = (rc == PICO_ERROR_GENERIC)
                              ? I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS
                              : I2C_IC_RAW_INTR_STAT_TX_EMPTY_BITS |
                                    I2C_IC_RAW_INTR_STAT_STOP_DET_BITS;
  sdk->hw.tx_abrt_source = (rc == PICO_ERROR_GENERIC)
                               ? I2C_IC_TX_ABRT_SOURCE_ABRT_10ADDR1_NOACK_BITS
                               : 0u;
  jh_test_i2c_timeout = rc == PICO_ERROR_TIMEOUT;
}

void setUp(void) {
  jh_test_i2c[s_bus] = {};
  jh_test_i2c_timeout = false;
  s_address = s_10bit ? 0x2ABu : 0x50u;
#ifdef HAL_ENABLE_I2C_10BIT
  if (s_10bit) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_i2c_init_bus_10bit(s_bus, 4u, 5u, 100000u));
    return;
  }
#endif
  TEST_ASSERT_EQUAL(HAL_OK, hal_i2c_init_bus(s_bus, 4u, 5u, 100000u));
}

void tearDown(void) { hal_i2c_deinit_bus(s_bus); }

static void test_timeout_reaches_byte_and_buffer_api(void) {
  reply(PICO_ERROR_TIMEOUT);
  uint8_t data[2] = {0xCCu, 0xCCu};
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT,
                    hal_i2c_read_bytes_bus_ex(s_bus, s_address, data, 2u));
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT,
                    hal_i2c_read_byte_bus_ex(s_bus, s_address, &data[0]));
  TEST_ASSERT_EQUAL_UINT32(2u, hal_i2c_get_transaction_count_bus(s_bus));
  if (s_10bit) {
    TEST_ASSERT_EQUAL_UINT32(s_address, jh_test_i2c[s_bus].hw.tar);
  }
}

static void test_nack_remains_bus_error(void) {
  reply(PICO_ERROR_GENERIC);
  uint8_t byte = 0u;
  TEST_ASSERT_EQUAL(HAL_EBUS,
                    hal_i2c_read_byte_bus_ex(s_bus, s_address, &byte));
  uint8_t count = 99u;
  TEST_ASSERT_EQUAL(HAL_EBUS,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 2u, &count));
  TEST_ASSERT_EQUAL_UINT8(0u, count);
}

static void test_timeout_keeps_legacy_failure_results(void) {
  reply(PICO_ERROR_TIMEOUT);
  uint8_t byte = 0xCCu;
  TEST_ASSERT_FALSE(hal_i2c_read_bytes_bus(s_bus, s_address, &byte, 1u));
  bool ok = true;
  TEST_ASSERT_EQUAL_UINT8(0u, hal_i2c_read_byte_bus(s_bus, s_address, &ok));
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT8(0u, hal_i2c_request_from_bus(s_bus, s_address, 2u));
  TEST_ASSERT_EQUAL_INT(0, hal_i2c_available_bus(s_bus));
  TEST_ASSERT_EQUAL_INT(-1, hal_i2c_read_bus(s_bus));
}

static void test_request_timeout_clears_previous_receive_buffer(void) {
  reply(2);
  uint8_t count = 0u;
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 2u, &count));
  TEST_ASSERT_EQUAL_UINT8(2u, count);
  reply(PICO_ERROR_TIMEOUT);
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 2u, &count));
  TEST_ASSERT_EQUAL_UINT8(0u, count);
  TEST_ASSERT_EQUAL_INT(0, hal_i2c_available_bus(s_bus));
}

static void test_short_read_preserves_received_count_and_data(void) {
  if (s_10bit) {
    return; // The FIFO path reports a negative error, not a short SDK count.
  }
  reply(1);
  uint8_t count = 0u;
  TEST_ASSERT_EQUAL(HAL_EBUS,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 3u, &count));
  TEST_ASSERT_EQUAL_UINT8(1u, count);
  TEST_ASSERT_EQUAL_INT(1, hal_i2c_available_bus(s_bus));
  TEST_ASSERT_EQUAL_INT(0xA5, hal_i2c_read_bus(s_bus));
  TEST_ASSERT_EQUAL_INT(-1, hal_i2c_read_bus(s_bus));
  TEST_ASSERT_EQUAL_UINT8(1u, hal_i2c_request_from_bus(s_bus, s_address, 3u));
}

static void test_write_read_and_read_only_delegation_keep_timeout(void) {
  reply(PICO_ERROR_TIMEOUT);
  const uint8_t reg = 0u;
  uint8_t data = 0u;
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, hal_i2c_write_read_bus_ex(
                                      s_bus, s_address, &reg, 1u, &data, 1u));
  TEST_ASSERT_EQUAL(
      HAL_ETIMEOUT,
      hal_i2c_write_read_bus_ex(s_bus, s_address, nullptr, 0u, &data, 1u));
}

static void test_zero_length_and_invalid_arguments_do_not_transfer(void) {
  reply(PICO_ERROR_TIMEOUT);
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_i2c_read_bytes_bus_ex(s_bus, s_address, nullptr, 0u));
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_i2c_read_bytes_bus_ex(s_bus, s_address, nullptr, 1u));
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 1u, nullptr));
  uint8_t count = 99u;
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 0u, &count));
  TEST_ASSERT_EQUAL_UINT8(0u, count);
  TEST_ASSERT_EQUAL_UINT32(0u, jh_test_i2c[s_bus].read_calls);
}

static void test_error_releases_recursive_bus_lock(void) {
  hal_i2c_lock_bus(s_bus);
  reply(PICO_ERROR_TIMEOUT);
  uint8_t byte = 0u;
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT,
                    hal_i2c_read_byte_bus_ex(s_bus, s_address, &byte));
  hal_i2c_unlock_bus(s_bus);
  reply(1);
  hal_status_t status = HAL_NONE;
  std::thread reader([&] {
    jh_test_core_num = 1u;
    status = hal_i2c_read_byte_bus_ex(s_bus, s_address, &byte);
  });
  reader.join();
  TEST_ASSERT_EQUAL(HAL_OK, status);
}

int main(void) {
  UNITY_BEGIN();
#ifdef HAL_ENABLE_I2C_10BIT
  constexpr unsigned kModes = 2u;
#else
  constexpr unsigned kModes = 1u;
#endif
  for (s_bus = 0u; s_bus < 2u; ++s_bus) {
    for (unsigned mode = 0u; mode < kModes; ++mode) {
      s_10bit = mode != 0u;
      RUN_TEST(test_timeout_reaches_byte_and_buffer_api);
      RUN_TEST(test_nack_remains_bus_error);
      RUN_TEST(test_timeout_keeps_legacy_failure_results);
      RUN_TEST(test_request_timeout_clears_previous_receive_buffer);
      RUN_TEST(test_short_read_preserves_received_count_and_data);
      RUN_TEST(test_write_read_and_read_only_delegation_keep_timeout);
      RUN_TEST(test_zero_length_and_invalid_arguments_do_not_transfer);
      RUN_TEST(test_error_releases_recursive_bus_lock);
    }
  }
  return UNITY_END();
}
