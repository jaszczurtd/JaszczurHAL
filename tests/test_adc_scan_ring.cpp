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

static bool pick(bool busy0, bool after0, uint32_t written0, bool busy1,
                 bool after1, uint32_t written1, uint32_t sequence,
                 uint8_t completed) {
  const jh_adc_scan_channel_t channels[2] = {{busy0, after0, written0},
                                             {busy1, after1, written1}};
  s_half = 0xFFu;
  s_frame = 0xFFFFFFFFu;
  return jh_adc_scan_pick_frame(channels, PINS, FRAMES, sequence, completed,
                                &s_half, &s_frame);
}

void test_pick_treats_a_channel_that_finished_between_two_looks_as_full(void) {
  // Half 1 was busy; its completion interrupt re-armed the pointer to the
  // base before it was read. The whole half is there, not none of it, and
  // half 0 is the one being overwritten right now.
  TEST_ASSERT_TRUE(pick(false, false, 0u, true, false, 0u, 4u, 0u));
  TEST_ASSERT_EQUAL_UINT8(1u, s_half);
  TEST_ASSERT_EQUAL_UINT32(FRAMES - 1u, s_frame);
  // The pointer may equally have been read just before the re-arm.
  TEST_ASSERT_TRUE(
      pick(false, false, 0u, true, false, (FRAMES * PINS) - 1u, 4u, 0u));
  TEST_ASSERT_EQUAL_UINT8(1u, s_half);
  TEST_ASSERT_EQUAL_UINT32(FRAMES - 1u, s_frame);
  // Still busy with the pointer at the base is a genuine fresh trigger: the
  // other half just filled and is the one to read.
  TEST_ASSERT_TRUE(pick(false, false, 0u, true, true, 0u, 4u, 0u));
  TEST_ASSERT_EQUAL_UINT8(0u, s_half);
  TEST_ASSERT_EQUAL_UINT32(FRAMES - 1u, s_frame);
}

void test_pick_prefers_the_half_at_its_end_when_no_channel_is_busy(void) {
  // Half 0 just completed and neither the chain nor its interrupt has run:
  // the bookkeeping still names half 1, which is a block older.
  TEST_ASSERT_TRUE(pick(false, false, FRAMES * PINS, false, false, 0u, 4u, 1u));
  TEST_ASSERT_EQUAL_UINT8(0u, s_half);
  TEST_ASSERT_EQUAL_UINT32(FRAMES - 1u, s_frame);
  // Both re-armed and idle leaves only the bookkeeping.
  TEST_ASSERT_TRUE(pick(false, false, 0u, false, false, 0u, 4u, 1u));
  TEST_ASSERT_EQUAL_UINT8(1u, s_half);
  TEST_ASSERT_EQUAL_UINT32(FRAMES - 1u, s_frame);
  TEST_ASSERT_FALSE(pick(false, false, 0u, false, false, 0u, 0u, 0u));
}

void test_pick_keeps_the_choice_inside_a_block_in_progress(void) {
  TEST_ASSERT_TRUE(
      pick(true, true, (7u * PINS) + 2u, false, false, 0u, 5u, 1u));
  TEST_ASSERT_EQUAL_UINT8(0u, s_half);
  TEST_ASSERT_EQUAL_UINT32(6u, s_frame);
  const jh_adc_scan_channel_t none[2] = {{true, true, 9u}, {false, false, 0u}};
  TEST_ASSERT_FALSE(
      jh_adc_scan_pick_frame(none, 0u, FRAMES, 1u, 0u, &s_half, &s_frame));
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
  RUN_TEST(test_pick_treats_a_channel_that_finished_between_two_looks_as_full);
  RUN_TEST(test_pick_prefers_the_half_at_its_end_when_no_channel_is_busy);
  RUN_TEST(test_pick_keeps_the_choice_inside_a_block_in_progress);
  RUN_TEST(test_rp_clkdiv_runs_back_to_back_at_the_minimum_period);
  return UNITY_END();
}
