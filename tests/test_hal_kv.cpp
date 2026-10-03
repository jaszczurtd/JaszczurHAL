#include "hal/impl/.mock/hal_mock.h"
#include "hal/security/hal_crc.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/hal_kv.h"
#include "utils/unity.h"

#include <atomic>
#include <cstring>
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

void test_key_index_capacity_is_reported_and_enforced(void) {
  hal_kv_stats_t stats = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_stats_ex(&stats));
  TEST_ASSERT_EQUAL_UINT16(HAL_KV_MAX_KEYS, stats.key_capacity);
  TEST_ASSERT_EQUAL_UINT16(0u, stats.key_count);

  // Fill every index slot with a small record; the bank has room for far more.
  for (uint16_t key = 1u; key <= stats.key_capacity; key++) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(key, key));
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_stats_ex(&stats));
  TEST_ASSERT_EQUAL_UINT16(stats.key_capacity, stats.key_count);

  // The next distinct key finds no slot; an existing key still updates.
  hal_mock_serial_reset();
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM,
                        hal_kv_set_u32_ex(stats.key_capacity + 1u, 7u));
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_serial_last_line(), "key index full"));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1u, 99u));

  // Deleting a key frees its slot for a new one.
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_delete_ex(2u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(stats.key_capacity + 1u, 7u));
  uint32_t out = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_get_u32_ex(stats.key_capacity + 1u, &out));
  TEST_ASSERT_EQUAL_UINT32(7u, out);
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

/* Record and commit-record sizes of the on-storage format: 16-byte header,
 * payload, 2-byte footer; a commit record carries 4 payload bytes. */
static const uint32_t kU32RecordBytes = 16u + 4u + 2u;
static const uint32_t kCommitBytes = 16u + 4u + 2u;

void test_deferred_commit_appends_one_batch(void) {
  hal_mock_eeprom_clear_write_count();
  hal_mock_eeprom_clear_erase_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(801u, 11u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(802u, 22u));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());

  /* Both records and one commit record, appended without an erase. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_commit_ex());
  TEST_ASSERT_EQUAL_UINT32(2u * kU32RecordBytes + kCommitBytes,
                           hal_mock_eeprom_get_write_count());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_erase_count());

  hal_mock_kv_full_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
  uint32_t first = 0u;
  uint32_t second = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(801u, &first));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(802u, &second));
  TEST_ASSERT_EQUAL_UINT32(11u, first);
  TEST_ASSERT_EQUAL_UINT32(22u, second);
}

/* Stage a change, then force the compaction into the other bank. */
static hal_status_t compact_with_staged_u32(uint16_t key, uint32_t value) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(key, value));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(true));
  return hal_kv_gc_ex();
}

void test_interrupted_compaction_keeps_previous_bank(void) {
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
    TEST_ASSERT_EQUAL_INT(HAL_EIO, compact_with_staged_u32(901u, 200u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_read_through(true));
    uint32_t unpublished = UINT32_MAX;
    TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_kv_get_u32_ex(901u, &unpublished));
    TEST_ASSERT_EQUAL_UINT32(0u, unpublished);

    hal_mock_kv_full_reset();
    hal_mock_eeprom_set_replace_fail_phase(HAL_MOCK_EEPROM_REPLACE_FAIL_NONE);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
    uint32_t value = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(901u, &value));
    TEST_ASSERT_EQUAL_UINT32(100u, value);
  }
}

void test_completed_compaction_is_recovered_after_late_error(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(902u, 100u));
  hal_mock_eeprom_set_replace_fail_phase(
      HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_PUBLISH);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, compact_with_staged_u32(902u, 200u));

  hal_mock_kv_full_reset();
  hal_mock_eeprom_set_replace_fail_phase(HAL_MOCK_EEPROM_REPLACE_FAIL_NONE);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(902u, &value));
  TEST_ASSERT_EQUAL_UINT32(200u, value);
}

static void reload_kv(void) {
  hal_mock_kv_full_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
}

static uint32_t read_u32(uint16_t key) {
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(key, &value));
  return value;
}

static hal_kv_stats_t kv_stats(void) {
  hal_kv_stats_t stats = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_stats_ex(&stats));
  return stats;
}

