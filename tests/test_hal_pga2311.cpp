#include "hal/audio/hal_pga2311.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#ifdef HAL_ENABLE_PGA2311

void setUp(void) { hal_mock_spi_reset(); }

void tearDown(void) {}

static uint32_t spi_tx_len(uint8_t bus) {
  return (uint32_t)hal_mock_spi_get_tx(bus, NULL, 0u);
}

static void assert_spi_tail(uint8_t bus, const uint8_t *tail, size_t len) {
  uint8_t tx[64] = {};
  const size_t total = hal_mock_spi_get_tx(bus, tx, sizeof(tx));
  TEST_ASSERT_TRUE_MESSAGE(total <= sizeof(tx),
                           "SPI log longer than test buffer");
  TEST_ASSERT_TRUE_MESSAGE(total >= len, "SPI log shorter than expected tail");

  const size_t start = total - len;
  for (size_t i = 0; i < len; ++i) {
    TEST_ASSERT_EQUAL_UINT8(tail[i], tx[start + i]);
  }
}

void test_default_config_matches_module_defaults(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();

  TEST_ASSERT_EQUAL_UINT8(0u, cfg.spi_bus);
  TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_PIN_NONE, cfg.cs_pin);
  TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_MUTE_PIN_NONE, cfg.mute_pin);
  TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_MUTE_ACTIVE_LOW,
                          (uint8_t)cfg.mute_polarity);
  TEST_ASSERT_EQUAL_UINT32(HAL_PGA2311_SPI_DEFAULT_HZ, cfg.spi_clock_hz);
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MSBFIRST, cfg.spi_bit_order);
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE0, cfg.spi_mode);
  TEST_ASSERT_FALSE(cfg.start_muted);
}

void test_init_validation_rejects_invalid_config(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();

  /* CS pin is required. */
  TEST_ASSERT_NULL(hal_pga2311_init(&cfg));

  cfg.cs_pin = 5u;
  cfg.mute_pin = 5u; /* mute and CS cannot share a pin */
  TEST_ASSERT_NULL(hal_pga2311_init(&cfg));

  cfg.mute_pin = HAL_PGA2311_MUTE_PIN_NONE;
  cfg.spi_mode = 99u;
  TEST_ASSERT_NULL(hal_pga2311_init(&cfg));

  cfg.spi_mode = HAL_SPI_MODE0;
  cfg.spi_bit_order = 99u;
  TEST_ASSERT_NULL(hal_pga2311_init(&cfg));
}

void test_status_init_reports_invalid_arguments_and_pool_exhaustion(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 5u;
  hal_pga2311_t h = (hal_pga2311_t)1;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pga2311_init_ex(NULL, &h));
  TEST_ASSERT_NULL(h);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pga2311_init_ex(&cfg, NULL));

  hal_pga2311_t handles[HAL_PGA2311_MAX_INSTANCES] = {};
  for (size_t i = 0; i < HAL_PGA2311_MAX_INSTANCES; ++i) {
    cfg.cs_pin = (uint8_t)(40u + i);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_init_ex(&cfg, &handles[i]));
    TEST_ASSERT_NOT_NULL(handles[i]);
  }

  h = (hal_pga2311_t)1;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_pga2311_init_ex(&cfg, &h));
  TEST_ASSERT_NULL(h);

  for (size_t i = 0; i < HAL_PGA2311_MAX_INSTANCES; ++i) {
    hal_pga2311_deinit(handles[i]);
  }
}

void test_status_init_propagates_start_muted_spi_failure_and_releases_slot(
    void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 6u;
  cfg.start_muted = true;
  hal_pga2311_t h = (hal_pga2311_t)1;

  hal_mock_spi_fail_next_write(0u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_pga2311_init_ex(&cfg, &h));
  TEST_ASSERT_NULL(h);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(cfg.cs_pin));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_init_ex(&cfg, &h));
  TEST_ASSERT_NOT_NULL(h);
  hal_pga2311_deinit(h);
}

void test_init_sets_gpio_defaults_for_cs_and_hw_mute(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 10u;
  cfg.mute_pin = 11u;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);

  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(10u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10u));
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(11u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(11u)); /* active-low unmuted */
  TEST_ASSERT_FALSE(hal_pga2311_is_muted(h));

  hal_pga2311_deinit(h);
}

