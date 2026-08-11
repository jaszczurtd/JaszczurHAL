#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/hal_sdlogger.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_eeprom_reset();
  hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0x50);
  hal_mock_sdlogger_reset();
  hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_log_init_uses_eeprom_number_and_commits_next(void) {
  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, 7);
  hal_mock_eeprom_clear_committed_flag();

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_init_ex(5));
  TEST_ASSERT_TRUE(hal_sdlogger_is_initialized());
  TEST_ASSERT_EQUAL_STRING("log00007.txt", hal_mock_sdlogger_log_filename());
  TEST_ASSERT_EQUAL_INT(8, hal_sdlogger_get_log_number());
  TEST_ASSERT_TRUE(hal_mock_eeprom_was_committed());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_sdlogger_sd_begin_count());
}

void test_log_init_keeps_filename_in_8dot3_form(void) {
  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, 100000);
  hal_mock_eeprom_clear_committed_flag();

  TEST_ASSERT_TRUE(hal_sdlogger_init(5));
  TEST_ASSERT_EQUAL_STRING("log00000.txt", hal_mock_sdlogger_log_filename());
  TEST_ASSERT_EQUAL_INT(100001, hal_sdlogger_get_log_number());
  TEST_ASSERT_TRUE(hal_mock_eeprom_was_committed());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_close());
  hal_mock_sdlogger_reset();
  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, -1);
  hal_mock_eeprom_clear_committed_flag();

  TEST_ASSERT_TRUE(hal_sdlogger_init(5));
  TEST_ASSERT_EQUAL_STRING("log00000.txt", hal_mock_sdlogger_log_filename());
  TEST_ASSERT_EQUAL_INT(0, hal_sdlogger_get_log_number());
  TEST_ASSERT_TRUE(hal_mock_eeprom_was_committed());
}

void test_log_append_flushes_on_interval_and_close_flushes_remaining(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_init_ex(5));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_append("first"));
  TEST_ASSERT_EQUAL_STRING("", hal_mock_sdlogger_log_content());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_sdlogger_log_flush_count());

  hal_mock_advance_millis(HAL_SDLOGGER_WRITE_INTERVAL_MS);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_append("second"));
  TEST_ASSERT_EQUAL_STRING("first\nsecond\n", hal_mock_sdlogger_log_content());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_sdlogger_log_flush_count());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_append("third"));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_close());
  TEST_ASSERT_FALSE(hal_sdlogger_is_initialized());
  TEST_ASSERT_TRUE(hal_mock_sdlogger_log_was_closed());
  TEST_ASSERT_EQUAL_STRING("first\nsecond\nthird\n",
                           hal_mock_sdlogger_log_content());
  TEST_ASSERT_EQUAL_UINT32(2u, hal_mock_sdlogger_log_flush_count());
}

void test_log_operations_report_uninitialized_and_overflow(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_sdlogger_append("before init"));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_sdlogger_close());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_init_ex(5));
  char too_large[HAL_SDLOGGER_LOG_BUFFER_SIZE];
  memset(too_large, 'x', sizeof(too_large) - 1u);
  too_large[sizeof(too_large) - 1u] = '\0';

  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_sdlogger_append(too_large));
  TEST_ASSERT_EQUAL_STRING("", hal_mock_sdlogger_log_content());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_close());
}

void test_crash_init_writes_filename_and_corresponding_log_line(void) {
  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, 4);
  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR, 2);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_crash_init_ex("boot", 5));
  TEST_ASSERT_TRUE(hal_sdlogger_crash_is_initialized());
  TEST_ASSERT_EQUAL_STRING("wd000002.txt", hal_mock_sdlogger_crash_filename());
  TEST_ASSERT_EQUAL_INT(3, hal_sdlogger_get_crash_number());
  TEST_ASSERT_TRUE(
      strstr(hal_mock_sdlogger_crash_content(), "crash tag: boot") != NULL);
  TEST_ASSERT_TRUE(strstr(hal_mock_sdlogger_crash_content(),
                          "corresponded log file: log00003.txt") != NULL);
}

void test_crash_report_formats_and_close_flushes(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_crash_init_ex(NULL, 5));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_crash_report("fault %d", 42));
  TEST_ASSERT_TRUE(strstr(hal_mock_sdlogger_crash_content(), "fault 42") !=
                   NULL);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_crash_close());
  TEST_ASSERT_FALSE(hal_sdlogger_crash_is_initialized());
  TEST_ASSERT_TRUE(hal_mock_sdlogger_crash_was_closed());
  TEST_ASSERT_TRUE(hal_mock_sdlogger_crash_flush_count() >= 2u);
}

