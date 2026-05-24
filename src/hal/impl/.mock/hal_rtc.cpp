#include "../../hal_config.h"
#ifndef HAL_DISABLE_RTC

#include "../../hal_rtc.h"
#include "../../hal_i2c.h"
#include "../../hal_sync.h"
#include "../../hal_serial.h"
#include "hal_mock.h"

#include <string.h>

struct hal_rtc_impl_s {
    hal_rtc_chip_t     chip;
    bool               in_use;
    hal_mutex_t        mutex;
    hal_rtc_datetime_t dt;
    uint8_t            irq_mask;
    uint8_t            flags;
    hal_rtc_clkout_mode_t clkout_mode;
    hal_rtc_timer_clock_t timer_clock;
    uint8_t            timer_count;
    hal_rtc_alarm_t    alarm;
    uint8_t            i2c_bus;
    uint8_t            i2c_addr;
};

static hal_rtc_impl_t s_pool[HAL_RTC_MAX_INSTANCES];

static void rtc_release_pool_slot(hal_rtc_impl_t *h) {
    hal_critical_section_enter();
    h->in_use = false;
    hal_critical_section_exit();
}

static bool rtc_validate_datetime(const hal_rtc_datetime_t *dt) {
    if (!dt) {
        return false;
    }
    if (dt->second > 59u || dt->minute > 59u || dt->hour > 23u) {
        return false;
    }
    if (dt->day < 1u || dt->day > 31u) {
        return false;
    }
    if (dt->weekday > 6u) {
        return false;
    }
    if (dt->month < 1u || dt->month > 12u) {
        return false;
    }
    if (dt->year < 1900u || dt->year > 2099u) {
        return false;
    }
    return true;
}

static bool rtc_validate_alarm(const hal_rtc_alarm_t *alarm) {
    if (!alarm) {
        return false;
    }
    if (alarm->minute_enabled && alarm->minute > 59u) {
        return false;
    }
    if (alarm->hour_enabled && alarm->hour > 23u) {
        return false;
    }
    if (alarm->day_enabled && (alarm->day < 1u || alarm->day > 31u)) {
        return false;
    }
    if (alarm->weekday_enabled && alarm->weekday > 6u) {
        return false;
    }
    return true;
}

static bool rtc_validate_clkout_mode(hal_rtc_clkout_mode_t mode) {
    switch (mode) {
        case HAL_RTC_CLKOUT_DISABLED:
        case HAL_RTC_CLKOUT_1_HZ:
        case HAL_RTC_CLKOUT_32_HZ:
        case HAL_RTC_CLKOUT_1024_HZ:
        case HAL_RTC_CLKOUT_32768_HZ:
            return true;
        default:
            return false;
    }
}

static bool rtc_validate_timer_clock(hal_rtc_timer_clock_t timer_clock) {
    switch (timer_clock) {
        case HAL_RTC_TIMER_DISABLED:
        case HAL_RTC_TIMER_1_60_HZ:
        case HAL_RTC_TIMER_1_HZ:
        case HAL_RTC_TIMER_64_HZ:
        case HAL_RTC_TIMER_4096_HZ:
            return true;
        default:
            return false;
    }
}

hal_rtc_t hal_rtc_init(const hal_rtc_config_t *cfg) {
    if (!cfg) {
        return NULL;
    }

    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < HAL_RTC_MAX_INSTANCES; ++i) {
        if (!s_pool[i].in_use) {
            slot = i;
            s_pool[i].in_use = true;
            break;
        }
    }
    hal_critical_section_exit();

    if (slot < 0) {
        return NULL;
    }

    hal_rtc_impl_t *h = &s_pool[slot];
    memset(h, 0, sizeof(*h));
    h->in_use = true;
    h->chip = cfg->chip;
    h->mutex = hal_mutex_create();

    if (!h->mutex) {
        rtc_release_pool_slot(h);
        return NULL;
    }

    if (cfg->chip != HAL_RTC_CHIP_PCF8563) {
        hal_serial_println("hal_rtc_init: unsupported RTC backend");
        hal_mutex_destroy(h->mutex);
        h->mutex = NULL;
        rtc_release_pool_slot(h);
        return NULL;
    }

    const hal_rtc_i2c_cfg_t *ic = &cfg->bus.i2c;
    const uint8_t addr = (ic->i2c_addr != 0u)
        ? ic->i2c_addr
        : (uint8_t)HAL_RTC_PCF8563_DEFAULT_I2C_ADDR;

    h->i2c_bus = ic->i2c_bus;
    h->i2c_addr = addr;

    hal_i2c_init_bus(ic->i2c_bus, ic->sda_pin, ic->scl_pin, ic->clock_hz);

    h->dt.second = 0;
    h->dt.minute = 0;
    h->dt.hour = 0;
    h->dt.day = 1;
    h->dt.weekday = 0;
    h->dt.month = 1;
    h->dt.year = 2000;
    h->dt.clock_integrity = true;

    return h;
}

