#include "utils/unity.h"
#include "hal/hal_i2c_slave.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_i2c_slave_deinit();
    hal_i2c_slave_deinit_bus(1);
    hal_i2c_slave_init(4, 5, 0x30);
}

void tearDown(void) {}

/* ── Init / Deinit ────────────────────────────────────────────────────────── */

void test_init_sets_address_and_marks_initialized(void) {
    TEST_ASSERT_TRUE(hal_mock_i2c_slave_is_initialized());
    TEST_ASSERT_EQUAL_UINT8(0x30, hal_i2c_slave_get_address());
}

void test_init_bus1_independent(void) {
    hal_i2c_slave_init_bus(1, 6, 7, 0x42);
    TEST_ASSERT_TRUE(hal_mock_i2c_slave_is_initialized_bus(1));
    TEST_ASSERT_EQUAL_UINT8(0x42, hal_i2c_slave_get_address_bus(1));
    /* Bus 0 unchanged */
    TEST_ASSERT_EQUAL_UINT8(0x30, hal_i2c_slave_get_address());
}

void test_deinit_marks_uninitialized(void) {
    TEST_ASSERT_TRUE(hal_mock_i2c_slave_is_initialized());
    hal_i2c_slave_deinit();
    TEST_ASSERT_FALSE(hal_mock_i2c_slave_is_initialized());
}

void test_deinit_bus1(void) {
    hal_i2c_slave_init_bus(1, 6, 7, 0x42);
    TEST_ASSERT_TRUE(hal_mock_i2c_slave_is_initialized_bus(1));
    hal_i2c_slave_deinit_bus(1);
    TEST_ASSERT_FALSE(hal_mock_i2c_slave_is_initialized_bus(1));
    /* Bus 0 still alive */
    TEST_ASSERT_TRUE(hal_mock_i2c_slave_is_initialized());
}

/* ── Register write / read 8-bit ──────────────────────────────────────────── */

void test_write8_read8_roundtrip(void) {
    hal_i2c_slave_reg_write8(0x00, 0xAB);
    TEST_ASSERT_EQUAL_UINT8(0xAB, hal_i2c_slave_reg_read8(0x00));
}

void test_write8_multiple_registers(void) {
    hal_i2c_slave_reg_write8(0x00, 0x11);
    hal_i2c_slave_reg_write8(0x01, 0x22);
    hal_i2c_slave_reg_write8(0x02, 0x33);
    TEST_ASSERT_EQUAL_UINT8(0x11, hal_i2c_slave_reg_read8(0x00));
    TEST_ASSERT_EQUAL_UINT8(0x22, hal_i2c_slave_reg_read8(0x01));
    TEST_ASSERT_EQUAL_UINT8(0x33, hal_i2c_slave_reg_read8(0x02));
}

void test_write8_out_of_range_ignored(void) {
    hal_i2c_slave_reg_write8(HAL_I2C_SLAVE_REG_MAP_SIZE, 0xFF);
    /* No crash, register map unchanged */
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_slave_reg_read8(0x00));
}

void test_read8_out_of_range_returns_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_slave_reg_read8(HAL_I2C_SLAVE_REG_MAP_SIZE));
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_slave_reg_read8(0xFF));
}

/* ── Register write / read 16-bit (big-endian) ────────────────────────────── */

void test_write16_read16_roundtrip(void) {
    hal_i2c_slave_reg_write16(0x00, 0x1234);
    TEST_ASSERT_EQUAL_HEX16(0x1234, hal_i2c_slave_reg_read16(0x00));
}

void test_write16_big_endian_layout(void) {
    hal_i2c_slave_reg_write16(0x04, 0xABCD);
    TEST_ASSERT_EQUAL_UINT8(0xAB, hal_i2c_slave_reg_read8(0x04));
    TEST_ASSERT_EQUAL_UINT8(0xCD, hal_i2c_slave_reg_read8(0x05));
}

void test_write16_out_of_range_ignored(void) {
    /* Last valid register is MAP_SIZE-1, so reg+1 would overflow */
    hal_i2c_slave_reg_write16(HAL_I2C_SLAVE_REG_MAP_SIZE - 1, 0xFFFF);
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_slave_reg_read8(HAL_I2C_SLAVE_REG_MAP_SIZE - 1));
}

void test_read16_out_of_range_returns_zero(void) {
    TEST_ASSERT_EQUAL_HEX16(0, hal_i2c_slave_reg_read16(HAL_I2C_SLAVE_REG_MAP_SIZE - 1));
}

/* ── Bus-specific register access ─────────────────────────────────────────── */

void test_bus1_registers_independent(void) {
    hal_i2c_slave_init_bus(1, 6, 7, 0x42);
    hal_i2c_slave_reg_write8(0x00, 0xAA);
    hal_i2c_slave_reg_write8_bus(1, 0x00, 0xBB);
    TEST_ASSERT_EQUAL_UINT8(0xAA, hal_i2c_slave_reg_read8(0x00));
    TEST_ASSERT_EQUAL_UINT8(0xBB, hal_i2c_slave_reg_read8_bus(1, 0x00));
}

void test_bus1_write16_read16(void) {
    hal_i2c_slave_init_bus(1, 6, 7, 0x42);
    hal_i2c_slave_reg_write16_bus(1, 0x02, 0xDEAD);
    TEST_ASSERT_EQUAL_HEX16(0xDEAD, hal_i2c_slave_reg_read16_bus(1, 0x02));
}

/* ── Mock simulate receive (master write) ─────────────────────────────────── */

void test_simulate_receive_sets_register_pointer(void) {
    const uint8_t data[] = {0x05}; /* just pointer, no data */
    hal_mock_i2c_slave_simulate_receive(data, 1);
    TEST_ASSERT_EQUAL_UINT8(0x05, hal_mock_i2c_slave_get_reg_ptr());
}

