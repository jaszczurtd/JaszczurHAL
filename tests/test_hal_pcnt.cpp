#include "utils/unity.h"
#include "hal/hal_pcnt.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_pcnt_init(0, 10, HAL_PCNT_EDGE_RISING);
    hal_pcnt_init(1, 11, HAL_PCNT_EDGE_BOTH);
}
void tearDown(void) {}

void test_supported_and_channel_count(void) {
    TEST_ASSERT_TRUE(hal_pcnt_is_supported());
    TEST_ASSERT_EQUAL_UINT8(4, hal_pcnt_channel_count());
}

void test_init_valid_and_invalid(void) {
    TEST_ASSERT_TRUE(hal_pcnt_init(0, 5, HAL_PCNT_EDGE_RISING));
    TEST_ASSERT_FALSE(hal_pcnt_init(99, 5, HAL_PCNT_EDGE_RISING));
}

void test_init_records_pin_and_edge(void) {
    hal_pcnt_init(0, 7, HAL_PCNT_EDGE_FALLING);
    TEST_ASSERT_EQUAL_UINT8(7, hal_mock_pcnt_get_pin(0));
    TEST_ASSERT_EQUAL_INT(HAL_PCNT_EDGE_FALLING, hal_mock_pcnt_get_edge(0));
}

void test_inject_and_read(void) {
    hal_mock_pcnt_inject(0, 123);
    hal_mock_pcnt_inject(0, 7);
    TEST_ASSERT_EQUAL_UINT32(130, hal_pcnt_read(0));
}

void test_reset(void) {
    hal_mock_pcnt_inject(1, 50);
    TEST_ASSERT_EQUAL_UINT32(50, hal_pcnt_read(1));
    hal_pcnt_reset(1);
    TEST_ASSERT_EQUAL_UINT32(0, hal_pcnt_read(1));
}

void test_read_and_reset(void) {
    hal_mock_pcnt_inject(0, 42);
    TEST_ASSERT_EQUAL_UINT32(42, hal_pcnt_read_and_reset(0));
    TEST_ASSERT_EQUAL_UINT32(0, hal_pcnt_read(0));
}

void test_uninitialised_channel_reads_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, hal_pcnt_read(3));   // valid but not init in setUp
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_supported_and_channel_count);
    RUN_TEST(test_init_valid_and_invalid);
    RUN_TEST(test_init_records_pin_and_edge);
    RUN_TEST(test_inject_and_read);
    RUN_TEST(test_reset);
    RUN_TEST(test_read_and_reset);
    RUN_TEST(test_uninitialised_channel_reads_zero);
    return UNITY_END();
}