void hal_rtc_deinit(hal_rtc_t h) {
    if (!h) {
        return;
    }

    hal_mutex_lock(h->mutex);
    h->in_use = false;
    hal_mutex_t m = h->mutex;
    h->mutex = NULL;
    hal_mutex_unlock(m);
    hal_mutex_destroy(m);
}

bool hal_rtc_get_datetime(hal_rtc_t h, hal_rtc_datetime_t *out_dt) {
    if (!h || !out_dt) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    *out_dt = h->dt;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt) {
    if (!h || !rtc_validate_datetime(dt)) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    h->dt.second = dt->second;
    h->dt.minute = dt->minute;
    h->dt.hour = dt->hour;
    h->dt.day = dt->day;
    h->dt.weekday = dt->weekday;
    h->dt.month = dt->month;
    h->dt.year = dt->year;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_get_clock_integrity(hal_rtc_t h, bool *out_ok) {
    if (!h || !out_ok) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    *out_ok = h->dt.clock_integrity;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_set_interrupt_enable(hal_rtc_t h, uint8_t irq_mask) {
    if (!h) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    h->irq_mask = (uint8_t)(irq_mask & (HAL_RTC_IRQ_ALARM | HAL_RTC_IRQ_TIMER));
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_get_interrupt_enable(hal_rtc_t h, uint8_t *out_irq_mask) {
    if (!h || !out_irq_mask) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    *out_irq_mask = h->irq_mask;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_get_and_clear_flags(hal_rtc_t h, uint8_t *out_flags) {
    if (!h || !out_flags) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    *out_flags = h->flags;
    h->flags = 0;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_set_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t mode) {
    if (!h || !rtc_validate_clkout_mode(mode)) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    h->clkout_mode = mode;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_get_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t *out_mode) {
    if (!h || !out_mode) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    *out_mode = h->clkout_mode;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_set_timer(hal_rtc_t h, hal_rtc_timer_clock_t timer_clock, uint8_t count) {
    if (!h || !rtc_validate_timer_clock(timer_clock)) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    h->timer_clock = timer_clock;
    h->timer_count = count;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_get_timer(hal_rtc_t h, hal_rtc_timer_clock_t *out_timer_clock, uint8_t *out_count) {
    if (!h || !out_timer_clock || !out_count) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    *out_timer_clock = h->timer_clock;
    *out_count = h->timer_count;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_set_alarm(hal_rtc_t h, const hal_rtc_alarm_t *alarm) {
    if (!h || !rtc_validate_alarm(alarm)) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    h->alarm = *alarm;
    hal_mutex_unlock(h->mutex);
    return true;
}

bool hal_rtc_get_alarm(hal_rtc_t h, hal_rtc_alarm_t *out_alarm) {
    if (!h || !out_alarm) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    *out_alarm = h->alarm;
    hal_mutex_unlock(h->mutex);
    return true;
}

void hal_mock_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt) {
    if (!h || !rtc_validate_datetime(dt)) {
        return;
    }

    hal_mutex_lock(h->mutex);
    h->dt.second = dt->second;
    h->dt.minute = dt->minute;
    h->dt.hour = dt->hour;
    h->dt.day = dt->day;
    h->dt.weekday = dt->weekday;
    h->dt.month = dt->month;
    h->dt.year = dt->year;
    hal_mutex_unlock(h->mutex);
}

void hal_mock_rtc_set_clock_integrity(hal_rtc_t h, bool ok) {
    if (!h) {
        return;
    }

    hal_mutex_lock(h->mutex);
    h->dt.clock_integrity = ok;
    hal_mutex_unlock(h->mutex);
}

void hal_mock_rtc_set_flags(hal_rtc_t h, uint8_t flags) {
    if (!h) {
        return;
    }

    hal_mutex_lock(h->mutex);
    h->flags = (uint8_t)(flags & (HAL_RTC_FLAG_ALARM | HAL_RTC_FLAG_TIMER));
    hal_mutex_unlock(h->mutex);
}

#endif /* HAL_DISABLE_RTC */
