#include "hal/i2c/hal_i2c_slave.h"
#include "utils/unity.h"

void setUp(void) {
  hal_i2c_slave_deinit();
  hal_i2c_slave_deinit_bus(1);
  hal_i2c_slave_init(0, 0, 0x30);
}

void tearDown(void) {}

void test_init_sets_address(void) {
  TEST_ASSERT_EQUAL_UINT8(0x30, hal_i2c_slave_get_address());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_i2c_slave_get_transaction_count());
}

void test_bus1_is_independent(void) {
  hal_i2c_slave_init_bus(1, 0, 0, 0x42);
  hal_i2c_slave_reg_write8(0x00, 0xAA);
  hal_i2c_slave_reg_write8_bus(1, 0x00, 0xBB);

  TEST_ASSERT_EQUAL_UINT8(0x30, hal_i2c_slave_get_address());
  TEST_ASSERT_EQUAL_UINT8(0x42, hal_i2c_slave_get_address_bus(1));
  TEST_ASSERT_EQUAL_UINT8(0xAA, hal_i2c_slave_reg_read8(0x00));
  TEST_ASSERT_EQUAL_UINT8(0xBB, hal_i2c_slave_reg_read8_bus(1, 0x00));
}

void test_register_write_read8_roundtrip(void) {
  hal_i2c_slave_reg_write8(0x02, 0x5A);
  TEST_ASSERT_EQUAL_UINT8(0x5A, hal_i2c_slave_reg_read8(0x02));
}

void test_register_write_read16_roundtrip_big_endian(void) {
  hal_i2c_slave_reg_write16(0x04, 0x1234);
  TEST_ASSERT_EQUAL_HEX16(0x1234, hal_i2c_slave_reg_read16(0x04));
  TEST_ASSERT_EQUAL_UINT8(0x12, hal_i2c_slave_reg_read8(0x04));
  TEST_ASSERT_EQUAL_UINT8(0x34, hal_i2c_slave_reg_read8(0x05));
}

void test_out_of_range_access_is_ignored(void) {
  hal_i2c_slave_reg_write8(HAL_I2C_SLAVE_REG_MAP_SIZE, 0xFF);
  hal_i2c_slave_reg_write16(HAL_I2C_SLAVE_REG_MAP_SIZE - 1u, 0xFFFF);

  TEST_ASSERT_EQUAL_UINT8(0u,
                          hal_i2c_slave_reg_read8(HAL_I2C_SLAVE_REG_MAP_SIZE));
  TEST_ASSERT_EQUAL_HEX16(
      0u, hal_i2c_slave_reg_read16(HAL_I2C_SLAVE_REG_MAP_SIZE - 1u));
}

void test_reinit_clears_register_map_and_counter(void) {
  hal_i2c_slave_reg_write8(0x00, 0x77);
  hal_i2c_slave_init(0, 0, 0x31);

  TEST_ASSERT_EQUAL_UINT8(0x31, hal_i2c_slave_get_address());
  TEST_ASSERT_EQUAL_UINT8(0u, hal_i2c_slave_reg_read8(0x00));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_i2c_slave_get_transaction_count());
}

void test_deinit_clears_address_and_registers(void) {
  hal_i2c_slave_reg_write8(0x01, 0x44);
  hal_i2c_slave_deinit();

  TEST_ASSERT_EQUAL_UINT8(0u, hal_i2c_slave_get_address());
  TEST_ASSERT_EQUAL_UINT8(0u, hal_i2c_slave_reg_read8(0x01));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_i2c_slave_get_transaction_count());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_sets_address);
  RUN_TEST(test_bus1_is_independent);
  RUN_TEST(test_register_write_read8_roundtrip);
  RUN_TEST(test_register_write_read16_roundtrip_big_endian);
  RUN_TEST(test_out_of_range_access_is_ignored);
  RUN_TEST(test_reinit_clears_register_map_and_counter);
  RUN_TEST(test_deinit_clears_address_and_registers);
  return UNITY_END();
}
