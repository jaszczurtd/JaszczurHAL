#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_RTC

#include "hal/rtc/jh_rtc_provider.h"

#if defined(HAL_ENABLE_INTERNAL_RTC) && defined(JH_STM32G474_HW)

#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "port/stm32g474_regs.h"
#include "port/stm32g474_rtc_codec.h"
#include "port/stm32g474_rtc_wakeup.h"

#include <cstddef>
#include <cstring>

#define JH_G474_RTC_BACKUP_MAGIC 0x4A485254u /* "JHRT" */
#define JH_G474_RTC_REGISTER_POLL_LIMIT 1000000u
#define JH_G474_RTC_ALARM_EXTI_LINE (1u << 18)
#define JH_G474_RTC_WAKEUP_EXTI_LINE (1u << 20)

typedef struct {
  hal_rtc_clock_source_t clock_source;
  hal_rtc_wakeup_state_t wakeup;
  bool initialized;
} jh_stm32g474_rtc_context_t;

static_assert(sizeof(jh_stm32g474_rtc_context_t) <=
                  JH_RTC_PROVIDER_STORAGE_SIZE,
              "STM32G474 RTC provider storage is too small");
static_assert(alignof(jh_stm32g474_rtc_context_t) <= alignof(std::max_align_t),
              "STM32G474 RTC provider alignment is too strict");

static jh_stm32g474_rtc_context_t *s_active_context = nullptr;
static volatile bool s_alarm_pending = false;
static volatile bool s_wakeup_pending = false;

static jh_stm32g474_rtc_context_t *rtc_context(void *context) {
  return static_cast<jh_stm32g474_rtc_context_t *>(context);
}

static bool rtc_claim_context(jh_stm32g474_rtc_context_t *context) {
  hal_critical_section_enter();
  const bool available = s_active_context == nullptr;
  if (available) {
    s_active_context = context;
  }
  hal_critical_section_exit();
  return available;
}

static void rtc_release_context(jh_stm32g474_rtc_context_t *context) {
  hal_critical_section_enter();
  if (s_active_context == context) {
    s_active_context = nullptr;
  }
  hal_critical_section_exit();
}

static void rtc_clear_pending_alarm(void) {
  hal_critical_section_enter();
  s_alarm_pending = false;
  hal_critical_section_exit();
}

static void rtc_clear_pending_wakeup(void) {
  hal_critical_section_enter();
  s_wakeup_pending = false;
  hal_critical_section_exit();
}

static bool rtc_wait_register_set(volatile uint32_t *reg, uint32_t mask) {
  for (uint32_t poll = 0u; poll < JH_G474_RTC_REGISTER_POLL_LIMIT; ++poll) {
    if ((*reg & mask) == mask) {
      return true;
    }
  }
  return false;
}

static bool rtc_wait_clock_ready(volatile uint32_t *reg, uint32_t mask,
                                 uint32_t timeout_ms) {
  const uint32_t started = hal_millis();
  do {
    if ((*reg & mask) == mask) {
      return true;
    }
    hal_delay_ms(1u);
  } while (!hal_millis_deadline_expired(started, timeout_ms));
  return (*reg & mask) == mask;
}

static void rtc_write_enable(void) {
  RTC_WPR = RTC_WPR_KEY1;
  RTC_WPR = RTC_WPR_KEY2;
}

static void rtc_write_disable(void) { RTC_WPR = RTC_WPR_LOCK; }

static hal_status_t rtc_enable_backup_access(void) {
  RCC_APB1ENR1 |= RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN;
  (void)RCC_APB1ENR1;
  PWR_CR1 |= PWR_CR1_DBP;
  return rtc_wait_register_set(&PWR_CR1, PWR_CR1_DBP) ? HAL_OK : HAL_ETIMEOUT;
}

static hal_rtc_clock_source_t rtc_source_from_bdcr(uint32_t bdcr) {
  switch (bdcr & RCC_BDCR_RTCSEL_MASK) {
  case RCC_BDCR_RTCSEL_LSE:
    return HAL_RTC_CLOCK_SOURCE_LSE;
  case RCC_BDCR_RTCSEL_LSI:
    return HAL_RTC_CLOCK_SOURCE_LSI;
  case RCC_BDCR_RTCSEL_HSE_DIV32:
    return HAL_RTC_CLOCK_SOURCE_HSE_DIV32;
  default:
    return HAL_RTC_CLOCK_SOURCE_AUTO;
  }
}