void test_set_raw_writes_two_byte_spi_frame_with_configured_settings(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.spi_bus = 1u;
  cfg.cs_pin = 12u;
  cfg.spi_clock_hz = 2000000UL;
  cfg.spi_mode = HAL_SPI_MODE1;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);

  TEST_ASSERT_TRUE(hal_pga2311_set_raw(h, 0x12u, 0x34u));

  /* PGA2311 expects the RIGHT channel byte first, then LEFT: set_raw(left,
   * right) = (0x12, 0x34) must be framed on the wire as {right, left}. */
  const uint8_t expected_tail[2] = {0x34u, 0x12u};
  assert_spi_tail(1u, expected_tail, sizeof(expected_tail));

  TEST_ASSERT_EQUAL_UINT8(1u, hal_mock_spi_get_bus());
  TEST_ASSERT_EQUAL_UINT32(2000000UL, hal_mock_spi_get_clock_hz(1u));
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MSBFIRST, hal_mock_spi_get_bit_order(1u));
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE1, hal_mock_spi_get_data_mode(1u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(12u));

  hal_pga2311_deinit(h);
}

void test_status_write_failure_preserves_target_and_allows_retry(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 13u;
  hal_pga2311_t h = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_init_ex(&cfg, &h));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_set_raw_ex(h, 0x11u, 0x22u));

  hal_mock_spi_fail_next_write(0u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_pga2311_set_raw_ex(h, 0x33u, 0x44u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(cfg.cs_pin));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));

  uint8_t left = 0u;
  uint8_t right = 0u;
  TEST_ASSERT_TRUE(hal_pga2311_get_target_raw(h, &left, &right));
  TEST_ASSERT_EQUAL_UINT8(0x11u, left);
  TEST_ASSERT_EQUAL_UINT8(0x22u, right);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_set_raw_ex(h, 0x33u, 0x44u));
  TEST_ASSERT_TRUE(hal_pga2311_get_target_raw(h, &left, &right));
  TEST_ASSERT_EQUAL_UINT8(0x33u, left);
  TEST_ASSERT_EQUAL_UINT8(0x44u, right);

  hal_pga2311_deinit(h);
}

void test_status_soft_mute_failure_preserves_state_and_allows_retry(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 14u;
  hal_pga2311_t h = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_init_ex(&cfg, &h));

  hal_mock_spi_fail_next_write(0u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_pga2311_set_mute_ex(h, true));
  TEST_ASSERT_FALSE(hal_pga2311_is_muted(h));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_set_mute_ex(h, true));
  TEST_ASSERT_TRUE(hal_pga2311_is_muted(h));

  hal_mock_spi_fail_next_write(0u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_pga2311_set_mute_ex(h, false));
  TEST_ASSERT_TRUE(hal_pga2311_is_muted(h));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_set_mute_ex(h, false));
  TEST_ASSERT_FALSE(hal_pga2311_is_muted(h));

  hal_pga2311_deinit(h);
}

void test_gain_conversion_and_set_gain_db(void) {
  uint8_t code = 0u;
  int16_t half_db = 0;

  TEST_ASSERT_TRUE(hal_pga2311_gain_half_db_to_raw(0, &code));
  TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_CODE_0DB, code);
  TEST_ASSERT_TRUE(
      hal_pga2311_gain_half_db_to_raw(HAL_PGA2311_GAIN_HALF_DB_MIN, &code));
  TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_CODE_MIN, code);
  TEST_ASSERT_TRUE(
      hal_pga2311_gain_half_db_to_raw(HAL_PGA2311_GAIN_HALF_DB_MAX, &code));
  TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_CODE_MAX, code);

  TEST_ASSERT_FALSE(
      hal_pga2311_gain_half_db_to_raw(HAL_PGA2311_GAIN_HALF_DB_MIN - 1, &code));
  TEST_ASSERT_FALSE(
      hal_pga2311_gain_half_db_to_raw(HAL_PGA2311_GAIN_HALF_DB_MAX + 1, &code));

  TEST_ASSERT_TRUE(
      hal_pga2311_raw_to_gain_half_db(HAL_PGA2311_CODE_0DB, &half_db));
  TEST_ASSERT_EQUAL_INT16(0, half_db);
  TEST_ASSERT_FALSE(
      hal_pga2311_raw_to_gain_half_db(HAL_PGA2311_CODE_MUTE, &half_db));

  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 7u;
  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);

  TEST_ASSERT_TRUE(hal_pga2311_set_gain_db(h, 0.0f, -95.5f));
  /* left=0 dB (CODE_0DB), right=-95.5 dB (CODE_MIN); wire order is {right,
   * left}. */
  const uint8_t expected_tail[2] = {HAL_PGA2311_CODE_MIN, HAL_PGA2311_CODE_0DB};
  assert_spi_tail(0u, expected_tail, sizeof(expected_tail));

  TEST_ASSERT_FALSE(hal_pga2311_set_gain_db(h, -96.0f, 0.0f));
  TEST_ASSERT_FALSE(hal_pga2311_set_gain_db(h, 0.0f, 32.0f));

  hal_pga2311_deinit(h);
}

