#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal_pwm_stm32g474.h"
#include "port/stm32g474_regs.h"

#include <stddef.h>
#include <stdint.h>

namespace {
constexpr uint32_t kMaxPeriodTicks = 65536u;
constexpr uint32_t kMaxPrescalerDivider = 65536u;

enum PwmTimer : uint8_t {
  PWM_TIMER_TIM2 = 0,
  PWM_TIMER_TIM3,
  PWM_TIMER_TIM4,
  PWM_TIMER_TIM15,
  PWM_TIMER_TIM16,
  PWM_TIMER_TIM17,
  PWM_TIMER_COUNT,
};

struct PwmPinMap {
  uint8_t pin;
  uint8_t timer;
  uint8_t channel;
  uint8_t af;
};

static const PwmPinMap kPinMap[] = {
    {5u, PWM_TIMER_TIM2, 1u, 1u},    // PA5, Nucleo LD2 / Arduino D13
    {1u, PWM_TIMER_TIM2, 2u, 1u},    // PA1
    {2u, PWM_TIMER_TIM15, 1u, 9u},   // PA2, TIM15 avoids TIM2 when possible
    {3u, PWM_TIMER_TIM15, 2u, 9u},   // PA3
    {6u, PWM_TIMER_TIM3, 1u, 2u},    // PA6
    {7u, PWM_TIMER_TIM3, 2u, 2u},    // PA7
    {11u, PWM_TIMER_TIM4, 1u, 10u},  // PA11
    {12u, PWM_TIMER_TIM4, 2u, 10u},  // PA12
    {16u, PWM_TIMER_TIM3, 3u, 2u},   // PB0
    {17u, PWM_TIMER_TIM3, 4u, 2u},   // PB1
    {20u, PWM_TIMER_TIM16, 1u, 1u},  // PB4
    {21u, PWM_TIMER_TIM17, 1u, 10u}, // PB5
    {22u, PWM_TIMER_TIM4, 1u, 2u},   // PB6
    {23u, PWM_TIMER_TIM4, 2u, 2u},   // PB7
    {24u, PWM_TIMER_TIM4, 3u, 2u},   // PB8
    {25u, PWM_TIMER_TIM4, 4u, 2u},   // PB9
    {30u, PWM_TIMER_TIM15, 1u, 1u},  // PB14
    {31u, PWM_TIMER_TIM15, 2u, 1u},  // PB15
    {38u, PWM_TIMER_TIM3, 1u, 2u},   // PC6
    {39u, PWM_TIMER_TIM3, 2u, 2u},   // PC7
    {40u, PWM_TIMER_TIM3, 3u, 2u},   // PC8
    {41u, PWM_TIMER_TIM3, 4u, 2u},   // PC9
};

struct TimerState {
  uint32_t frequency_hz;
  uint32_t period_ticks;
};

static TimerState s_timer_state[PWM_TIMER_COUNT] = {};

static const PwmPinMap *find_pin(uint8_t pin) {
  for (size_t i = 0; i < sizeof(kPinMap) / sizeof(kPinMap[0]); i++) {
    if (kPinMap[i].pin == pin) {
      return &kPinMap[i];
    }
  }
  return nullptr;
}

#ifdef JH_STM32G474_HW
struct TimerHw {
  uint32_t base;
  uint32_t clock_hz;
  uint32_t rcc_mask;
  bool apb2;
  bool has_bdtr;
};

static const TimerHw kTimerHw[PWM_TIMER_COUNT] = {
    {TIM2_BASE, JH_G474_PCLK1_HZ, RCC_APB1ENR1_TIM2EN, false, false},
    {TIM3_BASE, JH_G474_PCLK1_HZ, RCC_APB1ENR1_TIM3EN, false, false},
    {TIM4_BASE, JH_G474_PCLK1_HZ, RCC_APB1ENR1_TIM4EN, false, false},
    {TIM15_BASE, JH_G474_PCLK2_HZ, RCC_APB2ENR_TIM15EN, true, true},
    {TIM16_BASE, JH_G474_PCLK2_HZ, RCC_APB2ENR_TIM16EN, true, true},
    {TIM17_BASE, JH_G474_PCLK2_HZ, RCC_APB2ENR_TIM17EN, true, true},
};

static void enable_timer_clock(const TimerHw &timer) {
  if (timer.apb2) {
    RCC_APB2ENR |= timer.rcc_mask;
    (void)RCC_APB2ENR;
  } else {
    RCC_APB1ENR1 |= timer.rcc_mask;
    (void)RCC_APB1ENR1;
  }
}

static void gpio_set_af(uint8_t pin, uint8_t af) {
  const uint32_t port = (uint32_t)(pin >> 4);
  const uint32_t n = (uint32_t)(pin & 0x0Fu);
  if (port > 6u) {
    return;
  }

  RCC_AHB2ENR |= (1u << port);
  (void)RCC_AHB2ENR;

  GPIO_MODER(port) =
      (GPIO_MODER(port) & ~(0x3u << (n * 2u))) | (GPIO_MODE_AF << (n * 2u));
  GPIO_OTYPER(port) &= ~(1u << n);
  GPIO_OSPEEDR(port) |= (0x3u << (n * 2u));
  GPIO_PUPDR(port) =
      (GPIO_PUPDR(port) & ~(0x3u << (n * 2u))) | (GPIO_PUPD_NONE << (n * 2u));

  if (n < 8u) {
    GPIO_AFRL(port) =
        (GPIO_AFRL(port) & ~(0xFu << (n * 4u))) | ((uint32_t)af << (n * 4u));
  } else {
    const uint32_t p = n - 8u;
    GPIO_AFRH(port) =
        (GPIO_AFRH(port) & ~(0xFu << (p * 4u))) | ((uint32_t)af << (p * 4u));
  }
}

static uint32_t prescaler_for(uint32_t clock_hz, uint32_t frequency_hz,
                              uint32_t period_ticks) {
  const uint64_t target = (uint64_t)frequency_hz * (uint64_t)period_ticks;
  if (target == 0u) {
    return 0u;
  }

  uint64_t divider = ((uint64_t)clock_hz + (target / 2u)) / target;
  if (divider < 1u) {
    divider = 1u;
  } else if (divider > kMaxPrescalerDivider) {
    divider = kMaxPrescalerDivider;
  }
  return (uint32_t)(divider - 1u);
}

static void configure_channel_mode(uint32_t base, uint8_t channel) {
  if (channel == 1u) {
    uint32_t ccmr = TIM_CCMR1_REG(base);
    ccmr &= ~(TIM_CCMR1_CC1S_MASK | TIM_CCMR1_OC1M_MASK);
    ccmr |= TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_PWM1;
    TIM_CCMR1_REG(base) = ccmr;
  } else if (channel == 2u) {
    uint32_t ccmr = TIM_CCMR1_REG(base);
    ccmr &= ~(TIM_CCMR1_CC2S_MASK | TIM_CCMR1_OC2M_MASK);
    ccmr |= TIM_CCMR1_OC2PE | TIM_CCMR1_OC2M_PWM1;
    TIM_CCMR1_REG(base) = ccmr;
  } else if (channel == 3u) {
    uint32_t ccmr = TIM_CCMR2_REG(base);
    ccmr &= ~(TIM_CCMR2_CC3S_MASK | TIM_CCMR2_OC3M_MASK);
    ccmr |= TIM_CCMR2_OC3PE | TIM_CCMR2_OC3M_PWM1;
    TIM_CCMR2_REG(base) = ccmr;
  } else if (channel == 4u) {
    uint32_t ccmr = TIM_CCMR2_REG(base);
    ccmr &= ~(TIM_CCMR2_CC4S_MASK | TIM_CCMR2_OC4M_MASK);
    ccmr |= TIM_CCMR2_OC4PE | TIM_CCMR2_OC4M_PWM1;
    TIM_CCMR2_REG(base) = ccmr;
  }
}

static uint32_t ccer_shift(uint8_t channel) {
  return (uint32_t)(channel - 1u) * 4u;
}

static void enable_channel(uint32_t base, uint8_t channel) {
  const uint32_t shift = ccer_shift(channel);
  TIM_CCER_REG(base) = (TIM_CCER_REG(base) & ~(0xFu << shift)) | (1u << shift);
}

static void disable_channel(uint32_t base, uint8_t channel) {
  TIM_CCER_REG(base) &= ~(1u << ccer_shift(channel));
}

static void write_ccr(uint32_t base, uint8_t channel, uint32_t compare) {
  if (channel == 1u) {
    TIM_CCR1(base) = compare;
  } else if (channel == 2u) {
    TIM_CCR2(base) = compare;
  } else if (channel == 3u) {
    TIM_CCR3(base) = compare;
  } else if (channel == 4u) {
    TIM_CCR4(base) = compare;
  }
}

static bool configure_hw(const PwmPinMap &map, uint32_t frequency_hz,
                         uint32_t period_ticks) {
  const TimerHw &timer = kTimerHw[map.timer];
  const uint32_t arr = period_ticks - 1u;
  enable_timer_clock(timer);

  TIM_SMCR(timer.base) = 0u;
  TIM_PSC(timer.base) =
      prescaler_for(timer.clock_hz, frequency_hz, period_ticks);
  TIM_ARR(timer.base) = arr;
  TIM_CR1(timer.base) |= TIM_CR1_ARPE;
  configure_channel_mode(timer.base, map.channel);
  TIM_EGR(timer.base) = TIM_EGR_UG;

  if (timer.has_bdtr) {
    TIM_BDTR(timer.base) |= TIM_BDTR_MOE;
  }
  return true;
}

static uint32_t timer_clock_hz(uint8_t timer) {
  return kTimerHw[timer].clock_hz;
}
#else
static uint32_t s_host_compare[128] = {};
static uint32_t s_host_frequency[128] = {};
static uint32_t s_host_period[128] = {};

static bool configure_hw(const PwmPinMap &map, uint32_t frequency_hz,
                         uint32_t period_ticks) {
  s_host_frequency[map.pin] = frequency_hz;
  s_host_period[map.pin] = period_ticks;
  return true;
}

static uint32_t timer_clock_hz(uint8_t timer) {
  (void)timer;
  return JH_G474_CORE_CLOCK_HZ;
}
#endif

static bool valid_period(uint32_t period_ticks) {
  return period_ticks > 0u && period_ticks <= kMaxPeriodTicks;
}

static bool period_can_reach_frequency(uint32_t clock_hz, uint32_t frequency_hz,
                                       uint32_t period_ticks) {
  const uint64_t target = (uint64_t)frequency_hz * (uint64_t)period_ticks;
  return target <= (uint64_t)clock_hz &&
         (uint64_t)clock_hz <= target * (uint64_t)kMaxPrescalerDivider;
}

static bool period_is_too_slow(uint32_t clock_hz, uint32_t frequency_hz,
                               uint32_t period_ticks) {
  const uint64_t target = (uint64_t)frequency_hz * (uint64_t)period_ticks;
  return target > 0u &&
         (uint64_t)clock_hz > target * (uint64_t)kMaxPrescalerDivider;
}

static bool period_is_too_fast(uint32_t clock_hz, uint32_t frequency_hz,
                               uint32_t period_ticks) {
  const uint64_t target = (uint64_t)frequency_hz * (uint64_t)period_ticks;
  return target > (uint64_t)clock_hz;
}
} // namespace