static hal_status_t rtc_enable_source(hal_rtc_clock_source_t source) {
  switch (source) {
  case HAL_RTC_CLOCK_SOURCE_LSE:
    RCC_BDCR |= RCC_BDCR_LSEON;
    return rtc_wait_clock_ready(&RCC_BDCR, RCC_BDCR_LSERDY,
                                HAL_STM32_RTC_LSE_STARTUP_TIMEOUT_MS)
               ? HAL_OK
               : HAL_ETIMEOUT;
  case HAL_RTC_CLOCK_SOURCE_LSI:
    RCC_CSR |= RCC_CSR_LSION;
    return rtc_wait_clock_ready(&RCC_CSR, RCC_CSR_LSIRDY,
                                HAL_STM32_RTC_LSI_STARTUP_TIMEOUT_MS)
               ? HAL_OK
               : HAL_ETIMEOUT;
  default:
    return HAL_EUNSUPPORTED;
  }
}

static hal_status_t
rtc_select_clock_source(hal_rtc_clock_source_t requested,
                        hal_rtc_clock_source_t *out_source) {
  if (out_source == nullptr || (requested != HAL_RTC_CLOCK_SOURCE_AUTO &&
                                requested != HAL_RTC_CLOCK_SOURCE_LSE &&
                                requested != HAL_RTC_CLOCK_SOURCE_LSI)) {
    return HAL_EUNSUPPORTED;
  }

  hal_rtc_clock_source_t selected = rtc_source_from_bdcr(RCC_BDCR);
  if (selected != HAL_RTC_CLOCK_SOURCE_AUTO) {
    if (requested != HAL_RTC_CLOCK_SOURCE_AUTO && requested != selected) {
      return HAL_EBUSY;
    }
    const hal_status_t status = rtc_enable_source(selected);
    if (status != HAL_OK) {
      return status;
    }
    RCC_BDCR |= RCC_BDCR_RTCEN;
    *out_source = selected;
    return HAL_OK;
  }

  if (requested == HAL_RTC_CLOCK_SOURCE_AUTO) {
    selected = HAL_RTC_CLOCK_SOURCE_LSE;
    if (rtc_enable_source(selected) != HAL_OK) {
      RCC_BDCR &= ~RCC_BDCR_LSEON;
      selected = HAL_RTC_CLOCK_SOURCE_LSI;
    }
  } else {
    selected = requested;
  }

  const hal_status_t source_status = rtc_enable_source(selected);
  if (source_status != HAL_OK) {
    return source_status;
  }

  const uint32_t selection = selected == HAL_RTC_CLOCK_SOURCE_LSE
                                 ? RCC_BDCR_RTCSEL_LSE
                                 : RCC_BDCR_RTCSEL_LSI;
  RCC_BDCR = (RCC_BDCR & ~RCC_BDCR_RTCSEL_MASK) | selection | RCC_BDCR_RTCEN;
  if ((RCC_BDCR & (RCC_BDCR_RTCSEL_MASK | RCC_BDCR_RTCEN)) !=
      (selection | RCC_BDCR_RTCEN)) {
    return HAL_EHW;
  }
  *out_source = selected;
  return HAL_OK;
}

static bool rtc_clock_is_ready(hal_rtc_clock_source_t source) {
  if ((RCC_BDCR & RCC_BDCR_RTCEN) == 0u) {
    return false;
  }
  if (source == HAL_RTC_CLOCK_SOURCE_LSE) {
    return (RCC_BDCR & (RCC_BDCR_LSERDY | RCC_BDCR_LSECSSD)) == RCC_BDCR_LSERDY;
  }
  if (source == HAL_RTC_CLOCK_SOURCE_LSI) {
    return (RCC_CSR & RCC_CSR_LSIRDY) != 0u;
  }
  return false;
}

