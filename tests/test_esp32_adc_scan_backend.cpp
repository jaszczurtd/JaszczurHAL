// The ESP32-S3 ADC scan backend on the fake continuous driver: the stream is
// configured as documented, decoded into blocks with resynchronisation, and
// the newest sample is fresh whether or not the application takes blocks.

#include "esp_idf_fake.h"

#include "hal/analog/jh_adc_scan_backend.h"
#include "hal/impl/esp32/jh_esp32_adc_scan.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <string.h>

#define PINS 2u
#define FRAMES 4u
#define BLOCK (FRAMES * PINS)
#define PIN_A 3u /* GPIO3 -> ADC1 channel 2 */
#define PIN_B 4u /* GPIO4 -> ADC1 channel 3 */

static uint16_t s_ring[2u * BLOCK] __attribute__((aligned(4)));
static uint32_t s_micros;
static jh_esp32_adc_scan_reader_fn s_reader;
static uint8_t s_positions[HAL_ADC_SCAN_MAX_PINS];
static uint32_t s_period_ns;
static uint32_t s_marker_calls;
static uint32_t s_marker_base = 700u;

extern "C" {
uint32_t hal_micros(void) { return s_micros; }
void jh_esp32_adc_set_scan_reader(jh_esp32_adc_scan_reader_fn reader) {
  s_reader = reader;
}
}

static uint32_t marker(void *user) {
  ++s_marker_calls;
  return *(const uint32_t *)user + s_marker_calls;
}

static hal_adc_scan_config_t config(void) {
  hal_adc_scan_config_t cfg = {};
  cfg.pins[0] = PIN_A;
  cfg.pins[1] = PIN_B;
  cfg.pin_count = PINS;
  cfg.conversion_period_ns = 12000u;
  cfg.buffer = s_ring;
  cfg.block_frames = FRAMES;
  cfg.marker = marker;
  cfg.marker_user = &s_marker_base;
  return cfg;
}

static void start(void) {
  const hal_adc_scan_config_t cfg = config();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
}

// Samples carry their frame index and position: frame f of pin p is
// 100 * f + p, so any value names where it came from.
static void feed_frames(uint32_t first_frame, uint32_t count) {
  for (uint32_t f = first_frame; f < first_frame + count; ++f) {
    fake_idf::adc_continuous_push(0u, 2u, (uint16_t)((100u * f) + 0u));
    fake_idf::adc_continuous_push(0u, 3u, (uint16_t)((100u * f) + 1u));
  }
}

void setUp(void) {
  (void)jh_adc_scan_stop();
  fake_idf::reset();
  memset(s_ring, 0xFF, sizeof(s_ring));
  s_micros = 1000u;
  s_reader = NULL;
  s_marker_calls = 0u;
}

void tearDown(void) { (void)jh_adc_scan_stop(); }

void test_start_configures_the_driver_and_stop_releases_it(void) {
  start();
  const fake_idf::AdcContinuousState &state = fake_idf::adc_continuous_state();
  TEST_ASSERT_TRUE(state.live);
  TEST_ASSERT_TRUE(state.started);
  TEST_ASSERT_EQUAL_UINT32(83333u, state.sample_freq_hz);
  TEST_ASSERT_EQUAL_INT(ADC_CONV_SINGLE_UNIT_1, state.conv_mode);
  TEST_ASSERT_EQUAL_size_t(PINS, state.pattern.size());
  TEST_ASSERT_EQUAL_UINT8(2u, state.pattern[0].channel);
  TEST_ASSERT_EQUAL_UINT8(3u, state.pattern[1].channel);
  TEST_ASSERT_EQUAL_UINT8(0u, state.pattern[0].unit);
  TEST_ASSERT_EQUAL_UINT8(ADC_ATTEN_DB_12, state.pattern[0].atten);
  TEST_ASSERT_EQUAL_UINT8(12u, state.pattern[1].bit_width);
  TEST_ASSERT_EQUAL_UINT32(256u, state.conv_frame_size);
  // Small blocks get the driver minimum of eight chunks.
  TEST_ASSERT_EQUAL_UINT32(8u * 256u, state.max_store_buf_size);
  TEST_ASSERT_EQUAL_UINT8(0u, s_positions[0]);
  TEST_ASSERT_EQUAL_UINT8(1u, s_positions[1]);
  TEST_ASSERT_EQUAL_UINT32((1000000000ull * PINS) / 83333u, s_period_ns);
  TEST_ASSERT_NOT_NULL(s_reader);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        jh_adc_scan_start(NULL, s_positions, &s_period_ns));

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
  TEST_ASSERT_FALSE(state.started);
  TEST_ASSERT_FALSE(state.live);
  TEST_ASSERT_NULL(s_reader);
  TEST_ASSERT_TRUE(fake_idf::find_call("adc_continuous_stop") >= 0);
  TEST_ASSERT_TRUE(fake_idf::find_call("adc_continuous_deinit") >
                   fake_idf::find_call("adc_continuous_stop"));
  uint16_t raw = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_adc_scan_latest(0u, &raw));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
}

