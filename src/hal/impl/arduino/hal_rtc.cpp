#include "../../hal_config.h"
#ifndef HAL_DISABLE_RTC

#include "../../hal_rtc.h"
#include "../../hal_i2c.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"

#include <string.h>

/* PCF8563 register map used by this HAL module. */
#define PCF8563_REG_CONTROL_STATUS_1 0x00
#define PCF8563_REG_CONTROL_STATUS_2 0x01
#define PCF8563_REG_SECONDS          0x02
#define PCF8563_REG_ALARM_MINUTE     0x09
#define PCF8563_REG_CLKOUT_CONTROL   0x0D
#define PCF8563_REG_TIMER_CONTROL    0x0E

#define PCF8563_SECONDS_VL_MASK  0x80
#define PCF8563_SECONDS_MASK     0x7F
#define PCF8563_MINUTES_MASK     0x7F
#define PCF8563_HOURS_MASK       0x3F
#define PCF8563_DAY_MASK         0x3F
#define PCF8563_WEEKDAY_MASK     0x07
#define PCF8563_MONTH_MASK       0x1F
#define PCF8563_MONTH_CENTURY    0x80

#define PCF8563_ALARM_DISABLE    0x80

#define PCF8563_CS2_TIE          (1u << 0)
#define PCF8563_CS2_AIE          (1u << 1)
#define PCF8563_CS2_TF           (1u << 2)
#define PCF8563_CS2_AF           (1u << 3)
#define PCF8563_CS2_TI_TP        (1u << 4)

#define PCF8563_CLKOUT_MASK      0x83

#define PCF8563_TIMER_MASK       0x83
#define PCF8563_TIMER_DISABLED   0x03
#define PCF8563_TIMER_1_60_HZ    0x83
#define PCF8563_TIMER_1_HZ       0x82
#define PCF8563_TIMER_64_HZ      0x81
#define PCF8563_TIMER_4096_HZ    0x80

struct hal_rtc_impl_s {
    hal_rtc_chip_t chip;
    bool           in_use;
    hal_mutex_t    mutex;
    struct {
        uint8_t i2c_bus;
        uint8_t i2c_addr;
    } pcf;
};

static hal_rtc_impl_t s_pool[HAL_RTC_MAX_INSTANCES];

/* ── Helpers ─────────────────────────────────────────────────────────────── */

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

static uint8_t rtc_to_bcd(uint8_t value) {
    return (uint8_t)((((uint8_t)(value / 10u)) << 4) | (uint8_t)(value % 10u));
}

static uint8_t rtc_from_bcd(uint8_t value) {
    return (uint8_t)((uint8_t)(((value >> 4) & 0x0Fu) * 10u) + (value & 0x0Fu));
}

static bool pcf_write_regs(hal_rtc_impl_t *h,
                           uint8_t reg,
                           const uint8_t *data,
                           uint8_t count) {
    if (!h || !data || count == 0u) {
        return false;
    }

    hal_i2c_begin_transmission_bus(h->pcf.i2c_bus, h->pcf.i2c_addr);

    if (hal_i2c_write_bus(h->pcf.i2c_bus, reg) != 1u) {
        (void)hal_i2c_end_transmission_bus(h->pcf.i2c_bus);
        return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (hal_i2c_write_bus(h->pcf.i2c_bus, data[i]) != 1u) {
            (void)hal_i2c_end_transmission_bus(h->pcf.i2c_bus);
            return false;
        }
    }

    return hal_i2c_end_transmission_bus(h->pcf.i2c_bus) == 0u;
}

static bool pcf_read_regs(hal_rtc_impl_t *h,
                          uint8_t reg,
                          uint8_t *data,
                          uint8_t count) {
    if (!h || !data || count == 0u) {
        return false;
    }

    hal_i2c_begin_transmission_bus(h->pcf.i2c_bus, h->pcf.i2c_addr);

    if (hal_i2c_write_bus(h->pcf.i2c_bus, reg) != 1u) {
        (void)hal_i2c_end_transmission_bus(h->pcf.i2c_bus);
        return false;
    }

    if (hal_i2c_end_transmission_bus(h->pcf.i2c_bus) != 0u) {
        return false;
    }

    const uint8_t received = hal_i2c_request_from_bus(h->pcf.i2c_bus,
                                                       h->pcf.i2c_addr,
                                                       count);
    if (received != count) {
        return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (hal_i2c_available_bus(h->pcf.i2c_bus) <= 0) {
            return false;
        }
        const int raw = hal_i2c_read_bus(h->pcf.i2c_bus);
        if (raw < 0) {
            return false;
        }
        data[i] = (uint8_t)raw;
    }

    return true;
}

