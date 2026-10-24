// hal_kv over the STM32G474 flash EEPROM backend, compiled for the host on a
// flash model with ECC semantics: every double word can be programmed once
// per erase, even with 0xFF. A programmer that writes a full image (0xFF
// included) leaves the KV region reading erased while refusing writes;
// formatting and appending must still succeed by erasing first.

#include "hal/impl/stm32g474/drivers/stm32g474/stm32g474_flash.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/hal_kv.h"
#include "hal/storage/jh_eeprom_provider.h"
#include "utils/unity.h"

#include <stdint.h>
#include <string.h>

#define FLASH_BYTES HAL_STM32_FLASH_EEPROM_SIZE
#define PAGE_BYTES 2048u
#define DOUBLE_WORDS (FLASH_BYTES / 8u)
#define STR2(x) #x
#define STR(x) STR2(x)

extern "C" {
alignas(8) uint8_t jh_test_flash[FLASH_BYTES];
}
/* The linker script provides these on the target. */
__asm__(".globl __hal_stm32_eeprom_flash_start\n"
        ".set __hal_stm32_eeprom_flash_start, jh_test_flash\n"
        ".globl __hal_stm32_eeprom_flash_end\n"
        ".set __hal_stm32_eeprom_flash_end, jh_test_flash + " STR(
            HAL_STM32_FLASH_EEPROM_SIZE) "\n");

/* The KV and EEPROM errors are printed through the debug console. */
extern "C" void hal_derr(const char *format, ...) { (void)format; }
/* Only the flash provider is used here. */
const jh_eeprom_provider_ops_t *jh_at24c256_provider_get_ops(void) {
  return nullptr;
}

static bool s_programmed[DOUBLE_WORDS];
static unsigned s_erases;
static unsigned s_refused;

static uint32_t offset_of(uintptr_t address) {
  const uintptr_t base = (uintptr_t)jh_test_flash;
  TEST_ASSERT_TRUE(address >= base && address < base + FLASH_BYTES);
  return (uint32_t)(address - base);
}

bool jh_stm32g474_flash_unlock(void) { return true; }
void jh_stm32g474_flash_lock(void) {}

bool jh_stm32g474_flash_erase_page(uintptr_t address) {
  const uint32_t offset = offset_of(address);
  TEST_ASSERT_EQUAL_UINT32(0u, offset % PAGE_BYTES);
  memset(&jh_test_flash[offset], 0xFF, PAGE_BYTES);
  memset(&s_programmed[offset / 8u], 0, PAGE_BYTES / 8u);
  ++s_erases;
  return true;
}

/* PROGERR: a double word programmed since its erase takes no second write. */
bool jh_stm32g474_flash_program_doubleword(uintptr_t address,
                                           const uint8_t *data) {
  const uint32_t offset = offset_of(address);
  TEST_ASSERT_EQUAL_UINT32(0u, offset % 8u);
  if (s_programmed[offset / 8u]) {
    ++s_refused;
    return false;
  }
  memcpy(&jh_test_flash[offset], data, 8u);
  s_programmed[offset / 8u] = true;
  return true;
}

/* What a programmer writing a full image does: every double word holds its
 * current bytes, 0xFF included, as programmed data. */
static void program_full_image(void) {
  for (uint32_t i = 0u; i < DOUBLE_WORDS; i++) {
    s_programmed[i] = true;
  }
}

static void start(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, FLASH_BYTES));
}

static uint32_t value_of(uint16_t key) {
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(key, &value));
  return value;
}

void setUp(void) {
  memset(jh_test_flash, 0xFF, sizeof(jh_test_flash));
  memset(s_programmed, 0, sizeof(s_programmed));
  s_erases = 0u;
  s_refused = 0u;
}

void tearDown(void) {}

void test_kv_formats_over_flash_a_programmer_filled_with_ff(void) {
  program_full_image();
  start();
  TEST_ASSERT_TRUE(s_refused > 0u); /* the first try did meet the refusal */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(7u, 0x12345678u));
  start();
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, value_of(7u));
}

void test_kv_appends_after_a_full_image_restore(void) {
  start();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(1u, 11u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(2u, 22u));
  /* The store is written back by a programmer as part of a full image: the
   * records keep their bytes, the erased log tail turns into programmed
   * 0xFF. */
  program_full_image();
  start();
  TEST_ASSERT_EQUAL_UINT32(11u, value_of(1u));
  /* The append meets the refusal; the same commit compacts into the other
   * bank instead. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(3u, 33u));
  TEST_ASSERT_TRUE(s_refused > 0u);
  start();
  TEST_ASSERT_EQUAL_UINT32(11u, value_of(1u));
  TEST_ASSERT_EQUAL_UINT32(22u, value_of(2u));
  TEST_ASSERT_EQUAL_UINT32(33u, value_of(3u));
  /* Later writes append again, without erasing. */
  const unsigned erases = s_erases;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(4u, 44u));
  TEST_ASSERT_EQUAL_UINT32(erases, s_erases);
}

