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
  hal_eeprom_init(HAL_EEPROM_FLASH, 1024, 0x50);
  TEST_ASSERT_TRUE(hal_kv_init(0, 512));
}

void tearDown(void) {}

void test_set_get_u32_and_reinit(void) {
  TEST_ASSERT_TRUE(hal_kv_set_u32(100, 0x12345678u));

  uint32_t out = 0;
  TEST_ASSERT_TRUE(hal_kv_get_u32(100, &out));
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, out);

  TEST_ASSERT_TRUE(hal_kv_init(0, 512));
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

  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_kv_init_ex(900u, 512u));
  hal_mock_eeprom_reset();
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_kv_init_ex(0u, 512u));
}

void test_ex_blob_too_large_reports_overflow(void) {
  uint8_t payload[256] = {};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_kv_set_blob_ex(777u, payload, sizeof(payload)));
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
  return UNITY_END();
}
