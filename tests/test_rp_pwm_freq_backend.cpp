/* The RP frequency PWM backend on simulated slices: creation as before,
 * frequency changes by period or by divider, duty scaling after a change, the
 * pairing of the two buffered register writes and two channels on one
 * slice. */
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/gpio/hal_pwm_freq_internal.h"
#include "hal_sync_host.h"
#include "hardware/pwm.h"
#include "rp_pwm_fake.h"
#include "utils/unity.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" void hal_derr_limited(const char *, const char *, ...) {}

extern "C" void hal_assert_fail(const char *msg) {
  fprintf(stderr, "unexpected HAL assertion: %s\n", msg);
  abort();
}

namespace {

constexpr uint32_t kClockHz = 150000000u;
constexpr uint8_t kPin = 9u;  /* slice 4, channel B */
constexpr uint8_t kPeer = 8u; /* slice 4, channel A */
constexpr unsigned kSlice = 4u;
constexpr uint32_t kResolution = 2047u;

hal_pwm_freq_channel_t s_channel;

rp_pwm_fake::Slice &slice() { return rp_pwm_fake::slice(kSlice); }

/* Lets the simulated counter wrap so that pending registers become active. */
void run_one_period() {
  rp_pwm_fake::set_counter_step(kSlice, 1024u);
  const uint32_t wraps = slice().wraps;
  for (int i = 0; i < 200 && slice().wraps == wraps; ++i) {
    (void)pwm_get_counter(kSlice);
  }
  rp_pwm_fake::set_counter_step(kSlice, 1u);
}

hal_status_t s_race_status;

void retune_in_the_race(void) {
  s_race_status = hal_pwm_freq_set_frequency(s_channel, 400u);
}

} // namespace

void setUp(void) {
  rp_pwm_fake::reset(kClockHz);
  s_channel = hal_pwm_freq_create(kPin, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(s_channel);
}

void tearDown(void) {
  hal_pwm_freq_destroy(s_channel);
  s_channel = nullptr;
}

/* The 200 Hz configuration an existing application gets must not move. */
static void test_create_and_write_keep_the_known_200hz_registers(void) {
  TEST_ASSERT_EQUAL_UINT32(2931u, slice().div16);
  TEST_ASSERT_EQUAL_UINT16(4093u, slice().top);
  TEST_ASSERT_FALSE(slice().enabled);
  TEST_ASSERT_FALSE(rp_pwm_fake::pin_is_pwm(kPin));
  TEST_ASSERT_EQUAL_UINT32(4999798u, hal_pwm_freq_get_period_ns(s_channel));

  hal_pwm_freq_write(s_channel, 1000);
  TEST_ASSERT_TRUE(slice().enabled);
  TEST_ASSERT_TRUE(rp_pwm_fake::pin_is_pwm(kPin));
  TEST_ASSERT_EQUAL_UINT16(2000u, slice().pending_cc[1]);
  hal_pwm_freq_write(s_channel, (int)kResolution);
  TEST_ASSERT_EQUAL_UINT16(4094u, slice().pending_cc[1]);
  hal_pwm_freq_write(s_channel, -5);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().pending_cc[1]);
}

static void test_doubling_the_frequency_changes_the_period_only(void) {
  hal_pwm_freq_write(s_channel, 1000);
  run_one_period();

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL_UINT32(0u, slice().div_writes);
  TEST_ASSERT_EQUAL_UINT32(2931u, slice().div16);
  TEST_ASSERT_EQUAL_UINT16(2046u, slice().pending_top);
  TEST_ASSERT_EQUAL_UINT16(1000u, slice().pending_cc[1]);
  /* Until the wrap the running period keeps the old pair. */
  TEST_ASSERT_EQUAL_UINT16(4093u, slice().top);
  TEST_ASSERT_EQUAL_UINT16(2000u, slice().cc[1]);
  TEST_ASSERT_TRUE(slice().enabled);
  TEST_ASSERT_EQUAL_UINT32(2499899u, hal_pwm_freq_get_period_ns(s_channel));

  hal_pwm_freq_write(s_channel, 500);
  TEST_ASSERT_EQUAL_UINT16(500u, slice().pending_cc[1]);
  hal_pwm_freq_write(s_channel, (int)kResolution);
  TEST_ASSERT_EQUAL_UINT16(2047u, slice().pending_cc[1]);
}