static bool pcf_get_datetime(hal_rtc_impl_t *h, hal_rtc_datetime_t *out_dt) {
    uint8_t buffer[7] = {0};
    if (!pcf_read_regs(h, PCF8563_REG_SECONDS, buffer, (uint8_t)sizeof(buffer))) {
        return false;
    }

    out_dt->clock_integrity = (buffer[0] & PCF8563_SECONDS_VL_MASK) == 0u;
    out_dt->second = rtc_from_bcd((uint8_t)(buffer[0] & PCF8563_SECONDS_MASK));
    out_dt->minute = rtc_from_bcd((uint8_t)(buffer[1] & PCF8563_MINUTES_MASK));
    out_dt->hour = rtc_from_bcd((uint8_t)(buffer[2] & PCF8563_HOURS_MASK));
    out_dt->day = rtc_from_bcd((uint8_t)(buffer[3] & PCF8563_DAY_MASK));
    out_dt->weekday = (uint8_t)(buffer[4] & PCF8563_WEEKDAY_MASK);
    out_dt->month = rtc_from_bcd((uint8_t)(buffer[5] & PCF8563_MONTH_MASK));

    if ((buffer[5] & PCF8563_MONTH_CENTURY) != 0u) {
        out_dt->year = (uint16_t)(2000u + rtc_from_bcd(buffer[6]));
    } else {
        out_dt->year = (uint16_t)(1900u + rtc_from_bcd(buffer[6]));
    }

    return rtc_validate_datetime(out_dt);
}

static bool pcf_set_datetime(hal_rtc_impl_t *h, const hal_rtc_datetime_t *dt) {
    if (!rtc_validate_datetime(dt)) {
        return false;
    }

    uint8_t buffer[7] = {0};

    buffer[0] = (uint8_t)(rtc_to_bcd(dt->second) & PCF8563_SECONDS_MASK);
    buffer[1] = (uint8_t)(rtc_to_bcd(dt->minute) & PCF8563_MINUTES_MASK);
    buffer[2] = (uint8_t)(rtc_to_bcd(dt->hour) & PCF8563_HOURS_MASK);
    buffer[3] = (uint8_t)(rtc_to_bcd(dt->day) & PCF8563_DAY_MASK);
    buffer[4] = (uint8_t)(rtc_to_bcd(dt->weekday) & PCF8563_WEEKDAY_MASK);
    buffer[5] = (uint8_t)(rtc_to_bcd(dt->month) & PCF8563_MONTH_MASK);

    if (dt->year >= 2000u) {
        buffer[5] = (uint8_t)(buffer[5] | PCF8563_MONTH_CENTURY);
        buffer[6] = rtc_to_bcd((uint8_t)(dt->year - 2000u));
    } else {
        buffer[6] = rtc_to_bcd((uint8_t)(dt->year - 1900u));
    }

    return pcf_write_regs(h, PCF8563_REG_SECONDS, buffer, (uint8_t)sizeof(buffer));
}

static bool pcf_get_clock_integrity(hal_rtc_impl_t *h, bool *out_ok) {
    uint8_t seconds = 0;
    if (!pcf_read_regs(h, PCF8563_REG_SECONDS, &seconds, 1u)) {
        return false;
    }
    *out_ok = (seconds & PCF8563_SECONDS_VL_MASK) == 0u;
    return true;
}

static bool pcf_set_interrupt_enable(hal_rtc_impl_t *h, uint8_t irq_mask) {
    uint8_t cs2 = 0;
    if (!pcf_read_regs(h, PCF8563_REG_CONTROL_STATUS_2, &cs2, 1u)) {
        return false;
    }

    cs2 = (uint8_t)(cs2 & (uint8_t)~(PCF8563_CS2_AIE | PCF8563_CS2_TIE));
    if ((irq_mask & HAL_RTC_IRQ_ALARM) != 0u) {
        cs2 = (uint8_t)(cs2 | PCF8563_CS2_AIE);
    }
    if ((irq_mask & HAL_RTC_IRQ_TIMER) != 0u) {
        cs2 = (uint8_t)(cs2 | PCF8563_CS2_TIE);
    }

    return pcf_write_regs(h, PCF8563_REG_CONTROL_STATUS_2, &cs2, 1u);
}

