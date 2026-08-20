#pragma once

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_RTC

/**
 * @file hal_rtc.h
 * @brief Hardware abstraction for real-time clocks (RTC).
 *
 * The module is designed to support multiple RTC backends under one API.
 * Available backends are PCF8563 and DS3231 over I2C plus an optional
 * target-native internal RTC.
 *
 * Backend selection (compile-time, opt-in):
 *   HAL_ENABLE_PCF8563 - enable the PCF8563 backend (propagates
 *                        HAL_ENABLE_RTC + HAL_ENABLE_I2C).
 *   HAL_ENABLE_DS3231  - enable the DS3231 backend  (propagates
 *                        HAL_ENABLE_RTC + HAL_ENABLE_I2C).
 *   HAL_ENABLE_INTERNAL_RTC - enable the target-native RTC backend
 *                             (propagates HAL_ENABLE_RTC).
 *
 * Multiple backends may be enabled simultaneously; chip selection is then
 * made per-handle via @ref hal_rtc_config_t::chip. Enabling
 * HAL_ENABLE_RTC alone (without a backend) triggers a compile-time #error
 * from hal_config.h. Internal backends are available on STM32G474 and the
 * RP2040/RP2350 family; unsupported targets reject them at runtime.
 *
 * The target-independent facade owns handle allocation, validation, locking,
 * epoch conversion, and legacy wrappers. Internal providers implement only
 * chip I/O or deterministic mock storage.
 */

#include "hal/core/hal_status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of simultaneous RTC handles. */
#ifndef HAL_RTC_MAX_INSTANCES
#define HAL_RTC_MAX_INSTANCES 4
#endif

/** @brief Lowest calendar year accepted by RTC date-time APIs. */
#define HAL_RTC_MIN_YEAR 1900u
/** @brief Highest calendar year accepted by RTC date-time APIs. */
#define HAL_RTC_MAX_YEAR 2099u
/** @brief Default 7-bit I2C address for PCF8563. */
#ifndef HAL_RTC_PCF8563_DEFAULT_I2C_ADDR
#define HAL_RTC_PCF8563_DEFAULT_I2C_ADDR 0x51
#endif

/** @brief Default 7-bit I2C address for DS3231. */
#ifndef HAL_RTC_DS3231_DEFAULT_I2C_ADDR
#define HAL_RTC_DS3231_DEFAULT_I2C_ADDR 0x68
#endif

/** @brief Supported RTC backends. */
typedef enum {
  HAL_RTC_CHIP_PCF8563 = 0, /**< PCF8563 over I2C. */
  HAL_RTC_CHIP_DS3231,      /**< DS3231 over I2C.  */
  HAL_RTC_CHIP_INTERNAL,    /**< Target-native internal RTC. */
} hal_rtc_chip_t;

/** @brief RTC oscillator source selected by a backend. */
typedef enum {
  HAL_RTC_CLOCK_SOURCE_AUTO = 0,  /**< Choose a suitable internal source. */
  HAL_RTC_CLOCK_SOURCE_EXTERNAL,  /**< Oscillator owned by an external RTC. */
  HAL_RTC_CLOCK_SOURCE_LSE,       /**< Low-speed external crystal/clock. */
  HAL_RTC_CLOCK_SOURCE_LSI,       /**< Low-speed internal RC oscillator. */
  HAL_RTC_CLOCK_SOURCE_HSE_DIV32, /**< HSE divided by 32. */
  HAL_RTC_CLOCK_SOURCE_AON,       /**< Target always-on timer clock domain. */
} hal_rtc_clock_source_t;

/** @brief I2C bus parameters used by I2C RTC backends (PCF8563 / DS3231). */
typedef struct {
  uint8_t sda_pin;   /**< SDA GPIO pin. */
  uint8_t scl_pin;   /**< SCL GPIO pin. */
  uint32_t clock_hz; /**< I2C bus speed in Hz (for example 100000 or 400000). */
  uint8_t i2c_bus;   /**< I2C bus index: 0 = default, 1 = second. */
  uint8_t i2c_addr;  /**< 7-bit I2C address (0 = backend default address). */
} hal_rtc_i2c_cfg_t;

/** @brief Configuration used by a target-native internal RTC backend. */
typedef struct {
  /**
   * Requested oscillator. AUTO selects the target default while preserving
   * an already configured clock domain whenever the backend supports it.
   */
  hal_rtc_clock_source_t clock_source;
} hal_rtc_internal_cfg_t;

