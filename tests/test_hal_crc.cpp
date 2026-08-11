#include "hal/security/hal_crc.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Standard "123456789" catalog check string. */
static const uint8_t kCheck[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

/* ── CRC-8/MAXIM-DOW ────────────────────────────────────────────────────────
 */

void test_crc8_maxim_catalog_check(void) {
  TEST_ASSERT_EQUAL_HEX8(0xA1, hal_crc8_maxim(kCheck, sizeof(kCheck)));
}

void test_crc8_maxim_onewire_rom_vector(void) {
  /* Preserved regression vector from the former hal_onewire CRC test. */
  const uint8_t rom[7] = {0x28, 0xFF, 0x6C, 0x92, 0x61, 0x16, 0x03};
  TEST_ASSERT_EQUAL_HEX8(0x34, hal_crc8_maxim(rom, sizeof(rom)));
}

void test_crc8_maxim_rejects_bad_args(void) {
  TEST_ASSERT_EQUAL_HEX8(0x00, hal_crc8_maxim(NULL, 4u));
  TEST_ASSERT_EQUAL_HEX8(0x00, hal_crc8_maxim(kCheck, 0u));
}

/* ── Maxim 1-Wire CRC-16 ────────────────────────────────────────────────────
 */

void test_crc16_maxim_onewire_vector_and_check(void) {
  /* Preserved regression vector from the former hal_onewire CRC test. */
  const uint8_t data[6] = {0xF0, 0x88, 0x00, 0xAA, 0x55, 0xFF};
  const uint8_t inverted_crc[2] = {0x54, 0x20};

  TEST_ASSERT_EQUAL_HEX16(0xDFAB, hal_crc16_maxim(data, sizeof(data), 0u));
  TEST_ASSERT_TRUE(hal_crc16_maxim_check(data, sizeof(data), inverted_crc, 0u));
}

void test_crc16_maxim_is_seedable_across_split(void) {
  const uint8_t data[6] = {0xF0, 0x88, 0x00, 0xAA, 0x55, 0xFF};
  const uint16_t whole = hal_crc16_maxim(data, sizeof(data), 0u);
  const uint16_t part = hal_crc16_maxim(data, 3u, 0u);
  const uint16_t split = hal_crc16_maxim(data + 3, 3u, part);
  TEST_ASSERT_EQUAL_HEX16(whole, split);
}

void test_crc16_maxim_check_rejects_bad_args(void) {
  const uint8_t data[2] = {0x01, 0x02};
  const uint8_t inverted_crc[2] = {0x00, 0x00};
  TEST_ASSERT_FALSE(hal_crc16_maxim_check(NULL, 2u, inverted_crc, 0u));
  TEST_ASSERT_FALSE(hal_crc16_maxim_check(data, 2u, NULL, 0u));
}

/* ── CRC-16/CCITT-FALSE ─────────────────────────────────────────────────────
 */

void test_crc16_ccitt_catalog_check(void) {
  TEST_ASSERT_EQUAL_HEX16(
      0x29B1, hal_crc16_ccitt(kCheck, sizeof(kCheck), HAL_CRC16_CCITT_INIT));
}

void test_crc16_ccitt_is_seedable_across_split(void) {
  const uint16_t whole =
      hal_crc16_ccitt(kCheck, sizeof(kCheck), HAL_CRC16_CCITT_INIT);
  const uint16_t part = hal_crc16_ccitt(kCheck, 4u, HAL_CRC16_CCITT_INIT);
  const uint16_t split = hal_crc16_ccitt(kCheck + 4, 5u, part);
  TEST_ASSERT_EQUAL_HEX16(whole, split);
}

/* ── CRC-32/ISO-HDLC ────────────────────────────────────────────────────────
 */

void test_crc32_catalog_check(void) {
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, hal_crc32(kCheck, sizeof(kCheck)));
}

void test_crc32_rejects_bad_args(void) {
  TEST_ASSERT_EQUAL_HEX32(0x00000000u, hal_crc32(NULL, 4u));
  TEST_ASSERT_EQUAL_HEX32(0x00000000u, hal_crc32(kCheck, 0u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_crc8_maxim_catalog_check);
  RUN_TEST(test_crc8_maxim_onewire_rom_vector);
  RUN_TEST(test_crc8_maxim_rejects_bad_args);
  RUN_TEST(test_crc16_maxim_onewire_vector_and_check);
  RUN_TEST(test_crc16_maxim_is_seedable_across_split);
  RUN_TEST(test_crc16_maxim_check_rejects_bad_args);
  RUN_TEST(test_crc16_ccitt_catalog_check);
  RUN_TEST(test_crc16_ccitt_is_seedable_across_split);
  RUN_TEST(test_crc32_catalog_check);
  RUN_TEST(test_crc32_rejects_bad_args);
  return UNITY_END();
}