void test_driver_ring_holds_two_blocks_of_a_large_scan(void) {
  static uint16_t big[2u * 500u * PINS] __attribute__((aligned(4)));
  hal_adc_scan_config_t cfg = config();
  cfg.buffer = big;
  cfg.block_frames = 500u; /* 1000 records per block */
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
  // Two blocks of 4-byte records, rounded up to whole chunks: 8000 -> 8192.
  TEST_ASSERT_EQUAL_UINT32(8192u,
                           fake_idf::adc_continuous_state().max_store_buf_size);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
}

void test_period_is_clamped_to_the_converter_and_bad_pins_are_refused(void) {
  hal_adc_scan_config_t cfg = config();
  cfg.conversion_period_ns = 2000000u; /* 500 Hz asked, 611 Hz is the floor */
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
  TEST_ASSERT_EQUAL_UINT32(611u,
                           fake_idf::adc_continuous_state().sample_freq_hz);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());

  cfg.conversion_period_ns = 1000u; /* 1 MHz: beyond the converter */
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
  TEST_ASSERT_FALSE(fake_idf::adc_continuous_state().live);

  cfg = config();
  cfg.pins[1] = HAL_ADC_SCAN_PIN_TEMPERATURE;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
  cfg.pins[1] = 11u; /* ADC2 pad */
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
  cfg.pins[1] = 40u; /* no converter */
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
  TEST_ASSERT_FALSE(fake_idf::adc_continuous_state().live);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
}

void test_a_failed_driver_start_leaves_no_handle_behind(void) {
  fake_idf::fail_next("adc_continuous_start", ESP_ERR_INVALID_STATE, 1u);
  const hal_adc_scan_config_t cfg = config();
  TEST_ASSERT_TRUE(jh_adc_scan_start(&cfg, s_positions, &s_period_ns) !=
                   HAL_OK);
  TEST_ASSERT_FALSE(fake_idf::adc_continuous_state().live);
  TEST_ASSERT_EQUAL_size_t(1u, fake_idf::count_calls("adc_continuous_deinit"));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());

  fake_idf::fail_next("adc_continuous_new_handle", ESP_ERR_NO_MEM, 1u);
  TEST_ASSERT_TRUE(jh_adc_scan_start(&cfg, s_positions, &s_period_ns) !=
                   HAL_OK);
  TEST_ASSERT_FALSE(fake_idf::adc_continuous_state().live);
  // And the next start works as if nothing happened.
  start();
}

void test_stream_is_decoded_into_alternating_blocks(void) {
  start();
  hal_adc_scan_block_t block = {};
  TEST_ASSERT_FALSE(jh_adc_scan_completed(&block));

  s_micros = 2000u;
  feed_frames(0u, FRAMES - 1u);
  TEST_ASSERT_FALSE(jh_adc_scan_completed(&block));
  feed_frames(FRAMES - 1u, 1u);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_ring[0], block.samples);
  TEST_ASSERT_EQUAL_UINT32(FRAMES, block.frames);
  TEST_ASSERT_EQUAL_UINT8(PINS, block.pin_count);
  TEST_ASSERT_EQUAL_UINT32(2000u, block.completed_us);
  TEST_ASSERT_EQUAL_UINT32(s_marker_base + 1u, block.marker);
  for (uint32_t f = 0u; f < FRAMES; ++f) {
    TEST_ASSERT_EQUAL_UINT16(100u * f, block.samples[(f * PINS) + 0u]);
    TEST_ASSERT_EQUAL_UINT16((100u * f) + 1u, block.samples[(f * PINS) + 1u]);
  }
  // The same block again until the next one completes.
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1u, block.sequence);

  s_micros = 3000u;
  feed_frames(FRAMES, FRAMES);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(2u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_ring[BLOCK], block.samples);
  TEST_ASSERT_EQUAL_UINT32(3000u, block.completed_us);
  TEST_ASSERT_EQUAL_UINT16(100u * FRAMES, block.samples[0]);

  feed_frames(2u * FRAMES, FRAMES);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(3u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_ring[0], block.samples);
  TEST_ASSERT_EQUAL_UINT16(100u * 2u * FRAMES, block.samples[0]);
  TEST_ASSERT_EQUAL_size_t(0u, fake_idf::adc_continuous_pending_bytes());
}