/**
 * @brief Runtime configuration for an RTC handle.
 *
 * Set @ref chip to select backend, then fill matching union member.
 */
typedef struct {
  hal_rtc_chip_t chip;
  union {
    hal_rtc_i2c_cfg_t
        i2c; /**< Used for HAL_RTC_CHIP_PCF8563 and HAL_RTC_CHIP_DS3231. */
    hal_rtc_internal_cfg_t internal; /**< Used for HAL_RTC_CHIP_INTERNAL. */
  } bus;
} hal_rtc_config_t;

/** @brief Opaque RTC handle. */
typedef struct hal_rtc_impl_s hal_rtc_impl_t;
typedef hal_rtc_impl_t *hal_rtc_t;

/**
 * @brief Date-time payload used by RTC APIs.
 *
 * Ranges:
 * - second:   0..59
 * - minute:   0..59
 * - hour:     0..23
 * - day:      1..days in the selected month
 * - weekday:  0..6
 * - month:    1..12
 * - year:     HAL_RTC_MIN_YEAR..HAL_RTC_MAX_YEAR
 *
 * @ref clock_integrity is filled by @ref hal_rtc_get_datetime and indicates
 * whether the backend reported valid oscillator/time integrity.
 */
typedef struct {
  uint8_t second;
  uint8_t minute;
  uint8_t hour;
  uint8_t day;
  uint8_t weekday;
  uint8_t month;
  uint16_t year;
  bool clock_integrity;
} hal_rtc_datetime_t;

/** @brief Generic RTC event flag: alarm condition occurred. */
#define HAL_RTC_FLAG_ALARM (1u << 0)
/** @brief Generic RTC event flag: timer condition occurred. */
#define HAL_RTC_FLAG_TIMER (1u << 1)
/** @brief Generic RTC event flag: relative wake-up condition occurred. */
#define HAL_RTC_FLAG_WAKEUP (1u << 2)

/** @brief Generic RTC interrupt-enable bit: alarm interrupt. */
#define HAL_RTC_IRQ_ALARM (1u << 0)
/** @brief Generic RTC interrupt-enable bit: timer interrupt. */
#define HAL_RTC_IRQ_TIMER (1u << 1)
/** @brief Generic RTC interrupt-enable bit: relative wake-up interrupt. */
#define HAL_RTC_IRQ_WAKEUP (1u << 2)

/** @brief Request backend wake routing suitable for deep low-power modes. */
#define HAL_RTC_WAKEUP_LOW_POWER (UINT32_C(1) << 0u)

/** @brief Current state of the target-native relative wake-up timer. */
typedef struct {
  bool armed;                    /**< A relative wake-up is currently armed. */
  bool pending;                  /**< Wake-up fired and has not been cleared. */
  uint64_t requested_timeout_us; /**< Timeout supplied by the caller. */
  uint64_t programmed_timeout_us; /**< Rounded hardware timeout. */
  uint64_t resolution_us;         /**< Active hardware timer resolution. */
  uint32_t flags;                 /**< Active HAL_RTC_WAKEUP_* flags. */
} hal_rtc_wakeup_state_t;

/** @brief Clock output mode. */
typedef enum {
  HAL_RTC_CLKOUT_DISABLED = 0,
  HAL_RTC_CLKOUT_1_HZ,
  HAL_RTC_CLKOUT_32_HZ,
  HAL_RTC_CLKOUT_1024_HZ,
  HAL_RTC_CLKOUT_32768_HZ,
} hal_rtc_clkout_mode_t;

/** @brief RTC timer clock source / divider. */
typedef enum {
  HAL_RTC_TIMER_DISABLED = 0,
  HAL_RTC_TIMER_1_60_HZ,
  HAL_RTC_TIMER_1_HZ,
  HAL_RTC_TIMER_64_HZ,
  HAL_RTC_TIMER_4096_HZ,
} hal_rtc_timer_clock_t;

/**
 * @brief RTC alarm descriptor.
 *
 * Each field can be enabled or disabled independently.
 * Disabled fields are treated as "don't care" match.
 */
