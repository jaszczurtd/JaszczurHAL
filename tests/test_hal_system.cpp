#include "utils/unity.h"
#include "hal/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_mock_set_millis(0);
    hal_mock_watchdog_reset_flag();
    hal_mock_set_caused_reboot(false);
    hal_mock_set_free_heap(256 * 1024);
    hal_mock_set_chip_temp(25.0f);
    hal_mock_bootloader_reset_flag();
    hal_mock_reset_device_uid();
}

void tearDown(void) {}

void test_delay_ms_updates_millis_and_micros(void) {
    hal_delay_ms(10);
    TEST_ASSERT_EQUAL_UINT32(10, hal_millis());
    TEST_ASSERT_EQUAL_UINT32(10000, hal_micros());
    TEST_ASSERT_EQUAL_UINT64(10000, hal_micros64());
}

void test_delay_us_updates_micros_only(void) {
    hal_mock_set_millis(5);
    hal_delay_us(250);

    TEST_ASSERT_EQUAL_UINT32(5, hal_millis());
    TEST_ASSERT_EQUAL_UINT32(5250, hal_micros());
}

void test_micros_helpers_keep_millis_in_sync(void) {
    hal_mock_set_micros(12345);
    TEST_ASSERT_EQUAL_UINT32(12, hal_millis());
    TEST_ASSERT_EQUAL_UINT32(12345, hal_micros());

    hal_mock_advance_micros(2000);
    TEST_ASSERT_EQUAL_UINT32(14, hal_millis());
    TEST_ASSERT_EQUAL_UINT32(14345, hal_micros());
}

void test_watchdog_feed_and_reboot_flag(void) {
    TEST_ASSERT_FALSE(hal_mock_watchdog_was_fed());
    hal_watchdog_feed();
    TEST_ASSERT_TRUE(hal_mock_watchdog_was_fed());

    TEST_ASSERT_FALSE(hal_watchdog_caused_reboot());
    hal_mock_set_caused_reboot(true);
    TEST_ASSERT_TRUE(hal_watchdog_caused_reboot());
}

void test_heap_and_chip_temp_helpers(void) {
    hal_mock_set_free_heap(123456);
    TEST_ASSERT_EQUAL_UINT32(123456, hal_get_free_heap());

    hal_mock_set_chip_temp(41.75f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 41.75f, hal_read_chip_temp());
}

void test_enter_bootloader_sets_mock_flag(void) {
    TEST_ASSERT_FALSE(hal_mock_bootloader_was_requested());
    hal_enter_bootloader();
    TEST_ASSERT_TRUE(hal_mock_bootloader_was_requested());
}

void test_constrain_clamps_to_range(void) {
    TEST_ASSERT_EQUAL_INT32(0, hal_constrain(-5, 0, 100));
    TEST_ASSERT_EQUAL_INT32(42, hal_constrain(42, 0, 100));
    TEST_ASSERT_EQUAL_INT32(100, hal_constrain(150, 0, 100));
}