static bool pcf_get_interrupt_enable(hal_rtc_impl_t *h, uint8_t *out_irq_mask) {
    uint8_t cs2 = 0;
    if (!pcf_read_regs(h, PCF8563_REG_CONTROL_STATUS_2, &cs2, 1u)) {
        return false;
    }

    uint8_t irq = 0;
    if ((cs2 & PCF8563_CS2_AIE) != 0u) {
        irq = (uint8_t)(irq | HAL_RTC_IRQ_ALARM);
    }
    if ((cs2 & PCF8563_CS2_TIE) != 0u) {
        irq = (uint8_t)(irq | HAL_RTC_IRQ_TIMER);
    }

    *out_irq_mask = irq;
    return true;
}

static bool pcf_get_and_clear_flags(hal_rtc_impl_t *h, uint8_t *out_flags) {
    uint8_t cs2 = 0;
    if (!pcf_read_regs(h, PCF8563_REG_CONTROL_STATUS_2, &cs2, 1u)) {
        return false;
    }

    uint8_t flags = 0;
    if ((cs2 & PCF8563_CS2_AF) != 0u) {
        flags = (uint8_t)(flags | HAL_RTC_FLAG_ALARM);
    }
    if ((cs2 & PCF8563_CS2_TF) != 0u) {
        flags = (uint8_t)(flags | HAL_RTC_FLAG_TIMER);
    }

    const uint8_t cleared = (uint8_t)(cs2 & (PCF8563_CS2_TI_TP | PCF8563_CS2_AIE | PCF8563_CS2_TIE));
    if (!pcf_write_regs(h, PCF8563_REG_CONTROL_STATUS_2, &cleared, 1u)) {
        return false;
    }

    *out_flags = flags;
    return true;
}

static bool pcf_encode_clkout_mode(hal_rtc_clkout_mode_t mode, uint8_t *out_reg) {
    switch (mode) {
        case HAL_RTC_CLKOUT_DISABLED:
            *out_reg = 0x00;
            return true;
        case HAL_RTC_CLKOUT_1_HZ:
            *out_reg = 0x83;
            return true;
        case HAL_RTC_CLKOUT_32_HZ:
            *out_reg = 0x82;
            return true;
        case HAL_RTC_CLKOUT_1024_HZ:
            *out_reg = 0x81;
            return true;
        case HAL_RTC_CLKOUT_32768_HZ:
            *out_reg = 0x80;
            return true;
        default:
            return false;
    }
}

static bool pcf_decode_clkout_mode(uint8_t reg, hal_rtc_clkout_mode_t *out_mode) {
    switch ((uint8_t)(reg & PCF8563_CLKOUT_MASK)) {
        case 0x00:
            *out_mode = HAL_RTC_CLKOUT_DISABLED;
            return true;
        case 0x83:
            *out_mode = HAL_RTC_CLKOUT_1_HZ;
            return true;
        case 0x82:
            *out_mode = HAL_RTC_CLKOUT_32_HZ;
            return true;
        case 0x81:
            *out_mode = HAL_RTC_CLKOUT_1024_HZ;
            return true;
        case 0x80:
            *out_mode = HAL_RTC_CLKOUT_32768_HZ;
            return true;
        default:
            return false;
    }
}

static bool pcf_set_clkout_mode(hal_rtc_impl_t *h, hal_rtc_clkout_mode_t mode) {
    uint8_t reg = 0;
    if (!pcf_encode_clkout_mode(mode, &reg)) {
        return false;
    }
    return pcf_write_regs(h, PCF8563_REG_CLKOUT_CONTROL, &reg, 1u);
}

static bool pcf_get_clkout_mode(hal_rtc_impl_t *h, hal_rtc_clkout_mode_t *out_mode) {
    uint8_t reg = 0;
    if (!pcf_read_regs(h, PCF8563_REG_CLKOUT_CONTROL, &reg, 1u)) {
        return false;
    }
    return pcf_decode_clkout_mode(reg, out_mode);
}

static bool pcf_encode_timer_clock(hal_rtc_timer_clock_t timer_clock, uint8_t *out_reg) {
    switch (timer_clock) {
        case HAL_RTC_TIMER_DISABLED:
            *out_reg = PCF8563_TIMER_DISABLED;
            return true;
        case HAL_RTC_TIMER_1_60_HZ:
            *out_reg = PCF8563_TIMER_1_60_HZ;
            return true;
        case HAL_RTC_TIMER_1_HZ:
            *out_reg = PCF8563_TIMER_1_HZ;
            return true;
        case HAL_RTC_TIMER_64_HZ:
            *out_reg = PCF8563_TIMER_64_HZ;
            return true;
        case HAL_RTC_TIMER_4096_HZ:
            *out_reg = PCF8563_TIMER_4096_HZ;
            return true;
        default:
            return false;
    }
}