typedef struct {
  bool minute_enabled;
  uint8_t minute; /**< 0..59 when enabled. */
  bool hour_enabled;
  uint8_t hour; /**< 0..23 when enabled. */
  bool day_enabled;
  uint8_t day; /**< 1..31 when enabled. */
  bool weekday_enabled;
  uint8_t weekday; /**< 0..6 when enabled. */
} hal_rtc_alarm_t;

/**
 * @brief Initialize an RTC backend and return an opaque handle.
 *
 * For I2C backends this configures the I2C bus and probes the device.
 *
 * @param cfg Backend configuration.
 * @return Handle on success, NULL on invalid config/probe failure/pool
 * exhaustion.
 */
hal_rtc_t hal_rtc_init(const hal_rtc_config_t *cfg);

/**
 * @brief Release an RTC handle back to the static pool.
 *
 * Passing NULL is safe.
 */
void hal_rtc_deinit(hal_rtc_t h);

/**
 * @brief Read current date/time from RTC.
 *
 * On success, @ref clock_integrity is updated in @p out_dt.
 *
 * @param h      Valid handle.
 * @param out_dt Output date-time struct.
 * @return true on successful read, false on communication/argument error.
 */
bool hal_rtc_get_datetime(hal_rtc_t h, hal_rtc_datetime_t *out_dt);

/**
 * @brief Write date/time to RTC.
 *
 * @param h  Valid handle.
 * @param dt Input date-time struct.
 * @return true on success, false on validation/communication/argument error.
 */
bool hal_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt);

/**
 * @brief Read current RTC date/time as Unix epoch seconds.
 *
 * Conversion is performed in UTC and supports years 1970..2099.
 *
 * @param h          Valid handle.
 * @param out_epoch  Output epoch value.
 * @return true on success, false on communication/argument/range error.
 */
bool hal_rtc_get_epoch(hal_rtc_t h, uint64_t *out_epoch);

/**
 * @brief Write RTC date/time from Unix epoch seconds.
 *
 * Conversion is performed in UTC and supports years 1970..2099.
 *
 * @param h      Valid handle.
 * @param epoch  Unix epoch seconds.
 * @return true on success, false on argument/range/communication error.
 */
bool hal_rtc_set_epoch(hal_rtc_t h, uint64_t epoch);

/**
 * @brief Read RTC clock-integrity status.
 *
 * For PCF8563 this maps to the VL (voltage-low) status bit in seconds register.
 * For DS3231 this maps to the OSF (oscillator-stop) status via
 * oscillatorCheck().
 *
 * @param h      Valid handle.
 * @param out_ok Output: true when integrity is OK.
 * @return true on successful read, false on communication/argument error.
 */
bool hal_rtc_get_clock_integrity(hal_rtc_t h, bool *out_ok);

/**
 * @brief Read the oscillator source used by the selected RTC backend.
 *
 * External I2C RTCs report HAL_RTC_CLOCK_SOURCE_EXTERNAL. Internal backends
 * report the source resolved during initialization.
 */
bool hal_rtc_get_clock_source(hal_rtc_t h, hal_rtc_clock_source_t *out_source);

/**
 * @brief Configure RTC interrupt enables.
 *
 * Use HAL_RTC_IRQ_ALARM, HAL_RTC_IRQ_TIMER, and HAL_RTC_IRQ_WAKEUP bits in
 * @p irq_mask. Unknown bits are ignored. A provider may reject a supported bit
 * that its hardware does not implement.
 */
bool hal_rtc_set_interrupt_enable(hal_rtc_t h, uint8_t irq_mask);

/**
 * @brief Read RTC interrupt-enable mask.
 *
 * Returns combination of HAL_RTC_IRQ_* bits.
 */
bool hal_rtc_get_interrupt_enable(hal_rtc_t h, uint8_t *out_irq_mask);

/**
 * @brief Read and clear RTC event flags.
 *
 * Returns combination of HAL_RTC_FLAG_* bits that were pending.
 */
bool hal_rtc_get_and_clear_flags(hal_rtc_t h, uint8_t *out_flags);

/**
 * @brief Read RTC chip temperature if the selected backend supports it.
 *
 * DS3231 exposes an internal temperature sensor. PCF8563 does not.
 */
bool hal_rtc_get_temperature(hal_rtc_t h, float *out_temperature_c);

/** @brief Set clock output mode. */
bool hal_rtc_set_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t mode);

/** @brief Read clock output mode. */
bool hal_rtc_get_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t *out_mode);

