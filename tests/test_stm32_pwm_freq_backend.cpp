/* The STM32G474 frequency PWM backend on a register table: a frequency
 * change loads prescaler, period and compare together and never forces a
 * timer update, a timer that runs a frequency channel keeps its timing, and
 * PWM audio blocks a change of its timer only while it exists. */
#include "hal/analog/hal_adc.h"
#include "hal/audio/hal_dma_pwm_audio.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/gpio/hal_pwm_freq_internal.h"
#include "hal/impl/stm32g474/hal_pwm_stm32g474.h"
#include "hal/impl/stm32g474/port/stm32g474_regs.h"
#include "hal/impl/stm32g474/stm32g474_adc_shared.h"
#include "hal_sync_host.h"
#include "jh_stm32g474_host_regs.h"
#include "utils/unity.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" void hal_derr_limited(const char *, const char *, ...) {}

extern "C" void hal_assert_fail(const char *msg) {
  fprintf(stderr, "unexpected HAL assertion: %s\n", msg);
  abort();
}

/* ADC side of PWM audio: every pin is accepted and the DMA reservation gives
 * the status the test selects. */
static hal_status_t s_adc_acquire_status = HAL_OK;

extern "C" bool hal_adc_is_pin_supported(uint8_t) { return true; }
extern "C" hal_status_t stm32g474_adc_acquire_dma(void) {
  return s_adc_acquire_status;
}
extern "C" void stm32g474_adc_release_dma(void) {}

namespace {

constexpr uint8_t kPin = 22u;     /* PB6, TIM4 channel 1 */
constexpr uint8_t kSibling = 23u; /* PB7, TIM4 channel 2 */
constexpr uint8_t kAlias = 11u;   /* PA11, TIM4 channel 1 as well */
constexpr uint32_t kResolution = 2047u;

/* Prescaler register for a frequency and a counter period, written out
 * independently of the backend: nearest integer divider, minus one. */
uint32_t psc_for(uint32_t frequency_hz, uint32_t period_ticks) {
  const uint64_t target = (uint64_t)frequency_hz * period_ticks;
  return (uint32_t)(((uint64_t)JH_G474_TIMCLK1_HZ + target / 2u) / target) - 1u;
}

uint32_t period_ns_for(uint32_t frequency_hz, uint32_t period_ticks) {
  const uint64_t ticks =
      ((uint64_t)psc_for(frequency_hz, period_ticks) + 1u) * period_ticks;
  return (uint32_t)((ticks * UINT64_C(1000000000) + JH_G474_TIMCLK1_HZ / 2u) /
                    JH_G474_TIMCLK1_HZ);
}

hal_pwm_freq_channel_t s_channel;

constexpr uint8_t kAdcPin = 0u; /* PA0, ADC1 channel 1 */
uint16_t s_audio_samples[8];
volatile uint16_t s_adc_sample;

/* PWM audio on a pin of TIM4 with the timing the frequency channel gave the
 * timer, the only one a held timer accepts. */
hal_dma_pwm_audio_config_t audio_config(uint8_t pin) {
  hal_dma_pwm_audio_config_t cfg = {};
  cfg.pwm_pin = pin;
  cfg.sample_rate_hz = 200u;
  cfg.period_ticks = kResolution;
  cfg.block_size = 4u;
  cfg.buffer_a = &s_audio_samples[0];
  cfg.buffer_b = &s_audio_samples[4];
  return cfg;
}

uint32_t s_race_frequency_hz;
hal_status_t s_race_status;

void retune_in_the_race(void) {
  s_race_status = hal_pwm_freq_set_frequency(s_channel, s_race_frequency_hz);
}

} // namespace

