// The real STM32 I2C master on scripted ISR flags, independent of firmware.
#include "hal/core/hal_array.h"
#include "hal/i2c/hal_i2c.h"
#include "jh_stm32g474_host_regs.h"
#include "utils/unity.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>

extern "C" {
void hal_assert_fail(const char *msg) {
  std::fprintf(stderr, "HAL assert: %s\n", msg != nullptr ? msg : "");
  std::abort();
}
void hal_delay_us(uint32_t) {}
}

namespace {

constexpr uintptr_t kBase[] = {0x40005400u, 0x40005800u};
constexpr uint32_t kTxis = 1u << 1;
constexpr uint32_t kRxne = 1u << 2;
constexpr uint32_t kNack = 1u << 4;
constexpr uint32_t kStop = 1u << 5;
constexpr uint32_t kTc = 1u << 6;
uint8_t s_bus;
bool s_10bit;
hal_i2c_address_t s_address;
std::vector<uint32_t> s_flags;
size_t s_next;
uint32_t s_tail;

uint32_t read_register(uintptr_t address, uint32_t cell) {
  if (address == kBase[s_bus] + 0x18u) {
    return s_next < s_flags.size() ? s_flags[s_next++] : s_tail;
  }
  return cell;
}

void script(std::initializer_list<uint32_t> flags, uint32_t tail = 0u) {
  s_flags = flags;
  s_next = 0u;
  s_tail = tail;
}

hal_status_t write_read(uint8_t *data, size_t len) {
  const uint8_t reg = 0x20u;
  return hal_i2c_write_read_bus_ex(s_bus, s_address, &reg, 1u, data, len);
}

} // namespace

void setUp(void) {
  jh_stm32g474_host_regs_reset();
  jh_stm32g474_host_regs_set_hooks(read_register, nullptr);
  script({});
  s_address = s_10bit ? 0x2ABu : 0x50u;
#ifdef HAL_ENABLE_I2C_10BIT
  if (s_10bit) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_i2c_init_bus_10bit(s_bus, 0u, 0u, 100000u));
  } else
#endif
  {
    TEST_ASSERT_EQUAL(HAL_OK, hal_i2c_init_bus(s_bus, 0u, 0u, 100000u));
  }
  *jh_stm32g474_host_reg32(kBase[s_bus] + 0x24u) = 0xA5u;
}

void tearDown(void) { hal_i2c_deinit_bus(s_bus); }

static void test_success_and_address_encoding(void) {
  script({kRxne, kRxne, kStop});
  uint8_t data[2] = {};
  TEST_ASSERT_EQUAL(
      HAL_OK, hal_i2c_read_bytes_bus_ex(s_bus, s_address, data, COUNTOF(data)));
  TEST_ASSERT_EQUAL_UINT8(0xA5u, data[0]);
  TEST_ASSERT_EQUAL_UINT8(0xA5u, data[1]);
  const uint32_t cr2 = *jh_stm32g474_host_reg32(kBase[s_bus] + 0x04u);
  const uint32_t address_bits =
      s_10bit ? s_address | (1u << 11) : (uint32_t)s_address << 1;
  TEST_ASSERT_EQUAL_HEX32(address_bits, cr2 & 0xBFFu);
  TEST_ASSERT_EQUAL_UINT32(1u, hal_i2c_get_transaction_count_bus(s_bus));
  script({kTxis, kTc, kRxne, kStop});
  TEST_ASSERT_EQUAL(HAL_OK, write_read(data, 1u));
  TEST_ASSERT_EQUAL_UINT32(3u, hal_i2c_get_transaction_count_bus(s_bus));
}

static void test_timeout_before_first_byte(void) {
  script({}, kStop);
  uint8_t byte = 0xCCu;
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT,
                    hal_i2c_read_byte_bus_ex(s_bus, s_address, &byte));
  TEST_ASSERT_EQUAL_UINT8(0u, byte);
  uint8_t count = 99u;
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 2u, &count));
  TEST_ASSERT_EQUAL_UINT8(0u, count);
  TEST_ASSERT_EQUAL_INT(0, hal_i2c_available_bus(s_bus));
}

static void test_partial_timeout_preserves_buffer_and_count(void) {
  script({kRxne}, kStop);
  uint8_t data[2] = {0xCCu, 0xCCu};
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, hal_i2c_read_bytes_bus_ex(
                                      s_bus, s_address, data, COUNTOF(data)));
  TEST_ASSERT_EQUAL_UINT8(0xA5u, data[0]);
  TEST_ASSERT_EQUAL_UINT8(0xCCu, data[1]);
  script({kRxne}, kStop);
  uint8_t count = 99u;
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 2u, &count));
  TEST_ASSERT_EQUAL_UINT8(1u, count);
  TEST_ASSERT_EQUAL_INT(1, hal_i2c_available_bus(s_bus));
  TEST_ASSERT_EQUAL_INT(0xA5, hal_i2c_read_bus(s_bus));
  TEST_ASSERT_EQUAL_INT(-1, hal_i2c_read_bus(s_bus));
  script({kRxne}, kStop);
  TEST_ASSERT_EQUAL_UINT8(1u, hal_i2c_request_from_bus(s_bus, s_address, 2u));
}

