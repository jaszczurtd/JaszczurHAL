#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_PWM_FREQ

#include "hal/core/hal_array.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/gpio/hal_pwm_freq_internal.h"
#include "hal/gpio/hal_pwm_freq_pool.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>
#include <string.h>

static hal_mutex_t pwm_mutex = NULL;

static void pwm_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&pwm_mutex);
}

struct hal_pwm_freq_channel_impl_s {
  uint32_t pin;
  uint32_t frequency_hz;
  uint32_t analogScale; // effective wrap value after scaling
  uint16_t
      pseudoScale; // left-shift applied before write  (>255 clkdiv workaround)
  uint16_t
      slowScale; // right-shift applied before write (<1   clkdiv workaround)
  int in_use;
  int started;         // deferred start: 0 until first write
  uint32_t resolution; // logical full scale requested by the caller
  uint32_t div16;      // programmed clock divider in 1/16 steps
  int last_value;      // last logical duty written, 0 before the first write
  int ratio_scale;     // 1: duty scaled by analogScale/resolution, not shifts
};

namespace {

// Counter values scanned while waiting for a wrap; bounds the wait when the
// slice does not count.
constexpr uint32_t kWrapWaitPolls = 4096u;
// Largest counter period whose full-scale compare value still fits the
// 16-bit compare register.
constexpr uint32_t kMaxCounts = 65535u;

struct SliceTiming {
  uint32_t analogScale;
  uint16_t pseudoScale;
  uint16_t slowScale;
  float divider;
};

// Period and divider for a frequency, as hal_pwm_freq_create() derives them.
SliceTiming timing_for(uint32_t frequency_hz, uint32_t resolution) {
  SliceTiming timing = {resolution, 1u, 1u, 0.0f};
  // Scale up until clkdiv fits in [1, 255]
  while (((clock_get_hz(clk_sys) / ((float)timing.analogScale * frequency_hz)) >
          255.0f) &&
         (timing.analogScale < 32768u)) {
    timing.pseudoScale++;
    timing.analogScale *= 2u;
  }
  // Scale down if clkdiv would be < 1
  while (((clock_get_hz(clk_sys) / ((float)timing.analogScale * frequency_hz)) <
          1.0f) &&
         (timing.analogScale >= 6u)) {
    timing.slowScale++;
    timing.analogScale /= 2u;
  }
  timing.divider =
      clock_get_hz(clk_sys) / ((float)timing.analogScale * frequency_hz);
  return timing;
}

// Counter compare value of a logical duty for the current period. The duty is
// clamped to [0, resolution] first, so the scaling never shifts a negative
// value and never leaves the counter period.
uint16_t level_for(const hal_pwm_freq_channel_impl_t *cfg, int value) {
  if (value <= 0) {
    return 0u;
  }
  uint32_t duty = (uint32_t)value;
  if (duty > cfg->resolution) {
    duty = cfg->resolution;
  }
  uint32_t level = 0u;
  if (cfg->ratio_scale != 0) {
    level = duty == cfg->resolution
                ? cfg->analogScale
                : (uint32_t)(((uint64_t)duty * cfg->analogScale +
                              cfg->resolution / 2u) /
                             cfg->resolution);
  } else {
    level = (duty << cfg->pseudoScale) >> cfg->slowScale;
  }
  if (level > cfg->analogScale) {
    level = cfg->analogScale;
  }
  return (uint16_t)level;
}

// Compare value a frequency change writes: the duty written last while the
// channel runs. A channel that waits for its first write or was stopped stays
// at zero duty, so the change cannot bring its output back.
uint16_t retune_level(const hal_pwm_freq_channel_impl_t *cfg) {
  return cfg->started != 0 ? level_for(cfg, cfg->last_value) : (uint16_t)0u;
}

// Active channel, other than self, on the slice that drives pin. A slice has
// two outputs and each takes one channel, so there is at most one.
hal_pwm_freq_channel_impl_t *
slice_peer(hal_pwm_freq_channel_impl_t *pool, size_t count, uint pin,
           const hal_pwm_freq_channel_impl_t *self) {
  const uint slice = pwm_gpio_to_slice_num(pin);
  for (size_t i = 0u; i < count; ++i) {
    if (&pool[i] != self && pool[i].in_use != 0 &&
        pwm_gpio_to_slice_num(pool[i].pin) == slice) {
      return &pool[i];
    }
  }
  return nullptr;
}

// Duty scaling for a counter period of counts: a shift when the period is the
// resolution times a power of two, the ratio otherwise.
void set_period(hal_pwm_freq_channel_impl_t *cfg, uint32_t counts) {
  cfg->analogScale = counts;
  cfg->ratio_scale = 1;
  for (uint16_t shift = 0u; shift < 16u; ++shift) {
    if (((uint64_t)cfg->resolution << shift) == counts) {
      cfg->ratio_scale = 0;
      cfg->pseudoScale = (uint16_t)(shift + 1u);
      cfg->slowScale = 1u;
      break;
    }
  }
}

// Writes the period and the compare value so that both are latched by the
// same counter wrap. TOP and CC are double-buffered; a wrap between the two
// writes would run one period with a mismatched pair.
void write_period_and_level(uint slice, uint pin, uint32_t old_counts,
                            uint32_t counts_per_2us, uint16_t wrap,
                            uint16_t level, bool running) {
  const uint32_t irq_state = save_and_disable_interrupts();
  if (running) {
    const uint32_t guard = counts_per_2us + 2u;
    uint16_t counter = pwm_get_counter(slice);
    if ((old_counts - 1u) - (uint32_t)counter <= guard) {
      for (uint32_t poll = 0u; poll < kWrapWaitPolls; ++poll) {
        const uint16_t now = pwm_get_counter(slice);
        if (now < counter) {
          break;
        }
        counter = now;
      }
    }
  }
  pwm_set_wrap(slice, wrap);
  pwm_set_gpio_level(pin, level);
  restore_interrupts(irq_state);
}

} // namespace