void setUp(void) {
  s_adc_acquire_status = HAL_OK;
  jh_stm32g474_host_regs_reset();
  /* The backend remembers the timing of TIM4 across the register reset. A
   * channel at another frequency first makes the next one program the timer
   * again, whatever the test before left behind. */
  hal_pwm_freq_destroy(hal_pwm_freq_create(kPin, 1000u, kResolution));
  s_channel = hal_pwm_freq_create(kPin, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(s_channel);
}

void tearDown(void) {
  hal_pwm_freq_destroy(s_channel);
  s_channel = nullptr;
}

static void
test_change_loads_prescaler_and_compare_without_forced_update(void) {
  hal_pwm_freq_write(s_channel, 1000);
  TEST_ASSERT_EQUAL_UINT32(psc_for(200u, 2047u), TIM_PSC(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(2046u, TIM_ARR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(1000u, TIM_CCR1(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(period_ns_for(200u, 2047u),
                           hal_pwm_freq_get_period_ns(s_channel));
  TIM_EGR(TIM4_BASE) = 0u;
  const uint32_t cr1 = TIM_CR1(TIM4_BASE);
  TEST_ASSERT_TRUE((cr1 & TIM_CR1_CEN) != 0u);

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL_UINT32(psc_for(400u, 2047u), TIM_PSC(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(2046u, TIM_ARR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(1000u, TIM_CCR1(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(0u, TIM_EGR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(cr1, TIM_CR1(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(period_ns_for(400u, 2047u),
                           hal_pwm_freq_get_period_ns(s_channel));
}

/* 1 Hz needs a longer counter period than the resolution; the compare value
 * of the duty written last follows the new period. */
static void test_change_that_doubles_the_period_rescales_the_duty(void) {
  hal_pwm_freq_write(s_channel, 1000);

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 1u));
  TEST_ASSERT_EQUAL_UINT32(psc_for(1u, 4094u), TIM_PSC(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(4093u, TIM_ARR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(2000u, TIM_CCR1(TIM4_BASE));

  hal_pwm_freq_write(s_channel, 500);
  TEST_ASSERT_EQUAL_UINT32(1000u, TIM_CCR1(TIM4_BASE));
  hal_pwm_freq_write(s_channel, (int)kResolution);
  TEST_ASSERT_EQUAL_UINT32(4094u, TIM_CCR1(TIM4_BASE));

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 200u));
  TEST_ASSERT_EQUAL_UINT32(2046u, TIM_ARR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(2047u, TIM_CCR1(TIM4_BASE));
}

static void test_second_channel_of_the_timer_blocks_the_change(void) {
  hal_pwm_freq_write(s_channel, 1000);
  hal_pwm_freq_channel_t sibling =
      hal_pwm_freq_create(kSibling, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(sibling);
  const uint32_t psc = TIM_PSC(TIM4_BASE);

  const hal_status_t blocked = hal_pwm_freq_set_frequency(s_channel, 400u);
  const uint32_t psc_after = TIM_PSC(TIM4_BASE);
  const uint32_t period_after = hal_pwm_freq_get_period_ns(s_channel);
  hal_pwm_freq_destroy(sibling);

  TEST_ASSERT_EQUAL(HAL_EBUSY, blocked);
  TEST_ASSERT_EQUAL_UINT32(psc, psc_after);
  TEST_ASSERT_EQUAL_UINT32(period_ns_for(200u, 2047u), period_after);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
}

/* Registers of a running first channel, with the update flag cleared so that
 * a reconfiguration of the timer shows. */
struct TimerSnapshot {
  uint32_t psc;
  uint32_t arr;
  uint32_t ccr1;
  uint32_t cr1;
  uint32_t ccer;
};

static TimerSnapshot start_first_channel() {
  hal_pwm_freq_write(s_channel, 1000);
  TIM_EGR(TIM4_BASE) = 0u;
  return {TIM_PSC(TIM4_BASE), TIM_ARR(TIM4_BASE), TIM_CCR1(TIM4_BASE),
          TIM_CR1(TIM4_BASE), TIM_CCER_REG(TIM4_BASE)};
}

static void assert_timer_untouched(const TimerSnapshot &before) {
  TEST_ASSERT_EQUAL_UINT32(before.psc, TIM_PSC(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(before.arr, TIM_ARR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(before.ccr1, TIM_CCR1(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(before.cr1, TIM_CR1(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(0u, TIM_EGR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(period_ns_for(200u, 2047u),
                           hal_pwm_freq_get_period_ns(s_channel));
}

static void test_second_channel_at_another_frequency_is_refused(void) {
  const TimerSnapshot before = start_first_channel();
  TEST_ASSERT_EQUAL_UINT32(psc_for(200u, 2047u), before.psc);

  /* More refusals than the pool has slots: a refused request keeps none. */
  for (int i = 0; i < 64; ++i) {
    TEST_ASSERT_NULL(hal_pwm_freq_create(kSibling, 400u, kResolution));
  }
  /* The same frequency with a resolution that needs another counter period. */
  TEST_ASSERT_NULL(hal_pwm_freq_create(kSibling, 200u, 1000u));
  hal_pwm_freq_channel_t refused = s_channel;
  TEST_ASSERT_EQUAL(HAL_EBUSY, jh_hal_pwm_freq_try_create(
                                   kSibling, 400u, kResolution, &refused));
  TEST_ASSERT_NULL(refused);
  assert_timer_untouched(before);
  TEST_ASSERT_EQUAL_UINT32(before.ccer, TIM_CCER_REG(TIM4_BASE));

  hal_pwm_freq_channel_t sibling =
      hal_pwm_freq_create(kSibling, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(sibling);
  hal_pwm_freq_write(sibling, 500);
  TEST_ASSERT_EQUAL_UINT32(500u, TIM_CCR2(TIM4_BASE));
  assert_timer_untouched(before);
  hal_pwm_freq_destroy(sibling);
}

static void test_timer_channel_that_has_a_channel_is_refused(void) {
  const TimerSnapshot before = start_first_channel();

  TEST_ASSERT_NULL(hal_pwm_freq_create(kPin, 200u, kResolution));
  TEST_ASSERT_NULL(hal_pwm_freq_create(kAlias, 200u, kResolution));
  hal_pwm_freq_channel_t refused = s_channel;
  TEST_ASSERT_EQUAL(
      HAL_EBUSY, jh_hal_pwm_freq_try_create(kPin, 200u, kResolution, &refused));
  TEST_ASSERT_NULL(refused);
  /* An unsupported pin is still a configuration error, not a busy timer. */
  TEST_ASSERT_EQUAL(
      HAL_EIO, jh_hal_pwm_freq_try_create(127u, 200u, kResolution, &refused));
  assert_timer_untouched(before);
  TEST_ASSERT_EQUAL_UINT32(before.ccer, TIM_CCER_REG(TIM4_BASE));

  /* A released channel frees both its output and the timing of the timer. */
  hal_pwm_freq_destroy(s_channel);
  s_channel = hal_pwm_freq_create(kAlias, 400u, kResolution);
  TEST_ASSERT_NOT_NULL(s_channel);
  TEST_ASSERT_EQUAL_UINT32(psc_for(400u, 2047u), TIM_PSC(TIM4_BASE));
}

/* hal_pwm and PWM audio prepare pins through the same timer code. */
static void test_other_pwm_users_cannot_retime_a_held_timer(void) {
  const TimerSnapshot before = start_first_channel();
  jh_stm32_pwm_channel_desc other = {};

  TEST_ASSERT_FALSE(jh_stm32_pwm_prepare_pin(kSibling, 1000u, 256u, &other));
  TEST_ASSERT_FALSE(jh_stm32_pwm_prepare_pin(kPin, 1000u, 256u, &other));
  assert_timer_untouched(before);

  /* With the timing of the timer the other channel is theirs to use. */
  TEST_ASSERT_TRUE(jh_stm32_pwm_prepare_pin(kSibling, 200u, 2047u, &other));
  assert_timer_untouched(before);
  jh_stm32_pwm_forget_channel(&other);
}

/* A frequency change that gets its lock between the entry of a write and the
 * lock of that write: the duty must be scaled for the period it finds. */
static void test_write_overtaken_by_a_change_scales_for_the_new_period(void) {
  hal_pwm_freq_write(s_channel, 1000);
  s_race_frequency_hz = 1u;
  s_race_status = HAL_EINVAL;

  hal_sync_host_before_next_lock(retune_in_the_race);
  hal_pwm_freq_write(s_channel, 500);

  TEST_ASSERT_EQUAL(HAL_OK, s_race_status);
  TEST_ASSERT_EQUAL_UINT32(4093u, TIM_ARR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(1000u, TIM_CCR1(TIM4_BASE));

  /* Full scale takes the period of the timer, not the one read too early. */
  s_race_frequency_hz = 200u;
  hal_sync_host_before_next_lock(retune_in_the_race);
  hal_pwm_freq_write(s_channel, (int)kResolution);
  TEST_ASSERT_EQUAL(HAL_OK, s_race_status);
  TEST_ASSERT_EQUAL_UINT32(2046u, TIM_ARR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(2047u, TIM_CCR1(TIM4_BASE));
}

/* PWM audio on the other output of the timer blocks a frequency change for
 * as long as it exists, stopped or not, and no longer. */
static void test_audio_blocks_the_change_until_it_is_destroyed(void) {
  hal_pwm_freq_write(s_channel, 1000);
  const hal_dma_pwm_audio_config_t cfg = audio_config(kSibling);
  hal_dma_pwm_audio_t audio = nullptr;
  TEST_ASSERT_EQUAL(HAL_OK, hal_dma_pwm_audio_create_ex(&cfg, &audio));
  TEST_ASSERT_NOT_NULL(audio);

  TEST_ASSERT_EQUAL(HAL_EBUSY, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL(HAL_OK, hal_dma_pwm_audio_stop(audio));
  TEST_ASSERT_EQUAL(HAL_EBUSY, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL_UINT32(psc_for(200u, 2047u), TIM_PSC(TIM4_BASE));

  TEST_ASSERT_EQUAL(HAL_OK, hal_dma_pwm_audio_destroy_ex(audio));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL_UINT32(psc_for(400u, 2047u), TIM_PSC(TIM4_BASE));
}

/* A create that fails after it prepared the pin leaves nothing on the timer. */
static void test_failed_audio_create_does_not_block_the_change(void) {
  hal_pwm_freq_write(s_channel, 1000);
  hal_dma_pwm_audio_t audio = nullptr;

  /* The ADC is taken: the failure comes from the ADC setup. */
  hal_dma_pwm_audio_config_t with_adc = audio_config(kSibling);
  with_adc.adc_pins = &kAdcPin;
  with_adc.adc_count = 1u;
  with_adc.adc_buffer = &s_adc_sample;
  s_adc_acquire_status = HAL_EBUSY;
  TEST_ASSERT_EQUAL(HAL_EBUSY, hal_dma_pwm_audio_create_ex(&with_adc, &audio));
  TEST_ASSERT_NULL(audio);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 200u));

  /* Buffers that are not one block apart: the failure comes from the DMA
   * setup of the PWM side. */
  hal_dma_pwm_audio_config_t split = audio_config(kSibling);
  split.buffer_b = &s_audio_samples[5];
  TEST_ASSERT_EQUAL(HAL_EIO, hal_dma_pwm_audio_create_ex(&split, &audio));
  TEST_ASSERT_NULL(audio);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 400u));

  /* The slot of a failed create is free for the next one. */
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 200u));
  const hal_dma_pwm_audio_config_t cfg = audio_config(kSibling);
  TEST_ASSERT_EQUAL(HAL_OK, hal_dma_pwm_audio_create_ex(&cfg, &audio));
  TEST_ASSERT_EQUAL(HAL_OK, hal_dma_pwm_audio_destroy_ex(audio));
}

/* PWM audio through the other pin of the timer channel the frequency channel
 * uses shares its compare register, so only the timing is compared here: the
 * release of the audio must not take the hold of the frequency channel away. */
static void test_audio_release_keeps_the_hold_of_a_frequency_channel(void) {
  const TimerSnapshot before = start_first_channel();
  const hal_dma_pwm_audio_config_t cfg = audio_config(kAlias);
  hal_dma_pwm_audio_t audio = nullptr;
  TEST_ASSERT_EQUAL(HAL_OK, hal_dma_pwm_audio_create_ex(&cfg, &audio));
  TEST_ASSERT_EQUAL(HAL_OK, hal_dma_pwm_audio_destroy_ex(audio));

  TEST_ASSERT_NULL(hal_pwm_freq_create(kSibling, 400u, kResolution));
  TEST_ASSERT_NULL(hal_pwm_freq_create(kAlias, 200u, kResolution));
  hal_pwm_freq_channel_t sibling =
      hal_pwm_freq_create(kSibling, 200u, kResolution);
  TEST_ASSERT_NOT_NULL(sibling);
  const hal_status_t blocked = hal_pwm_freq_set_frequency(sibling, 400u);
  hal_pwm_freq_destroy(sibling);
  TEST_ASSERT_EQUAL(HAL_EBUSY, blocked);
  TEST_ASSERT_EQUAL_UINT32(before.psc, TIM_PSC(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(before.arr, TIM_ARR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(0u, TIM_EGR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(period_ns_for(200u, 2047u),
                           hal_pwm_freq_get_period_ns(s_channel));
}

static void test_invalid_requests_leave_the_timer_alone(void) {
  hal_pwm_freq_write(s_channel, 1000);
  const uint32_t psc = TIM_PSC(TIM4_BASE);

  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pwm_freq_set_frequency(nullptr, 400u));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pwm_freq_set_frequency(s_channel, 0u));
  /* Above the timer clock no counter period is left. */
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pwm_freq_set_frequency(
                                    s_channel, JH_G474_TIMCLK1_HZ + 1u));
  TEST_ASSERT_EQUAL_UINT32(psc, TIM_PSC(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(2046u, TIM_ARR(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(1000u, TIM_CCR1(TIM4_BASE));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_pwm_freq_get_period_ns(nullptr));

  /* The same frequency is accepted and writes nothing. */
  TIM_PSC(TIM4_BASE) = 0xA5A5u;
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(s_channel, 200u));
  TEST_ASSERT_EQUAL_UINT32(0xA5A5u, TIM_PSC(TIM4_BASE));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_change_loads_prescaler_and_compare_without_forced_update);
  RUN_TEST(test_change_that_doubles_the_period_rescales_the_duty);
  RUN_TEST(test_second_channel_of_the_timer_blocks_the_change);
  RUN_TEST(test_second_channel_at_another_frequency_is_refused);
  RUN_TEST(test_timer_channel_that_has_a_channel_is_refused);
  RUN_TEST(test_other_pwm_users_cannot_retime_a_held_timer);
  RUN_TEST(test_write_overtaken_by_a_change_scales_for_the_new_period);
  RUN_TEST(test_audio_blocks_the_change_until_it_is_destroyed);
  RUN_TEST(test_failed_audio_create_does_not_block_the_change);
  RUN_TEST(test_audio_release_keeps_the_hold_of_a_frequency_channel);
  RUN_TEST(test_invalid_requests_leave_the_timer_alone);
  return UNITY_END();
}
