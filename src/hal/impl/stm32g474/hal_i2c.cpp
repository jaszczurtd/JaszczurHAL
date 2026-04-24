#if !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32)

#include "../../hal_config.h"
#ifndef HAL_DISABLE_I2C

#include "../../hal_i2c.h"
#include "../../hal_sync.h"

#include <string.h>

#define STM32_I2C_BUF_SIZE 255

typedef struct {
    uint8_t rx_buf[STM32_I2C_BUF_SIZE];
    int rx_len;
    int rx_pos;
    uint8_t cur_addr;
    bool busy;
    bool initialized;
    uint32_t transaction_count;
    uint32_t bus_clear_count;
    hal_mutex_t mutex;
} i2c_bus_state_t;

static i2c_bus_state_t s_i2c[2] = {};

static inline uint8_t i2c_bus_index(uint8_t bus) {
    return bus == 1u ? 1u : 0u;
}

static inline i2c_bus_state_t *i2c_state(uint8_t bus) {
    return &s_i2c[i2c_bus_index(bus)];
}

static void i2c_ensure_mutex(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    if (st->mutex == NULL) {
        hal_critical_section_enter();
        if (st->mutex == NULL) {
            st->mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

void hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
    hal_i2c_init_bus(0, sda_pin, scl_pin, clock_hz);
}

void hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
    (void)sda_pin;
    (void)scl_pin;
    (void)clock_hz;

    i2c_ensure_mutex(bus);

    i2c_bus_state_t *st = i2c_state(bus);
    memset(st->rx_buf, 0, sizeof(st->rx_buf));
    st->rx_len = 0;
    st->rx_pos = 0;
    st->cur_addr = 0u;
    st->busy = false;
    st->transaction_count = 0u;
    st->bus_clear_count = 0u;
    st->initialized = true;
}

void hal_i2c_deinit(void) {
    hal_i2c_deinit_bus(0);
}

void hal_i2c_deinit_bus(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    st->initialized = false;
    st->rx_len = 0;
    st->rx_pos = 0;
    st->cur_addr = 0u;
}

void hal_i2c_lock(void) {
    hal_i2c_lock_bus(0);
}

void hal_i2c_lock_bus(uint8_t bus) {
    i2c_ensure_mutex(bus);
    hal_mutex_lock(i2c_state(bus)->mutex);
}

void hal_i2c_unlock(void) {
    hal_i2c_unlock_bus(0);
}

void hal_i2c_unlock_bus(uint8_t bus) {
    i2c_ensure_mutex(bus);
    hal_mutex_unlock(i2c_state(bus)->mutex);
}

void hal_i2c_begin_transmission(uint8_t address) {
    hal_i2c_begin_transmission_bus(0, address);
}

void hal_i2c_begin_transmission_bus(uint8_t bus, uint8_t address) {
    hal_i2c_lock_bus(bus);
    i2c_state(bus)->cur_addr = address;
}

size_t hal_i2c_write(uint8_t data) {
    return hal_i2c_write_bus(0, data);
}

size_t hal_i2c_write_bus(uint8_t bus, uint8_t data) {
    (void)bus;
    (void)data;
    return 1u;
}

uint8_t hal_i2c_end_transmission(void) {
    return hal_i2c_end_transmission_bus(0);
}

uint8_t hal_i2c_end_transmission_bus(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    st->transaction_count++;
    hal_i2c_unlock_bus(bus);
    return st->busy ? 2u : 0u;
}

uint8_t hal_i2c_write_byte(uint8_t address, uint8_t data, bool *outWriteOk) {
    return hal_i2c_write_byte_bus(0, address, data, outWriteOk);
}

uint8_t hal_i2c_write_byte_bus(uint8_t bus, uint8_t address, uint8_t data, bool *outWriteOk) {
    hal_i2c_begin_transmission_bus(bus, address);
    size_t written = hal_i2c_write_bus(bus, data);
    if (outWriteOk != NULL) {
        *outWriteOk = (written == 1u);
    }
    return hal_i2c_end_transmission_bus(bus);
}

uint8_t hal_i2c_read_byte(uint8_t address, bool *outReadOk) {
    return hal_i2c_read_byte_bus(0, address, outReadOk);
}

uint8_t hal_i2c_read_byte_bus(uint8_t bus, uint8_t address, bool *outReadOk) {
    i2c_bus_state_t *st = i2c_state(bus);
    (void)address;

    hal_i2c_lock_bus(bus);
    st->transaction_count++;

    int raw = (st->rx_pos < st->rx_len) ? st->rx_buf[st->rx_pos++] : 0;

    hal_i2c_unlock_bus(bus);
    if (outReadOk != NULL) {
        *outReadOk = true;
    }
    return (uint8_t)raw;
}

uint8_t hal_i2c_request_from(uint8_t address, uint8_t count) {
    return hal_i2c_request_from_bus(0, address, count);
}

uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count) {
    i2c_bus_state_t *st = i2c_state(bus);
    (void)address;

    hal_i2c_lock_bus(bus);
    st->rx_len = count;
    st->rx_pos = 0;
    st->transaction_count++;
    hal_i2c_unlock_bus(bus);
    return count;
}

int hal_i2c_available(void) {
    return hal_i2c_available_bus(0);
}

int hal_i2c_available_bus(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    return st->rx_len - st->rx_pos;
}

int hal_i2c_read(void) {
    return hal_i2c_read_bus(0);
}

int hal_i2c_read_bus(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    if (st->rx_pos < st->rx_len) {
        return st->rx_buf[st->rx_pos++];
    }
    return -1;
}

bool hal_i2c_is_busy(uint8_t address) {
    return hal_i2c_is_busy_bus(0, address);
}

bool hal_i2c_is_busy_bus(uint8_t bus, uint8_t address) {
    hal_i2c_begin_transmission_bus(bus, address);
    bool busy = i2c_state(bus)->busy;
    (void)hal_i2c_end_transmission_bus(bus);
    return busy;
}

uint32_t hal_i2c_get_transaction_count(void) {
    return hal_i2c_get_transaction_count_bus(0);
}

uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus) {
    return i2c_state(bus)->transaction_count;
}

void hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin) {
    hal_i2c_bus_clear_bus(0, sda_pin, scl_pin);
}

void hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin) {
    (void)sda_pin;
    (void)scl_pin;
    i2c_state(bus)->bus_clear_count++;
}

#endif /* HAL_DISABLE_I2C */

#endif /* !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32) */
