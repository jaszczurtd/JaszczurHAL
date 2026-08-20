#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_RTC

#include "hal/rtc/jh_rtc_provider.h"

#ifdef HAL_ENABLE_INTERNAL_RTC
#include "hal/system/hal_sync.h"
#include "hal/time/jh_calendar.h"

#include "pico/aon_timer.h"
#include "pico/error.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <time.h>

namespace {

struct RpRtcContext {
  bool initialized;
  bool clock_integrity;
  hal_rtc_wakeup_state_t wakeup;
};

static_assert(sizeof(RpRtcContext) <= JH_RTC_PROVIDER_STORAGE_SIZE,
              "RP RTC provider storage is too small");
static_assert(alignof(RpRtcContext) <= alignof(std::max_align_t),
              "RP RTC provider alignment is too strict");

void *s_internal_owner = nullptr;
volatile bool s_wakeup_pending = false;

RpRtcContext *rtc_context(void *context) {
  return static_cast<RpRtcContext *>(context);
}

bool context_valid(void *context) {
  return context != nullptr && rtc_context(context)->initialized;
}

bool claim_context(void *context) {
  hal_critical_section_enter();
  const bool available = s_internal_owner == nullptr;
  if (available) {
    s_internal_owner = context;
  }
  hal_critical_section_exit();
  return available;
}

void release_context(void *context) {
  hal_critical_section_enter();
  if (s_internal_owner == context) {
    s_internal_owner = nullptr;
  }
  hal_critical_section_exit();
}

void set_invalid_datetime(hal_rtc_datetime_t *datetime) {
  *datetime = {0u, 0u, 0u, 1u, 6u, 1u, 2000u, false};
}

void wakeup_handler(void) {
  s_wakeup_pending = true;
  if (s_internal_owner != nullptr) {
    RpRtcContext *internal = rtc_context(s_internal_owner);
    internal->wakeup.armed = false;
    internal->wakeup.pending = true;
  }
}

uint64_t timespec_to_us(const struct timespec &value) {
  return static_cast<uint64_t>(value.tv_sec) * UINT64_C(1000000) +
         static_cast<uint64_t>(value.tv_nsec) / UINT64_C(1000);
}

bool add_timeout_to_timespec(const struct timespec &now, uint64_t timeout_us,
                             struct timespec *out_deadline) {
  if (out_deadline == nullptr || now.tv_sec < 0 || now.tv_nsec < 0 ||
      now.tv_nsec >= 1000000000L) {
    return false;
  }

  const uint64_t seconds = timeout_us / UINT64_C(1000000);
  const uint64_t microseconds = timeout_us % UINT64_C(1000000);
  if (seconds > static_cast<uint64_t>(std::numeric_limits<time_t>::max()) ||
      static_cast<uint64_t>(now.tv_sec) >
          static_cast<uint64_t>(std::numeric_limits<time_t>::max()) - seconds) {
    return false;
  }