static bool pcf_decode_timer_clock(uint8_t reg, hal_rtc_timer_clock_t *out_timer_clock) {
    const uint8_t mode = (uint8_t)(reg & PCF8563_TIMER_MASK);
    if ((mode & 0x80u) == 0u) {
        *out_timer_clock = HAL_RTC_TIMER_DISABLED;
        return true;
    }

    switch (mode) {
        case PCF8563_TIMER_1_60_HZ:
            *out_timer_clock = HAL_RTC_TIMER_1_60_HZ;
            return true;
        case PCF8563_TIMER_1_HZ:
            *out_timer_clock = HAL_RTC_TIMER_1_HZ;
            return true;
        case PCF8563_TIMER_64_HZ:
            *out_timer_clock = HAL_RTC_TIMER_64_HZ;
            return true;
        case PCF8563_TIMER_4096_HZ:
            *out_timer_clock = HAL_RTC_TIMER_4096_HZ;
            return true;
        default:
            return false;
    }
}

static bool pcf_set_timer(hal_rtc_impl_t *h, hal_rtc_timer_clock_t timer_clock, uint8_t count) {
    uint8_t mode = 0;
    if (!pcf_encode_timer_clock(timer_clock, &mode)) {
        return false;
    }

    uint8_t buffer[2] = {mode, count};
    return pcf_write_regs(h, PCF8563_REG_TIMER_CONTROL, buffer, (uint8_t)sizeof(buffer));
}

static bool pcf_get_timer(hal_rtc_impl_t *h, hal_rtc_timer_clock_t *out_timer_clock, uint8_t *out_count) {
    uint8_t buffer[2] = {0};
    if (!pcf_read_regs(h, PCF8563_REG_TIMER_CONTROL, buffer, (uint8_t)sizeof(buffer))) {
        return false;
    }
    if (!pcf_decode_timer_clock(buffer[0], out_timer_clock)) {
        return false;
    }
    *out_count = buffer[1];
    return true;
}

static bool pcf_set_alarm(hal_rtc_impl_t *h, const hal_rtc_alarm_t *alarm) {
    if (!rtc_validate_alarm(alarm)) {
        return false;
    }

    uint8_t buffer[4] = {0};

    buffer[0] = alarm->minute_enabled
        ? (uint8_t)(rtc_to_bcd(alarm->minute) & PCF8563_MINUTES_MASK)
        : PCF8563_ALARM_DISABLE;
    buffer[1] = alarm->hour_enabled
        ? (uint8_t)(rtc_to_bcd(alarm->hour) & PCF8563_HOURS_MASK)
        : PCF8563_ALARM_DISABLE;
    buffer[2] = alarm->day_enabled
        ? (uint8_t)(rtc_to_bcd(alarm->day) & PCF8563_DAY_MASK)
        : PCF8563_ALARM_DISABLE;
    buffer[3] = alarm->weekday_enabled
        ? (uint8_t)(rtc_to_bcd(alarm->weekday) & PCF8563_WEEKDAY_MASK)
        : PCF8563_ALARM_DISABLE;

    return pcf_write_regs(h, PCF8563_REG_ALARM_MINUTE, buffer, (uint8_t)sizeof(buffer));
}

