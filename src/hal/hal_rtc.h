#pragma once

#include "hal_config.h"
#ifndef HAL_DISABLE_RTC

/**
 * @file hal_rtc.h
 * @brief Hardware abstraction for real-time clocks (RTC).
 *
 * The module is designed to support multiple RTC backends under one API.
 * At the moment the only available backend is PCF8563 (I2C).
 *
 * Backend selection (compile-time):
 *   HAL_DISABLE_PCF8563 - exclude the PCF8563 backend.
 *
 * With the current single-backend implementation, disabling PCF8563 also
 * disables the RTC module (HAL_DISABLE_RTC) via hal_config.h propagation.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of simultaneous RTC handles. */
#ifndef HAL_RTC_MAX_INSTANCES
#define HAL_RTC_MAX_INSTANCES 4
#endif

/** @brief Default 7-bit I2C address for PCF8563. */
#ifndef HAL_RTC_PCF8563_DEFAULT_I2C_ADDR
#define HAL_RTC_PCF8563_DEFAULT_I2C_ADDR 0x51
#endif

/** @brief Supported RTC backends. */
typedef enum {
    HAL_RTC_CHIP_PCF8563 = 0, /**< PCF8563 over I2C. */
} hal_rtc_chip_t;

/** @brief I2C bus parameters used by PCF8563 backend. */
typedef struct {
    uint8_t  sda_pin;   /**< SDA GPIO pin. */
    uint8_t  scl_pin;   /**< SCL GPIO pin. */
    uint32_t clock_hz;  /**< I2C bus speed in Hz (for example 100000 or 400000). */
    uint8_t  i2c_bus;   /**< I2C bus index: 0 = Wire, 1 = Wire1. */
    uint8_t  i2c_addr;  /**< 7-bit I2C address (0 = use HAL_RTC_PCF8563_DEFAULT_I2C_ADDR). */
} hal_rtc_i2c_cfg_t;

/**
 * @brief Runtime configuration for an RTC handle.
 *
 * Set @ref chip to select backend, then fill matching union member.
 */
typedef struct {
    hal_rtc_chip_t chip;
    union {
        hal_rtc_i2c_cfg_t i2c; /**< Used for HAL_RTC_CHIP_PCF8563. */
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
 * - day:      1..31
 * - weekday:  0..6
 * - month:    1..12
 * - year:     1900..2099
 *
 * @ref clock_integrity is filled by @ref hal_rtc_get_datetime and indicates
 * whether the backend reported valid oscillator/time integrity.
 */
typedef struct {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  weekday;
    uint8_t  month;
    uint16_t year;
    bool     clock_integrity;
} hal_rtc_datetime_t;

/** @brief Generic RTC event flag: alarm condition occurred. */
#define HAL_RTC_FLAG_ALARM (1u << 0)
/** @brief Generic RTC event flag: timer condition occurred. */
#define HAL_RTC_FLAG_TIMER (1u << 1)

/** @brief Generic RTC interrupt-enable bit: alarm interrupt. */
#define HAL_RTC_IRQ_ALARM  (1u << 0)
/** @brief Generic RTC interrupt-enable bit: timer interrupt. */
#define HAL_RTC_IRQ_TIMER  (1u << 1)

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
    bool    minute_enabled;
    uint8_t minute;   /**< 0..59 when enabled. */
    bool    hour_enabled;
    uint8_t hour;     /**< 0..23 when enabled. */
    bool    day_enabled;
    uint8_t day;      /**< 1..31 when enabled. */
    bool    weekday_enabled;
    uint8_t weekday;  /**< 0..6 when enabled. */
} hal_rtc_alarm_t;

/**
 * @brief Initialize an RTC backend and return an opaque handle.
 *
 * For PCF8563 this configures the I2C bus and probes the device.
 *
 * @param cfg Backend configuration.
 * @return Handle on success, NULL on invalid config/probe failure/pool exhaustion.
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
 * @brief Read RTC clock-integrity status.
 *
 * For PCF8563 this maps to the VL (voltage-low) status bit in seconds register.
 *
 * @param h      Valid handle.
 * @param out_ok Output: true when integrity is OK.
 * @return true on successful read, false on communication/argument error.
 */
bool hal_rtc_get_clock_integrity(hal_rtc_t h, bool *out_ok);

/**
 * @brief Configure RTC interrupt enables.
 *
 * Use HAL_RTC_IRQ_ALARM and HAL_RTC_IRQ_TIMER bits in @p irq_mask.
 * Unknown bits are ignored.
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
bool hal_rtc_set_timer(hal_rtc_t h, hal_rtc_timer_clock_t timer_clock, uint8_t count);

/**
 * @brief Read RTC timer source and countdown value.
 */
bool hal_rtc_get_timer(hal_rtc_t h, hal_rtc_timer_clock_t *out_timer_clock, uint8_t *out_count);

/** @brief Configure RTC alarm match fields. */
bool hal_rtc_set_alarm(hal_rtc_t h, const hal_rtc_alarm_t *alarm);

/** @brief Read RTC alarm match fields. */
bool hal_rtc_get_alarm(hal_rtc_t h, hal_rtc_alarm_t *out_alarm);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISABLE_RTC */