static void test_period_and_duty_are_written_inside_one_counter_period(void) {
  hal_pwm_freq_write(s_channel, 1000);
  run_one_period();
  /* Two counts before the wrap: the pair would straddle it. */
  slice().counter = 4092u;
  rp_pwm_fake::set_counter_step(kSlice, 1u);
  const uint32_t wraps = slice().wraps;

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL_UINT32(wraps + 1u, slice().wraps);
  TEST_ASSERT_TRUE(slice().counter_at_top_write < 16u);
  TEST_ASSERT_EQUAL_UINT32(0u, slice().wraps_between_top_and_cc);
  TEST_ASSERT_TRUE(slice().irq_masked_at_top_write);
  TEST_ASSERT_TRUE(slice().irq_masked_at_cc_write);
  TEST_ASSERT_FALSE(rp_pwm_fake::interrupts_masked());
}

static void test_change_far_from_the_wrap_does_not_wait(void) {
  hal_pwm_freq_write(s_channel, 1000);
  run_one_period();
  slice().counter = 100u;
  const uint32_t wraps = slice().wraps;

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL_UINT32(wraps, slice().wraps);
  TEST_ASSERT_UINT16_WITHIN(2u, 101u, slice().counter_at_top_write);
}

static void test_frequency_outside_the_period_range_changes_the_divider(void) {
  hal_pwm_freq_write(s_channel, 1000);
  run_one_period();

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 600u));
  TEST_ASSERT_EQUAL_UINT32(1u, slice().div_writes);
  TEST_ASSERT_EQUAL_UINT32(1954u, slice().div16);
  TEST_ASSERT_TRUE(slice().irq_masked_at_div_write);
  TEST_ASSERT_EQUAL_UINT16(2046u, slice().pending_top);
  TEST_ASSERT_EQUAL_UINT16(1000u, slice().pending_cc[1]);
  TEST_ASSERT_EQUAL_UINT32(1666599u, hal_pwm_freq_get_period_ns(s_channel));
  TEST_ASSERT_FALSE(rp_pwm_fake::interrupts_masked());
}

/* 600 -> 400 Hz keeps the divider; the period is then 1.5 x the resolution
 * and the duty is scaled by a ratio instead of a shift. */
static void test_duty_ratio_survives_a_period_that_is_no_power_of_two(void) {
  hal_pwm_freq_write(s_channel, 1000);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 600u));
  run_one_period();

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL_UINT32(1u, slice().div_writes);
  TEST_ASSERT_EQUAL_UINT16(3070u, slice().pending_top);
  TEST_ASSERT_EQUAL_UINT16(1500u, slice().pending_cc[1]);

  hal_pwm_freq_write(s_channel, (int)kResolution);
  TEST_ASSERT_EQUAL_UINT16(3071u, slice().pending_cc[1]);
  hal_pwm_freq_write(s_channel, 1);
  TEST_ASSERT_EQUAL_UINT16(2u, slice().pending_cc[1]);
  hal_pwm_freq_write(s_channel, 0);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().pending_cc[1]);

  /* Back to 200 Hz from the 600 Hz divider: 3 x resolution, a ratio again. */
  hal_pwm_freq_write(s_channel, 1000);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 200u));
  TEST_ASSERT_EQUAL_UINT16(6140u, slice().pending_top);
  TEST_ASSERT_EQUAL_UINT16(3000u, slice().pending_cc[1]);
}

static void test_channel_not_started_only_takes_the_new_period(void) {
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_FALSE(slice().enabled);
  TEST_ASSERT_FALSE(rp_pwm_fake::pin_is_pwm(kPin));
  TEST_ASSERT_EQUAL_UINT16(2046u, slice().top);

  hal_pwm_freq_write(s_channel, 1000);
  TEST_ASSERT_TRUE(slice().enabled);
  TEST_ASSERT_EQUAL_UINT16(1000u, slice().pending_cc[1]);
}