static hal_pwm_freq_channel_impl_t s_pool[HAL_PWM_FREQ_MAX_CHANNELS];

hal_status_t jh_hal_pwm_freq_try_create(uint8_t pin, uint32_t frequency_hz,
                                        uint32_t resolution,
                                        hal_pwm_freq_channel_t *out_channel) {
  const hal_status_t args_status =
      jh_hal_pwm_freq_prepare_create(frequency_hz, resolution, out_channel);
  if (args_status != HAL_OK) {
    return args_status;
  }
  hal_pwm_freq_channel_impl_t *cfg = jh_hal_pwm_freq_begin_locked_create(
      s_pool, hal_get_config()->pwm_freq_max_channels, &pwm_mutex);
  if (!cfg) {
    return HAL_ENOMEM;
  }

  // The two outputs of a slice share its counter. An output takes one
  // channel, and a second channel joins only at the frequency the slice
  // already has: anything else would retime the channel that runs there.
  const hal_pwm_freq_channel_impl_t *peer =
      slice_peer(s_pool, COUNTOF(s_pool), pin, cfg);
  if (peer != nullptr &&
      (pwm_gpio_to_channel(peer->pin) == pwm_gpio_to_channel(pin) ||
       peer->frequency_hz != frequency_hz)) {
    hal_mutex_unlock(pwm_mutex);
    return HAL_EBUSY;
  }

  cfg->in_use = 1;
  cfg->pin = pin;
  cfg->frequency_hz = frequency_hz;
  cfg->resolution = resolution;
  cfg->started = 0;
  if (peer != nullptr) {
    // Take the period and the divider of the slice as they are. The counter,
    // the enable bit and the other output stay untouched; this output waits
    // at zero duty for its first write.
    cfg->div16 = peer->div16;
    if (peer->resolution == resolution) {
      cfg->analogScale = peer->analogScale;
      cfg->pseudoScale = peer->pseudoScale;
      cfg->slowScale = peer->slowScale;
      cfg->ratio_scale = peer->ratio_scale;
    } else {
      set_period(cfg, peer->analogScale);
    }
    pwm_set_gpio_level(cfg->pin, 0u);
  } else {
    const SliceTiming timing = timing_for(frequency_hz, resolution);
    cfg->analogScale = timing.analogScale;
    cfg->pseudoScale = timing.pseudoScale;
    cfg->slowScale = timing.slowScale;

    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv(&c, timing.divider);
    pwm_config_set_wrap(&c, cfg->analogScale - 1u);
    cfg->div16 = c.div;

    // Configure slice but do NOT start yet - prevents glitch on pins
    // with inverted logic (0% duty = actuator ON).  The slice and GPIO
    // function are enabled on the first hal_pwm_freq_write() call,
    // after the correct duty-cycle level has been set.
    pwm_init(pwm_gpio_to_slice_num(cfg->pin), &c, false);
  }

  hal_mutex_unlock(pwm_mutex);
  *out_channel = cfg;
  return HAL_OK;
}

hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin, uint32_t frequency_hz,
                                           uint32_t resolution) {
  return jh_hal_pwm_freq_create_compat(pin, frequency_hz, resolution);
}

uint32_t hal_pwm_freq_source_clock_hz(uint8_t pin) {
  (void)pin;
  return clock_get_hz(clk_sys);
}

void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value) {
  if (!ch) {
    hal_derr_limited("pwm_freq", "write called with NULL channel");
    return;
  }
  pwm_ensure_mutex();

  hal_pwm_freq_channel_impl_t *cfg = ch;

  hal_mutex_lock(pwm_mutex);
  cfg->last_value = value;
  pwm_set_gpio_level(cfg->pin, level_for(cfg, value));
  if (!cfg->started) {
    gpio_set_function(cfg->pin, GPIO_FUNC_PWM);
    pwm_set_enabled(pwm_gpio_to_slice_num(cfg->pin), true);
    cfg->started = 1;
  }
  hal_mutex_unlock(pwm_mutex);
}

