#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/ota/hal_ota.h"
#include "utils/unity.h"

#include <string.h>

static int s_start_calls = 0;
static int s_end_calls = 0;
static int s_progress_calls = 0;
static int s_error_calls = 0;

static hal_ota_command_t s_last_command = HAL_OTA_COMMAND_UNKNOWN;
static hal_ota_error_t s_last_error = HAL_OTA_ERROR_UNKNOWN;
static uint32_t s_last_progress = 0;
static uint32_t s_last_total = 0;
static int s_user_match_count = 0;

static int s_user_token = 1234;

static void on_start(hal_ota_command_t command, void *user) {
  s_start_calls++;
  s_last_command = command;
  if (user == &s_user_token) {
    s_user_match_count++;
  }
}

static void on_end(void *user) {
  s_end_calls++;
  if (user == &s_user_token) {
    s_user_match_count++;
  }
}

static void on_progress(uint32_t progress, uint32_t total, void *user) {
  s_progress_calls++;
  s_last_progress = progress;
  s_last_total = total;
  if (user == &s_user_token) {
    s_user_match_count++;
  }
}

static void on_error(hal_ota_error_t error, void *user) {
  s_error_calls++;
  s_last_error = error;
  if (user == &s_user_token) {
    s_user_match_count++;
  }
}

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_ota_reset();

  s_start_calls = 0;
  s_end_calls = 0;
  s_progress_calls = 0;
  s_error_calls = 0;

  s_last_command = HAL_OTA_COMMAND_UNKNOWN;
  s_last_error = HAL_OTA_ERROR_UNKNOWN;
  s_last_progress = 0;
  s_last_total = 0;
  s_user_match_count = 0;
}

void tearDown(void) {}

void test_configuration_and_begin(void) {
  TEST_ASSERT_TRUE(hal_ota_set_port(3232u));
  TEST_ASSERT_TRUE(hal_ota_set_hostname("timer-ntp"));
  TEST_ASSERT_TRUE(hal_ota_set_password("secret"));

  TEST_ASSERT_EQUAL_UINT16(3232u, hal_mock_ota_get_port());
  TEST_ASSERT_EQUAL_STRING("timer-ntp", hal_mock_ota_get_hostname());
  TEST_ASSERT_EQUAL_STRING("secret", hal_mock_ota_get_password());

  TEST_ASSERT_TRUE(hal_ota_on_start(on_start, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_on_end(on_end, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_on_progress(on_progress, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_on_error(on_error, &s_user_token));

  TEST_ASSERT_TRUE(hal_ota_begin());
  TEST_ASSERT_TRUE(hal_ota_is_started());
}

void test_handle_dispatches_injected_events(void) {
  TEST_ASSERT_TRUE(hal_ota_on_start(on_start, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_on_end(on_end, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_on_progress(on_progress, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_on_error(on_error, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_begin());

  hal_mock_ota_inject_start(HAL_OTA_COMMAND_FILESYSTEM);
  hal_mock_ota_inject_progress(37u, 100u);
  hal_mock_ota_inject_error(HAL_OTA_ERROR_RECEIVE);
  hal_mock_ota_inject_end();

  hal_ota_handle();

  TEST_ASSERT_EQUAL_INT(1, s_start_calls);
  TEST_ASSERT_EQUAL_INT(1, s_end_calls);
  TEST_ASSERT_EQUAL_INT(1, s_progress_calls);
  TEST_ASSERT_EQUAL_INT(1, s_error_calls);
  TEST_ASSERT_EQUAL_INT(4, s_user_match_count);

  TEST_ASSERT_EQUAL_INT((int)HAL_OTA_COMMAND_FILESYSTEM, (int)s_last_command);
  TEST_ASSERT_EQUAL_UINT32(37u, s_last_progress);
  TEST_ASSERT_EQUAL_UINT32(100u, s_last_total);
  TEST_ASSERT_EQUAL_INT((int)HAL_OTA_ERROR_RECEIVE, (int)s_last_error);

  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_ota_get_handle_count());
}

void test_callbacks_can_be_replaced_and_unregistered(void) {
  TEST_ASSERT_TRUE(hal_ota_on_start(on_start, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_begin());

  hal_mock_ota_inject_start(HAL_OTA_COMMAND_SKETCH);
  hal_ota_handle();
  TEST_ASSERT_EQUAL_INT(1, s_start_calls);

  TEST_ASSERT_TRUE(hal_ota_on_start(NULL, NULL));
  hal_mock_ota_inject_start(HAL_OTA_COMMAND_FILESYSTEM);
  hal_ota_handle();
  TEST_ASSERT_EQUAL_INT(1, s_start_calls);

  TEST_ASSERT_TRUE(hal_ota_on_start(on_start, &s_user_token));
  hal_mock_ota_inject_start(HAL_OTA_COMMAND_FILESYSTEM);
  hal_ota_handle();

  TEST_ASSERT_EQUAL_INT(2, s_start_calls);
  TEST_ASSERT_EQUAL_INT((int)HAL_OTA_COMMAND_FILESYSTEM, (int)s_last_command);
  TEST_ASSERT_EQUAL_INT(2, s_user_match_count);
}

void test_rebegin_clears_queued_events(void) {
  TEST_ASSERT_TRUE(hal_ota_on_start(on_start, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_on_error(on_error, &s_user_token));
  TEST_ASSERT_TRUE(hal_ota_begin());

  hal_mock_ota_inject_start(HAL_OTA_COMMAND_SKETCH);
  hal_mock_ota_inject_error(HAL_OTA_ERROR_CONNECT);

  TEST_ASSERT_TRUE(hal_ota_begin());
  hal_ota_handle();

  TEST_ASSERT_EQUAL_INT(0, s_start_calls);
  TEST_ASSERT_EQUAL_INT(0, s_error_calls);
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_ota_get_handle_count());
}

void test_invalid_inputs_and_begin_failure(void) {
  TEST_ASSERT_FALSE(hal_ota_set_port(0u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_ota_set_hostname(NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_ota_set_password(NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  TEST_ASSERT_TRUE(hal_ota_on_start(on_start, NULL));
  TEST_ASSERT_TRUE(hal_ota_on_start(NULL, NULL));

  hal_mock_ota_set_begin_result(false);
  TEST_ASSERT_FALSE(hal_ota_begin());
  TEST_ASSERT_FALSE(hal_ota_is_started());

  hal_mock_ota_inject_start(HAL_OTA_COMMAND_SKETCH);
  hal_ota_handle();
  TEST_ASSERT_EQUAL_INT(0, s_start_calls);
}

void test_boot_status_api_is_status_based(void) {
  hal_ota_boot_info_t info = {};

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ota_get_boot_info_ex(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ota_get_boot_info_ex(&info));
  TEST_ASSERT_EQUAL_INT(HAL_OTA_BOOT_STABLE, info.mode);
  TEST_ASSERT_EQUAL_UINT8(3u, info.max_attempts);
  TEST_ASSERT_EQUAL_STRING("mock", info.program_version);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ota_confirm_boot_ex());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_configuration_and_begin);
  RUN_TEST(test_handle_dispatches_injected_events);
  RUN_TEST(test_callbacks_can_be_replaced_and_unregistered);
  RUN_TEST(test_rebegin_clears_queued_events);
  RUN_TEST(test_invalid_inputs_and_begin_failure);
  RUN_TEST(test_boot_status_api_is_status_based);
  return UNITY_END();
}
