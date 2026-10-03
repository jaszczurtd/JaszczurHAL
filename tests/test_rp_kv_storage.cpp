#include <hal/bluetooth/jh_gamepad_bond_kv_provider.h>
#include <hal/impl/rp2040/drivers/flash/rp_flash_storage.h>
#include <hal/impl/rp2040/drivers/flash/rp_flash_transaction.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>
#include <hal/storage/jh_eeprom_provider.h>
#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <utils/unity.h>

#include <cstring>

uint8_t test_rp_flash[4u * 1024u * 1024u];
static unsigned s_erases;
static unsigned s_programs;
static unsigned s_transactions;
static unsigned s_prepares;
static unsigned s_finishes;
static bool s_guarded;
static bool s_require_guard;
static hal_status_t s_prepare_status;
static jh_rp_flash_partition_t s_partition;

extern "C" void flash_range_erase(uint32_t offset, size_t size) {
  TEST_ASSERT_EQUAL_UINT32(0u, offset % FLASH_SECTOR_SIZE);
  TEST_ASSERT_EQUAL_UINT32(0u, size % FLASH_SECTOR_SIZE);
  TEST_ASSERT_TRUE(offset <= sizeof(test_rp_flash));
  TEST_ASSERT_TRUE(size <= sizeof(test_rp_flash) - offset);
  TEST_ASSERT_TRUE(!s_require_guard || s_guarded);
  std::memset(test_rp_flash + offset, 0xff, size);
  ++s_erases;
}

extern "C" void flash_range_program(uint32_t offset, const uint8_t *data,
                                    size_t size) {
  TEST_ASSERT_EQUAL_UINT32(0u, offset % FLASH_PAGE_SIZE);
  TEST_ASSERT_EQUAL_UINT32(0u, size % FLASH_PAGE_SIZE);
  TEST_ASSERT_TRUE(offset <= sizeof(test_rp_flash));
  TEST_ASSERT_TRUE(size <= sizeof(test_rp_flash) - offset);
  TEST_ASSERT_TRUE(!s_require_guard || s_guarded);
  for (size_t index = 0u; index < size; ++index) {
    test_rp_flash[offset + index] &= data[index];
  }
  ++s_programs;
}

extern "C" hal_status_t
jh_rp_flash_transaction_execute(jh_rp_flash_operation_t operation,
                                void *context, uint32_t timeout_ms) {
  (void)timeout_ms;
  ++s_transactions;
  return operation(context);
}

static hal_status_t prepare(void *context) {
  TEST_ASSERT_EQUAL_PTR(&s_partition, context);
  ++s_prepares;
  s_guarded = s_prepare_status == HAL_OK;
  return s_prepare_status;
}

static void finish(void *context) {
  TEST_ASSERT_EQUAL_PTR(&s_partition, context);
  TEST_ASSERT_TRUE(s_guarded);
  s_guarded = false;
  ++s_finishes;
}

static void reload(uint16_t base, uint16_t span) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(base, span));
}

void setUp(void) {
  std::memset(test_rp_flash, 0xa5, sizeof(test_rp_flash));
  s_erases = s_programs = s_transactions = s_prepares = s_finishes = 0u;
  s_guarded = s_require_guard = false;
  s_prepare_status = HAL_OK;
  jh_rp_flash_storage_set_fail_phase(JH_RP_FLASH_FAIL_NONE);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_set_flash_write_callbacks(nullptr, nullptr, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_read_through(false));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_rp_flash_storage_partition(JH_RP_FLASH_PARTITION_EEPROM,
                                            &s_partition));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u));
}

void tearDown(void) {
  (void)hal_eeprom_set_flash_write_callbacks(nullptr, nullptr, nullptr);
}

void test_rp_rejects_old_offsets_before_write_callbacks(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_set_flash_write_callbacks(
                                    prepare, finish, &s_partition));
  const uint16_t bases[] = {96u, 128u};
  for (uint16_t base : bases) {
    TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_kv_init_ex(base, 16384u));
  }
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_kv_init_ex(0u, 4096u));
  TEST_ASSERT_EQUAL_UINT(0u, s_transactions);
  TEST_ASSERT_EQUAL_UINT(0u, s_prepares);
}

void test_rp_aligned_layouts_preserve_surrounding_storage_and_reload(void) {
  const uint16_t bases[] = {0u, 4096u};
  for (uint16_t base : bases) {
    const uint16_t span = base == 0u ? 8192u : 16384u;
    std::memset(test_rp_flash, 0xa5, sizeof(test_rp_flash));
    reload(base, span);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1u, 11u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(2u, 22u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_commit_ex());
    reload(base, span);
    uint32_t value = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(1u, &value));
    TEST_ASSERT_EQUAL_UINT32(11u, value);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(2u, &value));
    TEST_ASSERT_EQUAL_UINT32(22u, value);
    const size_t first = s_partition.flash_offset + base;
    const size_t end = first + span;
    TEST_ASSERT_EACH_EQUAL_HEX8(0xa5u, test_rp_flash, first);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xa5u, test_rp_flash + end,
                                sizeof(test_rp_flash) - end);
  }
}

