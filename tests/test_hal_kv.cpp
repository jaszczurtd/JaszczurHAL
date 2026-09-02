#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/hal_kv.h"
#include "utils/unity.h"

#include <atomic>
#include <thread>
#include <vector>

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_eeprom_reset();
  hal_eeprom_init(HAL_EEPROM_FLASH, 8192, 0x50);
  TEST_ASSERT_TRUE(hal_kv_init(0, 8192));
}

void tearDown(void) {
  hal_mock_kv_full_reset();
  hal_mock_eeprom_reset();
  hal_mock_debug_serial_full_reset();
}

void test_set_get_u32_and_reinit(void) {
  TEST_ASSERT_TRUE(hal_kv_set_u32(100, 0x12345678u));

  uint32_t out = 0;
  TEST_ASSERT_TRUE(hal_kv_get_u32(100, &out));
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, out);

  TEST_ASSERT_TRUE(hal_kv_init(0, 8192));
  out = 0;
  TEST_ASSERT_TRUE(hal_kv_get_u32(100, &out));
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, out);
}

void test_blob_roundtrip_and_length_query(void) {
  const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7};
  TEST_ASSERT_TRUE(hal_kv_set_blob(200, data, sizeof(data)));

  uint16_t out_len = 0;
  TEST_ASSERT_TRUE(hal_kv_get_blob(200, NULL, 0, &out_len));
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(data), out_len);

  uint8_t out[16] = {0};
  TEST_ASSERT_TRUE(hal_kv_get_blob(200, out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(data), out_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, sizeof(data));
}

void test_delete_removes_key(void) {
  TEST_ASSERT_TRUE(hal_kv_set_u32(300, 42));

  uint32_t out = 0;
  TEST_ASSERT_TRUE(hal_kv_get_u32(300, &out));
  TEST_ASSERT_EQUAL_UINT32(42u, out);

  TEST_ASSERT_TRUE(hal_kv_delete(300));
  TEST_ASSERT_FALSE(hal_kv_get_u32(300, &out));
}

void test_unchanged_value_skips_writes(void) {
  TEST_ASSERT_TRUE(hal_kv_set_u32(400, 777u));

  hal_mock_eeprom_clear_write_count();
  TEST_ASSERT_TRUE(hal_kv_set_u32(400, 777u));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
}

void test_gc_and_concurrent_updates(void) {
  std::atomic<bool> failed(false);

  auto worker = [&failed](uint16_t key, uint32_t base) {
    for (uint32_t i = 0; i < 40; i++) {
      if (!hal_kv_set_u32(key, base + i)) {
        failed.store(true);
        return;
      }
    }
  };

  std::vector<std::thread> threads;
  threads.emplace_back(worker, 501, 1000);
  threads.emplace_back(worker, 502, 2000);
  threads.emplace_back(worker, 503, 3000);

  for (auto &t : threads) {
    t.join();
  }

  TEST_ASSERT_FALSE(failed.load());

  TEST_ASSERT_TRUE(hal_kv_gc());

  uint32_t v1 = 0, v2 = 0, v3 = 0;
  TEST_ASSERT_TRUE(hal_kv_get_u32(501, &v1));
  TEST_ASSERT_TRUE(hal_kv_get_u32(502, &v2));
  TEST_ASSERT_TRUE(hal_kv_get_u32(503, &v3));
  TEST_ASSERT_EQUAL_UINT32(1039u, v1);
  TEST_ASSERT_EQUAL_UINT32(2039u, v2);
  TEST_ASSERT_EQUAL_UINT32(3039u, v3);

  hal_kv_stats_t st = {};
  TEST_ASSERT_TRUE(hal_kv_get_stats(&st));
  TEST_ASSERT_TRUE(st.generation >= 2u);
  TEST_ASSERT_TRUE(st.key_count >= 3u);
}

/* ---- Status-returning (_ex) API coverage ---- */

void test_ex_u32_roundtrip_and_status(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(200, 0xCAFEBABEu));
  uint32_t out = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(200, &out));
  TEST_ASSERT_EQUAL_HEX32(0xCAFEBABEu, out);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_kv_get_u32_ex(200, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_kv_get_u32_ex(999, &out));
}

void test_ex_blob_reports_overflow_and_length(void) {
  const uint8_t payload[5] = {1, 2, 3, 4, 5};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_set_blob_ex(300, payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_kv_set_blob_ex(301, NULL, 4));

  /* Length-only query. */
  uint16_t len = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_blob_ex(300, NULL, 0, &len));
  TEST_ASSERT_EQUAL_UINT16(sizeof(payload), len);

  /* Too-small buffer is reported distinctly from a miss. */
  uint8_t small[2] = {0};
  len = 0;
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_kv_get_blob_ex(300, small, sizeof(small), &len));
  TEST_ASSERT_EQUAL_UINT16(sizeof(payload), len);

  uint8_t big[8] = {0};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_get_blob_ex(300, big, sizeof(big), &len));
  TEST_ASSERT_EQUAL_UINT16(sizeof(payload), len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, big, sizeof(payload));

  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_kv_get_blob_ex(999, big, sizeof(big), &len));
}

void test_ex_stats_and_commit_status(void) {
  hal_kv_stats_t stats;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_stats_ex(&stats));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_kv_get_stats_ex(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_commit_ex());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(true));
}

void test_ex_initialization_and_capacity_errors(void) {
  uint32_t value = 123u;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_kv_init_ex(0u, 32u));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_kv_get_u32_ex(1u, &value));
  TEST_ASSERT_EQUAL_UINT32(0u, value);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_kv_set_u32_ex(1u, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_kv_commit_ex());

  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_kv_init_ex(1024u, 8192u));
  hal_mock_eeprom_reset();
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_kv_init_ex(0u, 8192u));
}

