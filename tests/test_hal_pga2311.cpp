#include "hal/hal_pga2311.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#ifdef HAL_ENABLE_PGA2311

void setUp(void) {
    hal_mock_spi_reset();
}

void tearDown(void) {}

static uint32_t spi_tx_len(uint8_t bus) {
    return (uint32_t)hal_mock_spi_get_tx(bus, NULL, 0u);
}

static void assert_spi_tail(uint8_t bus, const uint8_t *tail, size_t len) {
    uint8_t tx[64] = {};
    const size_t total = hal_mock_spi_get_tx(bus, tx, sizeof(tx));
    TEST_ASSERT_TRUE_MESSAGE(total <= sizeof(tx), "SPI log longer than test buffer");
    TEST_ASSERT_TRUE_MESSAGE(total >= len, "SPI log shorter than expected tail");

    const size_t start = total - len;
    for (size_t i = 0; i < len; ++i) {
        TEST_ASSERT_EQUAL_UINT8(tail[i], tx[start + i]);
    }
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

void test_gain_conversion_and_set_gain_db(void) {
    uint8_t code = 0u;
    int16_t half_db = 0;

    TEST_ASSERT_TRUE(hal_pga2311_gain_half_db_to_raw(0, &code));
    TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_CODE_0DB, code);
    TEST_ASSERT_TRUE(hal_pga2311_gain_half_db_to_raw(HAL_PGA2311_GAIN_HALF_DB_MIN, &code));
    TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_CODE_MIN, code);
    TEST_ASSERT_TRUE(hal_pga2311_gain_half_db_to_raw(HAL_PGA2311_GAIN_HALF_DB_MAX, &code));
    TEST_ASSERT_EQUAL_UINT8(HAL_PGA2311_CODE_MAX, code);

    TEST_ASSERT_FALSE(hal_pga2311_gain_half_db_to_raw(HAL_PGA2311_GAIN_HALF_DB_MIN - 1, &code));
    TEST_ASSERT_FALSE(hal_pga2311_gain_half_db_to_raw(HAL_PGA2311_GAIN_HALF_DB_MAX + 1, &code));

    TEST_ASSERT_TRUE(hal_pga2311_raw_to_gain_half_db(HAL_PGA2311_CODE_0DB, &half_db));
    TEST_ASSERT_EQUAL_INT16(0, half_db);
    TEST_ASSERT_FALSE(hal_pga2311_raw_to_gain_half_db(HAL_PGA2311_CODE_MUTE, &half_db));

    hal_pga2311_config_t cfg = hal_pga2311_default_config();
    cfg.cs_pin = 7u;
    hal_pga2311_t h = hal_pga2311_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    TEST_ASSERT_TRUE(hal_pga2311_set_gain_db(h, 0.0f, -95.5f));
    /* left=0 dB (CODE_0DB), right=-95.5 dB (CODE_MIN); wire order is {right, left}. */
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
    /* target left=0x33, right=0x44; restored on unmute as wire order {right, left}. */
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_validation_rejects_invalid_config);
    RUN_TEST(test_init_sets_gpio_defaults_for_cs_and_hw_mute);
    RUN_TEST(test_set_raw_writes_two_byte_spi_frame_with_configured_settings);
    RUN_TEST(test_gain_conversion_and_set_gain_db);
    RUN_TEST(test_soft_mute_caches_and_restores_volume);
    RUN_TEST(test_hw_mute_toggles_pin_without_extra_spi_writes);
    RUN_TEST(test_start_muted_without_hw_mute_pin_writes_mute_frame);
    return UNITY_END();
}

#else

int main(void) {
    return 0;
}

#endif /* HAL_ENABLE_PGA2311 */
