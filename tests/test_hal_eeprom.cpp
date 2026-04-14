#include "utils/unity.h"
#include "hal/hal_eeprom.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_mock_eeprom_reset();
    hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0x50);
}

void tearDown(void) {}

void test_init_sets_type(void) {
    TEST_ASSERT_EQUAL_INT(HAL_EEPROM_AT24C256, hal_mock_eeprom_get_type());
}

void test_init_sets_size_at24c256(void) {
    TEST_ASSERT_EQUAL_UINT16(32768, hal_eeprom_size());
}

void test_write_read_byte(void) {
    hal_eeprom_write_byte(10, 0xAB);
    TEST_ASSERT_EQUAL_UINT8(0xAB, hal_eeprom_read_byte(10));
}

void test_write_read_int(void) {
    hal_eeprom_write_int(20, -123456);
    TEST_ASSERT_EQUAL_INT32(-123456, hal_eeprom_read_int(20));
}

void test_mock_get_byte_matches_read(void) {
    hal_eeprom_write_byte(5, 0x55);
    TEST_ASSERT_EQUAL_UINT8(0x55, hal_mock_eeprom_get_byte(5));
}

void test_commit_sets_flag(void) {
    hal_mock_eeprom_clear_committed_flag();
    TEST_ASSERT_FALSE(hal_mock_eeprom_was_committed());
    hal_eeprom_commit();
    TEST_ASSERT_TRUE(hal_mock_eeprom_was_committed());
}

void test_clear_committed_flag(void) {
    hal_eeprom_commit();
    hal_mock_eeprom_clear_committed_flag();
    TEST_ASSERT_FALSE(hal_mock_eeprom_was_committed());
}

void test_unwritten_address_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0, hal_eeprom_read_byte(100));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_type);
    RUN_TEST(test_init_sets_size_at24c256);
    RUN_TEST(test_write_read_byte);
    RUN_TEST(test_write_read_int);
    RUN_TEST(test_mock_get_byte_matches_read);
    RUN_TEST(test_commit_sets_flag);
    RUN_TEST(test_clear_committed_flag);
    RUN_TEST(test_unwritten_address_is_zero);
    return UNITY_END();
}