void test_commits_append_without_erase_until_the_log_is_full(void) {
  const uint32_t generation = kv_stats().generation;
  hal_mock_eeprom_clear_erase_count();
  hal_mock_eeprom_clear_write_count();
  for (uint32_t round = 0u; round < 20u; round++) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1000u, round));
  }
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_erase_count());
  TEST_ASSERT_EQUAL_UINT32(20u * (kU32RecordBytes + kCommitBytes),
                           hal_mock_eeprom_get_write_count());
  TEST_ASSERT_EQUAL_UINT32(generation, kv_stats().generation);
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(19u, read_u32(1000u));

  /* Fill the log: the first set that does not fit compacts into the other
   * bank (a new generation) and the log continues there. */
  uint32_t round = 20u;
  while (kv_stats().generation == generation) {
    TEST_ASSERT_TRUE(round < 1000u);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1000u, round++));
  }
  TEST_ASSERT_EQUAL_UINT32(generation + 1u, kv_stats().generation);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1001u, 7u));
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(round - 1u, read_u32(1000u));
  TEST_ASSERT_EQUAL_UINT32(7u, read_u32(1001u));
}

/* Log writes until one more batch of a u32 would not fit the active bank. */
static void fill_log_to_the_brim(uint16_t key) {
  const uint32_t generation = kv_stats().generation;
  uint32_t round = 0u;
  for (;;) {
    const hal_kv_stats_t stats = kv_stats();
    TEST_ASSERT_EQUAL_UINT32(generation, stats.generation);
    if (stats.used_bytes + kU32RecordBytes + kCommitBytes >
        stats.capacity_bytes) {
      return;
    }
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(key, round++));
  }
}

void test_prepared_spare_bank_compacts_without_erase(void) {
  TEST_ASSERT_FALSE(kv_stats().spare_erased);
  hal_mock_eeprom_clear_erase_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_prepare_ex());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_eeprom_get_erase_count());
  TEST_ASSERT_TRUE(kv_stats().spare_erased);
  /* Already erased: nothing to do. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_prepare_ex());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_eeprom_get_erase_count());

  fill_log_to_the_brim(1100u);
  const uint32_t generation = kv_stats().generation;
  hal_mock_eeprom_clear_erase_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1100u, 4242u));
  TEST_ASSERT_EQUAL_UINT32(generation + 1u, kv_stats().generation);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_erase_count());
  /* The bank compacted from now holds old data again. */
  TEST_ASSERT_FALSE(kv_stats().spare_erased);

  /* Without a prepare the next compaction has to erase. */
  fill_log_to_the_brim(1100u);
  hal_mock_eeprom_clear_erase_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1100u, 4343u));
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_eeprom_get_erase_count());
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(4343u, read_u32(1100u));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_prepare_ex());
  hal_mock_kv_full_reset();
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_kv_prepare_ex());
}

void test_torn_append_keeps_the_last_commit_and_recovers(void) {
  /* Cut points: nothing, inside the first record, before the commit record
   * and inside the commit record of a two-record batch. */
  const uint16_t cuts[] = {0u, 5u, 2u * 22u, 2u * 22u + 21u};
  for (const uint16_t cut : cuts) {
    hal_mock_kv_full_reset();
    hal_mock_eeprom_reset();
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_eeprom_init(HAL_EEPROM_FLASH, 8192u, 0x50u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1200u, 100u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1201u, 100u));

    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1200u, 200u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1201u, 200u));
    hal_mock_eeprom_tear_next_append(cut);
    TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_kv_commit_ex());
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(true));

    /* After a power loss the batch is gone as a whole. */
    reload_kv();
    TEST_ASSERT_EQUAL_UINT32(100u, read_u32(1200u));
    TEST_ASSERT_EQUAL_UINT32(100u, read_u32(1201u));

    /* The cut tail is never appended to again: the next write compacts. */
    const uint32_t generation = kv_stats().generation;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1200u, 300u));
    TEST_ASSERT_EQUAL_UINT32(cut == 0u ? generation : generation + 1u,
                             kv_stats().generation);
    reload_kv();
    TEST_ASSERT_EQUAL_UINT32(300u, read_u32(1200u));
    TEST_ASSERT_EQUAL_UINT32(100u, read_u32(1201u));
  }
}

