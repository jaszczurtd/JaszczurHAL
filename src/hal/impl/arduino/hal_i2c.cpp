#include "../../hal_config.h"
#ifndef HAL_DISABLE_I2C

#include "../../hal_i2c.h"
#include "../../hal_sync.h"
#include <Wire.h>

static hal_mutex_t s_i2c_mutex[2] = {NULL, NULL};
static volatile uint32_t s_i2c_transaction_count[2] = {0, 0};

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
    s_i2c_transaction_count[idx] = 0;
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
    s_i2c_transaction_count[idx]++;
    hal_mutex_unlock(s_i2c_mutex[idx]);
    return result;
}

uint8_t hal_i2c_write_byte(uint8_t address, uint8_t data, bool *outWriteOk) {
    return hal_i2c_write_byte_bus(0, address, data, outWriteOk);
}

uint8_t hal_i2c_write_byte_bus(uint8_t bus, uint8_t address, uint8_t data, bool *outWriteOk) {
    hal_i2c_begin_transmission_bus(bus, address);
    size_t written = hal_i2c_write_bus(bus, data);
    if (outWriteOk != NULL) {
        *outWriteOk = (written == 1);
    }
    return hal_i2c_end_transmission_bus(bus);
}

uint8_t hal_i2c_read_byte(uint8_t address, bool *outReadOk) {
    return hal_i2c_read_byte_bus(0, address, outReadOk);
}

uint8_t hal_i2c_read_byte_bus(uint8_t bus, uint8_t address, bool *outReadOk) {
    uint8_t idx = i2c_bus_index(bus);
    i2c_ensure_mutex(idx);
    hal_mutex_lock(s_i2c_mutex[idx]);

    uint8_t received = i2c_bus_wire(idx)->requestFrom(address, (uint8_t)1);
    s_i2c_transaction_count[idx]++;
    if (received != 1) {
        if (outReadOk != NULL) *outReadOk = false;
        hal_mutex_unlock(s_i2c_mutex[idx]);
        return 0;
    }
    int raw = i2c_bus_wire(idx)->read();
    hal_mutex_unlock(s_i2c_mutex[idx]);
    if (raw < 0) {
        if (outReadOk != NULL) *outReadOk = false;
        return 0;
    }
    if (outReadOk != NULL) *outReadOk = true;
    return (uint8_t)raw;
}

uint8_t hal_i2c_request_from(uint8_t address, uint8_t count) {
    return hal_i2c_request_from_bus(0, address, count);
}

uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count) {
    uint8_t idx = i2c_bus_index(bus);
    i2c_ensure_mutex(idx);
    hal_mutex_lock(s_i2c_mutex[idx]);
    uint8_t received = i2c_bus_wire(idx)->requestFrom(address, count);
    s_i2c_transaction_count[idx]++;
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

uint32_t hal_i2c_get_transaction_count(void) {
    return hal_i2c_get_transaction_count_bus(0);
}

uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus) {
    return s_i2c_transaction_count[i2c_bus_index(bus)];
}

void hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin) {
    hal_i2c_bus_clear_bus(0, sda_pin, scl_pin);
}

void hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin) {
    (void)bus;

    // SDA as input (read), SCL as output (clock)
    pinMode(sda_pin, INPUT_PULLUP);
    pinMode(scl_pin, OUTPUT);
    digitalWrite(scl_pin, HIGH);
    delayMicroseconds(5);

    // Toggle SCL up to 9 times to release a stuck slave
    for (uint8_t i = 0; i < 9; i++) {
        if (digitalRead(sda_pin)) {
            break;   // SDA released
        }
        digitalWrite(scl_pin, LOW);
        delayMicroseconds(5);
        digitalWrite(scl_pin, HIGH);
        delayMicroseconds(5);
    }

    // Generate STOP condition: SDA low → SCL high → SDA high
    pinMode(sda_pin, OUTPUT);
    digitalWrite(sda_pin, LOW);
    delayMicroseconds(5);
    digitalWrite(scl_pin, HIGH);
    delayMicroseconds(5);
    digitalWrite(sda_pin, HIGH);
    delayMicroseconds(5);

    // Return pins to input — hal_i2c_init will reconfigure for I2C
    pinMode(sda_pin, INPUT_PULLUP);
    pinMode(scl_pin, INPUT_PULLUP);
}

#endif /* HAL_DISABLE_I2C */