void test_constrain_accepts_float(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, hal_constrain(-2.5f, 0.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, hal_constrain(0.25f, 0.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, hal_constrain(3.0f, 0.0f, 1.0f));
}

void test_map_maps_integer_ranges(void) {
    TEST_ASSERT_EQUAL_INT32(0, hal_map(0, 0, 100, 0, 1000));
    TEST_ASSERT_EQUAL_INT32(500, hal_map(50, 0, 100, 0, 1000));
    TEST_ASSERT_EQUAL_INT32(1000, hal_map(100, 0, 100, 0, 1000));
}

void test_map_equal_in_range_returns_out_min(void) {
    /* in_min == in_max: guard must return out_min, not divide by zero. */
    TEST_ASSERT_EQUAL_INT32(7, hal_map(5, 3, 3, 7, 99));
}

void test_nonull_macro_accepts_non_null(void) {
    /* NONULL must not branch when the pointer is valid. */
    int dummy = 42;
    int *p = &dummy;
    NONULL(p);
    TEST_ASSERT_EQUAL_INT(42, *p);
    return;
error:
    TEST_FAIL_MESSAGE("NONULL jumped to error on non-null pointer");
}

void test_nonull_macro_jumps_on_null(void) {
    void *p = NULL;
    int reached_error = 0;
    do {
        NONULL(p);
        TEST_FAIL_MESSAGE("NONULL did not jump on null pointer");
    } while (0);
    goto done;
error:
    reached_error = 1;
done:
    TEST_ASSERT_EQUAL_INT(1, reached_error);
}

void test_u32_to_bytes_be_converts_correctly(void) {
    uint8_t bytes[4] = {0u, 0u, 0u, 0u};
    hal_u32_to_bytes_be(0x12345678u, bytes);

    TEST_ASSERT_EQUAL_HEX8(0x12u, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34u, bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x56u, bytes[2]);
    TEST_ASSERT_EQUAL_HEX8(0x78u, bytes[3]);
}

void test_get_device_uid_returns_default_mock_pattern(void) {
    uint8_t uid[HAL_DEVICE_UID_BYTES] = {0};
    hal_get_device_uid(uid);
    /* Default mock UID is {0xE6,0x61,0xA4,0xD1,0x23,0x45,0x67,0xAB}. */
    TEST_ASSERT_EQUAL_HEX8(0xE6, uid[0]);
    TEST_ASSERT_EQUAL_HEX8(0x61, uid[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, uid[7]);
}

void test_get_device_uid_reflects_injected_value(void) {
    const uint8_t custom[HAL_DEVICE_UID_BYTES] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE
    };
    hal_mock_set_device_uid(custom);

    uint8_t out[HAL_DEVICE_UID_BYTES] = {0};
    hal_get_device_uid(out);

    for (size_t i = 0; i < HAL_DEVICE_UID_BYTES; ++i) {
        TEST_ASSERT_EQUAL_HEX8(custom[i], out[i]);
    }
}

void test_get_device_uid_null_arg_is_safe(void) {
    hal_get_device_uid(NULL);
    /* No crash, no observable effect. */
    TEST_PASS();
}

void test_get_device_uid_hex_formats_default_as_uppercase(void) {
    char buf[HAL_DEVICE_UID_HEX_BUF_SIZE] = {0};
    TEST_ASSERT_TRUE(hal_get_device_uid_hex(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("E661A4D1234567AB", buf);
}

void test_get_device_uid_hex_formats_injected_value(void) {
    const uint8_t custom[HAL_DEVICE_UID_BYTES] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
    };
    hal_mock_set_device_uid(custom);

    char buf[HAL_DEVICE_UID_HEX_BUF_SIZE] = {0};
    TEST_ASSERT_TRUE(hal_get_device_uid_hex(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("0123456789ABCDEF", buf);
}

void test_get_device_uid_hex_rejects_small_buffer(void) {
    char buf[HAL_DEVICE_UID_HEX_BUF_SIZE - 1u] = {0};
    TEST_ASSERT_FALSE(hal_get_device_uid_hex(buf, sizeof(buf)));
}

void test_get_device_uid_hex_null_buffer_is_safe(void) {
    TEST_ASSERT_FALSE(hal_get_device_uid_hex(NULL, 0));
    TEST_ASSERT_FALSE(hal_get_device_uid_hex(NULL, HAL_DEVICE_UID_HEX_BUF_SIZE));
}

void test_countof_returns_static_array_size(void) {
    int values[5] = {0};
    TEST_ASSERT_EQUAL_UINT32(5u, (uint32_t)COUNTOF(values));

    struct pair_t {
        uint8_t a;
        uint8_t b;
    };
    struct pair_t pairs[3] = {{0u, 0u}, {1u, 1u}, {2u, 2u}};
    TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)COUNTOF(pairs));

    char word[] = "AB";
    TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)COUNTOF(word));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_delay_ms_updates_millis_and_micros);
    RUN_TEST(test_delay_us_updates_micros_only);
    RUN_TEST(test_micros_helpers_keep_millis_in_sync);
    RUN_TEST(test_watchdog_feed_and_reboot_flag);
    RUN_TEST(test_heap_and_chip_temp_helpers);
    RUN_TEST(test_enter_bootloader_sets_mock_flag);
    RUN_TEST(test_constrain_clamps_to_range);
    RUN_TEST(test_constrain_accepts_float);
    RUN_TEST(test_map_maps_integer_ranges);
    RUN_TEST(test_map_equal_in_range_returns_out_min);
    RUN_TEST(test_nonull_macro_accepts_non_null);
    RUN_TEST(test_nonull_macro_jumps_on_null);
    RUN_TEST(test_u32_to_bytes_be_converts_correctly);
    RUN_TEST(test_get_device_uid_returns_default_mock_pattern);
    RUN_TEST(test_get_device_uid_reflects_injected_value);
    RUN_TEST(test_get_device_uid_null_arg_is_safe);
    RUN_TEST(test_get_device_uid_hex_formats_default_as_uppercase);
    RUN_TEST(test_get_device_uid_hex_formats_injected_value);
    RUN_TEST(test_get_device_uid_hex_rejects_small_buffer);
    RUN_TEST(test_get_device_uid_hex_null_buffer_is_safe);
    RUN_TEST(test_countof_returns_static_array_size);
    return UNITY_END();
}