void test_kv_on_freshly_erased_flash_never_erases_to_format(void) {
  start();
  TEST_ASSERT_EQUAL_UINT32(0u, s_refused);
  TEST_ASSERT_EQUAL_UINT32(0u, s_erases);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(5u, 55u));
  TEST_ASSERT_EQUAL_UINT32(0u, s_erases);
}

/* Offset of the only copy of pattern in the flash model. */
static uint32_t flash_offset_of(const uint8_t *pattern, uint32_t len) {
  uint32_t found = FLASH_BYTES;
  for (uint32_t offset = 0u; offset + len <= FLASH_BYTES; offset++) {
    if (memcmp(jh_test_flash + offset, pattern, len) == 0) {
      TEST_ASSERT_EQUAL_UINT32(FLASH_BYTES, found);
      found = offset;
    }
  }
  TEST_ASSERT_TRUE(found < FLASH_BYTES);
  return found;
}

/* A record that fills a bank alone still leaves room to delete, shrink or
 * rewrite it; each result survives a restart. */
void test_kv_full_bank_deletes_shrinks_and_rewrites(void) {
  start();
  static uint8_t full[FLASH_BYTES / 2u - HAL_KV_PUBLISH_SIZE - 18u];
  memset(full, 0x11, sizeof(full));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_blob_ex(17u, full, sizeof(full)));

  memset(full, 0x22, sizeof(full));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_blob_ex(17u, full, sizeof(full)));
  const uint8_t small[] = {0x33u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_blob_ex(17u, small, sizeof(small)));
  start();
  uint8_t out[sizeof(small)] = {};
  uint16_t len = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_get_blob_ex(17u, out, sizeof(out), &len));
  TEST_ASSERT_EQUAL_UINT16(sizeof(small), len);
  TEST_ASSERT_EQUAL_HEX8(0x33u, out[0]);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_blob_ex(17u, full, sizeof(full)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_delete_ex(17u));
  start();
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_kv_get_blob_ex(17u, nullptr, 0u, &len));
  TEST_ASSERT_EQUAL_UINT32(0u, s_refused);
}

/* Read-through reads the flash, not the EEPROM mirror: a payload changed on
 * the medium alone is rejected, while the cached read still serves it. */
void test_kv_read_through_reads_the_flash_not_the_mirror(void) {
  start();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(7u, 0x12345678u));
  const uint8_t blob[] = {0xC1u, 0xC2u, 0xC3u, 0xC4u, 0xC5u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_blob_ex(8u, blob, sizeof(blob)));
  const uint8_t word[] = {0x78u, 0x56u, 0x34u, 0x12u};
  const uint32_t word_at = flash_offset_of(word, sizeof(word));
  const uint32_t blob_at = flash_offset_of(blob, sizeof(blob));
  jh_test_flash[word_at] = 0x00u;
  jh_test_flash[blob_at + 2u] = 0x00u;

  TEST_ASSERT_EQUAL_HEX32(0x12345678u, value_of(7u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_read_through(true));
  uint32_t value = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_kv_get_u32_ex(7u, &value));
  TEST_ASSERT_EQUAL_HEX32(0u, value);
  uint8_t out[sizeof(blob)] = {};
  memset(out, 0xAAu, sizeof(out));
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        hal_kv_get_blob_ex(8u, out, sizeof(out), nullptr));
  for (uint8_t byte : out) {
    TEST_ASSERT_EQUAL_HEX8(0u, byte);
  }

  jh_test_flash[word_at] = 0x78u;
  jh_test_flash[blob_at + 2u] = 0xC3u;
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, value_of(7u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_get_blob_ex(8u, out, sizeof(out), nullptr));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(blob, out, sizeof(blob));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_read_through(false));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_kv_formats_over_flash_a_programmer_filled_with_ff);
  RUN_TEST(test_kv_appends_after_a_full_image_restore);
  RUN_TEST(test_kv_on_freshly_erased_flash_never_erases_to_format);
  RUN_TEST(test_kv_full_bank_deletes_shrinks_and_rewrites);
  RUN_TEST(test_kv_read_through_reads_the_flash_not_the_mirror);
  return UNITY_END();
}
