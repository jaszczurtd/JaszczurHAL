#include "utils/unity.h"
#include "hal/hal_bits.h"

void setUp(void) {}
void tearDown(void) {}

void test_mask_macros_work_on_u8(void) {
    uint8_t value = 0u;
    set_bit(value, 0x05u);
    TEST_ASSERT_TRUE(is_set(value, 0x01u));
    TEST_ASSERT_TRUE(is_set(value, 0x04u));

    clr_bit(value, 0x01u);
    TEST_ASSERT_EQUAL_UINT8(0x04u, value);
}

void test_mask_macros_work_on_u16(void) {
    uint16_t value = 0u;
    set_bit(value, 0x0201u);
    TEST_ASSERT_TRUE(is_set(value, 0x0200u));
    TEST_ASSERT_TRUE(is_set(value, 0x0001u));

    clr_bit(value, 0x0001u);
    TEST_ASSERT_EQUAL_UINT16(0x0200u, value);
}

void test_arduino_bit_macros_work_on_u32(void) {
    uint32_t value = 0u;

    bitSet(value, 9u);
    TEST_ASSERT_EQUAL_UINT32(0x00000200u, value);
    TEST_ASSERT_EQUAL_UINT32(1u, bitRead(value, 9u));

    bitClear(value, 9u);
    TEST_ASSERT_EQUAL_UINT32(0u, value);
    TEST_ASSERT_EQUAL_UINT32(0u, bitRead(value, 9u));
}

void test_volatile_register_helpers_work(void) {
    volatile uint8_t reg8 = 0u;
    set_bit_v(&reg8, 0x03u);
    TEST_ASSERT_EQUAL_UINT8(0x03u, reg8);

    clr_bit_v(&reg8, 0x01u);
    TEST_ASSERT_EQUAL_UINT8(0x02u, reg8);

    volatile uint32_t reg32 = 0u;
    set_bit_v(&reg32, 0x80000000u);
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, reg32);

    clr_bit_v(&reg32, 0x80000000u);
    TEST_ASSERT_EQUAL_UINT32(0u, reg32);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mask_macros_work_on_u8);
    RUN_TEST(test_mask_macros_work_on_u16);
    RUN_TEST(test_arduino_bit_macros_work_on_u32);
    RUN_TEST(test_volatile_register_helpers_work);
    return UNITY_END();
}
