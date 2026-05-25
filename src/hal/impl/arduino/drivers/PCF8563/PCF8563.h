#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t i2c_bus;
    uint8_t i2c_addr;
} pcf8563_t;

typedef struct {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  weekday;
    uint8_t  month;
    uint16_t year;
    bool     clock_integrity;
} pcf8563_datetime_t;

typedef struct {
    bool    minute_enabled;
    uint8_t minute;
    bool    hour_enabled;
    uint8_t hour;
    bool    day_enabled;
    uint8_t day;
    bool    weekday_enabled;
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
#define PCF8563_TIMER_1_60_HZ  ((pcf8563_timer_clock_t)1u)
#define PCF8563_TIMER_1_HZ     ((pcf8563_timer_clock_t)2u)
#define PCF8563_TIMER_64_HZ    ((pcf8563_timer_clock_t)3u)
#define PCF8563_TIMER_4096_HZ  ((pcf8563_timer_clock_t)4u)

bool pcf8563_probe(const pcf8563_t *dev);

bool pcf8563_get_datetime(const pcf8563_t *dev, pcf8563_datetime_t *out_dt);
bool pcf8563_set_datetime(const pcf8563_t *dev, const pcf8563_datetime_t *dt);

bool pcf8563_get_clock_integrity(const pcf8563_t *dev, bool *out_ok);

bool pcf8563_set_interrupt_enable(const pcf8563_t *dev,
                                  bool alarm_irq_enabled,
                                  bool timer_irq_enabled);
bool pcf8563_get_interrupt_enable(const pcf8563_t *dev,
                                  bool *out_alarm_irq_enabled,
                                  bool *out_timer_irq_enabled);

bool pcf8563_get_and_clear_flags(const pcf8563_t *dev,
                                 bool *out_alarm_flag,
                                 bool *out_timer_flag);

bool pcf8563_set_clkout_mode(const pcf8563_t *dev, pcf8563_clkout_mode_t mode);
bool pcf8563_get_clkout_mode(const pcf8563_t *dev, pcf8563_clkout_mode_t *out_mode);

bool pcf8563_set_timer(const pcf8563_t *dev, pcf8563_timer_clock_t timer_clock, uint8_t count);
bool pcf8563_get_timer(const pcf8563_t *dev,
                       pcf8563_timer_clock_t *out_timer_clock,
                       uint8_t *out_count);

bool pcf8563_set_alarm(const pcf8563_t *dev, const pcf8563_alarm_t *alarm);
bool pcf8563_get_alarm(const pcf8563_t *dev, pcf8563_alarm_t *out_alarm);

#ifdef __cplusplus
}
#endif