static void test_same_frequency_touches_no_register(void) {
  hal_pwm_freq_write(s_channel, 1000);
  const uint32_t top_writes = slice().top_writes;
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 200u));
  TEST_ASSERT_EQUAL_UINT32(top_writes, slice().top_writes);
  TEST_ASSERT_EQUAL_UINT32(0u, slice().div_writes);
}

static void test_second_channel_of_the_slice_blocks_the_change(void) {
  hal_pwm_freq_write(s_channel, 1000);
  hal_pwm_freq_channel_t sibling =
      hal_pwm_freq_create(kPeer, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(sibling);
  const uint32_t top_writes = slice().top_writes;
  const uint16_t level = slice().pending_cc[1];

  const hal_status_t blocked = hal_pwm_freq_set_frequency(s_channel, 400u);
  const uint32_t top_writes_after = slice().top_writes;
  const uint16_t level_after = slice().pending_cc[1];
  const uint32_t period_after = hal_pwm_freq_get_period_ns(s_channel);
  hal_pwm_freq_destroy(sibling);

  TEST_ASSERT_EQUAL(HAL_EBUSY, blocked);
  TEST_ASSERT_EQUAL_UINT32(top_writes, top_writes_after);
  TEST_ASSERT_EQUAL_UINT16(level, level_after);
  TEST_ASSERT_EQUAL_UINT32(4999798u, period_after);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
}

/* What a second channel must not disturb on a running slice. */
struct SliceSnapshot {
  bool enabled;
  uint32_t div16;
  uint16_t top;
  uint16_t pending_top;
  uint16_t level;
  uint16_t pending_level;
  uint16_t counter;
  uint32_t top_writes;
  uint32_t div_writes;
};

static SliceSnapshot snapshot() {
  const rp_pwm_fake::Slice &s = slice();
  return {s.enabled,       s.div16,   s.top,        s.pending_top, s.cc[1],
          s.pending_cc[1], s.counter, s.top_writes, s.div_writes};
}

static void assert_slice_untouched(const SliceSnapshot &before) {
  const SliceSnapshot now = snapshot();
  TEST_ASSERT_EQUAL(before.enabled, now.enabled);
  TEST_ASSERT_EQUAL_UINT32(before.div16, now.div16);
  TEST_ASSERT_EQUAL_UINT16(before.top, now.top);
  TEST_ASSERT_EQUAL_UINT16(before.pending_top, now.pending_top);
  TEST_ASSERT_EQUAL_UINT16(before.level, now.level);
  TEST_ASSERT_EQUAL_UINT16(before.pending_level, now.pending_level);
  TEST_ASSERT_EQUAL_UINT16(before.counter, now.counter);
  TEST_ASSERT_EQUAL_UINT32(before.top_writes, now.top_writes);
  TEST_ASSERT_EQUAL_UINT32(before.div_writes, now.div_writes);
}

/* Leaves the first channel running in the middle of a period. */
static SliceSnapshot start_first_channel() {
  hal_pwm_freq_write(s_channel, 1000);
  run_one_period();
  slice().counter = 1234u;
  return snapshot();
}

static void
test_second_channel_joins_a_running_slice_without_touching_it(void) {
  const SliceSnapshot before = start_first_channel();
  TEST_ASSERT_TRUE(before.enabled);
  TEST_ASSERT_EQUAL_UINT16(2000u, before.level);

  hal_pwm_freq_channel_t peer = hal_pwm_freq_create(kPeer, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(peer);
  assert_slice_untouched(before);
  TEST_ASSERT_FALSE(rp_pwm_fake::pin_is_pwm(kPeer));
  TEST_ASSERT_EQUAL_UINT16(0u, slice().pending_cc[0]);
  TEST_ASSERT_EQUAL_UINT32(4999798u, hal_pwm_freq_get_period_ns(peer));

  hal_pwm_freq_write(peer, 500);
  TEST_ASSERT_TRUE(rp_pwm_fake::pin_is_pwm(kPeer));
  TEST_ASSERT_EQUAL_UINT16(1000u, slice().pending_cc[0]);
  hal_pwm_freq_write(peer, (int)kResolution);
  TEST_ASSERT_EQUAL_UINT16(4094u, slice().pending_cc[0]);
  assert_slice_untouched(before);

  /* A released channel leaves its duty in the register. The next channel on
   * that output still waits at zero for its first write. */
  hal_pwm_freq_destroy(peer);
  peer = hal_pwm_freq_create(kPeer, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(peer);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().pending_cc[0]);
  assert_slice_untouched(before);

  hal_pwm_freq_destroy(peer);
}

/* Above 73 kHz the counter period is half the resolution and the duty is
 * shifted down. The second channel has to round exactly like the first. */
static void test_joining_channel_with_the_same_resolution_scales_alike(void) {
  hal_pwm_freq_destroy(s_channel);
  s_channel = hal_pwm_freq_create(kPin, 100000u, 2048u);
  TEST_ASSERT_NOT_NULL(s_channel);
  TEST_ASSERT_EQUAL_UINT16(1023u, slice().top);
  hal_pwm_freq_write(s_channel, 1023);
  TEST_ASSERT_EQUAL_UINT16(511u, slice().pending_cc[1]);

  hal_pwm_freq_channel_t peer = hal_pwm_freq_create(kPeer, 100000u, 2048u);
  TEST_ASSERT_NOT_NULL(peer);
  hal_pwm_freq_write(peer, 1023);
  TEST_ASSERT_EQUAL_UINT16(511u, slice().pending_cc[0]);
  hal_pwm_freq_write(peer, 2048);
  TEST_ASSERT_EQUAL_UINT16(1024u, slice().pending_cc[0]);

  hal_pwm_freq_destroy(peer);
}

/* The slice has one period: a channel with another resolution scales its
 * duty onto the period that is already there. */
static void test_joining_channel_scales_its_own_resolution(void) {
  const SliceSnapshot before = start_first_channel();

  hal_pwm_freq_channel_t peer = hal_pwm_freq_create(kPeer, 200u, 1000u);
  TEST_ASSERT_NOT_NULL(peer);
  hal_pwm_freq_write(peer, 500);
  TEST_ASSERT_EQUAL_UINT16(2047u, slice().pending_cc[0]);
  hal_pwm_freq_write(peer, 1);
  TEST_ASSERT_EQUAL_UINT16(4u, slice().pending_cc[0]);
  hal_pwm_freq_write(peer, 1000);
  TEST_ASSERT_EQUAL_UINT16(4094u, slice().pending_cc[0]);
  assert_slice_untouched(before);

  hal_pwm_freq_destroy(peer);
}

static void test_second_channel_at_another_frequency_is_refused(void) {
  const SliceSnapshot before = start_first_channel();

  /* More refusals than the pool has slots: a refused request keeps none. */
  for (int i = 0; i < 64; ++i) {
    TEST_ASSERT_NULL(hal_pwm_freq_create(kPeer, 400u, kResolution));
  }
  hal_pwm_freq_channel_t refused = s_channel;
  TEST_ASSERT_EQUAL(HAL_EBUSY, jh_hal_pwm_freq_try_create(
                                   kPeer, 400u, kResolution, &refused));
  TEST_ASSERT_NULL(refused);
  assert_slice_untouched(before);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().pending_cc[0]);
  TEST_ASSERT_EQUAL_UINT32(4999798u, hal_pwm_freq_get_period_ns(s_channel));

  hal_pwm_freq_channel_t peer = hal_pwm_freq_create(kPeer, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(peer);
  hal_pwm_freq_destroy(peer);
}

/* After a retune the slice counts at the new frequency, and that is the one a
 * second channel has to ask for. */
static void test_second_channel_follows_the_retuned_frequency(void) {
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  const SliceSnapshot before = start_first_channel();

  TEST_ASSERT_NULL(hal_pwm_freq_create(kPeer, 200u, kResolution));
  hal_pwm_freq_channel_t peer = hal_pwm_freq_create(kPeer, 400u, kResolution);
  TEST_ASSERT_NOT_NULL(peer);
  TEST_ASSERT_EQUAL_UINT32(2499899u, hal_pwm_freq_get_period_ns(peer));
  hal_pwm_freq_write(peer, 500);
  TEST_ASSERT_EQUAL_UINT16(500u, slice().pending_cc[0]);
  assert_slice_untouched(before);

  hal_pwm_freq_destroy(peer);
}

static void test_output_that_has_a_channel_is_refused(void) {
  const SliceSnapshot before = start_first_channel();

  TEST_ASSERT_NULL(hal_pwm_freq_create(kPin, 200u, kResolution));
  /* GPIO 25 is the same slice output as GPIO 9. */
  TEST_ASSERT_NULL(hal_pwm_freq_create(25u, 200u, kResolution));
  hal_pwm_freq_channel_t refused = s_channel;
  TEST_ASSERT_EQUAL(
      HAL_EBUSY, jh_hal_pwm_freq_try_create(kPin, 200u, kResolution, &refused));
  TEST_ASSERT_NULL(refused);
  assert_slice_untouched(before);

  hal_pwm_freq_destroy(s_channel);
  s_channel = hal_pwm_freq_create(kPin, 300u, kResolution);
  TEST_ASSERT_NOT_NULL(s_channel);
}

static void test_stop_leaves_the_running_peer_alone(void) {
  const SliceSnapshot before = start_first_channel();
  hal_pwm_freq_channel_t peer = hal_pwm_freq_create(kPeer, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(peer);
  hal_pwm_freq_write(peer, 500);

  hal_pwm_freq_stop(peer);
  assert_slice_untouched(before);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().pending_cc[0]);

  hal_pwm_freq_write(peer, 500);
  TEST_ASSERT_EQUAL_UINT16(1000u, slice().pending_cc[0]);
  hal_pwm_freq_stop(s_channel);
  TEST_ASSERT_TRUE(slice().enabled);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().pending_cc[1]);
  TEST_ASSERT_EQUAL_UINT16(1000u, slice().pending_cc[0]);

  /* The last running channel takes the counter with it. */
  hal_pwm_freq_stop(peer);
  TEST_ASSERT_FALSE(slice().enabled);

  hal_pwm_freq_destroy(peer);
}

/* A stopped channel keeps its pin on the slice. Its duty is zero from the
 * stop on, so it stays off when the other output starts the counter again. */
static void test_stopped_channel_does_not_resume_with_its_peer(void) {
  hal_pwm_freq_write(s_channel, 1000);
  run_one_period();
  TEST_ASSERT_EQUAL_UINT16(2000u, slice().cc[1]);
  hal_pwm_freq_stop(s_channel);
  TEST_ASSERT_FALSE(slice().enabled);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().cc[1]);

  hal_pwm_freq_channel_t peer = hal_pwm_freq_create(kPeer, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(peer);
  hal_pwm_freq_write(peer, 500);
  TEST_ASSERT_TRUE(slice().enabled);
  run_one_period();
  TEST_ASSERT_EQUAL_UINT16(0u, slice().cc[1]);
  TEST_ASSERT_EQUAL_UINT16(1000u, slice().cc[0]);

  hal_pwm_freq_write(s_channel, 1000);
  TEST_ASSERT_EQUAL_UINT16(2000u, slice().pending_cc[1]);

  hal_pwm_freq_destroy(peer);
}

/* A frequency change after a stop keeps the output off, whether it changes
 * the period or the divider, and the stopped output stays off when another
 * channel starts the counter of the slice. */
static void test_stopped_channel_stays_off_through_a_frequency_change(void) {
  hal_pwm_freq_write(s_channel, 1000);
  run_one_period();
  hal_pwm_freq_stop(s_channel);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().cc[1]);

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL_UINT32(0u, slice().div_writes);
  TEST_ASSERT_EQUAL_UINT16(2046u, slice().top);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().cc[1]);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().pending_cc[1]);
  TEST_ASSERT_FALSE(slice().enabled);

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 600u));
  TEST_ASSERT_EQUAL_UINT32(1u, slice().div_writes);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().cc[1]);
  TEST_ASSERT_EQUAL_UINT16(0u, slice().pending_cc[1]);
  TEST_ASSERT_FALSE(slice().enabled);

  hal_pwm_freq_channel_t peer = hal_pwm_freq_create(kPeer, 600u, kResolution);
  TEST_ASSERT_NOT_NULL(peer);
  hal_pwm_freq_write(peer, 500);
  TEST_ASSERT_TRUE(slice().enabled);
  run_one_period();
  TEST_ASSERT_EQUAL_UINT16(0u, slice().cc[1]);
  TEST_ASSERT_EQUAL_UINT16(500u, slice().cc[0]);

  /* The next write starts the output again with the new scaling. */
  hal_pwm_freq_write(s_channel, 1000);
  TEST_ASSERT_EQUAL_UINT16(1000u, slice().pending_cc[1]);

  hal_pwm_freq_destroy(peer);
}

