#include "../../hal_config.h"
#ifndef HAL_DISABLE_EEPROM

#include "../../hal_eeprom.h"
#include "../../hal_i2c.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"

#include <Arduino.h>
#include <EEPROM.h>

/* ── Internal state ───────────────────────────────────────────────────────────── */

static hal_eeprom_type_t s_type     = HAL_EEPROM_AT24C256;
static uint16_t          s_size     = 32768U;
static uint8_t           s_i2c_addr = EEPROM_I2C_ADDRESS;
static hal_mutex_t       s_eeprom_mutex = NULL;

static void eeprom_ensure_mutex(void) {
    if (!s_eeprom_mutex) {
        hal_critical_section_enter();
        if (!s_eeprom_mutex) {
            s_eeprom_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

/* ── AT24C256 helpers ───────────────────────────────────────────────────── */

static void at24_write_byte(uint16_t addr, uint8_t val) {
    hal_i2c_begin_transmission(s_i2c_addr);
    hal_i2c_write((uint8_t)(addr >> 8));
    hal_i2c_write((uint8_t)(addr & 0xFF));
    hal_i2c_write(val);
    hal_i2c_end_transmission();

    while (hal_i2c_is_busy(s_i2c_addr)) {
        hal_delay_us(100);
        hal_watchdog_feed();
    }
    hal_delay_ms(5);
}

static uint8_t at24_read_byte(uint16_t addr) {
    hal_i2c_begin_transmission(s_i2c_addr);
    hal_i2c_write((uint8_t)(addr >> 8));
    hal_i2c_write((uint8_t)(addr & 0xFF));
    hal_i2c_end_transmission();

    hal_i2c_request_from(s_i2c_addr, (uint8_t)1);
    if (hal_i2c_available()) {
        return (uint8_t)hal_i2c_read();
    }
    return 0;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void hal_eeprom_init(hal_eeprom_type_t type, uint16_t size, uint8_t i2c_addr) {
    eeprom_ensure_mutex();
    s_type = type;
    if (type == HAL_EEPROM_RP2040) {
        s_size = size;
        EEPROM.begin(size);
    } else {
        s_size     = 32768U;
        s_i2c_addr = (i2c_addr != 0) ? i2c_addr : EEPROM_I2C_ADDRESS;
    }
}

/* ── Lock-free helpers (caller holds s_eeprom_mutex) ────────────────── */

static void write_byte_nolock(uint16_t addr, uint8_t val) {
    if (s_type == HAL_EEPROM_RP2040) {
        EEPROM.write(addr, val);
    } else {
        at24_write_byte(addr, val);
    }
}

static uint8_t read_byte_nolock(uint16_t addr) {
    if (s_type == HAL_EEPROM_RP2040) {
        return EEPROM.read(addr);
    } else {
        return at24_read_byte(addr);
    }
}

void hal_eeprom_write_byte(uint16_t addr, uint8_t val) {
    eeprom_ensure_mutex();
    hal_mutex_lock(s_eeprom_mutex);
    write_byte_nolock(addr, val);
    hal_mutex_unlock(s_eeprom_mutex);
}

uint8_t hal_eeprom_read_byte(uint16_t addr) {
    eeprom_ensure_mutex();
    hal_mutex_lock(s_eeprom_mutex);
    uint8_t val = read_byte_nolock(addr);
    hal_mutex_unlock(s_eeprom_mutex);
    return val;
}

void hal_eeprom_write_int(uint16_t addr, int32_t val) {
    eeprom_ensure_mutex();
    hal_mutex_lock(s_eeprom_mutex);
    write_byte_nolock(addr + 0, (uint8_t)((val >>  0) & 0xFF));
    write_byte_nolock(addr + 1, (uint8_t)((val >>  8) & 0xFF));
    write_byte_nolock(addr + 2, (uint8_t)((val >> 16) & 0xFF));
    write_byte_nolock(addr + 3, (uint8_t)((val >> 24) & 0xFF));
    hal_mutex_unlock(s_eeprom_mutex);
}

int32_t hal_eeprom_read_int(uint16_t addr) {
    eeprom_ensure_mutex();
    hal_mutex_lock(s_eeprom_mutex);
    int32_t val = (int32_t)(
        ((uint32_t)read_byte_nolock(addr + 0))        |
        ((uint32_t)read_byte_nolock(addr + 1) <<  8)  |
        ((uint32_t)read_byte_nolock(addr + 2) << 16)  |
        ((uint32_t)read_byte_nolock(addr + 3) << 24)
    );
    hal_mutex_unlock(s_eeprom_mutex);
    return val;
}

void hal_eeprom_commit(void) {
    if (s_type == HAL_EEPROM_RP2040) {
        hal_mutex_lock(s_eeprom_mutex);
        EEPROM.commit();
        hal_mutex_unlock(s_eeprom_mutex);
    }
    /* AT24C256: no-op — writes are already committed to the chip */
}

void hal_eeprom_reset(void) {
    for (uint32_t a = 0; a < s_size; a++) {
        hal_eeprom_write_byte((uint16_t)a, 0);
        hal_watchdog_feed();
    }
    hal_eeprom_commit();
}

uint16_t hal_eeprom_size(void) {
    return s_size;
}


#endif /* HAL_DISABLE_EEPROM */
