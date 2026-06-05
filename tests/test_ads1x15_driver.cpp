#include "utils/unity.h"
#include "hal/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/ads1x15/ads1x15_driver.h"

static void inject_ready_and_value(uint16_t value) {
    const uint8_t rx[] = {
        0x80u, 0x00u,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFFu)
    };
    hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));
}

static void assert_write_frame(int index,
                               uint8_t b0,
                               uint8_t b1,
                               uint8_t b2) {
    uint8_t frame[4] = {};
    TEST_ASSERT_EQUAL_INT(3, hal_mock_i2c_get_write_frame(index, frame, 4));
    TEST_ASSERT_EQUAL_UINT8(b0, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(b1, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(b2, frame[2]);
}

void setUp(void) {
    hal_i2c_init_bus(0, 0, 0, HAL_I2C_CLOCK_STANDARD_HZ);
    hal_mock_i2c_set_busy(false);
    hal_mock_i2c_reset_write_log();
    hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_begin_validates_address_and_ack(void) {
    ADS1115 ads(0x48);
    TEST_ASSERT_TRUE(ads.begin());

    ADS1115 invalid(0x47);
    TEST_ASSERT_FALSE(invalid.begin());

    hal_mock_i2c_set_busy(true);
    ADS1115 absent(0x48);
    TEST_ASSERT_FALSE(absent.begin());
}

void test_ads1115_single_ended_read_writes_expected_config(void) {
    ADS1115 ads(0x48);
    inject_ready_and_value(0x1234u);

    TEST_ASSERT_EQUAL_INT16(0x1234, ads.readADC(2));
    TEST_ASSERT_EQUAL_INT8(ADS1X15_OK, ads.getError());
    TEST_ASSERT_EQUAL_UINT8(0x02, ads.lastRequest());

    assert_write_frame(0, 0x01u, 0xE1u, 0x8Bu);
}

void test_gain_mode_data_rate_and_voltage_mapping_match_ads1x15(void) {
    ADS1115 ads(0x48);

    ads.setGain(ADS1X15_GAIN_2048MV);
    TEST_ASSERT_EQUAL_UINT8(ADS1X15_GAIN_2048MV, ads.getGain());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.048f, ads.getMaxVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.048f, ads.toVoltage(32767.0f));

    ads.setMode(ADS1X15_MODE_CONTINUOUS);
    TEST_ASSERT_EQUAL_UINT8(ADS1X15_MODE_CONTINUOUS, ads.getMode());

    ads.setDataRate(ADS1X15_DATARATE_7);
    TEST_ASSERT_EQUAL_UINT8(ADS1X15_DATARATE_7, ads.getDataRate());

    ads.setGain(123);
    TEST_ASSERT_EQUAL_UINT8(ADS1X15_GAIN_6144MV, ads.getGain());
}

void test_ads1015_read_shifts_12_bit_result(void) {
    ADS1015 ads(0x48);
    inject_ready_and_value(0x7FF0u);

    TEST_ASSERT_EQUAL_INT16(2047, ads.readADC(0));
    assert_write_frame(0, 0x01u, 0xC1u, 0x8Bu);
}

void test_async_differential_request_tracks_last_request(void) {
    ADS1115 ads(0x48);
    ads.requestADC_Differential_1_3();

    TEST_ASSERT_EQUAL_UINT8(0x31, ads.lastRequest());
    assert_write_frame(0, 0x01u, 0xA1u, 0x8Bu);
}

void test_comparator_thresholds_are_big_endian_register_writes(void) {
    ADS1115 ads(0x48);

    ads.setComparatorThresholdLow((int16_t)0x1234);
    ads.setComparatorThresholdHigh((int16_t)0x5678);

    assert_write_frame(0, 0x02u, 0x12u, 0x34u);
    assert_write_frame(1, 0x03u, 0x56u, 0x78u);
}

void test_set_wire_clock_uses_hal_i2c_clock(void) {
    ADS1115 ads(0x48, 1);

    ads.setWireClock(HAL_I2C_CLOCK_FAST_HZ);

    TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_FAST_HZ, ads.getWireClock());
    TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_FAST_HZ,
                             hal_mock_i2c_get_clock_hz_bus(1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_validates_address_and_ack);
    RUN_TEST(test_ads1115_single_ended_read_writes_expected_config);
    RUN_TEST(test_gain_mode_data_rate_and_voltage_mapping_match_ads1x15);
    RUN_TEST(test_ads1015_read_shifts_12_bit_result);
    RUN_TEST(test_async_differential_request_tracks_last_request);
    RUN_TEST(test_comparator_thresholds_are_big_endian_register_writes);
    RUN_TEST(test_set_wire_clock_uses_hal_i2c_clock);
    return UNITY_END();
}