/* A frequency change that gets its lock between the entry of a write and the
 * lock of that write: the duty must be scaled for the period it finds. */
static void test_write_overtaken_by_a_change_scales_for_the_new_period(void) {
  hal_pwm_freq_write(s_channel, 1000);
  run_one_period();
  s_race_status = HAL_EINVAL;

  hal_sync_host_before_next_lock(retune_in_the_race);
  hal_pwm_freq_write(s_channel, 500);

  TEST_ASSERT_EQUAL(HAL_OK, s_race_status);
  TEST_ASSERT_EQUAL_UINT16(2046u, slice().pending_top);
  TEST_ASSERT_EQUAL_UINT16(500u, slice().pending_cc[1]);
}

static void test_invalid_requests_leave_the_channel_alone(void) {
  hal_pwm_freq_write(s_channel, 1000);
  const uint32_t top_writes = slice().top_writes;

  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pwm_freq_set_frequency(nullptr, 400u));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pwm_freq_set_frequency(s_channel, 0u));
  /* Faster than the system clock can count one period of two ticks. */
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_pwm_freq_set_frequency(s_channel, 400000000u));
  /* A rejected frequency is not remembered as the current one. */
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_pwm_freq_set_frequency(s_channel, 400000000u));
  TEST_ASSERT_EQUAL_UINT32(top_writes, slice().top_writes);
  TEST_ASSERT_EQUAL_UINT32(0u, slice().div_writes);
  TEST_ASSERT_EQUAL_UINT32(4999798u, hal_pwm_freq_get_period_ns(s_channel));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_pwm_freq_get_period_ns(nullptr));

  hal_pwm_freq_channel_t released = hal_pwm_freq_create(2u, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(released);
  hal_pwm_freq_destroy(released);
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pwm_freq_set_frequency(released, 400u));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_pwm_freq_get_period_ns(released));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_create_and_write_keep_the_known_200hz_registers);
  RUN_TEST(test_doubling_the_frequency_changes_the_period_only);
  RUN_TEST(test_period_and_duty_are_written_inside_one_counter_period);
  RUN_TEST(test_change_far_from_the_wrap_does_not_wait);
  RUN_TEST(test_frequency_outside_the_period_range_changes_the_divider);
  RUN_TEST(test_duty_ratio_survives_a_period_that_is_no_power_of_two);
  RUN_TEST(test_channel_not_started_only_takes_the_new_period);
  RUN_TEST(test_same_frequency_touches_no_register);
  RUN_TEST(test_second_channel_of_the_slice_blocks_the_change);
  RUN_TEST(test_second_channel_joins_a_running_slice_without_touching_it);
  RUN_TEST(test_joining_channel_scales_its_own_resolution);
  RUN_TEST(test_joining_channel_with_the_same_resolution_scales_alike);
  RUN_TEST(test_second_channel_at_another_frequency_is_refused);
  RUN_TEST(test_second_channel_follows_the_retuned_frequency);
  RUN_TEST(test_output_that_has_a_channel_is_refused);
  RUN_TEST(test_stop_leaves_the_running_peer_alone);
  RUN_TEST(test_stopped_channel_does_not_resume_with_its_peer);
  RUN_TEST(test_stopped_channel_stays_off_through_a_frequency_change);
  RUN_TEST(test_write_overtaken_by_a_change_scales_for_the_new_period);
  RUN_TEST(test_invalid_requests_leave_the_channel_alone);
  return UNITY_END();
}