/**
 * @brief Configure RTC timer source and countdown value.
 *
 * @param h            Valid handle.
 * @param timer_clock  Timer source/divider.
 * @param count        Timer register value.
 */
bool hal_rtc_set_timer(hal_rtc_t h, hal_rtc_timer_clock_t timer_clock,
                       uint8_t count);

/**
 * @brief Read RTC timer source and countdown value.
 */
bool hal_rtc_get_timer(hal_rtc_t h, hal_rtc_timer_clock_t *out_timer_clock,
                       uint8_t *out_count);

/** @brief Configure RTC alarm match fields. */
bool hal_rtc_set_alarm(hal_rtc_t h, const hal_rtc_alarm_t *alarm);

/** @brief Read RTC alarm match fields. */
bool hal_rtc_get_alarm(hal_rtc_t h, hal_rtc_alarm_t *out_alarm);

/**
 * @brief Arm a one-shot relative wake-up on a target-native RTC.
 *
 * The backend rounds upward to its supported resolution and reports the
 * programmed value through @ref hal_rtc_wakeup_get_state_ex. External RTC
 * providers may return HAL_EUNSUPPORTED. HAL_RTC_WAKEUP_LOW_POWER requests
 * routing that remains active in a target deep-sleep state.
 */
hal_status_t hal_rtc_wakeup_arm_ex(hal_rtc_t h, uint64_t timeout_us,
                                   uint32_t flags);

/** @brief Cancel an armed relative wake-up and clear its pending event. */
hal_status_t hal_rtc_wakeup_cancel_ex(hal_rtc_t h);

/** @brief Read relative wake-up state without clearing its pending event. */
hal_status_t hal_rtc_wakeup_get_state_ex(hal_rtc_t h,
                                         hal_rtc_wakeup_state_t *out_state);

/* ---- Status-returning APIs ---------------------------------------------- */
/*
 * Status-returning RTC APIs are implemented by the shared facade. Legacy
 * handle/bool entry points remain compatibility wrappers. Infallible deinit
 * stays void and has no artificial status companion.
 */
hal_status_t hal_rtc_init_ex(const hal_rtc_config_t *cfg,
                             hal_rtc_t *out_handle);
hal_status_t hal_rtc_get_datetime_ex(hal_rtc_t h, hal_rtc_datetime_t *out_dt);
hal_status_t hal_rtc_set_datetime_ex(hal_rtc_t h, const hal_rtc_datetime_t *dt);
hal_status_t hal_rtc_get_epoch_ex(hal_rtc_t h, uint64_t *out_epoch);
hal_status_t hal_rtc_set_epoch_ex(hal_rtc_t h, uint64_t epoch);
hal_status_t hal_rtc_get_clock_integrity_ex(hal_rtc_t h, bool *out_ok);
hal_status_t hal_rtc_get_clock_source_ex(hal_rtc_t h,
                                         hal_rtc_clock_source_t *out_source);
hal_status_t hal_rtc_set_interrupt_enable_ex(hal_rtc_t h, uint8_t irq_mask);
hal_status_t hal_rtc_get_interrupt_enable_ex(hal_rtc_t h,
                                             uint8_t *out_irq_mask);
hal_status_t hal_rtc_get_and_clear_flags_ex(hal_rtc_t h, uint8_t *out_flags);
hal_status_t hal_rtc_get_temperature_ex(hal_rtc_t h, float *out_temperature_c);
hal_status_t hal_rtc_set_clkout_mode_ex(hal_rtc_t h,
                                        hal_rtc_clkout_mode_t mode);
hal_status_t hal_rtc_get_clkout_mode_ex(hal_rtc_t h,
                                        hal_rtc_clkout_mode_t *out_mode);
hal_status_t hal_rtc_set_timer_ex(hal_rtc_t h,
                                  hal_rtc_timer_clock_t timer_clock,
                                  uint8_t count);
hal_status_t hal_rtc_get_timer_ex(hal_rtc_t h,
                                  hal_rtc_timer_clock_t *out_timer_clock,
                                  uint8_t *out_count);
hal_status_t hal_rtc_set_alarm_ex(hal_rtc_t h, const hal_rtc_alarm_t *alarm);
hal_status_t hal_rtc_get_alarm_ex(hal_rtc_t h, hal_rtc_alarm_t *out_alarm);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_RTC */