void test_failed_append_is_retried_by_a_compaction(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1300u, 1u));
  const uint32_t generation = kv_stats().generation;
  hal_mock_eeprom_tear_next_append(10u);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_kv_set_u32_ex(1300u, 2u));
  /* Still staged; the retry must not append over the cut bytes. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_commit_ex());
  TEST_ASSERT_EQUAL_UINT32(generation + 1u, kv_stats().generation);
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(2u, read_u32(1300u));
}

static hal_status_t refuse_flash_write(void *context) {
  (void)context;
  return HAL_EBUSY;
}

static void finish_flash_write(void *context) { (void)context; }

void test_refused_append_is_retried_in_place(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1350u, 1u));
  const uint32_t generation = kv_stats().generation;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_set_flash_write_callbacks(
                            refuse_flash_write, finish_flash_write, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_kv_set_u32_ex(1350u, 2u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_set_flash_write_callbacks(nullptr, nullptr, nullptr));
  /* Nothing reached the medium, so the retry appends instead of compacting. */
  hal_mock_eeprom_clear_erase_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_commit_ex());
  TEST_ASSERT_EQUAL_UINT32(generation, kv_stats().generation);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_erase_count());
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(2u, read_u32(1350u));
}

void test_bytes_written_behind_the_store_send_it_to_a_compaction(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1360u, 1u));
  const hal_kv_stats_t before = kv_stats();
  /* Someone else wrote where the next batch goes (bank 0 is active). */
  const uint8_t stray[2] = {0x12u, 0x34u};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_write_bytes(before.used_bytes, stray, sizeof(stray)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1360u, 2u));
  TEST_ASSERT_EQUAL_UINT32(before.generation + 1u, kv_stats().generation);
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(2u, read_u32(1360u));
}

void test_damaged_log_record_drops_it_and_everything_after(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1400u, 1u));
  const uint16_t second_batch = kv_stats().used_bytes;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1400u, 2u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1401u, 3u));

  /* Flip the stored value of the second batch (bank 0 is active). */
  const uint16_t payload = static_cast<uint16_t>(second_batch + 16u);
  const uint8_t flipped =
      static_cast<uint8_t>(~hal_mock_eeprom_get_byte(payload));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_write_bytes(payload, &flipped, 1u));
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(1u, read_u32(1400u));
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_kv_get_u32_ex(1401u, &value));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1401u, 4u));
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(1u, read_u32(1400u));
  TEST_ASSERT_EQUAL_UINT32(4u, read_u32(1401u));
}

/* Rewrite the payload of the commit record at offset and re-seal the record
 * itself, so only the batch check can reject it. */
static void reseal_commit(uint16_t offset, uint16_t batch, uint16_t batch_crc) {
  uint8_t record[16 + 4 + 2];
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_read_bytes(offset, record, sizeof(record)));
  TEST_ASSERT_EQUAL_UINT8(4u, record[4]);
  record[16] = static_cast<uint8_t>(batch & 0xFFu);
  record[17] = static_cast<uint8_t>(batch >> 8);
  record[18] = static_cast<uint8_t>(batch_crc & 0xFFu);
  record[19] = static_cast<uint8_t>(batch_crc >> 8);
  const uint16_t payload_crc =
      hal_crc16_ccitt(record + 16, 4u, HAL_CRC16_CCITT_INIT);
  record[12] = static_cast<uint8_t>(payload_crc & 0xFFu);
  record[13] = static_cast<uint8_t>(payload_crc >> 8);
  const uint16_t header_crc =
      hal_crc16_ccitt(record, 14u, HAL_CRC16_CCITT_INIT);
  record[14] = static_cast<uint8_t>(header_crc & 0xFFu);
  record[15] = static_cast<uint8_t>(header_crc >> 8);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_write_bytes(offset, record, sizeof(record)));
}

void test_commit_record_must_match_its_batch(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1700u, 1u));
  const uint16_t batch = kv_stats().used_bytes;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1700u, 2u));
  const uint16_t commit = static_cast<uint16_t>(batch + kU32RecordBytes);
  uint8_t stored[kU32RecordBytes];
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_read_bytes(batch, stored, sizeof(stored)));
  const uint16_t good_crc =
      hal_crc16_ccitt(stored, sizeof(stored), HAL_CRC16_CCITT_INIT);

  reseal_commit(commit, batch, static_cast<uint16_t>(good_crc ^ 1u));
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(1u, read_u32(1700u));

  reseal_commit(commit, static_cast<uint16_t>(batch + 1u), good_crc);
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(1u, read_u32(1700u));

  reseal_commit(commit, batch, good_crc);
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(2u, read_u32(1700u));
}