void test_rp_write_guard_skips_reads_and_cleans_up_failed_writes(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_set_flash_write_callbacks(
                                    prepare, finish, &s_partition));
  s_require_guard = true;
  reload(4096u, 16384u);
  TEST_ASSERT_EQUAL_UINT(1u, s_prepares);
  reload(4096u, 16384u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_commit());
  TEST_ASSERT_EQUAL_UINT(1u, s_prepares);
  s_prepare_status = HAL_EBUSY;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_kv_set_u32_ex(1u, 1u));
  TEST_ASSERT_EQUAL_UINT(1u, s_transactions);
  TEST_ASSERT_EQUAL_UINT(1u, s_finishes);
  s_prepare_status = HAL_OK;
  jh_rp_flash_storage_set_fail_phase(JH_RP_FLASH_FAIL_APPEND_TORN);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_kv_commit_ex());
  TEST_ASSERT_EQUAL_UINT(2u, s_finishes);
  TEST_ASSERT_FALSE(s_guarded);
  jh_rp_flash_storage_set_fail_phase(JH_RP_FLASH_FAIL_NONE);
  reload(4096u, 16384u);
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_kv_get_u32_ex(1u, &value));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_write_byte(0u, 0x51u));
  TEST_ASSERT_EQUAL_UINT(3u, s_prepares);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_commit());
  TEST_ASSERT_EQUAL_UINT(4u, s_prepares);
  TEST_ASSERT_EQUAL_UINT(3u, s_finishes);
}

/* Store the new bond as its own log batch, or compact it into the other
 * bank, while the given fault phase cuts the flash write short. */
static hal_status_t store_bond(const hal_gamepad_bond_provider_t &provider,
                               const hal_gamepad_bond_blob_t &bond,
                               jh_rp_flash_fail_phase_t phase) {
  const bool compaction = phase != JH_RP_FLASH_FAIL_APPEND_TORN &&
                          phase != JH_RP_FLASH_FAIL_APPEND_AFTER_PROGRAM;
  if (compaction) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
  }
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, compaction ? provider.store(provider.context, &bond) : HAL_OK);
  jh_rp_flash_storage_set_fail_phase(phase);
  hal_status_t status = HAL_OK;
  if (compaction) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(true));
    status = hal_kv_gc_ex();
  } else {
    status = provider.store(provider.context, &bond);
  }
  jh_rp_flash_storage_set_fail_phase(JH_RP_FLASH_FAIL_NONE);
  return status;
}

void test_rp_bond_and_foreign_keys_survive_interrupted_writes(void) {
  const jh_rp_flash_fail_phase_t phases[] = {
      JH_RP_FLASH_FAIL_AFTER_INVALIDATE, JH_RP_FLASH_FAIL_AFTER_BODY,
      JH_RP_FLASH_FAIL_AFTER_VERIFY,     JH_RP_FLASH_FAIL_AFTER_PUBLISH,
      JH_RP_FLASH_FAIL_APPEND_TORN,      JH_RP_FLASH_FAIL_APPEND_AFTER_PROGRAM};
  jh_gamepad_bond_kv_context_t context = {};
  const hal_gamepad_bond_provider_t provider =
      jh_gamepad_bond_kv_provider(&context, 900u);
  hal_gamepad_bond_blob_t old_bond = {}, new_bond = {}, loaded = {};
  std::memset(old_bond.bytes, 0x11, sizeof(old_bond.bytes));
  std::memset(new_bond.bytes, 0x22, sizeof(new_bond.bytes));
  for (auto phase : phases) {
    std::memset(test_rp_flash, 0xff, sizeof(test_rp_flash));
    reload(0u, 8192u);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_read_through(true));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1u, 77u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, provider.store(provider.context, &old_bond));
    TEST_ASSERT_EQUAL_INT(HAL_EIO, store_bond(provider, new_bond, phase));
    reload(0u, 8192u);
    TEST_ASSERT_EQUAL_INT(HAL_OK, provider.load(provider.context, &loaded));
    const bool complete = phase == JH_RP_FLASH_FAIL_AFTER_PUBLISH ||
                          phase == JH_RP_FLASH_FAIL_APPEND_AFTER_PROGRAM;
    const auto &expected = complete ? new_bond : old_bond;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.bytes, loaded.bytes,
                                  sizeof(loaded.bytes));
    uint32_t value = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(1u, &value));
    TEST_ASSERT_EQUAL_UINT32(77u, value);
    TEST_ASSERT_EQUAL_INT(HAL_OK, provider.erase(provider.context));
    reload(0u, 8192u);
    TEST_ASSERT_EQUAL_INT(HAL_ENOENT, provider.load(provider.context, &loaded));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(1u, &value));
    TEST_ASSERT_EQUAL_UINT32(77u, value);
  }
}

static hal_kv_stats_t kv_stats(void) {
  hal_kv_stats_t stats = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_stats_ex(&stats));
  return stats;
}