void test_simulate_receive_writes_registers(void) {
    const uint8_t data[] = {0x02, 0xAA, 0xBB, 0xCC};
    hal_mock_i2c_slave_simulate_receive(data, 4);
    TEST_ASSERT_EQUAL_UINT8(0xAA, hal_i2c_slave_reg_read8(0x02));
    TEST_ASSERT_EQUAL_UINT8(0xBB, hal_i2c_slave_reg_read8(0x03));
    TEST_ASSERT_EQUAL_UINT8(0xCC, hal_i2c_slave_reg_read8(0x04));
    /* Pointer advanced past last written byte */
    TEST_ASSERT_EQUAL_UINT8(0x05, hal_mock_i2c_slave_get_reg_ptr());
}

/* ── Mock simulate request (master read) ──────────────────────────────────── */

void test_simulate_request_reads_from_pointer(void) {
    hal_i2c_slave_reg_write8(0x00, 0x10);
    hal_i2c_slave_reg_write8(0x01, 0x20);
    hal_i2c_slave_reg_write8(0x02, 0x30);

    /* Set pointer to reg 0 via a receive */
    const uint8_t ptr_cmd[] = {0x00};
    hal_mock_i2c_slave_simulate_receive(ptr_cmd, 1);

    uint8_t buf[3];
    int n = hal_mock_i2c_slave_simulate_request(buf, 3);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_UINT8(0x10, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x20, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x30, buf[2]);
}

void test_simulate_request_auto_increments_pointer(void) {
    hal_i2c_slave_reg_write16(0x00, 0x1234);

    const uint8_t ptr_cmd[] = {0x00};
    hal_mock_i2c_slave_simulate_receive(ptr_cmd, 1);

    uint8_t buf[2];
    hal_mock_i2c_slave_simulate_request(buf, 2);
    TEST_ASSERT_EQUAL_UINT8(0x12, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x02, hal_mock_i2c_slave_get_reg_ptr());
}

/* ── Init clears register map ─────────────────────────────────────────────── */

void test_init_clears_registers(void) {
    hal_i2c_slave_reg_write8(0x00, 0xFF);
    hal_i2c_slave_init(4, 5, 0x30);
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_slave_reg_read8(0x00));
}

/* ── Mock direct set/get helpers ──────────────────────────────────────────── */

void test_mock_set_get_reg(void) {
    hal_mock_i2c_slave_set_reg(0x10, 0x77);
    TEST_ASSERT_EQUAL_UINT8(0x77, hal_mock_i2c_slave_get_reg(0x10));
    TEST_ASSERT_EQUAL_UINT8(0x77, hal_i2c_slave_reg_read8(0x10));
}

void test_mock_get_reg_out_of_range(void) {
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_i2c_slave_get_reg(HAL_I2C_SLAVE_REG_MAP_SIZE));
}

/* ── reg_write return values ───────────────────────────────────────────────── */

void test_write8_returns_1_on_success(void) {
    TEST_ASSERT_EQUAL_UINT8(1, hal_i2c_slave_reg_write8(0x00, 0xAB));
}

void test_write8_returns_0_on_out_of_range(void) {
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_slave_reg_write8(HAL_I2C_SLAVE_REG_MAP_SIZE, 0xFF));
}

void test_write16_returns_2_on_success(void) {
    TEST_ASSERT_EQUAL_UINT16(2, hal_i2c_slave_reg_write16(0x00, 0x1234));
}

void test_write16_returns_0_on_out_of_range(void) {
    TEST_ASSERT_EQUAL_UINT16(0, hal_i2c_slave_reg_write16(HAL_I2C_SLAVE_REG_MAP_SIZE - 1, 0xFFFF));
}

void test_write8_bus_returns_1_on_success(void) {
    hal_i2c_slave_init_bus(1, 6, 7, 0x42);
    TEST_ASSERT_EQUAL_UINT8(1, hal_i2c_slave_reg_write8_bus(1, 0x00, 0xCC));
}

/* ── Runner ───────────────────────────────────────────────────────────────── */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_address_and_marks_initialized);
    RUN_TEST(test_init_bus1_independent);
    RUN_TEST(test_deinit_marks_uninitialized);
    RUN_TEST(test_deinit_bus1);
    RUN_TEST(test_write8_read8_roundtrip);
    RUN_TEST(test_write8_multiple_registers);
    RUN_TEST(test_write8_out_of_range_ignored);
    RUN_TEST(test_read8_out_of_range_returns_zero);
    RUN_TEST(test_write16_read16_roundtrip);
    RUN_TEST(test_write16_big_endian_layout);
    RUN_TEST(test_write16_out_of_range_ignored);
    RUN_TEST(test_read16_out_of_range_returns_zero);
    RUN_TEST(test_bus1_registers_independent);
    RUN_TEST(test_bus1_write16_read16);
    RUN_TEST(test_simulate_receive_sets_register_pointer);
    RUN_TEST(test_simulate_receive_writes_registers);
    RUN_TEST(test_simulate_request_reads_from_pointer);
    RUN_TEST(test_simulate_request_auto_increments_pointer);
    RUN_TEST(test_init_clears_registers);
    RUN_TEST(test_mock_set_get_reg);
    RUN_TEST(test_mock_get_reg_out_of_range);
    RUN_TEST(test_write8_returns_1_on_success);
    RUN_TEST(test_write8_returns_0_on_out_of_range);
    RUN_TEST(test_write16_returns_2_on_success);
    RUN_TEST(test_write16_returns_0_on_out_of_range);
    RUN_TEST(test_write8_bus_returns_1_on_success);
    return UNITY_END();
}