/* Rewrite the header CRC after a test edited header fields. */
static void reseal_header(uint8_t header[24]) {
  const uint16_t crc = hal_crc16_ccitt(header, 22u, HAL_CRC16_CCITT_INIT);
  header[22] = static_cast<uint8_t>(crc & 0xFFu);
  header[23] = static_cast<uint8_t>(crc >> 8);
}

void test_version_2_bank_is_read_and_extended(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1500u, 77u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_gc_ex());
  /* Only the rewritten bank may hold the value. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_prepare_ex());
  /* A compacted bank with an empty log is the version 2 layout: rewrite its
   * version byte and header CRC the way a version 2 build wrote them. */
  /* Generation 1 went to bank 0, each compaction alternates. */
  const uint16_t bank = kv_stats().generation % 2u == 1u ? 0u : 4096u;
  uint8_t header[24];
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_read_bytes(bank, header, 24u));
  TEST_ASSERT_EQUAL_UINT8(3u, header[4]);
  header[4] = 2u;
  reseal_header(header);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_write_bytes(bank, header, 24u));

  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(77u, read_u32(1500u));
  const uint32_t generation = kv_stats().generation;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1500u, 78u));
  TEST_ASSERT_EQUAL_UINT32(generation, kv_stats().generation);
  reload_kv();
  TEST_ASSERT_EQUAL_UINT32(78u, read_u32(1500u));
}

void test_commit_record_inside_the_body_is_rejected(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1800u, 5u));
  /* Stretch the body over the log batch, commit record included. */
  const uint16_t end = kv_stats().used_bytes;
  uint8_t bank[4096];
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_read_bytes(0u, bank, end));
  bank[12] = static_cast<uint8_t>(end & 0xFFu);
  bank[13] = static_cast<uint8_t>(end >> 8);
  bank[16] = static_cast<uint8_t>(bank[16] + 2u);
  const uint16_t body_crc = hal_crc16_ccitt(
      bank + HAL_KV_PUBLISH_SIZE,
      static_cast<uint16_t>(end - HAL_KV_PUBLISH_SIZE), HAL_CRC16_CCITT_INIT);
  bank[18] = static_cast<uint8_t>(body_crc & 0xFFu);
  bank[19] = static_cast<uint8_t>(body_crc >> 8);
  reseal_header(bank);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_write_bytes(0u, bank, 24u));

  reload_kv();
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_kv_get_u32_ex(1800u, &value));
}