void test_rp_commits_program_the_log_without_erasing(void) {
  std::memset(test_rp_flash, 0xff, sizeof(test_rp_flash));
  reload(4096u, 16384u);
  s_erases = s_programs = s_transactions = 0u;
  /* 44-byte batches: several share a page, which gets programmed again with
   * the earlier bytes sent as 0xFF. */
  for (uint32_t round = 0u; round < 30u; round++) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(5u, round));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(6u, round * 3u));
  }
  TEST_ASSERT_EQUAL_UINT(0u, s_erases);
  TEST_ASSERT_EQUAL_UINT(60u, s_transactions);
  /* One page per batch, a second one when a batch crosses a page. */
  TEST_ASSERT_TRUE(s_programs >= 60u && s_programs <= 72u);
  reload(4096u, 16384u);
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(5u, &value));
  TEST_ASSERT_EQUAL_UINT32(29u, value);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(6u, &value));
  TEST_ASSERT_EQUAL_UINT32(87u, value);
}

void test_rp_prepared_bank_takes_the_compaction_without_erasing(void) {
  reload(4096u, 16384u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(7u, 1u));
  s_erases = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_prepare_ex());
  /* One erase call covers the whole 8 KiB bank (two sectors). */
  TEST_ASSERT_EQUAL_UINT(1u, s_erases);
  TEST_ASSERT_TRUE(kv_stats().spare_erased);
  const uint32_t generation = kv_stats().generation;
  s_erases = s_programs = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_gc_ex());
  TEST_ASSERT_EQUAL_UINT32(generation + 1u, kv_stats().generation);
  TEST_ASSERT_EQUAL_UINT(0u, s_erases);
  /* Only the pages holding data: body page and header page. */
  TEST_ASSERT_EQUAL_UINT(2u, s_programs);
  reload(4096u, 16384u);
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(7u, &value));
  TEST_ASSERT_EQUAL_UINT32(1u, value);
}

void test_rp_torn_append_recovers_through_a_compaction(void) {
  std::memset(test_rp_flash, 0xff, sizeof(test_rp_flash));
  reload(0u, 8192u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(8u, 100u));
  jh_rp_flash_storage_set_fail_phase(JH_RP_FLASH_FAIL_APPEND_TORN);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_kv_set_u32_ex(8u, 200u));
  jh_rp_flash_storage_set_fail_phase(JH_RP_FLASH_FAIL_NONE);
  reload(0u, 8192u);
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(8u, &value));
  TEST_ASSERT_EQUAL_UINT32(100u, value);
  const uint32_t generation = kv_stats().generation;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(8u, 300u));
  TEST_ASSERT_EQUAL_UINT32(generation + 1u, kv_stats().generation);
  reload(0u, 8192u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(8u, &value));
  TEST_ASSERT_EQUAL_UINT32(300u, value);
}

void test_rp_append_verifies_and_resyncs_after_stray_bits(void) {
  std::memset(test_rp_flash, 0xff, sizeof(test_rp_flash));
  reload(0u, 8192u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(9u, 1u));
  /* A bit pattern the mirror does not know about where the next batch goes;
   * its first byte is the low byte of the record magic, 0x5A. */
  const uint16_t next = kv_stats().used_bytes;
  test_rp_flash[s_partition.flash_offset + next] = 0x00u;
  const uint32_t generation = kv_stats().generation;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_kv_set_u32_ex(9u, 2u));
  /* The failed program reloaded the mirror, so the retry compacts. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_commit_ex());
  TEST_ASSERT_EQUAL_UINT32(generation + 1u, kv_stats().generation);
  reload(0u, 8192u);
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(9u, &value));
  TEST_ASSERT_EQUAL_UINT32(2u, value);
}

void test_rp_append_refuses_programmed_bytes(void) {
  std::memset(test_rp_flash, 0xff, sizeof(test_rp_flash));
  reload(0u, 8192u);
  const uint8_t data[4] = {1u, 2u, 3u, 4u};
  s_transactions = 0u;
  /* The bank header is programmed. */
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_eeprom_append_region(0u, data, 4u));
  TEST_ASSERT_EQUAL_UINT(0u, s_transactions);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_eeprom_append_region(4092u, data, 4u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      data, test_rp_flash + s_partition.flash_offset + 4092u, 4u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_rp_rejects_old_offsets_before_write_callbacks);
  RUN_TEST(test_rp_aligned_layouts_preserve_surrounding_storage_and_reload);
  RUN_TEST(test_rp_write_guard_skips_reads_and_cleans_up_failed_writes);
  RUN_TEST(test_rp_bond_and_foreign_keys_survive_interrupted_writes);
  RUN_TEST(test_rp_commits_program_the_log_without_erasing);
  RUN_TEST(test_rp_prepared_bank_takes_the_compaction_without_erasing);
  RUN_TEST(test_rp_torn_append_recovers_through_a_compaction);
  RUN_TEST(test_rp_append_verifies_and_resyncs_after_stray_bits);
  RUN_TEST(test_rp_append_refuses_programmed_bytes);
  return UNITY_END();
}