hal_status_t hal_pwm_freq_set_frequency(hal_pwm_freq_channel_t ch,
                                        uint32_t frequency_hz) {
  if (!ch || frequency_hz == 0u) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = jh_hal_mutex_try_create_once(&pwm_mutex);
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  hal_pwm_freq_channel_impl_t *cfg = ch;
  if (cfg->in_use == 0) {
    hal_mutex_unlock(mutex);
    return HAL_EINVAL;
  }
  if (cfg->frequency_hz == frequency_hz) {
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }
  if (slice_peer(s_pool, COUNTOF(s_pool), cfg->pin, cfg) != nullptr) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }

  const uint slice = pwm_gpio_to_slice_num(cfg->pin);
  const uint32_t clock_hz = clock_get_hz(clk_sys);
  const uint32_t old_counts = cfg->analogScale;
  const uint32_t counts_per_2us =
      (uint32_t)(((uint64_t)clock_hz * 16u) / cfg->div16 / 500000u);

  // First choice: keep the divider and change the counter period only. Both
  // buffered registers switch at one wrap, so no period is irregular.
  const uint64_t ticks_per_second16 = (uint64_t)clock_hz * 16u;
  const uint64_t divided = (uint64_t)cfg->div16 * frequency_hz;
  const uint64_t counts = (ticks_per_second16 + divided / 2u) / divided;
  if (counts >= cfg->resolution && counts <= kMaxCounts) {
    set_period(cfg, (uint32_t)counts);
    cfg->frequency_hz = frequency_hz;
    write_period_and_level(slice, cfg->pin, old_counts, counts_per_2us,
                           (uint16_t)(counts - 1u), retune_level(cfg),
                           cfg->started != 0);
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }

  // Otherwise the divider has to change. It is not buffered, so the period
  // in progress finishes at the new counting rate.
  const SliceTiming timing = timing_for(frequency_hz, cfg->resolution);
  if (!(timing.divider >= 1.0f && timing.divider < 256.0f)) {
    hal_mutex_unlock(mutex);
    return HAL_EINVAL;
  }
  pwm_config c = pwm_get_default_config();
  pwm_config_set_clkdiv(&c, timing.divider);
  cfg->analogScale = timing.analogScale;
  cfg->pseudoScale = timing.pseudoScale;
  cfg->slowScale = timing.slowScale;
  cfg->ratio_scale = 0;
  cfg->div16 = c.div;
  cfg->frequency_hz = frequency_hz;
  const uint32_t irq_state = save_and_disable_interrupts();
  pwm_set_clkdiv_int_frac4(slice, (uint8_t)(cfg->div16 >> 4),
                           (uint8_t)(cfg->div16 & 0xFu));
  pwm_set_wrap(slice, (uint16_t)(cfg->analogScale - 1u));
  pwm_set_gpio_level(cfg->pin, retune_level(cfg));
  restore_interrupts(irq_state);
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

uint32_t hal_pwm_freq_get_period_ns(hal_pwm_freq_channel_t ch) {
  if (!ch) {
    return 0u;
  }
  hal_mutex_t mutex = jh_hal_mutex_try_create_once(&pwm_mutex);
  if (mutex == NULL) {
    return 0u;
  }
  hal_mutex_lock(mutex);
  uint32_t period_ns = 0u;
  if (ch->in_use != 0) {
    const uint64_t numerator =
        (uint64_t)ch->analogScale * ch->div16 * UINT64_C(1000000000);
    const uint64_t denominator = (uint64_t)clock_get_hz(clk_sys) * 16u;
    period_ns = (uint32_t)((numerator + denominator / 2u) / denominator);
  }
  hal_mutex_unlock(mutex);
  return period_ns;
}

void hal_pwm_freq_stop(hal_pwm_freq_channel_t ch) {
  if (!ch) {
    return;
  }
  pwm_ensure_mutex();
  hal_mutex_lock(pwm_mutex);
  // The counter stops only when no other channel runs on the slice. Either
  // way this output goes to zero duty: its pin stays on the slice, and it
  // must not resume the old duty when the other channel starts the counter.
  // A stopped slice latches the level at once, a running one at its wrap.
  const hal_pwm_freq_channel_impl_t *peer =
      slice_peer(s_pool, COUNTOF(s_pool), ch->pin, ch);
  if (peer == nullptr || peer->started == 0) {
    pwm_set_enabled(pwm_gpio_to_slice_num(ch->pin), false);
  }
  pwm_set_gpio_level(ch->pin, 0u);
  ch->started = 0;
  hal_mutex_unlock(pwm_mutex);
}

void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch) {
  if (!ch) {
    return;
  }
  pwm_ensure_mutex();
  hal_mutex_lock(pwm_mutex);
  ch->started = 0;
  ch->in_use = 0;
  hal_mutex_unlock(pwm_mutex);
}

#endif /* HAL_ENABLE_PWM_FREQ */
#endif // HAL_TARGET_IS_RP