void test_soft_mute_caches_and_restores_volume(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 8u;
  cfg.mute_pin = HAL_PGA2311_MUTE_PIN_NONE;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);

  TEST_ASSERT_TRUE(hal_pga2311_set_raw(h, 0xAAu, 0x55u));
  uint32_t tx_before_mute = spi_tx_len(0u);

  TEST_ASSERT_TRUE(hal_pga2311_set_mute(h, true));
  TEST_ASSERT_TRUE(hal_pga2311_is_muted(h));
  const uint8_t mute_tail[2] = {HAL_PGA2311_CODE_MUTE, HAL_PGA2311_CODE_MUTE};
  assert_spi_tail(0u, mute_tail, sizeof(mute_tail));

  uint32_t tx_after_mute = spi_tx_len(0u);
  TEST_ASSERT_TRUE(hal_pga2311_set_raw(h, 0x33u, 0x44u));
  TEST_ASSERT_EQUAL_UINT32(tx_after_mute, spi_tx_len(0u));

  uint8_t left = 0u;
  uint8_t right = 0u;
  TEST_ASSERT_TRUE(hal_pga2311_get_target_raw(h, &left, &right));
  TEST_ASSERT_EQUAL_UINT8(0x33u, left);
  TEST_ASSERT_EQUAL_UINT8(0x44u, right);

  TEST_ASSERT_TRUE(hal_pga2311_set_mute(h, false));
  TEST_ASSERT_FALSE(hal_pga2311_is_muted(h));
  /* target left=0x33, right=0x44; restored on unmute as wire order {right,
   * left}. */
  const uint8_t unmute_tail[2] = {0x44u, 0x33u};
  assert_spi_tail(0u, unmute_tail, sizeof(unmute_tail));
  TEST_ASSERT_TRUE(spi_tx_len(0u) > tx_before_mute);

  hal_pga2311_deinit(h);
}

void test_hw_mute_toggles_pin_without_extra_spi_writes(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 20u;
  cfg.mute_pin = 21u;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);

  TEST_ASSERT_TRUE(hal_pga2311_set_raw(h, 0x21u, 0x43u));
  const uint32_t tx_before = spi_tx_len(0u);

  TEST_ASSERT_TRUE(hal_pga2311_set_mute(h, true));
  TEST_ASSERT_TRUE(hal_pga2311_is_muted(h));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_EQUAL_UINT32(tx_before, spi_tx_len(0u));

  TEST_ASSERT_TRUE(hal_pga2311_set_mute(h, false));
  TEST_ASSERT_FALSE(hal_pga2311_is_muted(h));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_EQUAL_UINT32(tx_before, spi_tx_len(0u));

  hal_pga2311_deinit(h);
}

void test_start_muted_without_hw_mute_pin_writes_mute_frame(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 30u;
  cfg.start_muted = true;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_TRUE(hal_pga2311_is_muted(h));

  const uint8_t mute_tail[2] = {HAL_PGA2311_CODE_MUTE, HAL_PGA2311_CODE_MUTE};
  assert_spi_tail(0u, mute_tail, sizeof(mute_tail));

  hal_pga2311_deinit(h);
}