static bool rtc_time_is_valid(const jh_stm32g474_rtc_context_t *context) {
  return context != nullptr && context->initialized &&
         rtc_clock_is_ready(context->clock_source) &&
         (RTC_ICSR & RTC_ICSR_INITS) != 0u &&
         TAMP_BKPR(HAL_STM32_RTC_BACKUP_REGISTER_INDEX) ==
             JH_G474_RTC_BACKUP_MAGIC;
}

static hal_status_t rtc_wait_shadow_sync(void) {
  rtc_write_enable();
  RTC_ICSR &= ~RTC_ICSR_RSF;
  rtc_write_disable();
  return rtc_wait_register_set(&RTC_ICSR, RTC_ICSR_RSF) ? HAL_OK : HAL_ETIMEOUT;
}

static hal_status_t
rtc_write_calendar(const jh_stm32g474_rtc_context_t *context,
                   const hal_rtc_datetime_t *datetime, bool mark_valid) {
  uint32_t tr = 0u;
  uint32_t dr = 0u;
  if (context == nullptr ||
      !jh_stm32g474_rtc_encode_datetime(datetime, &tr, &dr)) {
    return datetime != nullptr && datetime->year < JH_G474_RTC_MIN_YEAR
               ? HAL_EOVERFLOW
               : HAL_EINVAL;
  }

  TAMP_BKPR(HAL_STM32_RTC_BACKUP_REGISTER_INDEX) = 0u;
  rtc_write_enable();
  RTC_ICSR |= RTC_ICSR_INIT;
  if (!rtc_wait_register_set(&RTC_ICSR, RTC_ICSR_INITF)) {
    rtc_write_disable();
    return HAL_ETIMEOUT;
  }
  RTC_CR &= ~RTC_CR_FMT;
  RTC_PRER = context->clock_source == HAL_RTC_CLOCK_SOURCE_LSE
                 ? JH_G474_RTC_PRER_LSE
                 : JH_G474_RTC_PRER_LSI;
  RTC_TR = tr;
  RTC_DR = dr;
  RTC_ICSR &= ~RTC_ICSR_INIT;
  rtc_write_disable();

  const hal_status_t sync_status = rtc_wait_shadow_sync();
  if (sync_status == HAL_OK) {
    TAMP_BKPR(HAL_STM32_RTC_BACKUP_REGISTER_INDEX) =
        mark_valid ? JH_G474_RTC_BACKUP_MAGIC : 0u;
  }
  return sync_status;
}

static void rtc_alarm_irq_disable(void) {
  NVIC_ICER(RTC_Alarm_IRQn / 32u) = 1u << (RTC_Alarm_IRQn % 32u);
  EXTI_IMR1 &= ~JH_G474_RTC_ALARM_EXTI_LINE;
  rtc_write_enable();
  RTC_CR &= ~RTC_CR_ALRAIE;
  rtc_write_disable();
}

static void rtc_wakeup_irq_disable(void) {
  NVIC_ICER(RTC_WKUP_IRQn / 32u) = 1u << (RTC_WKUP_IRQn % 32u);
  EXTI_IMR1 &= ~JH_G474_RTC_WAKEUP_EXTI_LINE;
  rtc_write_enable();
  RTC_CR &= ~RTC_CR_WUTIE;
  rtc_write_disable();
}

static hal_status_t rtc_wakeup_disable_hardware(void) {
  rtc_wakeup_irq_disable();
  rtc_write_enable();
  RTC_CR &= ~RTC_CR_WUTE;
  const bool writable = rtc_wait_register_set(&RTC_ICSR, RTC_ICSR_WUTWF);
  RTC_SCR = RTC_SCR_CWUTF;
  rtc_write_disable();
  EXTI_PR1 = JH_G474_RTC_WAKEUP_EXTI_LINE;
  return writable ? HAL_OK : HAL_ETIMEOUT;
}

