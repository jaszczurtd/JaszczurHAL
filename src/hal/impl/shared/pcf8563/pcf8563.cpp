/**
 * @file pcf8563.cpp
 * @brief Portable I2C RTC driver for NXP PCF8563.
 *
 * Implementation details based on NXP PCF8563 datasheet and algorithm
 * patterns from embedded real-time clock driver communities. Ported
 * to use JaszczurHAL I2C primitives for cross-platform compatibility
 * (RP2040, STM32G474, and host/mock targets).
 */

#include "../../../hal_config.h"
#if defined(HAL_ENABLE_RTC) && defined(HAL_ENABLE_PCF8563)

#include "pcf8563.h"

#include "../../../hal_i2c.h"

#include <string.h>

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

#define PCF8563_TIMER_REG_MASK       0x83
#define PCF8563_TIMER_REG_DISABLED   0x03
#define PCF8563_TIMER_REG_1_60_HZ    0x83
#define PCF8563_TIMER_REG_1_HZ       0x82
#define PCF8563_TIMER_REG_64_HZ      0x81
#define PCF8563_TIMER_REG_4096_HZ    0x80

static bool pcf8563_validate_datetime(const pcf8563_datetime_t *dt) {
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

static bool pcf8563_validate_alarm(const pcf8563_alarm_t *alarm) {
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

static uint8_t pcf8563_to_bcd(uint8_t value) {
    return (uint8_t)((((uint8_t)(value / 10u)) << 4) | (uint8_t)(value % 10u));
}

static uint8_t pcf8563_from_bcd(uint8_t value) {
    return (uint8_t)((uint8_t)(((value >> 4) & 0x0Fu) * 10u) + (value & 0x0Fu));
}

static bool pcf8563_write_regs(const pcf8563_t *dev,
                               uint8_t reg,
                               const uint8_t *data,
                               uint8_t count) {
    if (!dev || !data || count == 0u) {
        return false;
    }

    hal_i2c_begin_transmission_bus(dev->i2c_bus, dev->i2c_addr);

    if (hal_i2c_write_bus(dev->i2c_bus, reg) != 1u) {
        (void)hal_i2c_end_transmission_bus(dev->i2c_bus);
        return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (hal_i2c_write_bus(dev->i2c_bus, data[i]) != 1u) {
            (void)hal_i2c_end_transmission_bus(dev->i2c_bus);
            return false;
        }
    }

    return hal_i2c_end_transmission_bus(dev->i2c_bus) == 0u;
}

static bool pcf8563_read_regs(const pcf8563_t *dev,
                              uint8_t reg,
                              uint8_t *data,
                              uint8_t count) {
    if (!dev || !data || count == 0u) {
        return false;
    }

    hal_i2c_begin_transmission_bus(dev->i2c_bus, dev->i2c_addr);

    if (hal_i2c_write_bus(dev->i2c_bus, reg) != 1u) {
        (void)hal_i2c_end_transmission_bus(dev->i2c_bus);
        return false;
    }

    if (hal_i2c_end_transmission_bus(dev->i2c_bus) != 0u) {
        return false;
    }

    const uint8_t received = hal_i2c_request_from_bus(dev->i2c_bus,
                                                       dev->i2c_addr,
                                                       count);
    if (received != count) {
        return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (hal_i2c_available_bus(dev->i2c_bus) <= 0) {
            return false;
        }
        const int raw = hal_i2c_read_bus(dev->i2c_bus);
        if (raw < 0) {
            return false;
        }
        data[i] = (uint8_t)raw;
    }

    return true;
}

static bool pcf8563_encode_clkout_mode(pcf8563_clkout_mode_t mode, uint8_t *out_reg) {
    if (!out_reg) {
        return false;
    }

    switch (mode) {
        case PCF8563_CLKOUT_DISABLED:
            *out_reg = 0x00;
            return true;
        case PCF8563_CLKOUT_1_HZ:
            *out_reg = 0x83;
            return true;
        case PCF8563_CLKOUT_32_HZ:
            *out_reg = 0x82;
            return true;
        case PCF8563_CLKOUT_1024_HZ:
            *out_reg = 0x81;
            return true;
        case PCF8563_CLKOUT_32768_HZ:
            *out_reg = 0x80;
            return true;
        default:
            return false;
    }
}

static bool pcf8563_decode_clkout_mode(uint8_t reg, pcf8563_clkout_mode_t *out_mode) {
    if (!out_mode) {
        return false;
    }

    switch ((uint8_t)(reg & PCF8563_CLKOUT_MASK)) {
        case 0x00:
            *out_mode = PCF8563_CLKOUT_DISABLED;
            return true;
        case 0x83:
            *out_mode = PCF8563_CLKOUT_1_HZ;
            return true;
        case 0x82:
            *out_mode = PCF8563_CLKOUT_32_HZ;
            return true;
        case 0x81:
            *out_mode = PCF8563_CLKOUT_1024_HZ;
            return true;
        case 0x80:
            *out_mode = PCF8563_CLKOUT_32768_HZ;
            return true;
        default:
            return false;
    }
}

static bool pcf8563_encode_timer_clock(pcf8563_timer_clock_t timer_clock, uint8_t *out_reg) {
    if (!out_reg) {
        return false;
    }

    switch (timer_clock) {
        case PCF8563_TIMER_DISABLED:
            *out_reg = PCF8563_TIMER_REG_DISABLED;
            return true;
        case PCF8563_TIMER_1_60_HZ:
            *out_reg = PCF8563_TIMER_REG_1_60_HZ;
            return true;
        case PCF8563_TIMER_1_HZ:
            *out_reg = PCF8563_TIMER_REG_1_HZ;
            return true;
        case PCF8563_TIMER_64_HZ:
            *out_reg = PCF8563_TIMER_REG_64_HZ;
            return true;
        case PCF8563_TIMER_4096_HZ:
            *out_reg = PCF8563_TIMER_REG_4096_HZ;
            return true;
        default:
            return false;
    }
}

static bool pcf8563_decode_timer_clock(uint8_t reg, uint8_t *out_timer_clock) {
    if (!out_timer_clock) {
        return false;
    }

    const uint8_t mode = (uint8_t)(reg & PCF8563_TIMER_REG_MASK);
    if ((mode & 0x80u) == 0u) {
        *out_timer_clock = (uint8_t)PCF8563_TIMER_DISABLED;
        return true;
    }

    switch (mode) {
        case PCF8563_TIMER_REG_1_60_HZ:
            *out_timer_clock = (uint8_t)PCF8563_TIMER_1_60_HZ;
            return true;
        case PCF8563_TIMER_REG_1_HZ:
            *out_timer_clock = (uint8_t)PCF8563_TIMER_1_HZ;
            return true;
        case PCF8563_TIMER_REG_64_HZ:
            *out_timer_clock = (uint8_t)PCF8563_TIMER_64_HZ;
            return true;
        case PCF8563_TIMER_REG_4096_HZ:
            *out_timer_clock = (uint8_t)PCF8563_TIMER_4096_HZ;
            return true;
        default:
            return false;
    }
}

bool pcf8563_probe(const pcf8563_t *dev) {
    uint8_t probe = 0;
    return pcf8563_read_regs(dev, PCF8563_REG_CONTROL_STATUS_1, &probe, 1u);
}

bool pcf8563_get_datetime(const pcf8563_t *dev, pcf8563_datetime_t *out_dt) {
    if (!dev || !out_dt) {
        return false;
    }

    uint8_t buffer[7] = {0};
    if (!pcf8563_read_regs(dev, PCF8563_REG_SECONDS, buffer, (uint8_t)sizeof(buffer))) {
        return false;
    }

    out_dt->clock_integrity = (buffer[0] & PCF8563_SECONDS_VL_MASK) == 0u;
    out_dt->second = pcf8563_from_bcd((uint8_t)(buffer[0] & PCF8563_SECONDS_MASK));
    out_dt->minute = pcf8563_from_bcd((uint8_t)(buffer[1] & PCF8563_MINUTES_MASK));
    out_dt->hour = pcf8563_from_bcd((uint8_t)(buffer[2] & PCF8563_HOURS_MASK));
    out_dt->day = pcf8563_from_bcd((uint8_t)(buffer[3] & PCF8563_DAY_MASK));
    out_dt->weekday = (uint8_t)(buffer[4] & PCF8563_WEEKDAY_MASK);
    out_dt->month = pcf8563_from_bcd((uint8_t)(buffer[5] & PCF8563_MONTH_MASK));

    /* PCF8563 century bit (Months/century reg, bit 7): C=0 -> 20xx, C=1 -> 19xx
     * (NXP PCF8563 datasheet, Table 13). */
    if ((buffer[5] & PCF8563_MONTH_CENTURY) == 0u) {
        out_dt->year = (uint16_t)(2000u + pcf8563_from_bcd(buffer[6]));
    } else {
        out_dt->year = (uint16_t)(1900u + pcf8563_from_bcd(buffer[6]));
    }

    return pcf8563_validate_datetime(out_dt);
}

bool pcf8563_set_datetime(const pcf8563_t *dev, const pcf8563_datetime_t *dt) {
    if (!dev || !pcf8563_validate_datetime(dt)) {
        return false;
    }

    uint8_t buffer[7] = {0};

    buffer[0] = (uint8_t)(pcf8563_to_bcd(dt->second) & PCF8563_SECONDS_MASK);
    buffer[1] = (uint8_t)(pcf8563_to_bcd(dt->minute) & PCF8563_MINUTES_MASK);
    buffer[2] = (uint8_t)(pcf8563_to_bcd(dt->hour) & PCF8563_HOURS_MASK);
    buffer[3] = (uint8_t)(pcf8563_to_bcd(dt->day) & PCF8563_DAY_MASK);
    buffer[4] = (uint8_t)(pcf8563_to_bcd(dt->weekday) & PCF8563_WEEKDAY_MASK);
    buffer[5] = (uint8_t)(pcf8563_to_bcd(dt->month) & PCF8563_MONTH_MASK);

    /* C=0 -> 20xx, C=1 -> 19xx (datasheet Table 13): leave the century bit clear
     * for years >= 2000, set it for the 1900s. */
    if (dt->year >= 2000u) {
        buffer[6] = pcf8563_to_bcd((uint8_t)(dt->year - 2000u));
    } else {
        buffer[5] = (uint8_t)(buffer[5] | PCF8563_MONTH_CENTURY);
        buffer[6] = pcf8563_to_bcd((uint8_t)(dt->year - 1900u));
    }

    return pcf8563_write_regs(dev, PCF8563_REG_SECONDS, buffer, (uint8_t)sizeof(buffer));
}

bool pcf8563_get_clock_integrity(const pcf8563_t *dev, bool *out_ok) {
    if (!dev || !out_ok) {
        return false;
    }

    uint8_t seconds = 0;
    if (!pcf8563_read_regs(dev, PCF8563_REG_SECONDS, &seconds, 1u)) {
        return false;
    }

    *out_ok = (seconds & PCF8563_SECONDS_VL_MASK) == 0u;
    return true;
}

bool pcf8563_set_interrupt_enable(const pcf8563_t *dev,
                                  bool alarm_irq_enabled,
                                  bool timer_irq_enabled) {
    if (!dev) {
        return false;
    }

    uint8_t cs2 = 0;
    if (!pcf8563_read_regs(dev, PCF8563_REG_CONTROL_STATUS_2, &cs2, 1u)) {
        return false;
    }

    cs2 = (uint8_t)(cs2 & (uint8_t)~(PCF8563_CS2_AIE | PCF8563_CS2_TIE));
    if (alarm_irq_enabled) {
        cs2 = (uint8_t)(cs2 | PCF8563_CS2_AIE);
    }
    if (timer_irq_enabled) {
        cs2 = (uint8_t)(cs2 | PCF8563_CS2_TIE);
    }

    return pcf8563_write_regs(dev, PCF8563_REG_CONTROL_STATUS_2, &cs2, 1u);
}

bool pcf8563_get_interrupt_enable(const pcf8563_t *dev,
                                  bool *out_alarm_irq_enabled,
                                  bool *out_timer_irq_enabled) {
    if (!dev || !out_alarm_irq_enabled || !out_timer_irq_enabled) {
        return false;
    }

    uint8_t cs2 = 0;
    if (!pcf8563_read_regs(dev, PCF8563_REG_CONTROL_STATUS_2, &cs2, 1u)) {
        return false;
    }

    *out_alarm_irq_enabled = (cs2 & PCF8563_CS2_AIE) != 0u;
    *out_timer_irq_enabled = (cs2 & PCF8563_CS2_TIE) != 0u;
    return true;
}

bool pcf8563_get_and_clear_flags(const pcf8563_t *dev,
                                 bool *out_alarm_flag,
                                 bool *out_timer_flag) {
    if (!dev || !out_alarm_flag || !out_timer_flag) {
        return false;
    }

    uint8_t cs2 = 0;
    if (!pcf8563_read_regs(dev, PCF8563_REG_CONTROL_STATUS_2, &cs2, 1u)) {
        return false;
    }

    *out_alarm_flag = (cs2 & PCF8563_CS2_AF) != 0u;
    *out_timer_flag = (cs2 & PCF8563_CS2_TF) != 0u;

    const uint8_t cleared = (uint8_t)(cs2 & (PCF8563_CS2_TI_TP | PCF8563_CS2_AIE | PCF8563_CS2_TIE));
    return pcf8563_write_regs(dev, PCF8563_REG_CONTROL_STATUS_2, &cleared, 1u);
}

bool pcf8563_set_clkout_mode(const pcf8563_t *dev, pcf8563_clkout_mode_t mode) {
    if (!dev) {
        return false;
    }

    uint8_t reg = 0;
    if (!pcf8563_encode_clkout_mode(mode, &reg)) {
        return false;
    }

    return pcf8563_write_regs(dev, PCF8563_REG_CLKOUT_CONTROL, &reg, 1u);
}

bool pcf8563_get_clkout_mode(const pcf8563_t *dev, pcf8563_clkout_mode_t *out_mode) {
    if (!dev || !out_mode) {
        return false;
    }

    uint8_t reg = 0;
    if (!pcf8563_read_regs(dev, PCF8563_REG_CLKOUT_CONTROL, &reg, 1u)) {
        return false;
    }

    return pcf8563_decode_clkout_mode(reg, out_mode);
}

bool pcf8563_set_timer(const pcf8563_t *dev, pcf8563_timer_clock_t timer_clock, uint8_t count) {
    if (!dev) {
        return false;
    }

    uint8_t mode = 0;
    if (!pcf8563_encode_timer_clock(timer_clock, &mode)) {
        return false;
    }

    uint8_t buffer[2] = {mode, count};
    return pcf8563_write_regs(dev, PCF8563_REG_TIMER_CONTROL, buffer, (uint8_t)sizeof(buffer));
}

bool pcf8563_get_timer(const pcf8563_t *dev,
                       pcf8563_timer_clock_t *out_timer_clock,
                       uint8_t *out_count) {
    if (!dev || !out_timer_clock || !out_count) {
        return false;
    }

    uint8_t buffer[2] = {0};
    if (!pcf8563_read_regs(dev, PCF8563_REG_TIMER_CONTROL, buffer, (uint8_t)sizeof(buffer))) {
        return false;
    }
    uint8_t timer_clock_raw = 0u;
    if (!pcf8563_decode_timer_clock(buffer[0], &timer_clock_raw)) {
        return false;
    }

    memcpy(out_timer_clock, &timer_clock_raw, sizeof(timer_clock_raw));

    *out_count = buffer[1];
    return true;
}

bool pcf8563_set_alarm(const pcf8563_t *dev, const pcf8563_alarm_t *alarm) {
    if (!dev || !pcf8563_validate_alarm(alarm)) {
        return false;
    }

    uint8_t buffer[4] = {0};

    buffer[0] = alarm->minute_enabled
        ? (uint8_t)(pcf8563_to_bcd(alarm->minute) & PCF8563_MINUTES_MASK)
        : PCF8563_ALARM_DISABLE;
    buffer[1] = alarm->hour_enabled
        ? (uint8_t)(pcf8563_to_bcd(alarm->hour) & PCF8563_HOURS_MASK)
        : PCF8563_ALARM_DISABLE;
    buffer[2] = alarm->day_enabled
        ? (uint8_t)(pcf8563_to_bcd(alarm->day) & PCF8563_DAY_MASK)
        : PCF8563_ALARM_DISABLE;
    buffer[3] = alarm->weekday_enabled
        ? (uint8_t)(pcf8563_to_bcd(alarm->weekday) & PCF8563_WEEKDAY_MASK)
        : PCF8563_ALARM_DISABLE;

    return pcf8563_write_regs(dev, PCF8563_REG_ALARM_MINUTE, buffer, (uint8_t)sizeof(buffer));
}

bool pcf8563_get_alarm(const pcf8563_t *dev, pcf8563_alarm_t *out_alarm) {
    if (!dev || !out_alarm) {
        return false;
    }

    uint8_t buffer[4] = {0};
    if (!pcf8563_read_regs(dev, PCF8563_REG_ALARM_MINUTE, buffer, (uint8_t)sizeof(buffer))) {
        return false;
    }

    out_alarm->minute_enabled = (buffer[0] & PCF8563_ALARM_DISABLE) == 0u;
    out_alarm->hour_enabled = (buffer[1] & PCF8563_ALARM_DISABLE) == 0u;
    out_alarm->day_enabled = (buffer[2] & PCF8563_ALARM_DISABLE) == 0u;
    out_alarm->weekday_enabled = (buffer[3] & PCF8563_ALARM_DISABLE) == 0u;

    out_alarm->minute = out_alarm->minute_enabled
        ? pcf8563_from_bcd((uint8_t)(buffer[0] & PCF8563_MINUTES_MASK))
        : 0u;
    out_alarm->hour = out_alarm->hour_enabled
        ? pcf8563_from_bcd((uint8_t)(buffer[1] & PCF8563_HOURS_MASK))
        : 0u;
    out_alarm->day = out_alarm->day_enabled
        ? pcf8563_from_bcd((uint8_t)(buffer[2] & PCF8563_DAY_MASK))
        : 0u;
    out_alarm->weekday = out_alarm->weekday_enabled
        ? pcf8563_from_bcd((uint8_t)(buffer[3] & PCF8563_WEEKDAY_MASK))
        : 0u;

    return pcf8563_validate_alarm(out_alarm);
}

#endif /* HAL_ENABLE_RTC && HAL_ENABLE_PCF8563 */
