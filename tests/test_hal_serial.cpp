#include "utils/unity.h"
#include "hal/hal_serial.h"
#include "hal/impl/.mock/hal_mock.h"

#include <stdio.h>
#include <string.h>

static bool fixed_ts_hook(char *out, size_t out_size, void *user) {
    (void)user;
    if (!out || out_size == 0) {
        return false;
    }
    snprintf(out, out_size, "T+1.234s");
    return true;
}

void setUp(void) {
    hal_debug_init(115200);
    hal_mock_serial_reset();
    hal_debug_set_timestamp_hook(NULL, NULL);
}

void tearDown(void) {}

void test_println_captured(void) {
    hal_serial_println("hello");
    TEST_ASSERT_EQUAL_STRING("hello", hal_mock_serial_last_line());
}

void test_println_overwrites_previous(void) {
    hal_serial_println("first");
    hal_serial_println("second");
    TEST_ASSERT_EQUAL_STRING("second", hal_mock_serial_last_line());
}

void test_reset_clears_last_line(void) {
    hal_serial_println("something");
    hal_mock_serial_reset();
    TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
}

void test_deb_captured(void) {
    hal_deb("deb_test");
    TEST_ASSERT_EQUAL_STRING("deb_test", hal_mock_deb_last_line());
}

void test_derr_without_timestamp_hook(void) {
    hal_derr("plain error");
    const char *line = hal_mock_serial_last_line();
    TEST_ASSERT_NOT_NULL(strstr(line, "plain error"));
    TEST_ASSERT_NOT_NULL(strstr(line, "ERROR!"));
}

void test_derr_with_timestamp_hook(void) {
    hal_debug_set_timestamp_hook(fixed_ts_hook, NULL);
    hal_derr("hooked error");
    const char *line = hal_mock_serial_last_line();
    TEST_ASSERT_NOT_NULL(strstr(line, "[T+1.234s]"));
    TEST_ASSERT_NOT_NULL(strstr(line, "hooked error"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_println_captured);
    RUN_TEST(test_println_overwrites_previous);
    RUN_TEST(test_reset_clears_last_line);
    RUN_TEST(test_deb_captured);
    RUN_TEST(test_derr_without_timestamp_hook);
    RUN_TEST(test_derr_with_timestamp_hook);
    return UNITY_END();
}