static hal_status_t internal_initialize(void *context,
                                        const hal_rtc_config_t *config) {
  if (context == nullptr || config == nullptr ||
      config->chip != HAL_RTC_CHIP_INTERNAL) {
    return HAL_EINVAL;
  }

  jh_stm32g474_rtc_context_t *internal = rtc_context(context);
  if (!rtc_claim_context(internal)) {
    return HAL_EBUSY;
  }
  std::memset(internal, 0, sizeof(*internal));

  hal_status_t status = rtc_enable_backup_access();
  if (status == HAL_OK) {
    status = rtc_select_clock_source(config->bus.internal.clock_source,
                                     &internal->clock_source);
  }
  if (status == HAL_OK && (RTC_ICSR & RTC_ICSR_INITS) != 0u &&
      (RTC_CR & RTC_CR_FMT) != 0u) {
    status = HAL_ECONFIG;
  }

  if (status == HAL_OK && (RTC_ICSR & RTC_ICSR_INITS) == 0u) {
    const hal_rtc_datetime_t initial = {
        0u, 0u, 0u, 1u, 6u, 1u, 2000u, false,
    };
    status = rtc_write_calendar(internal, &initial, false);
  } else if (status == HAL_OK) {
    status = rtc_wait_shadow_sync();
  }

  if (status != HAL_OK) {
    std::memset(internal, 0, sizeof(*internal));
    rtc_release_context(internal);
    return status;
  }

  rtc_alarm_irq_disable();
  status = rtc_wakeup_disable_hardware();
  rtc_clear_pending_alarm();
  rtc_clear_pending_wakeup();
  if (status != HAL_OK) {
    std::memset(internal, 0, sizeof(*internal));
    rtc_release_context(internal);
    return status;
  }
  internal->initialized = true;
  return HAL_OK;
}

static void internal_deinitialize(void *context) {
  if (context == nullptr) {
    return;
  }
  jh_stm32g474_rtc_context_t *internal = rtc_context(context);
  if (internal->initialized) {
    rtc_alarm_irq_disable();
    (void)rtc_wakeup_disable_hardware();
  }
  rtc_clear_pending_alarm();
  rtc_clear_pending_wakeup();
  rtc_release_context(internal);
  std::memset(internal, 0, sizeof(*internal));
}

static hal_status_t internal_get_datetime(void *context,
                                          hal_rtc_datetime_t *out_datetime) {
  if (context == nullptr || out_datetime == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }

  const uint32_t tr = RTC_TR;
  const uint32_t dr = RTC_DR;
  hal_rtc_datetime_t value = {};
  if (!jh_stm32g474_rtc_decode_datetime(tr, dr, &value)) {
    return HAL_EIO;
  }
  value.clock_integrity = rtc_time_is_valid(rtc_context(context));
  *out_datetime = value;
  return HAL_OK;
}