void test_crash_operations_report_uninitialized_and_invalid_format(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_sdlogger_crash_append("before init"));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_sdlogger_crash_close());
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_sdlogger_crash_report(NULL));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_crash_init_ex(NULL, 5));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_sdlogger_crash_report(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sdlogger_crash_close());
}

void test_sd_begin_failure_rejects_log_init(void) {
  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, 7);
  hal_mock_eeprom_clear_committed_flag();
  hal_mock_eeprom_clear_write_count();
  hal_mock_sdlogger_set_sd_begin_result(false);

  TEST_ASSERT_EQUAL_INT(HAL_EBUS, hal_sdlogger_init_ex(5));
  TEST_ASSERT_FALSE(hal_sdlogger_init(5));
  TEST_ASSERT_FALSE(hal_sdlogger_is_initialized());
  TEST_ASSERT_EQUAL_INT(7, hal_sdlogger_get_log_number());
  TEST_ASSERT_FALSE(hal_mock_eeprom_was_committed());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_file_open_failure_rejects_log_init(void) {
  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_LOGGER_ADDR, 7);
  hal_mock_eeprom_clear_committed_flag();
  hal_mock_eeprom_clear_write_count();
  hal_mock_sdlogger_set_log_open_result(false);

  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_sdlogger_init_ex(5));
  TEST_ASSERT_FALSE(hal_sdlogger_init(5));
  TEST_ASSERT_FALSE(hal_sdlogger_is_initialized());
  TEST_ASSERT_EQUAL_INT(7, hal_sdlogger_get_log_number());
  TEST_ASSERT_FALSE(hal_mock_eeprom_was_committed());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_sd_begin_failure_rejects_crash_init_without_counter_write(void) {
  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR, 9);
  hal_mock_eeprom_clear_committed_flag();
  hal_mock_eeprom_clear_write_count();
  hal_mock_sdlogger_set_sd_begin_result(false);

  TEST_ASSERT_EQUAL_INT(HAL_EBUS, hal_sdlogger_crash_init_ex("boot", 5));
  TEST_ASSERT_FALSE(hal_sdlogger_crash_init("boot", 5));
  TEST_ASSERT_FALSE(hal_sdlogger_crash_is_initialized());
  TEST_ASSERT_EQUAL_INT(9, hal_sdlogger_get_crash_number());
  TEST_ASSERT_FALSE(hal_mock_eeprom_was_committed());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_file_open_failure_rejects_crash_init_without_counter_write(void) {
  hal_eeprom_write_int(HAL_SDLOGGER_EEPROM_CRASH_ADDR, 9);
  hal_mock_eeprom_clear_committed_flag();
  hal_mock_eeprom_clear_write_count();
  hal_mock_sdlogger_set_crash_open_result(false);

  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_sdlogger_crash_init_ex("boot", 5));
  TEST_ASSERT_FALSE(hal_sdlogger_crash_init("boot", 5));
  TEST_ASSERT_FALSE(hal_sdlogger_crash_is_initialized());
  TEST_ASSERT_EQUAL_INT(9, hal_sdlogger_get_crash_number());
  TEST_ASSERT_FALSE(hal_mock_eeprom_was_committed());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_log_init_uses_eeprom_number_and_commits_next);
  RUN_TEST(test_log_init_keeps_filename_in_8dot3_form);
  RUN_TEST(test_log_append_flushes_on_interval_and_close_flushes_remaining);
  RUN_TEST(test_log_operations_report_uninitialized_and_overflow);
  RUN_TEST(test_crash_init_writes_filename_and_corresponding_log_line);
  RUN_TEST(test_crash_report_formats_and_close_flushes);
  RUN_TEST(test_crash_operations_report_uninitialized_and_invalid_format);
  RUN_TEST(test_sd_begin_failure_rejects_log_init);
  RUN_TEST(test_file_open_failure_rejects_log_init);
  RUN_TEST(test_sd_begin_failure_rejects_crash_init_without_counter_write);
  RUN_TEST(test_file_open_failure_rejects_crash_init_without_counter_write);
  return UNITY_END();
}