bool jh_stm32_pwm_prepare_pin(uint8_t pin, uint32_t frequency_hz,
                              uint32_t period_ticks,
                              jh_stm32_pwm_channel_desc *out) {
  const PwmPinMap *map = find_pin(pin);
  if (!map || !out || frequency_hz == 0u || !valid_period(period_ticks)) {
    return false;
  }

  if (s_timer_state[map->timer].frequency_hz != frequency_hz ||
      s_timer_state[map->timer].period_ticks != period_ticks) {
    if (!configure_hw(*map, frequency_hz, period_ticks)) {
      return false;
    }
    s_timer_state[map->timer].frequency_hz = frequency_hz;
    s_timer_state[map->timer].period_ticks = period_ticks;
  } else {
#ifdef JH_STM32G474_HW
    configure_channel_mode(kTimerHw[map->timer].base, map->channel);
#endif
  }

  out->pin = map->pin;
  out->timer = map->timer;
  out->channel = map->channel;
  out->valid = 1u;
  out->period_ticks = period_ticks;
  return true;
}

bool jh_stm32_pwm_pin_supported(uint8_t pin) {
  return find_pin(pin) != nullptr;
}

bool jh_stm32_pwm_prepare_frequency_pin(uint8_t pin, uint32_t frequency_hz,
                                        uint32_t requested_period_ticks,
                                        jh_stm32_pwm_channel_desc *out,
                                        uint8_t *left_shift,
                                        uint8_t *right_shift) {
  const PwmPinMap *map = find_pin(pin);
  if (!map || !out || !left_shift || !right_shift || frequency_hz == 0u ||
      requested_period_ticks == 0u ||
      requested_period_ticks > kMaxPeriodTicks) {
    return false;
  }

  uint32_t period_ticks = requested_period_ticks;
  uint8_t lshift = 0u;
  uint8_t rshift = 0u;
  const uint32_t clock_hz = timer_clock_hz(map->timer);

  while (period_is_too_slow(clock_hz, frequency_hz, period_ticks) &&
         period_ticks <= (kMaxPeriodTicks / 2u)) {
    period_ticks *= 2u;
    lshift++;
  }

  while (period_is_too_fast(clock_hz, frequency_hz, period_ticks) &&
         period_ticks > 1u) {
    period_ticks /= 2u;
    rshift++;
  }

  if (!period_can_reach_frequency(clock_hz, frequency_hz, period_ticks)) {
    return false;
  }

  if (!jh_stm32_pwm_prepare_pin(pin, frequency_hz, period_ticks, out)) {
    return false;
  }

  *left_shift = lshift;
  *right_shift = rshift;
  return true;
}