void test_stm32_log_batches_keep_double_word_alignment(void) {
  hal_mock_kv_full_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_init(HAL_EEPROM_STM32_FLASH, 4096u, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 4096u));
  const uint32_t generation = kv_stats().generation;
  const uint8_t blob[] = {1u, 2u, 3u};
  for (uint16_t round = 0u; round < 10u; round++) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_blob_ex(1600u, blob, round % 4u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1601u, round));
    TEST_ASSERT_EQUAL_UINT16(0u, kv_stats().used_bytes % 8u);
  }
  TEST_ASSERT_EQUAL_UINT32(generation, kv_stats().generation);
  /* A compacted body ending inside a double word: the log resumes on the
   * next one. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_gc_ex());
  TEST_ASSERT_TRUE(kv_stats().used_bytes % 8u == 0u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1601u, 9u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_blob_ex(1600u, blob, 1u));
  hal_mock_kv_full_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 4096u));
  TEST_ASSERT_EQUAL_UINT32(9u, read_u32(1601u));
  uint16_t length = 0u;
  uint8_t out[4] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_get_blob_ex(1600u, out, sizeof(out), &length));
  TEST_ASSERT_EQUAL_UINT16(1u, length);
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

void test_read_through_rejects_unpublished_ram_image(void) {
  const uint8_t oldBlob[] = {1u, 2u, 3u};
  const uint8_t newBlob[] = {7u, 8u, 9u, 10u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(960u, 10u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_set_blob_ex(961u, oldBlob, sizeof(oldBlob)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_read_through(true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(960u, 20u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_set_blob_ex(961u, newBlob, sizeof(newBlob)));

  uint32_t value = UINT32_MAX;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_kv_get_u32_ex(960u, &value));
  TEST_ASSERT_EQUAL_UINT32(0u, value);
  TEST_ASSERT_FALSE(hal_kv_get_u32(960u, &value));

  uint8_t blobOut[sizeof(newBlob)] = {};
  uint16_t length = UINT16_MAX;
  TEST_ASSERT_EQUAL_INT(
      HAL_EBUSY, hal_kv_get_blob_ex(961u, blobOut, sizeof(blobOut), &length));
  TEST_ASSERT_EQUAL_UINT16(0u, length);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        hal_kv_get_blob_ex(961u, nullptr, 0u, &length));
  TEST_ASSERT_EQUAL_UINT16(0u, length);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_commit_ex());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(960u, &value));
  TEST_ASSERT_EQUAL_UINT32(20u, value);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_kv_get_blob_ex(961u, blobOut, sizeof(blobOut), &length));
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(newBlob), length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(newBlob, blobOut, sizeof(newBlob));
}

void test_flash_rejects_unaligned_banks_without_writing(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, 16384u, 0u));
  const uint16_t bases[] = {96u, 128u};
  for (uint16_t base : bases) {
    hal_mock_eeprom_clear_write_count();
    TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_kv_init_ex(base, 8192u));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
    TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_kv_set_u32_ex(1u, 7u));
  }
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_kv_init_ex(0u, 4096u));
}

void test_flash_rejects_misaligned_existing_bank_before_loading(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1u, 7u));
  uint8_t banks[8192];
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_read_bytes(0u, banks, sizeof(banks)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, 16384u, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_write_bytes(128u, banks, sizeof(banks)));
  hal_mock_eeprom_clear_write_count();
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_kv_init_ex(128u, sizeof(banks)));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
}

void test_at24_and_stm32_keep_their_own_bank_geometry(void) {
  const hal_eeprom_type_t types[] = {HAL_EEPROM_AT24C256,
                                     HAL_EEPROM_STM32_FLASH};
  for (hal_eeprom_type_t type : types) {
    const uint16_t base = type == HAL_EEPROM_AT24C256 ? 128u : 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_init(type, 4096u, 0u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(base, 4096u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1u, 23u));
    hal_mock_kv_full_reset();
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(base, 4096u));
    uint32_t value = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(1u, &value));
    TEST_ASSERT_EQUAL_UINT32(23u, value);
  }
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
  RUN_TEST(test_key_index_capacity_is_reported_and_enforced);
  RUN_TEST(test_ex_initialization_and_capacity_errors);
  RUN_TEST(test_ex_blob_too_large_reports_overflow);
  RUN_TEST(test_deferred_commit_appends_one_batch);
  RUN_TEST(test_interrupted_compaction_keeps_previous_bank);
  RUN_TEST(test_completed_compaction_is_recovered_after_late_error);
  RUN_TEST(test_commits_append_without_erase_until_the_log_is_full);
  RUN_TEST(test_prepared_spare_bank_compacts_without_erase);
  RUN_TEST(test_torn_append_keeps_the_last_commit_and_recovers);
  RUN_TEST(test_failed_append_is_retried_by_a_compaction);
  RUN_TEST(test_refused_append_is_retried_in_place);
  RUN_TEST(test_bytes_written_behind_the_store_send_it_to_a_compaction);
  RUN_TEST(test_damaged_log_record_drops_it_and_everything_after);
  RUN_TEST(test_commit_record_must_match_its_batch);
  RUN_TEST(test_version_2_bank_is_read_and_extended);
  RUN_TEST(test_commit_record_inside_the_body_is_rejected);
  RUN_TEST(test_stm32_log_batches_keep_double_word_alignment);
  RUN_TEST(test_bank_looks_present_detects_active_and_absent_banks);
  RUN_TEST(test_read_through_surfaces_live_eeprom_fault);
  RUN_TEST(test_read_through_rejects_unpublished_ram_image);
  RUN_TEST(test_flash_rejects_unaligned_banks_without_writing);
  RUN_TEST(test_flash_rejects_misaligned_existing_bank_before_loading);
  RUN_TEST(test_at24_and_stm32_keep_their_own_bank_geometry);
  return UNITY_END();
}