  struct timespec deadline = now;
  deadline.tv_sec += static_cast<time_t>(seconds);
  deadline.tv_nsec += static_cast<long>(microseconds * UINT64_C(1000));
  if (deadline.tv_nsec >= 1000000000L) {
    if (deadline.tv_sec == std::numeric_limits<time_t>::max()) {
      return false;
    }
    deadline.tv_nsec -= 1000000000L;
    ++deadline.tv_sec;
  }
  *out_deadline = deadline;
  return true;
}

uint64_t aon_resolution_us(void) {
  struct timespec resolution = {};
  aon_timer_get_resolution(&resolution);
  return timespec_to_us(resolution);
}

bool round_timeout_up(uint64_t timeout_us, uint64_t resolution_us,
                      uint64_t *out_timeout_us) {
  if (timeout_us == 0u || resolution_us == 0u || out_timeout_us == nullptr ||
      timeout_us > UINT64_MAX - (resolution_us - 1u)) {
    return false;
  }
  *out_timeout_us =
      ((timeout_us + resolution_us - 1u) / resolution_us) * resolution_us;
  return true;
}

#if HAL_TARGET_IS_RP2040
hal_status_t read_hardware_datetime(hal_rtc_datetime_t *out_datetime) {
  struct tm calendar = {};
  if (!aon_timer_get_time_calendar(&calendar)) {
    return HAL_EIO;
  }
  const int year = calendar.tm_year + 1900;
  if (year < static_cast<int>(HAL_RTC_MIN_YEAR) ||
      year > static_cast<int>(HAL_RTC_MAX_YEAR) || calendar.tm_mon < 0 ||
      calendar.tm_mon > 11 || calendar.tm_wday < 0 || calendar.tm_wday > 6) {
    return HAL_EOVERFLOW;
  }
  *out_datetime = {
      static_cast<uint8_t>(calendar.tm_sec),
      static_cast<uint8_t>(calendar.tm_min),
      static_cast<uint8_t>(calendar.tm_hour),
      static_cast<uint8_t>(calendar.tm_mday),
      static_cast<uint8_t>(calendar.tm_wday),
      static_cast<uint8_t>(calendar.tm_mon + 1),
      static_cast<uint16_t>(year),
      true,
  };
  const jh_calendar_datetime_t validated = {
      out_datetime->year,    out_datetime->month,  out_datetime->day,
      out_datetime->hour,    out_datetime->minute, out_datetime->second,
      out_datetime->weekday,
  };
  const hal_status_t status = jh_calendar_validate_datetime(
      &validated, HAL_RTC_MIN_YEAR, HAL_RTC_MAX_YEAR);
  return status == HAL_EINVAL ? HAL_EIO : status;
}

hal_status_t write_hardware_datetime(const hal_rtc_datetime_t *datetime) {
  struct tm calendar = {};
  calendar.tm_sec = datetime->second;
  calendar.tm_min = datetime->minute;
  calendar.tm_hour = datetime->hour;
  calendar.tm_mday = datetime->day;
  calendar.tm_wday = datetime->weekday;
  calendar.tm_mon = static_cast<int>(datetime->month) - 1;
  calendar.tm_year = static_cast<int>(datetime->year) - 1900;
  const bool written = aon_timer_is_running()
                           ? aon_timer_set_time_calendar(&calendar)
                           : aon_timer_start_calendar(&calendar);
  return written ? HAL_OK : HAL_EIO;
}
#else
hal_status_t read_hardware_datetime(hal_rtc_datetime_t *out_datetime) {
  struct timespec timestamp = {};
  if (!aon_timer_get_time(&timestamp) || timestamp.tv_sec < 0) {
    return HAL_EIO;
  }
  jh_calendar_datetime_t calendar = {};
  const hal_status_t status = jh_calendar_epoch_to_datetime(
      static_cast<uint64_t>(timestamp.tv_sec), HAL_RTC_MAX_YEAR, &calendar);
  if (status != HAL_OK) {
    return status;
  }
  *out_datetime = {
      calendar.second,  calendar.minute, calendar.hour, calendar.day,
      calendar.weekday, calendar.month,  calendar.year, true,
  };
  return HAL_OK;
}

hal_status_t write_hardware_datetime(const hal_rtc_datetime_t *datetime) {
  const jh_calendar_datetime_t calendar = {
      datetime->year,   datetime->month,  datetime->day,     datetime->hour,
      datetime->minute, datetime->second, datetime->weekday,
  };
  uint64_t epoch = 0u;
  const hal_status_t conversion =
      jh_calendar_datetime_to_epoch(&calendar, &epoch);
  if (conversion != HAL_OK) {
    return conversion;
  }
  struct timespec timestamp = {};
  timestamp.tv_sec = static_cast<time_t>(epoch);
  if (timestamp.tv_sec < 0 ||
      static_cast<uint64_t>(timestamp.tv_sec) != epoch) {
    return HAL_EOVERFLOW;
  }
  const bool written = aon_timer_is_running() ? aon_timer_set_time(&timestamp)
                                              : aon_timer_start(&timestamp);
  return written ? HAL_OK : HAL_EIO;
}
#endif

hal_status_t internal_initialize(void *context,
                                 const hal_rtc_config_t *config) {
  if (context == nullptr || config == nullptr ||
      config->chip != HAL_RTC_CHIP_INTERNAL) {
    return HAL_EINVAL;
  }
  const hal_rtc_clock_source_t requested = config->bus.internal.clock_source;
  if (requested != HAL_RTC_CLOCK_SOURCE_AUTO &&
      requested != HAL_RTC_CLOCK_SOURCE_AON) {
    return HAL_EUNSUPPORTED;
  }
  if (!claim_context(context)) {
    return HAL_EBUSY;
  }

  RpRtcContext *internal = rtc_context(context);
  std::memset(internal, 0, sizeof(*internal));
  internal->initialized = true;
  internal->clock_integrity = aon_timer_is_running();
  s_wakeup_pending = false;
  if (internal->clock_integrity) {
    hal_rtc_datetime_t probe = {};
    internal->clock_integrity = read_hardware_datetime(&probe) == HAL_OK;
  }
  return HAL_OK;
}

void internal_deinitialize(void *context) {
  if (context == nullptr) {
    return;
  }
  if (context_valid(context)) {
    aon_timer_disable_alarm();
  }
  s_wakeup_pending = false;
  release_context(context);
  std::memset(context, 0, sizeof(RpRtcContext));
}

hal_status_t internal_get_datetime(void *context,
                                   hal_rtc_datetime_t *out_datetime) {
  if (!context_valid(context) || out_datetime == nullptr) {
    return HAL_EINVAL;
  }
  RpRtcContext *internal = rtc_context(context);
  if (!internal->clock_integrity || !aon_timer_is_running()) {
    internal->clock_integrity = false;
    set_invalid_datetime(out_datetime);
    return HAL_OK;
  }
  const hal_status_t status = read_hardware_datetime(out_datetime);
  if (status != HAL_OK) {
    internal->clock_integrity = false;
  }
  return status;
}

hal_status_t internal_set_datetime(void *context,
                                   const hal_rtc_datetime_t *datetime) {
  if (!context_valid(context) || datetime == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t status = write_hardware_datetime(datetime);
  if (status == HAL_OK) {
    rtc_context(context)->clock_integrity = true;
  }
  return status;
}

hal_status_t internal_get_clock_integrity(void *context, bool *out_ok) {
  if (!context_valid(context) || out_ok == nullptr) {
    return HAL_EINVAL;
  }
  RpRtcContext *internal = rtc_context(context);
  internal->clock_integrity =
      internal->clock_integrity && aon_timer_is_running();
  *out_ok = internal->clock_integrity;
  return HAL_OK;
}

hal_status_t internal_get_clock_source(void *context,
                                       hal_rtc_clock_source_t *out_source) {
  if (!context_valid(context) || out_source == nullptr) {
    return HAL_EINVAL;
  }
  *out_source = HAL_RTC_CLOCK_SOURCE_AON;
  return HAL_OK;
}

hal_status_t internal_set_interrupt_enable(void *context, uint8_t irq_mask) {
  if (!context_valid(context)) {
    return HAL_EINVAL;
  }
  if ((irq_mask & ~HAL_RTC_IRQ_WAKEUP) != 0u) {
    return HAL_EUNSUPPORTED;
  }
  if ((irq_mask & HAL_RTC_IRQ_WAKEUP) != 0u &&
      !rtc_context(context)->wakeup.armed) {
    return HAL_ESTATE;
  }
  if (irq_mask == 0u) {
    aon_timer_disable_alarm();
    rtc_context(context)->wakeup.armed = false;
  }
  return HAL_OK;
}

hal_status_t internal_get_interrupt_enable(void *context,
                                           uint8_t *out_irq_mask) {
  if (!context_valid(context) || out_irq_mask == nullptr) {
    return HAL_EINVAL;
  }
  *out_irq_mask = rtc_context(context)->wakeup.armed ? HAL_RTC_IRQ_WAKEUP : 0u;
  return HAL_OK;
}

hal_status_t internal_get_and_clear_flags(void *context, uint8_t *out_flags) {
  if (!context_valid(context) || out_flags == nullptr) {
    return HAL_EINVAL;
  }
  const bool wakeup = s_wakeup_pending || rtc_context(context)->wakeup.pending;
  s_wakeup_pending = false;
  rtc_context(context)->wakeup.pending = false;
  *out_flags = wakeup ? HAL_RTC_FLAG_WAKEUP : 0u;
  return HAL_OK;
}

hal_status_t internal_get_temperature(void *context, float *out_temperature_c) {
  if (!context_valid(context) || out_temperature_c == nullptr) {
    return HAL_EINVAL;
  }
  return HAL_EUNSUPPORTED;
}

hal_status_t internal_set_clkout_mode(void *context,
                                      hal_rtc_clkout_mode_t mode) {
  if (!context_valid(context)) {
    return HAL_EINVAL;
  }
  return mode == HAL_RTC_CLKOUT_DISABLED ? HAL_OK : HAL_EUNSUPPORTED;
}

hal_status_t internal_get_clkout_mode(void *context,
                                      hal_rtc_clkout_mode_t *out_mode) {
  if (!context_valid(context) || out_mode == nullptr) {
    return HAL_EINVAL;
  }
  *out_mode = HAL_RTC_CLKOUT_DISABLED;
  return HAL_OK;
}

hal_status_t internal_set_timer(void *context,
                                hal_rtc_timer_clock_t timer_clock,
                                uint8_t count) {
  if (!context_valid(context)) {
    return HAL_EINVAL;
  }
  (void)timer_clock;
  (void)count;
  return HAL_EUNSUPPORTED;
}

hal_status_t internal_get_timer(void *context,
                                hal_rtc_timer_clock_t *out_timer_clock,
                                uint8_t *out_count) {
  if (!context_valid(context) || out_timer_clock == nullptr ||
      out_count == nullptr) {
    return HAL_EINVAL;
  }
  return HAL_EUNSUPPORTED;
}

hal_status_t internal_set_alarm(void *context, const hal_rtc_alarm_t *alarm) {
  if (!context_valid(context) || alarm == nullptr) {
    return HAL_EINVAL;
  }
  const bool enabled = alarm->minute_enabled || alarm->hour_enabled ||
                       alarm->day_enabled || alarm->weekday_enabled;
  return enabled ? HAL_EUNSUPPORTED : HAL_OK;
}

hal_status_t internal_get_alarm(void *context, hal_rtc_alarm_t *out_alarm) {
  if (!context_valid(context) || out_alarm == nullptr) {
    return HAL_EINVAL;
  }
  std::memset(out_alarm, 0, sizeof(*out_alarm));
  return HAL_OK;
}

hal_status_t internal_wakeup_arm(void *context, uint64_t timeout_us,
                                 uint32_t flags) {
  if (!context_valid(context) || timeout_us == 0u ||
      (flags & ~HAL_RTC_WAKEUP_LOW_POWER) != 0u) {
    return HAL_EINVAL;
  }
  if (!aon_timer_is_running()) {
    return HAL_ESTATE;
  }

  const uint64_t resolution_us = aon_resolution_us();
  uint64_t programmed_timeout_us = 0u;
  if (!round_timeout_up(timeout_us, resolution_us, &programmed_timeout_us)) {
    return HAL_EOVERFLOW;
  }

  struct timespec now = {};
  struct timespec deadline = {};
  if (!aon_timer_get_time(&now) ||
      !add_timeout_to_timespec(now, programmed_timeout_us, &deadline)) {
    return HAL_EOVERFLOW;
  }

  aon_timer_disable_alarm();
  s_wakeup_pending = false;
  const bool low_power = (flags & HAL_RTC_WAKEUP_LOW_POWER) != 0u;
  const aon_timer_alarm_handler_t previous =
      aon_timer_enable_alarm(&deadline, wakeup_handler, low_power);
  if (reinterpret_cast<intptr_t>(previous) == PICO_ERROR_INVALID_ARG) {
    return HAL_EOVERFLOW;
  }

  rtc_context(context)->wakeup = {
      true, false, timeout_us, programmed_timeout_us, resolution_us, flags,
  };
  return HAL_OK;
}

hal_status_t internal_wakeup_cancel(void *context) {
  if (!context_valid(context)) {
    return HAL_EINVAL;
  }
  aon_timer_disable_alarm();
  s_wakeup_pending = false;
  rtc_context(context)->wakeup = {};
  return HAL_OK;
}

hal_status_t internal_wakeup_get_state(void *context,
                                       hal_rtc_wakeup_state_t *out_state) {
  if (!context_valid(context) || out_state == nullptr) {
    return HAL_EINVAL;
  }
  hal_rtc_wakeup_state_t state = rtc_context(context)->wakeup;
  state.pending = state.pending || s_wakeup_pending;
  *out_state = state;
  return HAL_OK;
}

const jh_rtc_provider_ops_t s_internal_provider = {
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

} // namespace
#endif /* HAL_ENABLE_INTERNAL_RTC */

const jh_rtc_provider_ops_t *jh_rtc_provider_get_ops(hal_rtc_chip_t chip) {
#ifdef HAL_ENABLE_INTERNAL_RTC
  if (chip == HAL_RTC_CHIP_INTERNAL) {
    return &s_internal_provider;
  }
#endif
  return jh_rtc_i2c_provider_get_ops(chip);
}

#endif /* HAL_ENABLE_RTC */
#endif /* HAL_TARGET_IS_RP */