static bool pcf_get_alarm(hal_rtc_impl_t *h, hal_rtc_alarm_t *out_alarm) {
    uint8_t buffer[4] = {0};
    if (!pcf_read_regs(h, PCF8563_REG_ALARM_MINUTE, buffer, (uint8_t)sizeof(buffer))) {
        return false;
    }

    out_alarm->minute_enabled = (buffer[0] & PCF8563_ALARM_DISABLE) == 0u;
    out_alarm->hour_enabled = (buffer[1] & PCF8563_ALARM_DISABLE) == 0u;
    out_alarm->day_enabled = (buffer[2] & PCF8563_ALARM_DISABLE) == 0u;
    out_alarm->weekday_enabled = (buffer[3] & PCF8563_ALARM_DISABLE) == 0u;

    out_alarm->minute = out_alarm->minute_enabled
        ? rtc_from_bcd((uint8_t)(buffer[0] & PCF8563_MINUTES_MASK))
        : 0u;
    out_alarm->hour = out_alarm->hour_enabled
        ? rtc_from_bcd((uint8_t)(buffer[1] & PCF8563_HOURS_MASK))
        : 0u;
    out_alarm->day = out_alarm->day_enabled
        ? rtc_from_bcd((uint8_t)(buffer[2] & PCF8563_DAY_MASK))
        : 0u;
    out_alarm->weekday = out_alarm->weekday_enabled
        ? rtc_from_bcd((uint8_t)(buffer[3] & PCF8563_WEEKDAY_MASK))
        : 0u;

    return rtc_validate_alarm(out_alarm);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

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

    HAL_ASSERT(slot >= 0, "hal_rtc: pool exhausted - increase HAL_RTC_MAX_INSTANCES");
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
        hal_derr("hal_rtc_init: unsupported RTC backend %d", (int)cfg->chip);
        hal_mutex_destroy(h->mutex);
        h->mutex = NULL;
        rtc_release_pool_slot(h);
        return NULL;
    }

    const hal_rtc_i2c_cfg_t *ic = &cfg->bus.i2c;
    const uint8_t addr = (ic->i2c_addr != 0u)
        ? ic->i2c_addr
        : (uint8_t)HAL_RTC_PCF8563_DEFAULT_I2C_ADDR;

    if (addr > 0x7Fu || ic->clock_hz == 0u) {
        hal_derr("hal_rtc_init: invalid I2C config (addr=0x%02X, clock=%lu)",
                 (unsigned)addr,
                 (unsigned long)ic->clock_hz);
        hal_mutex_destroy(h->mutex);
        h->mutex = NULL;
        rtc_release_pool_slot(h);
        return NULL;
    }

    h->pcf.i2c_bus = ic->i2c_bus;
    h->pcf.i2c_addr = addr;

    hal_i2c_init_bus(ic->i2c_bus, ic->sda_pin, ic->scl_pin, ic->clock_hz);

    uint8_t probe = 0;
    if (!pcf_read_regs(h, PCF8563_REG_CONTROL_STATUS_1, &probe, 1u)) {
        hal_derr("hal_rtc_init: PCF8563 probe failed (bus=%u addr=0x%02X)",
                 (unsigned)h->pcf.i2c_bus,
                 (unsigned)h->pcf.i2c_addr);
        hal_mutex_destroy(h->mutex);
        h->mutex = NULL;
        rtc_release_pool_slot(h);
        return NULL;
    }
    (void)probe;

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
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_get_datetime(h, out_dt);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt) {
    if (!h || !dt) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_set_datetime(h, dt);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_get_clock_integrity(hal_rtc_t h, bool *out_ok) {
    if (!h || !out_ok) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_get_clock_integrity(h, out_ok);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_set_interrupt_enable(hal_rtc_t h, uint8_t irq_mask) {
    if (!h) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_set_interrupt_enable(h, irq_mask);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_get_interrupt_enable(hal_rtc_t h, uint8_t *out_irq_mask) {
    if (!h || !out_irq_mask) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_get_interrupt_enable(h, out_irq_mask);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_get_and_clear_flags(hal_rtc_t h, uint8_t *out_flags) {
    if (!h || !out_flags) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_get_and_clear_flags(h, out_flags);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_set_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t mode) {
    if (!h) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_set_clkout_mode(h, mode);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_get_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t *out_mode) {
    if (!h || !out_mode) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_get_clkout_mode(h, out_mode);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_set_timer(hal_rtc_t h, hal_rtc_timer_clock_t timer_clock, uint8_t count) {
    if (!h) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_set_timer(h, timer_clock, count);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_get_timer(hal_rtc_t h, hal_rtc_timer_clock_t *out_timer_clock, uint8_t *out_count) {
    if (!h || !out_timer_clock || !out_count) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_get_timer(h, out_timer_clock, out_count);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_set_alarm(hal_rtc_t h, const hal_rtc_alarm_t *alarm) {
    if (!h || !alarm) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_set_alarm(h, alarm);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_rtc_get_alarm(hal_rtc_t h, hal_rtc_alarm_t *out_alarm) {
    if (!h || !out_alarm) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    bool ok = false;

    switch (h->chip) {
        case HAL_RTC_CHIP_PCF8563:
            ok = pcf_get_alarm(h, out_alarm);
            break;
        default:
            ok = false;
            break;
    }

    hal_mutex_unlock(h->mutex);
    return ok;
}

#endif /* HAL_DISABLE_RTC */
