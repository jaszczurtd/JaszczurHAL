#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/hal_littlefs.h"
#include "utils/unity.h"

#include <string.h>

static uint32_t s_progress_calls = 0u;
static void *s_progress_ctx = NULL;

static void progress_callback(void *ctx) {
  s_progress_calls++;
  s_progress_ctx = ctx;
}

void setUp(void) {
  s_progress_calls = 0u;
  s_progress_ctx = NULL;
  hal_mock_serial_reset();
  hal_mock_littlefs_reset();
}

void tearDown(void) {}

void test_mount_stats_and_unmount(void) {
  hal_mock_littlefs_set_total_bytes(4096u);
  hal_mock_littlefs_set_used_bytes(1024u);

  TEST_ASSERT_TRUE(hal_littlefs_begin());
  TEST_ASSERT_TRUE(hal_littlefs_is_mounted());
  TEST_ASSERT_EQUAL_UINT32(4096u, (uint32_t)hal_littlefs_total_bytes());
  TEST_ASSERT_EQUAL_UINT32(1024u, (uint32_t)hal_littlefs_used_bytes());

  hal_littlefs_end();
  TEST_ASSERT_FALSE(hal_littlefs_is_mounted());
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)hal_littlefs_total_bytes());
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)hal_littlefs_used_bytes());
}

void test_exists_and_remove_flow(void) {
  TEST_ASSERT_TRUE(hal_littlefs_begin());

  hal_mock_littlefs_set_exists("/cfg.json", true);
  TEST_ASSERT_TRUE(hal_littlefs_exists("/cfg.json"));
  TEST_ASSERT_TRUE(hal_littlefs_remove("/cfg.json"));
  TEST_ASSERT_FALSE(hal_littlefs_exists("/cfg.json"));
}

void test_invalid_inputs_and_mount_failures(void) {
  TEST_ASSERT_FALSE(hal_littlefs_exists(NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_littlefs_remove(NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_littlefs_exists("/not-mounted"));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  hal_mock_littlefs_set_begin_result(false);
  TEST_ASSERT_FALSE(hal_littlefs_begin());
}

void test_format_clears_files_and_unmounts(void) {
  TEST_ASSERT_TRUE(hal_littlefs_begin());
  hal_mock_littlefs_set_exists("/tmp.txt", true);
  hal_mock_littlefs_set_used_bytes(512u);

  TEST_ASSERT_TRUE(hal_littlefs_format());
  TEST_ASSERT_FALSE(hal_littlefs_is_mounted());

  TEST_ASSERT_TRUE(hal_littlefs_begin());
  TEST_ASSERT_FALSE(hal_littlefs_exists("/tmp.txt"));
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)hal_littlefs_used_bytes());
}

void test_progress_callback_runs_during_format(void) {
  uint32_t marker = 0x5678u;

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_littlefs_set_progress_callback(progress_callback, &marker));

  TEST_ASSERT_TRUE(hal_littlefs_format());
  TEST_ASSERT_EQUAL_UINT32(2u, s_progress_calls);
  TEST_ASSERT_EQUAL_PTR(&marker, s_progress_ctx);
}

void test_format_failure_preserves_mount_and_data(void) {
  TEST_ASSERT_TRUE(hal_littlefs_begin());
  hal_mock_littlefs_set_exists("/keep.cfg", true);
  hal_mock_littlefs_set_used_bytes(1536u);

  hal_mock_littlefs_set_format_result(false);
  TEST_ASSERT_FALSE(hal_littlefs_format());

  TEST_ASSERT_TRUE(hal_littlefs_is_mounted());
  TEST_ASSERT_TRUE(hal_littlefs_exists("/keep.cfg"));
  TEST_ASSERT_EQUAL_UINT32(1536u, (uint32_t)hal_littlefs_used_bytes());
}

void test_remove_missing_path_returns_false(void) {
  TEST_ASSERT_TRUE(hal_littlefs_begin());
  TEST_ASSERT_FALSE(hal_littlefs_remove("/missing.txt"));
}

/* ---- Status-returning (_ex) API coverage ---- */

void test_ex_lifecycle_and_status(void) {
  size_t bytes = 123u;
  /* Unmounted: byte queries and path ops report HAL_EUNINIT. */
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_littlefs_total_bytes_ex(&bytes));
  TEST_ASSERT_EQUAL_INT(0u, bytes);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_littlefs_exists_ex("/cfg"));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_begin_ex());
  hal_mock_littlefs_set_total_bytes(4096u);
  hal_mock_littlefs_set_used_bytes(1024u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_total_bytes_ex(&bytes));
  TEST_ASSERT_EQUAL_INT(4096u, bytes);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_used_bytes_ex(&bytes));
  TEST_ASSERT_EQUAL_INT(1024u, bytes);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_end());
}

void test_ex_path_validation_and_lookup(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_littlefs_exists_ex(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_littlefs_exists_ex(""));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_littlefs_total_bytes_ex(NULL));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_begin_ex());
  hal_mock_littlefs_set_exists("/present.txt", true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_exists_ex("/present.txt"));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_littlefs_exists_ex("/missing.txt"));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_remove_ex("/present.txt"));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_littlefs_remove_ex("/present.txt"));
}

void test_ex_begin_failure_reports_io(void) {
  hal_mock_littlefs_set_begin_result(false);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_littlefs_begin_ex());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_mount_stats_and_unmount);
  RUN_TEST(test_exists_and_remove_flow);
  RUN_TEST(test_invalid_inputs_and_mount_failures);
  RUN_TEST(test_format_clears_files_and_unmounts);
  RUN_TEST(test_progress_callback_runs_during_format);
  RUN_TEST(test_format_failure_preserves_mount_and_data);
  RUN_TEST(test_remove_missing_path_returns_false);
  RUN_TEST(test_ex_lifecycle_and_status);
  RUN_TEST(test_ex_path_validation_and_lookup);
  RUN_TEST(test_ex_begin_failure_reports_io);
  return UNITY_END();
}