static hal_status_t internal_set_datetime(void *context,
                                          const hal_rtc_datetime_t *datetime) {
  if (context == nullptr || datetime == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  return rtc_write_calendar(rtc_context(context), datetime, true);
}

static hal_status_t internal_get_clock_integrity(void *context, bool *out_ok) {
  if (context == nullptr || out_ok == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  *out_ok = rtc_time_is_valid(rtc_context(context));
  return HAL_OK;
}

static hal_status_t
internal_get_clock_source(void *context, hal_rtc_clock_source_t *out_source) {
  if (context == nullptr || out_source == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  *out_source = rtc_context(context)->clock_source;
  return HAL_OK;
}

static hal_status_t internal_set_interrupt_enable(void *context,
                                                  uint8_t irq_mask) {
  if (context == nullptr || !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  if ((irq_mask & HAL_RTC_IRQ_TIMER) != 0u) {
    return HAL_EUNSUPPORTED;
  }
  if ((irq_mask & HAL_RTC_IRQ_WAKEUP) != 0u &&
      !rtc_context(context)->wakeup.armed) {
    return HAL_ESTATE;
  }

  const bool alarm_enable = (irq_mask & HAL_RTC_IRQ_ALARM) != 0u;
  if (!alarm_enable) {
    rtc_alarm_irq_disable();
  } else {
    rtc_write_enable();
    RTC_SCR = RTC_SCR_CALRAF;
    RTC_CR |= RTC_CR_ALRAIE;
    rtc_write_disable();
    EXTI_PR1 = JH_G474_RTC_ALARM_EXTI_LINE;
    EXTI_RTSR1 |= JH_G474_RTC_ALARM_EXTI_LINE;
    EXTI_IMR1 |= JH_G474_RTC_ALARM_EXTI_LINE;
    NVIC_ICPR(RTC_Alarm_IRQn / 32u) = 1u << (RTC_Alarm_IRQn % 32u);
    NVIC_IPR8(RTC_Alarm_IRQn) = JH_NVIC_PRIO_RTC;
    NVIC_ISER(RTC_Alarm_IRQn / 32u) = 1u << (RTC_Alarm_IRQn % 32u);
  }

  const bool wakeup_enable = (irq_mask & HAL_RTC_IRQ_WAKEUP) != 0u;
  if (!wakeup_enable) {
    rtc_wakeup_irq_disable();
  } else {
    rtc_write_enable();
    RTC_CR |= RTC_CR_WUTIE;
    rtc_write_disable();
    EXTI_PR1 = JH_G474_RTC_WAKEUP_EXTI_LINE;
    EXTI_RTSR1 |= JH_G474_RTC_WAKEUP_EXTI_LINE;
    EXTI_IMR1 |= JH_G474_RTC_WAKEUP_EXTI_LINE;
    NVIC_ICPR(RTC_WKUP_IRQn / 32u) = 1u << (RTC_WKUP_IRQn % 32u);
    NVIC_IPR8(RTC_WKUP_IRQn) = JH_NVIC_PRIO_RTC;
    NVIC_ISER(RTC_WKUP_IRQn / 32u) = 1u << (RTC_WKUP_IRQn % 32u);
  }
  return HAL_OK;
}

static hal_status_t internal_get_interrupt_enable(void *context,
                                                  uint8_t *out_irq_mask) {
  if (context == nullptr || out_irq_mask == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  uint8_t mask = 0u;
  if ((RTC_CR & RTC_CR_ALRAIE) != 0u) {
    mask |= HAL_RTC_IRQ_ALARM;
  }
  if ((RTC_CR & RTC_CR_WUTIE) != 0u) {
    mask |= HAL_RTC_IRQ_WAKEUP;
  }
  *out_irq_mask = mask;
  return HAL_OK;
}

static hal_status_t internal_get_and_clear_flags(void *context,
                                                 uint8_t *out_flags) {
  if (context == nullptr || out_flags == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }

  jh_stm32g474_rtc_context_t *internal = rtc_context(context);

  hal_critical_section_enter();
  const bool alarm = s_alarm_pending || (RTC_SR & RTC_SR_ALRAF) != 0u;
  const bool wakeup = s_wakeup_pending || (RTC_SR & RTC_SR_WUTF) != 0u;
  s_alarm_pending = false;
  s_wakeup_pending = false;
  internal->wakeup.pending = false;
  if (alarm) {
    RTC_SCR = RTC_SCR_CALRAF;
    EXTI_PR1 = JH_G474_RTC_ALARM_EXTI_LINE;
  }
  if (wakeup) {
    NVIC_ICER(RTC_WKUP_IRQn / 32u) = 1u << (RTC_WKUP_IRQn % 32u);
    EXTI_IMR1 &= ~JH_G474_RTC_WAKEUP_EXTI_LINE;
    rtc_write_enable();
    RTC_CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    RTC_SCR = RTC_SCR_CWUTF;
    rtc_write_disable();
    EXTI_PR1 = JH_G474_RTC_WAKEUP_EXTI_LINE;
    internal->wakeup.armed = false;
  }
  hal_critical_section_exit();
  *out_flags = (uint8_t)((alarm ? HAL_RTC_FLAG_ALARM : 0u) |
                         (wakeup ? HAL_RTC_FLAG_WAKEUP : 0u));
  return HAL_OK;
}

static hal_status_t internal_get_temperature(void *context,
                                             float *out_temperature_c) {
  if (context == nullptr || out_temperature_c == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  return HAL_EUNSUPPORTED;
}

static hal_status_t internal_set_clkout_mode(void *context,
                                             hal_rtc_clkout_mode_t mode) {
  if (context == nullptr || !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  if (mode != HAL_RTC_CLKOUT_DISABLED && mode != HAL_RTC_CLKOUT_1_HZ) {
    return HAL_EUNSUPPORTED;
  }

  rtc_write_enable();
  if (mode == HAL_RTC_CLKOUT_DISABLED) {
    RTC_CR &= ~(RTC_CR_COE | RTC_CR_COSEL);
  } else {
    RTC_CR |= RTC_CR_COSEL | RTC_CR_COE;
  }
  rtc_write_disable();
  return HAL_OK;
}

static hal_status_t internal_get_clkout_mode(void *context,
                                             hal_rtc_clkout_mode_t *out_mode) {
  if (context == nullptr || out_mode == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  if ((RTC_CR & RTC_CR_COE) == 0u) {
    *out_mode = HAL_RTC_CLKOUT_DISABLED;
    return HAL_OK;
  }
  if ((RTC_CR & RTC_CR_COSEL) == 0u) {
    return HAL_EUNSUPPORTED;
  }
  *out_mode = HAL_RTC_CLKOUT_1_HZ;
  return HAL_OK;
}

static hal_status_t internal_set_timer(void *context,
                                       hal_rtc_timer_clock_t timer_clock,
                                       uint8_t count) {
  if (context == nullptr || !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  (void)timer_clock;
  (void)count;
  return HAL_EUNSUPPORTED;
}

static hal_status_t internal_get_timer(void *context,
                                       hal_rtc_timer_clock_t *out_timer_clock,
                                       uint8_t *out_count) {
  if (context == nullptr || out_timer_clock == nullptr ||
      out_count == nullptr || !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  return HAL_EUNSUPPORTED;
}

static hal_status_t internal_set_alarm(void *context,
                                       const hal_rtc_alarm_t *alarm) {
  if (context == nullptr || alarm == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  if (alarm->day_enabled && alarm->weekday_enabled) {
    return HAL_EUNSUPPORTED;
  }

  uint32_t alarm_register = 0u;
  if (!jh_stm32g474_rtc_encode_alarm(alarm, &alarm_register)) {
    return HAL_EINVAL;
  }

  rtc_write_enable();
  RTC_CR &= ~RTC_CR_ALRAE;
  if (!rtc_wait_register_set(&RTC_ICSR, RTC_ICSR_ALRAWF)) {
    rtc_write_disable();
    return HAL_ETIMEOUT;
  }
  RTC_ALRMAR = alarm_register;
  RTC_ALRMASSR = 0u;
  RTC_SCR = RTC_SCR_CALRAF;
  if (jh_stm32g474_rtc_alarm_enabled(alarm)) {
    RTC_CR |= RTC_CR_ALRAE;
  }
  rtc_write_disable();
  return HAL_OK;
}

static hal_status_t internal_get_alarm(void *context,
                                       hal_rtc_alarm_t *out_alarm) {
  if (context == nullptr || out_alarm == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  return jh_stm32g474_rtc_decode_alarm(RTC_ALRMAR, out_alarm) ? HAL_OK
                                                              : HAL_EIO;
}

static hal_status_t internal_wakeup_arm(void *context, uint64_t timeout_us,
                                        uint32_t flags) {
  if (context == nullptr || !rtc_context(context)->initialized ||
      (flags & ~HAL_RTC_WAKEUP_LOW_POWER) != 0u) {
    return HAL_EINVAL;
  }

  uint16_t counter = 0u;
  uint64_t programmed_timeout_us = 0u;
  if (!jh_stm32g474_rtc_wakeup_compute(timeout_us, &counter,
                                       &programmed_timeout_us)) {
    return HAL_EOVERFLOW;
  }

  const hal_status_t disable_status = rtc_wakeup_disable_hardware();
  if (disable_status != HAL_OK) {
    return disable_status;
  }
  rtc_clear_pending_wakeup();

  jh_stm32g474_rtc_context_t *internal = rtc_context(context);
  internal->wakeup = {
      true,
      false,
      timeout_us,
      programmed_timeout_us,
      JH_G474_RTC_WAKEUP_RESOLUTION_US,
      flags,
  };

  rtc_write_enable();
  RTC_WUTR = counter;
  RTC_CR = (RTC_CR & ~RTC_CR_WUCKSEL_MASK) | RTC_CR_WUCKSEL_CK_SPRE_16BITS |
           RTC_CR_WUTIE | RTC_CR_WUTE;
  rtc_write_disable();

  EXTI_PR1 = JH_G474_RTC_WAKEUP_EXTI_LINE;
  EXTI_RTSR1 |= JH_G474_RTC_WAKEUP_EXTI_LINE;
  EXTI_IMR1 |= JH_G474_RTC_WAKEUP_EXTI_LINE;
  NVIC_ICPR(RTC_WKUP_IRQn / 32u) = 1u << (RTC_WKUP_IRQn % 32u);
  NVIC_IPR8(RTC_WKUP_IRQn) = JH_NVIC_PRIO_RTC;
  NVIC_ISER(RTC_WKUP_IRQn / 32u) = 1u << (RTC_WKUP_IRQn % 32u);
  return HAL_OK;
}

static hal_status_t internal_wakeup_cancel(void *context) {
  if (context == nullptr || !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  const hal_status_t status = rtc_wakeup_disable_hardware();
  rtc_context(context)->wakeup = {};
  rtc_clear_pending_wakeup();
  return status;
}

static hal_status_t
internal_wakeup_get_state(void *context, hal_rtc_wakeup_state_t *out_state) {
  if (context == nullptr || out_state == nullptr ||
      !rtc_context(context)->initialized) {
    return HAL_EINVAL;
  }
  hal_rtc_wakeup_state_t state = rtc_context(context)->wakeup;
  state.armed = state.armed && (RTC_CR & RTC_CR_WUTE) != 0u;
  state.pending =
      state.pending || s_wakeup_pending || (RTC_SR & RTC_SR_WUTF) != 0u;
  *out_state = state;
  return HAL_OK;
}

static const jh_rtc_provider_ops_t s_internal_provider = {
    JH_RTC_PROVIDER_BUS_INTERNAL,
    0u,
    false,
    internal_initialize,
    internal_deinitialize,
    internal_get_datetime,
    internal_set_datetime,
    internal_get_clock_integrity,
    internal_get_clock_source,
    internal_set_interrupt_enable,
    internal_get_interrupt_enable,
    internal_get_and_clear_flags,
    internal_get_temperature,
    internal_set_clkout_mode,
    internal_get_clkout_mode,
    internal_set_timer,
    internal_get_timer,
    internal_set_alarm,
    internal_get_alarm,
    internal_wakeup_arm,
    internal_wakeup_cancel,
    internal_wakeup_get_state,
};

extern "C" void RTC_Alarm_IRQHandler(void) {
  s_alarm_pending = true;
  RTC_SCR = RTC_SCR_CALRAF;
  EXTI_PR1 = JH_G474_RTC_ALARM_EXTI_LINE;
}

extern "C" void RTC_WKUP_IRQHandler(void) {
  s_wakeup_pending = true;
  if (s_active_context != nullptr) {
    s_active_context->wakeup.armed = false;
    s_active_context->wakeup.pending = true;
  }
  rtc_write_enable();
  RTC_CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
  RTC_SCR = RTC_SCR_CWUTF;
  rtc_write_disable();
  EXTI_PR1 = JH_G474_RTC_WAKEUP_EXTI_LINE;
}

#endif /* HAL_ENABLE_INTERNAL_RTC && JH_STM32G474_HW */

const jh_rtc_provider_ops_t *jh_rtc_provider_get_ops(hal_rtc_chip_t chip) {
#if defined(HAL_ENABLE_INTERNAL_RTC) && defined(JH_STM32G474_HW)
  if (chip == HAL_RTC_CHIP_INTERNAL) {
    return &s_internal_provider;
  }
#endif
  return jh_rtc_i2c_provider_get_ops(chip);
}

#endif /* HAL_ENABLE_RTC */
#endif /* HAL_TARGET_IS_STM32G474 */