void test_start_muted_with_hw_mute_pin_sets_pin_without_spi_write(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 31u;
  cfg.mute_pin = 32u;
  cfg.start_muted = true;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_TRUE(hal_pga2311_is_muted(h));
  TEST_ASSERT_FALSE(
      hal_mock_gpio_get_state(32u)); /* active-low mute asserted */
  TEST_ASSERT_EQUAL_UINT32(0u, spi_tx_len(0u));

  hal_pga2311_deinit(h);
}

void test_hw_mute_active_high_polarity_is_respected(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 33u;
  cfg.mute_pin = 34u;
  cfg.mute_polarity = HAL_PGA2311_MUTE_ACTIVE_HIGH;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(34u)); /* unmuted for active-high */

  TEST_ASSERT_TRUE(hal_pga2311_set_mute(h, true));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(34u));
  TEST_ASSERT_TRUE(hal_pga2311_set_mute(h, false));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(34u));

  hal_pga2311_deinit(h);
}

void test_set_raw_both_writes_same_code_to_both_channels(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 35u;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);

  TEST_ASSERT_TRUE(hal_pga2311_set_raw_both(h, 0xA5u));
  const uint8_t expected_tail[2] = {0xA5u, 0xA5u};
  assert_spi_tail(0u, expected_tail, sizeof(expected_tail));

  hal_pga2311_deinit(h);
}

void test_set_gain_half_db_and_set_gain_db_both_cover_rounding(void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 36u;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);

  TEST_ASSERT_TRUE(hal_pga2311_set_gain_half_db(h, -191, 63));
  /* left=-95.5 dB -> 0x01, right=+31.5 dB -> 0xFF, wire order {right,left}. */
  const uint8_t half_db_tail[2] = {0xFFu, 0x01u};
  assert_spi_tail(0u, half_db_tail, sizeof(half_db_tail));

  TEST_ASSERT_TRUE(hal_pga2311_set_gain_db_both(h, 0.24f));
  /* 0.24 dB rounds to 0.0 dB -> code 0xC0 on both channels. */
  const uint8_t db_tail[2] = {HAL_PGA2311_CODE_0DB, HAL_PGA2311_CODE_0DB};
  assert_spi_tail(0u, db_tail, sizeof(db_tail));

  TEST_ASSERT_FALSE(hal_pga2311_set_gain_half_db(h, -192, 0));
  TEST_ASSERT_FALSE(hal_pga2311_set_gain_half_db(h, 0, 64));
  TEST_ASSERT_FALSE(hal_pga2311_set_gain_db_both(h, -96.0f));

  hal_pga2311_deinit(h);
}

void test_get_target_gain_half_db_reports_cached_target_and_rejects_mute_code(
    void) {
  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.cs_pin = 37u;

  hal_pga2311_t h = hal_pga2311_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);

  TEST_ASSERT_TRUE(
      hal_pga2311_set_raw(h, HAL_PGA2311_CODE_0DB, HAL_PGA2311_CODE_MIN));

  int16_t left_half_db = 0;
  int16_t right_half_db = 0;
  TEST_ASSERT_TRUE(
      hal_pga2311_get_target_gain_half_db(h, &left_half_db, &right_half_db));
  TEST_ASSERT_EQUAL_INT16(0, left_half_db);
  TEST_ASSERT_EQUAL_INT16(HAL_PGA2311_GAIN_HALF_DB_MIN, right_half_db);

  TEST_ASSERT_TRUE(
      hal_pga2311_set_raw(h, HAL_PGA2311_CODE_MUTE, HAL_PGA2311_CODE_0DB));
  TEST_ASSERT_FALSE(
      hal_pga2311_get_target_gain_half_db(h, &left_half_db, &right_half_db));

  hal_pga2311_deinit(h);
}

