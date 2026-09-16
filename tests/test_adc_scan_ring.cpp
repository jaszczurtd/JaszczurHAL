#include "hal/analog/jh_adc_scan_ring.h"
#include "hal/impl/rp2040/jh_rp_adc_scan_clock.h"
#include "utils/unity.h"

#define PINS 3u
#define FRAMES 469u

void setUp(void) {}
void tearDown(void) {}

static uint8_t s_half;
static uint32_t s_frame;

static bool latest(uint8_t busy, uint32_t written, uint32_t sequence,
                   uint8_t completed) {
  s_half = 0xFFu;
  s_frame = 0xFFFFFFFFu;
  return jh_adc_scan_latest_frame(busy, written, PINS, FRAMES, sequence,
                                  completed, &s_half, &s_frame);
}

void test_block_in_progress_serves_its_newest_complete_frame(void) {
  TEST_ASSERT_TRUE(latest(1u, (7u * PINS) + 2u, 5u, 0u));
  TEST_ASSERT_EQUAL_UINT8(1u, s_half);
  TEST_ASSERT_EQUAL_UINT32(6u, s_frame);
  // A partial frame does not count; exactly one frame does.
  TEST_ASSERT_TRUE(latest(0u, PINS, 6u, 1u));
  TEST_ASSERT_EQUAL_UINT8(0u, s_half);
  TEST_ASSERT_EQUAL_UINT32(0u, s_frame);
}

void test_chain_boundary_reads_the_half_just_filled_before_its_interrupt(void) {
  // Half 0 has just handed over to half 1 and the completion interrupt has not
  // run yet: sequence and completed_half still describe the block before.
  TEST_ASSERT_TRUE(latest(1u, 0u, 4u, 1u));
  TEST_ASSERT_EQUAL_UINT8(0u, s_half);
  TEST_ASSERT_EQUAL_UINT32(FRAMES - 1u, s_frame);
  TEST_ASSERT_TRUE(latest(1u, PINS - 1u, 4u, 1u));
  TEST_ASSERT_EQUAL_UINT8(0u, s_half);
  // The same boundary after the interrupt gives the same frame.
  TEST_ASSERT_TRUE(latest(1u, 0u, 5u, 0u));
  TEST_ASSERT_EQUAL_UINT8(0u, s_half);
  TEST_ASSERT_EQUAL_UINT32(FRAMES - 1u, s_frame);
  // And symmetrically for half 1 chaining into half 0.
  TEST_ASSERT_TRUE(latest(0u, 0u, 5u, 0u));
  TEST_ASSERT_EQUAL_UINT8(1u, s_half);
  TEST_ASSERT_EQUAL_UINT32(FRAMES - 1u, s_frame);
  // First block ever finished, interrupt pending: half 0 is complete.
  TEST_ASSERT_TRUE(latest(1u, 0u, 0u, 0u));
  TEST_ASSERT_EQUAL_UINT8(0u, s_half);
}

void test_nothing_before_the_first_frame_and_fallback_without_busy_channel(
    void) {
  TEST_ASSERT_FALSE(latest(0u, 0u, 0u, 0u));
  TEST_ASSERT_FALSE(latest(0u, PINS - 1u, 0u, 0u));
  TEST_ASSERT_FALSE(latest(UINT8_MAX, 0u, 0u, 0u));
  TEST_ASSERT_TRUE(latest(UINT8_MAX, 0u, 3u, 1u));
  TEST_ASSERT_EQUAL_UINT8(1u, s_half);
  TEST_ASSERT_EQUAL_UINT32(FRAMES - 1u, s_frame);
  TEST_ASSERT_FALSE(
      jh_adc_scan_latest_frame(0u, 9u, 0u, FRAMES, 1u, 0u, &s_half, &s_frame));
  TEST_ASSERT_FALSE(
      jh_adc_scan_latest_frame(0u, 9u, PINS, 0u, 1u, 0u, &s_half, &s_frame));
}

void test_rp_clkdiv_runs_back_to_back_at_the_minimum_period(void) {
  // Divider 95 would trigger on the cycle the previous conversion ends and
  // halve the rate; the minimum period needs the free-running mode.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, jh_rp_adc_scan_clkdiv(96u));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, jh_rp_adc_scan_clkdiv(0u));
  TEST_ASSERT_EQUAL_FLOAT(96.0f, jh_rp_adc_scan_clkdiv(97u));
  TEST_ASSERT_EQUAL_FLOAT(383.0f, jh_rp_adc_scan_clkdiv(384u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_block_in_progress_serves_its_newest_complete_frame);
  RUN_TEST(test_chain_boundary_reads_the_half_just_filled_before_its_interrupt);
  RUN_TEST(
      test_nothing_before_the_first_frame_and_fallback_without_busy_channel);
  RUN_TEST(test_rp_clkdiv_runs_back_to_back_at_the_minimum_period);
  return UNITY_END();
}
