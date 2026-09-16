#include "hal/analog/hal_adc_scan.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <string.h>

// Three pins, four frames per block, two blocks in one buffer.
#define PINS 3u
#define FRAMES 4u
static uint16_t s_buffer[2u * FRAMES * PINS] __attribute__((aligned(4)));
static uint32_t s_marker_calls;

static uint32_t marker(void *user) {
  ++s_marker_calls;
  return *(const uint32_t *)user + s_marker_calls;
}

static hal_adc_scan_config_t base_config(void) {
  hal_adc_scan_config_t config = {};
  config.pins[0] = 26u;
  config.pins[1] = 28u;
  config.pins[2] = HAL_ADC_SCAN_PIN_TEMPERATURE;
  config.pin_count = PINS;
  config.conversion_period_ns = 2000u;
  config.buffer = s_buffer;
  config.block_frames = FRAMES;
  return config;
}

static void fill_frames(uint16_t *frames, uint16_t base) {
  for (uint16_t k = 0u; k < FRAMES; ++k) {
    for (uint16_t j = 0u; j < PINS; ++j) {
      frames[(k * PINS) + j] = (uint16_t)(base + (k * 10u) + j);
    }
  }
}

void setUp(void) {
  (void)hal_adc_scan_stop();
  s_marker_calls = 0u;
  memset(s_buffer, 0, sizeof(s_buffer));
  hal_mock_set_micros(1000u);
}

void tearDown(void) { (void)hal_adc_scan_stop(); }

void test_scan_rejects_invalid_configuration(void) {
  hal_adc_scan_config_t config = base_config();
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(NULL));
  config.pin_count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  config = base_config();
  config.pin_count = HAL_ADC_SCAN_MAX_PINS + 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  config = base_config();
  config.pins[1] = 26u; // duplicate
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  config = base_config();
  config.pins[1] = 200u; // not an ADC pin on the mock
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  config = base_config();
  config.conversion_period_ns = HAL_ADC_SCAN_MIN_CONVERSION_NS - 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  config = base_config();
  config.conversion_period_ns = HAL_ADC_SCAN_MAX_CONVERSION_NS + 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  config = base_config();
  config.buffer = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  config = base_config();
  config.buffer = s_buffer + 1u; // 2-byte aligned only
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  config = base_config();
  config.block_frames = HAL_ADC_SCAN_MIN_BLOCK_FRAMES - 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  config = base_config();
  config.block_frames = (HAL_ADC_SCAN_MAX_BLOCK_SAMPLES / PINS) + 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_start(&config));
  TEST_ASSERT_FALSE(hal_adc_scan_is_running());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_adc_scan_frame_period_ns());
}

void test_scan_lifecycle_and_frame_geometry(void) {
  hal_adc_scan_config_t config = base_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_start(&config));
  TEST_ASSERT_TRUE(hal_adc_scan_is_running());
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_adc_scan_start(&config));
  // Mock frames keep the configured order at the requested period.
  TEST_ASSERT_EQUAL_UINT32(6000u, hal_adc_scan_frame_period_ns());
  TEST_ASSERT_EQUAL_UINT8(0u, hal_adc_scan_pin_position(26u));
  TEST_ASSERT_EQUAL_UINT8(1u, hal_adc_scan_pin_position(28u));
  TEST_ASSERT_EQUAL_UINT8(
      2u, hal_adc_scan_pin_position(HAL_ADC_SCAN_PIN_TEMPERATURE));
  TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, hal_adc_scan_pin_position(27u));

  hal_adc_scan_block_t block = {};
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_adc_scan_take(&block));
  uint16_t raw = 77u;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_adc_scan_latest(28u, &raw));
  TEST_ASSERT_EQUAL_UINT16(77u, raw);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_adc_scan_latest(27u, &raw));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_latest(28u, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adc_scan_take(NULL));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_stop());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_stop());
  TEST_ASSERT_FALSE(hal_adc_scan_is_running());
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_adc_scan_take(&block));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_adc_scan_latest(28u, &raw));
  TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, hal_adc_scan_pin_position(26u));
}

void test_scan_hands_out_each_completed_block_once_with_its_marker(void) {
  uint32_t marker_base = 500u;
  hal_adc_scan_config_t config = base_config();
  config.marker = marker;
  config.marker_user = &marker_base;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_start(&config));

  uint16_t frames[FRAMES * PINS];
  fill_frames(frames, 100u);
  hal_mock_set_micros(2000u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_adc_scan_complete(frames, FRAMES));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_mock_adc_scan_complete(frames, FRAMES - 1u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_mock_adc_scan_complete(NULL, FRAMES));

  hal_adc_scan_block_t block = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_take(&block));
  TEST_ASSERT_EQUAL_PTR(s_buffer, block.samples);
  TEST_ASSERT_EQUAL_UINT32(FRAMES, block.frames);
  TEST_ASSERT_EQUAL_UINT8(PINS, block.pin_count);
  TEST_ASSERT_EQUAL_UINT32(1u, block.sequence);
  TEST_ASSERT_EQUAL_UINT32(2000u, block.completed_us);
  TEST_ASSERT_EQUAL_UINT32(501u, block.marker);
  // Frame-major: frame 2, position of pin 28.
  const uint8_t position = hal_adc_scan_pin_position(28u);
  TEST_ASSERT_EQUAL_UINT16(121u, block.samples[(2u * PINS) + position]);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_adc_scan_take(&block));

  uint16_t raw = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_adc_scan_latest(HAL_ADC_SCAN_PIN_TEMPERATURE, &raw));
  TEST_ASSERT_EQUAL_UINT16(132u, raw);

  // The second block lands in the other half; a block a slow consumer
  // skipped is never handed out, only visible as a sequence gap.
  fill_frames(frames, 200u);
  hal_mock_set_micros(3000u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_adc_scan_complete(frames, FRAMES));
  fill_frames(frames, 300u);
  hal_mock_set_micros(4000u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_adc_scan_complete(frames, FRAMES));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_take(&block));
  TEST_ASSERT_EQUAL_PTR(s_buffer, block.samples);
  TEST_ASSERT_EQUAL_UINT32(3u, block.sequence);
  TEST_ASSERT_EQUAL_UINT32(4000u, block.completed_us);
  TEST_ASSERT_EQUAL_UINT32(503u, block.marker);
  TEST_ASSERT_EQUAL_UINT16(300u, block.samples[0]);
  TEST_ASSERT_EQUAL_UINT16(200u, s_buffer[FRAMES * PINS]);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_latest(26u, &raw));
  TEST_ASSERT_EQUAL_UINT16(330u, raw);
  TEST_ASSERT_EQUAL_UINT32(3u, s_marker_calls);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_stop());
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_mock_adc_scan_complete(frames, FRAMES));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_scan_rejects_invalid_configuration);
  RUN_TEST(test_scan_lifecycle_and_frame_geometry);
  RUN_TEST(test_scan_hands_out_each_completed_block_once_with_its_marker);
  return UNITY_END();
}
