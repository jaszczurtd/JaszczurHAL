#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/hal_eeprom.h"
#include "utils/unity.h"

static uint32_t s_progress_calls = 0u;
static void *s_progress_ctx = NULL;

static void progress_callback(void *ctx) {
  s_progress_calls++;
  s_progress_ctx = ctx;
}

void setUp(void) {
  s_progress_calls = 0u;
  s_progress_ctx = NULL;
  hal_mock_eeprom_reset();
  hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0x50);
}

void tearDown(void) {}

void test_init_sets_type(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EEPROM_AT24C256, hal_mock_eeprom_get_type());
}

void test_init_sets_size_at24c256(void) {
  TEST_ASSERT_EQUAL_UINT16(32768, hal_eeprom_size());
}

void test_flash_alias_uses_requested_size(void) {
  hal_eeprom_init(HAL_EEPROM_FLASH, 1024, 0);

  TEST_ASSERT_EQUAL_INT(HAL_EEPROM_FLASH, hal_mock_eeprom_get_type());
  TEST_ASSERT_EQUAL_UINT16(1024, hal_eeprom_size());
}

void test_write_read_byte(void) {
  hal_eeprom_write_byte(10, 0xAB);
  TEST_ASSERT_EQUAL_UINT8(0xAB, hal_eeprom_read_byte(10));
}

void test_write_read_int(void) {
  hal_eeprom_write_int(20, -123456);
  TEST_ASSERT_EQUAL_INT32(-123456, hal_eeprom_read_int(20));
}

void test_mock_get_byte_matches_read(void) {
  hal_eeprom_write_byte(5, 0x55);
  TEST_ASSERT_EQUAL_UINT8(0x55, hal_mock_eeprom_get_byte(5));
}

void test_commit_sets_flag(void) {
  hal_mock_eeprom_clear_committed_flag();
  TEST_ASSERT_FALSE(hal_mock_eeprom_was_committed());
  hal_eeprom_commit();
  TEST_ASSERT_TRUE(hal_mock_eeprom_was_committed());
}

void test_progress_callback_runs_on_long_operations(void) {
  uint32_t marker = 0x1234u;

  hal_eeprom_set_progress_callback(progress_callback, &marker);
  hal_eeprom_commit();
  hal_eeprom_reset();

  TEST_ASSERT_EQUAL_UINT32(2u, s_progress_calls);
  TEST_ASSERT_EQUAL_PTR(&marker, s_progress_ctx);
}

void test_clear_committed_flag(void) {
  hal_eeprom_commit();
  hal_mock_eeprom_clear_committed_flag();
  TEST_ASSERT_FALSE(hal_mock_eeprom_was_committed());
}

void test_unwritten_address_is_zero(void) {
  TEST_ASSERT_EQUAL_UINT8(0, hal_eeprom_read_byte(100));
}

void test_write_bytes_clips_at_end_without_wraparound(void) {
  const uint16_t last = (uint16_t)(hal_eeprom_size() - 1u);
  const uint8_t data[] = {0x11u, 0x22u, 0x33u};

  hal_eeprom_write_byte(0, 0xAAu);
  hal_eeprom_write_bytes(last, data, sizeof(data));

  TEST_ASSERT_EQUAL_UINT8(0x11u, hal_eeprom_read_byte(last));
  TEST_ASSERT_EQUAL_UINT8(0xAAu, hal_eeprom_read_byte(0));
}

void test_read_bytes_pads_out_of_range_tail_with_zero(void) {
  const uint16_t last = (uint16_t)(hal_eeprom_size() - 1u);
  uint8_t out[] = {0xAAu, 0xBBu, 0xCCu};

  hal_eeprom_write_byte(last, 0x5Au);
  hal_eeprom_read_bytes(last, out, sizeof(out));

  TEST_ASSERT_EQUAL_UINT8(0x5Au, out[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00u, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00u, out[2]);
}

void test_write_int_at_end_does_not_wraparound(void) {
  const uint16_t last = (uint16_t)(hal_eeprom_size() - 1u);

  hal_eeprom_write_byte(0, 0x44u);
  hal_eeprom_write_int(last, 0x11223355);

  TEST_ASSERT_EQUAL_UINT8(0x55u, hal_eeprom_read_byte(last));
  TEST_ASSERT_EQUAL_UINT8(0x44u, hal_eeprom_read_byte(0));
  TEST_ASSERT_EQUAL_INT32(0x55, hal_eeprom_read_int(last));
}

/* ---- Status-returning (_ex) API coverage ---- */

void test_ex_byte_and_int_roundtrip(void) {
  uint8_t b = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_write_byte(10, 0xAB));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_read_byte_ex(10, &b));
  TEST_ASSERT_EQUAL_UINT8(0xAB, b);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_eeprom_read_byte_ex(10, NULL));

  int32_t v = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_write_int(20, -123456));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_read_int_ex(20, &v));
  TEST_ASSERT_EQUAL_INT32(-123456, v);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_eeprom_read_int_ex(20, NULL));
}

void test_ex_range_validation_reports_overflow(void) {
  const uint16_t size = hal_eeprom_size();
  const uint8_t data[3] = {1, 2, 3};

  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_eeprom_write_byte(size, 0));
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      hal_eeprom_write_bytes((uint16_t)(size - 2u), data, sizeof(data)));
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_eeprom_write_int((uint16_t)(size - 3u), 0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_eeprom_write_bytes(0, NULL, sizeof(data)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_write_bytes(0, data, 0));
}

void test_ex_uninitialised_and_size_status(void) {
  uint16_t sz = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_size_ex(&sz));
  TEST_ASSERT_EQUAL_UINT16(hal_eeprom_size(), sz);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_eeprom_size_ex(NULL));

  hal_mock_eeprom_reset(); /* size back to 0, not initialised */
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_eeprom_size_ex(&sz));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_eeprom_write_byte(0, 0xFF));

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_eeprom_init((hal_eeprom_type_t)99, 256, 0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, 256, 0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_commit());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_reset());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_sets_type);
  RUN_TEST(test_init_sets_size_at24c256);
  RUN_TEST(test_flash_alias_uses_requested_size);
  RUN_TEST(test_write_read_byte);
  RUN_TEST(test_write_read_int);
  RUN_TEST(test_mock_get_byte_matches_read);
  RUN_TEST(test_commit_sets_flag);
  RUN_TEST(test_progress_callback_runs_on_long_operations);
  RUN_TEST(test_clear_committed_flag);
  RUN_TEST(test_unwritten_address_is_zero);
  RUN_TEST(test_write_bytes_clips_at_end_without_wraparound);
  RUN_TEST(test_read_bytes_pads_out_of_range_tail_with_zero);
  RUN_TEST(test_write_int_at_end_does_not_wraparound);
  RUN_TEST(test_ex_byte_and_int_roundtrip);
  RUN_TEST(test_ex_range_validation_reports_overflow);
  RUN_TEST(test_ex_uninitialised_and_size_status);
  return UNITY_END();
}
