#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file pcf8563.h
 * @brief Portable I2C RTC (Real-Time Clock) driver for NXP PCF8563.
 *
 * This driver is designed to work across all JaszczurHAL target platforms
 * (Arduino/RP2040, STM32G474, and host/mock) by using only HAL I2C primitives
 * and avoiding platform-specific dependencies.
 *
 * Original algorithm and register sequences are based on NXP PCF8563 datasheet
 * and implementation patterns from embedded RTC driver communities.
 */

typedef struct {
  uint8_t i2c_bus;
  uint8_t i2c_addr;
} pcf8563_t;

typedef struct {
  uint8_t second;
  uint8_t minute;
  uint8_t hour;
  uint8_t day;
  uint8_t weekday;
  uint8_t month;
  uint16_t year;
  bool clock_integrity;
} pcf8563_datetime_t;

typedef struct {
  bool minute_enabled;
  uint8_t minute;
  bool hour_enabled;
  uint8_t hour;
  bool day_enabled;
  uint8_t day;
  bool weekday_enabled;
  uint8_t weekday;
} pcf8563_alarm_t;

typedef enum {
  PCF8563_CLKOUT_DISABLED = 0,
  PCF8563_CLKOUT_1_HZ,
  PCF8563_CLKOUT_32_HZ,
  PCF8563_CLKOUT_1024_HZ,
  PCF8563_CLKOUT_32768_HZ,
} pcf8563_clkout_mode_t;

typedef uint8_t pcf8563_timer_clock_t;

#define PCF8563_TIMER_DISABLED ((pcf8563_timer_clock_t)0u)
#define PCF8563_TIMER_1_60_HZ ((pcf8563_timer_clock_t)1u)
#define PCF8563_TIMER_1_HZ ((pcf8563_timer_clock_t)2u)
#define PCF8563_TIMER_64_HZ ((pcf8563_timer_clock_t)3u)
#define PCF8563_TIMER_4096_HZ ((pcf8563_timer_clock_t)4u)

/**
 * @brief Probe for PCF8563 presence on the I2C bus.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @return true if device is accessible, false otherwise.
 */
bool pcf8563_probe(const pcf8563_t *dev);

/**
 * @brief Read current date/time from RTC.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param out_dt Pointer to output pcf8563_datetime_t structure.
 * @return true on success, false on I2C error or validation failure.
 */
bool pcf8563_get_datetime(const pcf8563_t *dev, pcf8563_datetime_t *out_dt);

/**
 * @brief Write date/time to RTC.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param dt Pointer to pcf8563_datetime_t structure to write.
 * @return true on success, false on validation or I2C error.
 */
bool pcf8563_set_datetime(const pcf8563_t *dev, const pcf8563_datetime_t *dt);

/**
 * @brief Read clock integrity flag (oscillator status).
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param out_ok Pointer to output bool (true if oscillator is running).
 * @return true on success, false on I2C error.
 */
bool pcf8563_get_clock_integrity(const pcf8563_t *dev, bool *out_ok);

/**
 * @brief Enable/disable alarm and timer interrupt sources.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param alarm_irq_enabled true to enable alarm interrupt.
 * @param timer_irq_enabled true to enable timer interrupt.
 * @return true on success, false on I2C error.
 */
bool pcf8563_set_interrupt_enable(const pcf8563_t *dev, bool alarm_irq_enabled,
                                  bool timer_irq_enabled);

/**
 * @brief Read current interrupt enable state.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param out_alarm_irq_enabled Pointer to output alarm IRQ enable state.
 * @param out_timer_irq_enabled Pointer to output timer IRQ enable state.
 * @return true on success, false on I2C error.
 */
bool pcf8563_get_interrupt_enable(const pcf8563_t *dev,
                                  bool *out_alarm_irq_enabled,
                                  bool *out_timer_irq_enabled);

/**
 * @brief Read and clear interrupt flags.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param out_alarm_flag Pointer to output alarm flag (true if alarm occurred).
 * @param out_timer_flag Pointer to output timer flag (true if timer expired).
 * @return true on success, false on I2C error.
 */
bool pcf8563_get_and_clear_flags(const pcf8563_t *dev, bool *out_alarm_flag,
                                 bool *out_timer_flag);

/**
 * @brief Set CLKOUT output frequency.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param mode Output clock frequency (see pcf8563_clkout_mode_t).
 * @return true on success, false on validation or I2C error.
 */
bool pcf8563_set_clkout_mode(const pcf8563_t *dev, pcf8563_clkout_mode_t mode);

/**
 * @brief Read current CLKOUT output frequency.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param out_mode Pointer to output mode (see pcf8563_clkout_mode_t).
 * @return true on success, false on I2C error.
 */
bool pcf8563_get_clkout_mode(const pcf8563_t *dev,
                             pcf8563_clkout_mode_t *out_mode);

/**
 * @brief Set countdown timer frequency and initial count.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param timer_clock Timer clock source divider.
 * @param count Initial count value (auto-decrements at timer_clock rate).
 * @return true on success, false on validation or I2C error.
 */
bool pcf8563_set_timer(const pcf8563_t *dev, pcf8563_timer_clock_t timer_clock,
                       uint8_t count);

/**
 * @brief Read countdown timer frequency and current count.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param out_timer_clock Pointer to output timer clock source.
 * @param out_count Pointer to output current count value.
 * @return true on success, false on I2C error.
 */
bool pcf8563_get_timer(const pcf8563_t *dev,
                       pcf8563_timer_clock_t *out_timer_clock,
                       uint8_t *out_count);

/**
 * @brief Set alarm (year/month supported via day-of-week match).
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param alarm Pointer to pcf8563_alarm_t alarm descriptor.
 * @return true on success, false on validation or I2C error.
 */
bool pcf8563_set_alarm(const pcf8563_t *dev, const pcf8563_alarm_t *alarm);

/**
 * @brief Read current alarm settings.
 * @param dev Pointer to pcf8563_t device descriptor.
 * @param out_alarm Pointer to output pcf8563_alarm_t alarm descriptor.
 * @return true on success, false on I2C error.
 */
bool pcf8563_get_alarm(const pcf8563_t *dev, pcf8563_alarm_t *out_alarm);

#ifdef __cplusplus
}
#endif
