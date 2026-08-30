#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/hal_littlefs.h"
#include "utils/unity.h"

#include <atomic>
#include <string.h>
#include <thread>

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
  TEST_ASSERT_GREATER_THAN_UINT32(0u, s_progress_calls);
  TEST_ASSERT_EQUAL_PTR(&marker, s_progress_ctx);
}

void test_progress_callback_can_be_disabled_and_reset(void) {
  uint32_t marker = 0x1234u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_littlefs_set_progress_callback(progress_callback, &marker));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_set_progress_callback(NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_format_ex());
  TEST_ASSERT_EQUAL_UINT32(0u, s_progress_calls);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_littlefs_set_progress_callback(progress_callback, &marker));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_begin_ex());
  hal_mock_littlefs_reset();
  TEST_ASSERT_FALSE(hal_littlefs_is_mounted());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_format_ex());
  TEST_ASSERT_EQUAL_UINT32(0u, s_progress_calls);
}

void test_format_failure_best_effort_remounts_mock_state(void) {
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

void test_begin_is_idempotent_after_mount(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_begin_ex());
  hal_mock_littlefs_set_begin_result(false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_begin_ex());
  TEST_ASSERT_TRUE(hal_littlefs_is_mounted());
}

void test_begin_propagates_configuration_failure(void) {
  hal_mock_littlefs_set_begin_status(HAL_ECONFIG);
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, hal_littlefs_begin_ex());
  TEST_ASSERT_FALSE(hal_littlefs_is_mounted());
}

void test_end_failure_still_clears_mounted_state(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_begin_ex());
  hal_mock_littlefs_set_end_result(false);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_littlefs_end());
  TEST_ASSERT_FALSE(hal_littlefs_is_mounted());
}

void test_format_failure_with_failed_remount_stays_unmounted(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_begin_ex());
  hal_mock_littlefs_set_exists("/keep.cfg", true);
  hal_mock_littlefs_set_format_result(false);
  hal_mock_littlefs_set_begin_result(false);

  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_littlefs_format_ex());
  TEST_ASSERT_FALSE(hal_littlefs_is_mounted());

  hal_mock_littlefs_set_begin_result(true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_begin_ex());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_exists_ex("/keep.cfg"));
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
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_littlefs_remove_ex(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_littlefs_remove_ex(""));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_littlefs_total_bytes_ex(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_littlefs_used_bytes_ex(NULL));

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

void test_ex_unmounted_outputs_are_zeroed(void) {
  size_t total = 123u;
  size_t used = 456u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_littlefs_total_bytes_ex(&total));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_littlefs_used_bytes_ex(&used));
  TEST_ASSERT_EQUAL_size_t(0u, total);
  TEST_ASSERT_EQUAL_size_t(0u, used);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_littlefs_remove_ex("/not-mounted"));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_littlefs_end());
}

void test_public_facade_serializes_lifecycle_and_stats(void) {
  hal_mock_littlefs_set_total_bytes(4096u);
  std::atomic<bool> unexpected_status{false};

  std::thread lifecycle([&]() {
    for (size_t i = 0u; i < 500u; ++i) {
      if (hal_littlefs_begin_ex() != HAL_OK || hal_littlefs_end() != HAL_OK) {
        unexpected_status.store(true, std::memory_order_relaxed);
      }
    }
  });
  std::thread queries([&]() {
    for (size_t i = 0u; i < 500u; ++i) {
      size_t total = 0u;
      const hal_status_t total_status = hal_littlefs_total_bytes_ex(&total);
      if ((total_status != HAL_OK && total_status != HAL_EUNINIT) ||
          (total_status == HAL_OK && total != 4096u)) {
        unexpected_status.store(true, std::memory_order_relaxed);
      }
      (void)hal_littlefs_is_mounted();
    }
  });

  lifecycle.join();
  queries.join();
  TEST_ASSERT_FALSE(unexpected_status.load(std::memory_order_relaxed));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_mount_stats_and_unmount);
  RUN_TEST(test_exists_and_remove_flow);
  RUN_TEST(test_invalid_inputs_and_mount_failures);
  RUN_TEST(test_format_clears_files_and_unmounts);
  RUN_TEST(test_progress_callback_runs_during_format);
  RUN_TEST(test_progress_callback_can_be_disabled_and_reset);
  RUN_TEST(test_format_failure_best_effort_remounts_mock_state);
  RUN_TEST(test_remove_missing_path_returns_false);
  RUN_TEST(test_begin_is_idempotent_after_mount);
  RUN_TEST(test_begin_propagates_configuration_failure);
  RUN_TEST(test_end_failure_still_clears_mounted_state);
  RUN_TEST(test_format_failure_with_failed_remount_stays_unmounted);
  RUN_TEST(test_ex_lifecycle_and_status);
  RUN_TEST(test_ex_path_validation_and_lookup);
  RUN_TEST(test_ex_begin_failure_reports_io);
  RUN_TEST(test_ex_unmounted_outputs_are_zeroed);
  RUN_TEST(test_public_facade_serializes_lifecycle_and_stats);
  return UNITY_END();
}
