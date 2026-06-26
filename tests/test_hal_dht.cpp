#include "hal/hal_dht.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <stddef.h>
#include <stdint.h>

#define DHT_PIN 7u

static void push_level(bool *seq, size_t *len, bool value) {
  seq[*len] = value;
  *len = *len + 1u;
}

static void push_frame(uint8_t pin, const uint8_t bytes[5]) {
  bool seq[128] = {};
  size_t len = 0u;

  push_level(seq, &len, false); /* response low check */
  push_level(seq, &len, true);  /* response high check */
  push_level(seq, &len, false); /* first bit low preamble */

  for (size_t b = 0u; b < 5u; ++b) {
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
      const bool one = ((bytes[b] >> (7u - bit)) & 0x01u) != 0u;
      push_level(seq, &len, true);  /* low pulse ended */
      push_level(seq, &len, one);   /* level after 30 us */
      push_level(seq, &len, false); /* high pulse ended / bit low */
    }
  }

  hal_mock_gpio_push_read_sequence(pin, seq, len);
}

void setUp(void) {
  hal_mock_gpio_trace_reset();
  hal_mock_gpio_clear_read_sequence(DHT_PIN);
  hal_mock_critical_section_reset();
  hal_mock_mutex_stats_reset();
  hal_mock_set_micros(0u);
}

void tearDown(void) {}

void test_init_configures_idle_pullup(void) {
  hal_dht_config_t cfg = hal_dht_default_config(DHT_PIN);
  hal_dht_t dht = hal_dht_init(&cfg);

  TEST_ASSERT_NOT_NULL(dht);
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_INPUT_PULLUP, hal_mock_gpio_get_mode(DHT_PIN));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(DHT_PIN));

  hal_dht_deinit(dht);
}

void test_read_valid_frame_updates_sample(void) {
  hal_dht_config_t cfg = hal_dht_default_config(DHT_PIN);
  cfg.sensor = HAL_DHT_SENSOR_DHT11;
  hal_dht_t dht = hal_dht_init(&cfg);
  TEST_ASSERT_NOT_NULL(dht);

  const uint8_t frame[5] = {55u, 0u, 24u, 6u, 85u};
  push_frame(DHT_PIN, frame);

  TEST_ASSERT_TRUE(hal_dht_read(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.6f, hal_dht_get_temperature_c(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 76.28f, hal_dht_get_temperature_f(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 55.0f, hal_dht_get_humidity(dht));

  hal_dht_sample_t sample = {};
  TEST_ASSERT_TRUE(hal_dht_get_sample(dht, &sample));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.6f, sample.temperature_c);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 55.0f, sample.humidity);
  TEST_ASSERT_EQUAL_UINT32(40u, hal_mock_critical_enter_count());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_critical_depth());
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());

  hal_dht_deinit(dht);
}

void test_read_dht22_frame_decodes_decimal_humidity_and_temperature(void) {
  hal_dht_config_t cfg = hal_dht_default_config(DHT_PIN);
  cfg.sensor = HAL_DHT_SENSOR_DHT22;
  hal_dht_t dht = hal_dht_init(&cfg);
  TEST_ASSERT_NOT_NULL(dht);

  const uint8_t frame[5] = {0x02u, 0x8fu, 0x00u, 0xeau, 0x7bu};
  push_frame(DHT_PIN, frame);

  TEST_ASSERT_TRUE(hal_dht_read(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.4f, hal_dht_get_temperature_c(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 74.12f, hal_dht_get_temperature_f(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 65.5f, hal_dht_get_humidity(dht));

  hal_dht_sample_t sample = {};
  TEST_ASSERT_TRUE(hal_dht_get_sample(dht, &sample));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.4f, sample.temperature_c);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 65.5f, sample.humidity);

  hal_dht_deinit(dht);
}

void test_read_dht22_frame_decodes_negative_temperature(void) {
  hal_dht_config_t cfg = hal_dht_default_config(DHT_PIN);
  cfg.sensor = HAL_DHT_SENSOR_DHT22;
  hal_dht_t dht = hal_dht_init(&cfg);
  TEST_ASSERT_NOT_NULL(dht);

  const uint8_t frame[5] = {0x01u, 0xf4u, 0x80u, 0x37u, 0xacu};
  push_frame(DHT_PIN, frame);

  TEST_ASSERT_TRUE(hal_dht_read(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.5f, hal_dht_get_temperature_c(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 22.1f, hal_dht_get_temperature_f(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, hal_dht_get_humidity(dht));

  hal_dht_deinit(dht);
}

void test_bad_checksum_does_not_update_previous_sample(void) {
  hal_dht_config_t cfg = hal_dht_default_config(DHT_PIN);
  hal_dht_t dht = hal_dht_init(&cfg);
  TEST_ASSERT_NOT_NULL(dht);

  const uint8_t good[5] = {41u, 0u, 20u, 5u, 66u};
  push_frame(DHT_PIN, good);
  TEST_ASSERT_TRUE(hal_dht_read(dht));

  const uint8_t bad[5] = {60u, 0u, 30u, 1u, 0u};
  push_frame(DHT_PIN, bad);
  TEST_ASSERT_FALSE(hal_dht_read(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.5f, hal_dht_get_temperature_c(dht));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 41.0f, hal_dht_get_humidity(dht));

  hal_dht_deinit(dht);
}

void test_missing_response_high_returns_false_and_restores_interrupts(void) {
  hal_dht_config_t cfg = hal_dht_default_config(DHT_PIN);
  hal_dht_t dht = hal_dht_init(&cfg);
  TEST_ASSERT_NOT_NULL(dht);

  const bool no_response_high[2] = {false, false};
  hal_mock_gpio_push_read_sequence(DHT_PIN, no_response_high,
                                   sizeof(no_response_high) /
                                       sizeof(no_response_high[0]));

  TEST_ASSERT_FALSE(hal_dht_read(dht));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_critical_enter_count());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_critical_depth());
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());

  hal_dht_deinit(dht);
}

void test_invalid_config_is_rejected(void) {
  TEST_ASSERT_NULL(hal_dht_init(NULL));

  hal_dht_config_t cfg = hal_dht_default_config(DHT_PIN);
  cfg.sensor = (hal_dht_sensor_t)99;
  TEST_ASSERT_NULL(hal_dht_init(&cfg));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_configures_idle_pullup);
  RUN_TEST(test_read_valid_frame_updates_sample);
  RUN_TEST(test_read_dht22_frame_decodes_decimal_humidity_and_temperature);
  RUN_TEST(test_read_dht22_frame_decodes_negative_temperature);
  RUN_TEST(test_bad_checksum_does_not_update_previous_sample);
  RUN_TEST(test_missing_response_high_returns_false_and_restores_interrupts);
  RUN_TEST(test_invalid_config_is_rejected);
  return UNITY_END();
}