static void test_stop_timeout_after_complete_receive(void) {
  script({kRxne, kRxne});
  uint8_t count = 0u;
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 2u, &count));
  TEST_ASSERT_EQUAL_UINT8(2u, count);
  TEST_ASSERT_EQUAL_INT(2, hal_i2c_available_bus(s_bus));
  script({kRxne, kRxne});
  TEST_ASSERT_EQUAL_UINT8(2u, hal_i2c_request_from_bus(s_bus, s_address, 2u));
  script({kRxne});
  uint8_t byte = 0u;
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT,
                    hal_i2c_read_byte_bus_ex(s_bus, s_address, &byte));
  script({kRxne});
  bool ok = true;
  TEST_ASSERT_EQUAL_UINT8(0u, hal_i2c_read_byte_bus(s_bus, s_address, &ok));
  TEST_ASSERT_FALSE(ok);
}

static void test_nack_precedes_cleanup_timeout(void) {
  script({kRxne, kNack});
  uint8_t count = 0u;
  TEST_ASSERT_EQUAL(HAL_EBUS,
                    hal_i2c_request_from_bus_ex(s_bus, s_address, 2u, &count));
  TEST_ASSERT_EQUAL_UINT8(1u, count);
  TEST_ASSERT_EQUAL_INT(0xA5, hal_i2c_read_bus(s_bus));
  TEST_ASSERT_EQUAL_HEX32(kNack | kStop,
                          *jh_stm32g474_host_reg32(kBase[s_bus] + 0x1Cu));
}

static void test_write_read_reports_timeout_in_each_phase(void) {
  uint8_t byte = 0u;
  script({});
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, write_read(&byte, 1u));
  script({kTxis});
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, write_read(&byte, 1u));
  script({kTxis, kTc}, kStop);
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, write_read(&byte, 1u));
  script({kTxis, kTc, kRxne});
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, write_read(&byte, 1u));
  script({kTxis, kTc});
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, write_read(nullptr, 0u));
}

static void test_write_read_nack_in_each_phase(void) {
  uint8_t byte = 0xCCu;
  script({kTxis | kNack});
  TEST_ASSERT_EQUAL(HAL_EBUS, write_read(&byte, 1u));
  script({kTxis, kTc | kNack});
  TEST_ASSERT_EQUAL(HAL_EBUS, write_read(&byte, 1u));
  script({kTxis, kTc, kRxne | kNack}, kStop);
  TEST_ASSERT_EQUAL(HAL_EBUS, write_read(&byte, 1u));
  TEST_ASSERT_EQUAL_UINT8(0xCCu, byte);
}

static void test_read_only_delegation_and_legacy_bool(void) {
  uint8_t byte = 0u;
  script({}, kStop);
  TEST_ASSERT_EQUAL(
      HAL_ETIMEOUT,
      hal_i2c_write_read_bus_ex(s_bus, s_address, nullptr, 0u, &byte, 1u));
  script({}, kStop);
  TEST_ASSERT_FALSE(hal_i2c_read_bytes_bus(s_bus, s_address, &byte, 1u));
  script({kTxis, kTc, kRxne});
  const uint8_t reg = 0u;
  TEST_ASSERT_FALSE(
      hal_i2c_write_read_bus(s_bus, s_address, &reg, 1u, &byte, 1u));
}

static void test_empty_and_invalid_reads_do_not_poll(void) {
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
  TEST_ASSERT_EQUAL_UINT32(0u, s_next);
}

int main(void) {
  UNITY_BEGIN();
#ifdef HAL_ENABLE_I2C_10BIT
  constexpr unsigned kModes = 2u;
#else
  constexpr unsigned kModes = 1u;
#endif
  for (s_bus = 0u; s_bus < COUNTOF(kBase); ++s_bus) {
    for (unsigned mode = 0u; mode < kModes; ++mode) {
      s_10bit = mode != 0u;
      RUN_TEST(test_success_and_address_encoding);
      RUN_TEST(test_timeout_before_first_byte);
      RUN_TEST(test_partial_timeout_preserves_buffer_and_count);
      RUN_TEST(test_stop_timeout_after_complete_receive);
      RUN_TEST(test_nack_precedes_cleanup_timeout);
      RUN_TEST(test_write_read_reports_timeout_in_each_phase);
      RUN_TEST(test_write_read_nack_in_each_phase);
      RUN_TEST(test_read_only_delegation_and_legacy_bool);
      RUN_TEST(test_empty_and_invalid_reads_do_not_poll);
    }
  }
  return UNITY_END();
}