void jh_stm32_pwm_write_compare(const jh_stm32_pwm_channel_desc *ch,
                                uint32_t compare) {
  if (!ch || !ch->valid) {
    return;
  }
  if (compare > ch->period_ticks) {
    compare = ch->period_ticks;
  }

#ifdef JH_STM32G474_HW
  const TimerHw &timer = kTimerHw[ch->timer];
  if (compare > 0xFFFFu && timer.base != TIM2_BASE) {
    compare = 0xFFFFu;
  }
  write_ccr(timer.base, ch->channel, compare);
#else
  if (ch->pin < 128u) {
    s_host_compare[ch->pin] = compare;
  }
#endif
}

void jh_stm32_pwm_start_output(const jh_stm32_pwm_channel_desc *ch) {
  if (!ch || !ch->valid) {
    return;
  }

#ifdef JH_STM32G474_HW
  const PwmPinMap *map = find_pin(ch->pin);
  if (!map) {
    return;
  }
  const TimerHw &timer = kTimerHw[ch->timer];
  gpio_set_af(map->pin, map->af);
  enable_channel(timer.base, ch->channel);
  if (timer.has_bdtr) {
    TIM_BDTR(timer.base) |= TIM_BDTR_MOE;
  }
  TIM_CR1(timer.base) |= TIM_CR1_CEN;
#endif
}