void test_api_returns_false_for_invalid_handles_or_null_outputs(void) {
  uint8_t code = 0u;
  int16_t half_db = 0;
  uint8_t left = 0u, right = 0u;

  TEST_ASSERT_FALSE(hal_pga2311_set_raw(NULL, 0x01u, 0x02u));
  TEST_ASSERT_FALSE(hal_pga2311_set_raw_both(NULL, 0x12u));
  TEST_ASSERT_FALSE(hal_pga2311_set_gain_half_db(NULL, 0, 0));
  TEST_ASSERT_FALSE(hal_pga2311_set_gain_db(NULL, 0.0f, 0.0f));
  TEST_ASSERT_FALSE(hal_pga2311_set_gain_db_both(NULL, 0.0f));
  TEST_ASSERT_FALSE(hal_pga2311_set_mute(NULL, true));
  TEST_ASSERT_FALSE(hal_pga2311_get_target_raw(NULL, &left, &right));
  TEST_ASSERT_FALSE(hal_pga2311_get_target_raw(NULL, NULL, &right));
  TEST_ASSERT_FALSE(
      hal_pga2311_get_target_gain_half_db(NULL, &half_db, &half_db));
  TEST_ASSERT_FALSE(hal_pga2311_get_target_gain_half_db(NULL, NULL, &half_db));

  TEST_ASSERT_FALSE(hal_pga2311_gain_half_db_to_raw(0, NULL));
  TEST_ASSERT_FALSE(
      hal_pga2311_raw_to_gain_half_db(HAL_PGA2311_CODE_0DB, NULL));
}

void test_status_setters_and_converters_report_invalid_arguments(void) {
  uint8_t code = 0u;
  int16_t half_db = 0;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pga2311_set_raw_ex(NULL, 1u, 2u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pga2311_set_raw_both_ex(NULL, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pga2311_set_gain_half_db_ex(NULL, 0, 0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pga2311_set_gain_db_ex(NULL, 0.0f, 0.0f));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pga2311_set_gain_db_both_ex(NULL, 0.0f));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pga2311_set_mute_ex(NULL, true));

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pga2311_gain_half_db_to_raw_ex(
                            HAL_PGA2311_GAIN_HALF_DB_MIN - 1, &code));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pga2311_gain_half_db_to_raw_ex(0, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_gain_half_db_to_raw_ex(0, &code));
  TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_CODE_0DB, code);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pga2311_raw_to_gain_half_db_ex(
                                        HAL_PGA2311_CODE_MUTE, &half_db));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pga2311_raw_to_gain_half_db_ex(
                                        HAL_PGA2311_CODE_0DB, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pga2311_raw_to_gain_half_db_ex(
                                    HAL_PGA2311_CODE_0DB, &half_db));
  TEST_ASSERT_EQUAL_INT16(0, half_db);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_matches_module_defaults);
  RUN_TEST(test_init_validation_rejects_invalid_config);
  RUN_TEST(test_status_init_reports_invalid_arguments_and_pool_exhaustion);
  RUN_TEST(
      test_status_init_propagates_start_muted_spi_failure_and_releases_slot);
  RUN_TEST(test_init_sets_gpio_defaults_for_cs_and_hw_mute);
  RUN_TEST(test_set_raw_writes_two_byte_spi_frame_with_configured_settings);
  RUN_TEST(test_status_write_failure_preserves_target_and_allows_retry);
  RUN_TEST(test_status_soft_mute_failure_preserves_state_and_allows_retry);
  RUN_TEST(test_gain_conversion_and_set_gain_db);
  RUN_TEST(test_soft_mute_caches_and_restores_volume);
  RUN_TEST(test_hw_mute_toggles_pin_without_extra_spi_writes);
  RUN_TEST(test_start_muted_without_hw_mute_pin_writes_mute_frame);
  RUN_TEST(test_start_muted_with_hw_mute_pin_sets_pin_without_spi_write);
  RUN_TEST(test_hw_mute_active_high_polarity_is_respected);
  RUN_TEST(test_set_raw_both_writes_same_code_to_both_channels);
  RUN_TEST(test_set_gain_half_db_and_set_gain_db_both_cover_rounding);
  RUN_TEST(
      test_get_target_gain_half_db_reports_cached_target_and_rejects_mute_code);
  RUN_TEST(test_api_returns_false_for_invalid_handles_or_null_outputs);
  RUN_TEST(test_status_setters_and_converters_report_invalid_arguments);
  return UNITY_END();
}

#else

int main(void) { return 0; }

#endif /* HAL_ENABLE_PGA2311 */