void test_ex_blob_too_large_reports_overflow(void) {
  uint8_t payload[HAL_KV_MAX_BANK_SIZE] = {};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_kv_set_blob_ex(777u, payload, sizeof(payload)));
}

void test_deferred_commit_publishes_one_complete_bank(void) {
  hal_mock_eeprom_clear_write_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(801u, 11u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(802u, 22u));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_commit_ex());
  TEST_ASSERT_EQUAL_UINT32(HAL_KV_MAX_BANK_SIZE + HAL_KV_PUBLISH_SIZE,
                           hal_mock_eeprom_get_write_count());

  hal_mock_kv_full_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
  uint32_t first = 0u;
  uint32_t second = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(801u, &first));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(802u, &second));
  TEST_ASSERT_EQUAL_UINT32(11u, first);
  TEST_ASSERT_EQUAL_UINT32(22u, second);
}

void test_interrupted_publication_keeps_previous_bank(void) {
  const hal_mock_eeprom_replace_fail_phase_t phases[] = {
      HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_INVALIDATE,
      HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_BODY,
      HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_VERIFY,
  };
  for (const hal_mock_eeprom_replace_fail_phase_t phase : phases) {
    hal_mock_kv_full_reset();
    hal_mock_eeprom_reset();
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_eeprom_init(HAL_EEPROM_FLASH, 8192u, 0x50u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(901u, 100u));

    hal_mock_eeprom_set_replace_fail_phase(phase);
    TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_kv_set_u32_ex(901u, 200u));

    hal_mock_kv_full_reset();
    hal_mock_eeprom_set_replace_fail_phase(HAL_MOCK_EEPROM_REPLACE_FAIL_NONE);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
    uint32_t value = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(901u, &value));
    TEST_ASSERT_EQUAL_UINT32(100u, value);
  }
}

void test_completed_bank_is_recovered_after_late_error(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(902u, 100u));
  hal_mock_eeprom_set_replace_fail_phase(
      HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_PUBLISH);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_kv_set_u32_ex(902u, 200u));

  hal_mock_kv_full_reset();
  hal_mock_eeprom_set_replace_fail_phase(HAL_MOCK_EEPROM_REPLACE_FAIL_NONE);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(902u, &value));
  TEST_ASSERT_EQUAL_UINT32(200u, value);
}

void test_bank_looks_present_detects_active_and_absent_banks(void) {
  /* Fresh init publishes the first generation to bank 0; bank 1 stays
   * erased/0xFF until something writes it. */
  TEST_ASSERT_TRUE(hal_kv_bank_looks_present(0u, 4096u));
  TEST_ASSERT_FALSE(hal_kv_bank_looks_present(4096u, 4096u));

  bool present = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_bank_looks_present_ex(4096u, 4096u, &present));
  TEST_ASSERT_FALSE(present);

  /* A real header at the wrong expected size must report absent too. */
  TEST_ASSERT_FALSE(hal_kv_bank_looks_present(0u, 2048u));

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_kv_bank_looks_present_ex(0u, 4096u, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_kv_bank_looks_present_ex(0u, 4u, &present));
}

void test_read_through_surfaces_live_eeprom_fault(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(950u, 42u));
  const uint8_t blobData[] = {9, 8, 7, 6};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_set_blob_ex(951u, blobData, sizeof(blobData)));

  hal_mock_eeprom_set_io_status(HAL_EIO);

  /* Default mode: served from the RAM cache, blind to the live fault. */
  uint32_t cached = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(950u, &cached));
  TEST_ASSERT_EQUAL_UINT32(42u, cached);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_read_through(true));

  uint32_t verified = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_kv_get_u32_ex(950u, &verified));
  uint8_t blobOut[sizeof(blobData)] = {0};
  TEST_ASSERT_EQUAL_INT(
      HAL_EIO, hal_kv_get_blob_ex(951u, blobOut, sizeof(blobOut), nullptr));
  /* A length-only query (out == NULL) never touches EEPROM, matching
   * hal_kv_get_blob_ex()'s cached-mode contract. */
  uint16_t length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_blob_ex(951u, nullptr, 0u, &length));
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(blobData), length);

  hal_mock_eeprom_set_io_status(HAL_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(950u, &verified));
  TEST_ASSERT_EQUAL_UINT32(42u, verified);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_kv_get_blob_ex(951u, blobOut, sizeof(blobOut), nullptr));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(blobData, blobOut, sizeof(blobData));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_read_through(false));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_set_get_u32_and_reinit);
  RUN_TEST(test_blob_roundtrip_and_length_query);
  RUN_TEST(test_delete_removes_key);
  RUN_TEST(test_unchanged_value_skips_writes);
  RUN_TEST(test_gc_and_concurrent_updates);
  RUN_TEST(test_ex_u32_roundtrip_and_status);
  RUN_TEST(test_ex_blob_reports_overflow_and_length);
  RUN_TEST(test_ex_stats_and_commit_status);
  RUN_TEST(test_ex_initialization_and_capacity_errors);
  RUN_TEST(test_ex_blob_too_large_reports_overflow);
  RUN_TEST(test_deferred_commit_publishes_one_complete_bank);
  RUN_TEST(test_interrupted_publication_keeps_previous_bank);
  RUN_TEST(test_completed_bank_is_recovered_after_late_error);
  RUN_TEST(test_bank_looks_present_detects_active_and_absent_banks);
  RUN_TEST(test_read_through_surfaces_live_eeprom_fault);
  return UNITY_END();
}