void jh_stm32_pwm_release_output(const jh_stm32_pwm_channel_desc *ch) {
  if (!ch || !ch->valid) {
    return;
  }

#ifdef JH_STM32G474_HW
  const TimerHw &timer = kTimerHw[ch->timer];
  disable_channel(timer.base, ch->channel);
#endif
}

uint32_t jh_stm32_pwm_compare_address(const jh_stm32_pwm_channel_desc *ch) {
  if (!ch || !ch->valid) {
    return 0u;
  }

#ifdef JH_STM32G474_HW
  const TimerHw &timer = kTimerHw[ch->timer];
  if (ch->channel == 1u) {
    return timer.base + 0x34u;
  }
  if (ch->channel == 2u) {
    return timer.base + 0x38u;
  }
  if (ch->channel == 3u) {
    return timer.base + 0x3Cu;
  }
  if (ch->channel == 4u) {
    return timer.base + 0x40u;
  }
#endif

  return 0u;
}

uint32_t jh_stm32_pwm_timer_dma_request(const jh_stm32_pwm_channel_desc *ch) {
  if (!ch || !ch->valid) {
    return 0u;
  }

  switch (ch->timer) {
  case PWM_TIMER_TIM2:
    return DMA_REQUEST_TIM2_UP;
  case PWM_TIMER_TIM3:
    return DMA_REQUEST_TIM3_UP;
  case PWM_TIMER_TIM4:
    return DMA_REQUEST_TIM4_UP;
  case PWM_TIMER_TIM15:
    return DMA_REQUEST_TIM15_UP;
  case PWM_TIMER_TIM16:
    return DMA_REQUEST_TIM16_UP;
  case PWM_TIMER_TIM17:
    return DMA_REQUEST_TIM17_UP;
  default:
    return 0u;
  }
}

void jh_stm32_pwm_set_update_dma_request(const jh_stm32_pwm_channel_desc *ch,
                                         bool enabled) {
  if (!ch || !ch->valid) {
    return;
  }

#ifdef JH_STM32G474_HW
  const TimerHw &timer = kTimerHw[ch->timer];
  if (enabled) {
    TIM_DIER(timer.base) |= TIM_DIER_UDE;
  } else {
    TIM_DIER(timer.base) &= ~TIM_DIER_UDE;
  }
#else
  (void)enabled;
#endif
}

#endif // HAL_TARGET_IS_STM32G474
