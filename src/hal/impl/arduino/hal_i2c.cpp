#include "../../hal_config.h"
#ifndef HAL_DISABLE_I2C

#include "../../hal_i2c.h"
#include "../../hal_sync.h"
#include <Wire.h>

static hal_mutex_t s_i2c_mutex[2] = {NULL, NULL};

static inline uint8_t i2c_bus_index(uint8_t bus) {
    return bus == 1 ? 1 : 0;
}

static TwoWire *i2c_bus_wire(uint8_t bus) {
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    return i2c_bus_index(bus) == 1 ? &Wire1 : &Wire;
#else
    (void)bus;
    return &Wire;
#endif
}

static void i2c_ensure_mutex(uint8_t bus) {
    uint8_t idx = i2c_bus_index(bus);
    if (!s_i2c_mutex[idx]) {
        hal_critical_section_enter();
        if (!s_i2c_mutex[idx]) {
            s_i2c_mutex[idx] = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

void hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
    hal_i2c_init_bus(0, sda_pin, scl_pin, clock_hz);
}

void hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
    uint8_t idx = i2c_bus_index(bus);
    i2c_ensure_mutex(idx);
    TwoWire *wire = i2c_bus_wire(bus);
    wire->setSDA(sda_pin);
    wire->setSCL(scl_pin);
    wire->setClock(clock_hz);
    wire->begin();
}

void hal_i2c_deinit(void) {
    hal_i2c_deinit_bus(0);
}

void hal_i2c_deinit_bus(uint8_t bus) {
    uint8_t idx = i2c_bus_index(bus);
    i2c_bus_wire(idx)->end();
}

void hal_i2c_lock(void) {
    hal_i2c_lock_bus(0);
}

void hal_i2c_lock_bus(uint8_t bus) {
    uint8_t idx = i2c_bus_index(bus);
    i2c_ensure_mutex(idx);
    hal_mutex_lock(s_i2c_mutex[idx]);
}

void hal_i2c_unlock(void) {
    hal_i2c_unlock_bus(0);
}

void hal_i2c_unlock_bus(uint8_t bus) {
    uint8_t idx = i2c_bus_index(bus);
    i2c_ensure_mutex(idx);
    hal_mutex_unlock(s_i2c_mutex[idx]);
}

void hal_i2c_begin_transmission(uint8_t address) {
    hal_i2c_begin_transmission_bus(0, address);
}

void hal_i2c_begin_transmission_bus(uint8_t bus, uint8_t address) {
    uint8_t idx = i2c_bus_index(bus);
    i2c_ensure_mutex(idx);
    hal_mutex_lock(s_i2c_mutex[idx]);
    i2c_bus_wire(idx)->beginTransmission(address);
}

size_t hal_i2c_write(uint8_t data) {
    return hal_i2c_write_bus(0, data);
}

size_t hal_i2c_write_bus(uint8_t bus, uint8_t data) {
    return i2c_bus_wire(bus)->write(data);
}

uint8_t hal_i2c_end_transmission(void) {
    return hal_i2c_end_transmission_bus(0);
}

uint8_t hal_i2c_end_transmission_bus(uint8_t bus) {
    uint8_t idx = i2c_bus_index(bus);
    i2c_ensure_mutex(idx);
    uint8_t result = i2c_bus_wire(idx)->endTransmission();
    hal_mutex_unlock(s_i2c_mutex[idx]);
    return result;
}

uint8_t hal_i2c_request_from(uint8_t address, uint8_t count) {
    return hal_i2c_request_from_bus(0, address, count);
}

uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count) {
    uint8_t idx = i2c_bus_index(bus);
    i2c_ensure_mutex(idx);
    hal_mutex_lock(s_i2c_mutex[idx]);
    uint8_t received = i2c_bus_wire(idx)->requestFrom(address, count);
    hal_mutex_unlock(s_i2c_mutex[idx]);
    return received;
}

int hal_i2c_available(void) {
    return hal_i2c_available_bus(0);
}

int hal_i2c_available_bus(uint8_t bus) {
    return i2c_bus_wire(bus)->available();
}

int hal_i2c_read(void) {
    return hal_i2c_read_bus(0);
}

int hal_i2c_read_bus(uint8_t bus) {
    return i2c_bus_wire(bus)->read();
}

bool hal_i2c_is_busy(uint8_t address) {
    return hal_i2c_is_busy_bus(0, address);
}

bool hal_i2c_is_busy_bus(uint8_t bus, uint8_t address) {
    hal_i2c_begin_transmission_bus(bus, address);
    return hal_i2c_end_transmission_bus(bus) != 0;
}

#endif /* HAL_DISABLE_I2C */