void test_decoder_resynchronises_on_the_pattern_head(void) {
  start();
  // A stream caught mid-pattern: the lone second position is dropped, and so
  // are records of channels the scan does not carry or of the other unit.
  fake_idf::adc_continuous_push(0u, 3u, 999u);
  fake_idf::adc_continuous_push(0u, 7u, 998u);
  fake_idf::adc_continuous_push(1u, 2u, 997u);
  feed_frames(0u, 1u);
  // A dropped first position in the middle: the second position that follows
  // is skipped until the next head.
  fake_idf::adc_continuous_push(0u, 3u, 996u);
  feed_frames(1u, FRAMES - 1u);
  hal_adc_scan_block_t block = {};
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1u, block.sequence);
  for (uint32_t f = 0u; f < FRAMES; ++f) {
    TEST_ASSERT_EQUAL_UINT16(100u * f, block.samples[(f * PINS) + 0u]);
    TEST_ASSERT_EQUAL_UINT16((100u * f) + 1u, block.samples[(f * PINS) + 1u]);
  }
}

void test_latest_collects_the_stream_itself_and_stays_fresh(void) {
  start();
  uint16_t raw = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, jh_adc_scan_latest(0u, &raw));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_adc_scan_latest(PINS, &raw));

  // Nobody takes blocks; the newest sample must still follow the stream,
  // because hal_adc_read() of a scanned pin is served this way.
  feed_frames(0u, 1u);
  fake_idf::adc_continuous_push(0u, 2u, 3242u); /* half a frame more */
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(0u, &raw));
  TEST_ASSERT_EQUAL_UINT16(0u, raw);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(1u, &raw));
  TEST_ASSERT_EQUAL_UINT16(1u, raw);
  TEST_ASSERT_TRUE(s_reader(PIN_B, &raw));
  TEST_ASSERT_EQUAL_UINT16(1u, raw);
  TEST_ASSERT_FALSE(s_reader(9u, &raw));

  fake_idf::adc_continuous_push(0u, 3u, 3243u); /* frame 1 complete */
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(0u, &raw));
  TEST_ASSERT_EQUAL_UINT16(3242u, raw);

  // Across a block boundary the last frame of the finished block serves
  // until the next block holds a frame, then the new one does.
  feed_frames(2u, FRAMES - 2u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(1u, &raw));
  TEST_ASSERT_EQUAL_UINT16((100u * (FRAMES - 1u)) + 1u, raw);
  feed_frames(FRAMES, 1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(1u, &raw));
  TEST_ASSERT_EQUAL_UINT16((100u * FRAMES) + 1u, raw);
  // The block collected on the way is still handed out once.
  hal_adc_scan_block_t block = {};
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1u, block.sequence);
  TEST_ASSERT_EQUAL_UINT16(3242u, block.samples[(1u * PINS) + 0u]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_start_configures_the_driver_and_stop_releases_it);
  RUN_TEST(test_driver_ring_holds_two_blocks_of_a_large_scan);
  RUN_TEST(test_period_is_clamped_to_the_converter_and_bad_pins_are_refused);
  RUN_TEST(test_a_failed_driver_start_leaves_no_handle_behind);
  RUN_TEST(test_stream_is_decoded_into_alternating_blocks);
  RUN_TEST(test_decoder_resynchronises_on_the_pattern_head);
  RUN_TEST(test_latest_collects_the_stream_itself_and_stays_fresh);
  return UNITY_END();
}
