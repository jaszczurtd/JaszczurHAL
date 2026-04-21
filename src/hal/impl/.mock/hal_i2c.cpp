#include "../../hal_i2c.h"
#include "hal_mock.h"

#include <string.h>

/* request_from() uses uint8_t count, so 255 is the maximum meaningful size. */
#define MOCK_I2C_BUF_SIZE 255

typedef struct {
    uint8_t  rx_buf[MOCK_I2C_BUF_SIZE];
    int      rx_len;
    int      rx_pos;
    uint8_t  cur_addr;
    bool     busy;
    bool     initialized;
    int      lock_depth;
    int      read_byte_lock_depth_at_read;
    uint32_t transaction_count;
    uint32_t bus_clear_count;
} mock_i2c_bus_state_t;

static mock_i2c_bus_state_t s_i2c_state[2];

static inline uint8_t i2c_bus_index(uint8_t bus) {
    return bus == 1 ? 1 : 0;
}

static inline mock_i2c_bus_state_t *i2c_state(uint8_t bus) {
    return &s_i2c_state[i2c_bus_index(bus)];
}

void hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
    hal_i2c_init_bus(0, sda_pin, scl_pin, clock_hz);
}

void hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
    mock_i2c_bus_state_t *st = i2c_state(bus);
    (void)sda_pin; (void)scl_pin; (void)clock_hz;
    st->initialized = true;
    st->rx_len = 0;
    st->rx_pos = 0;
    st->lock_depth = 0;
    st->read_byte_lock_depth_at_read = 0;
    st->transaction_count = 0;
    st->bus_clear_count = 0;
}

void hal_i2c_deinit(void) {
    hal_i2c_deinit_bus(0);
}

void hal_i2c_deinit_bus(uint8_t bus) {
    mock_i2c_bus_state_t *st = i2c_state(bus);
    st->initialized = false;
    st->lock_depth = 0;
    st->read_byte_lock_depth_at_read = 0;
    st->rx_len = 0;
    st->rx_pos = 0;
    st->cur_addr = 0;
    st->busy = false;
}

void hal_i2c_lock(void) {
    hal_i2c_lock_bus(0);
}

void hal_i2c_unlock(void) {
    hal_i2c_unlock_bus(0);
}

void hal_i2c_lock_bus(uint8_t bus) {
    i2c_state(bus)->lock_depth++;
}

void hal_i2c_unlock_bus(uint8_t bus) {
    mock_i2c_bus_state_t *st = i2c_state(bus);
    if (st->lock_depth > 0) {
        st->lock_depth--;
    }
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
    return 1;
}

uint8_t hal_i2c_end_transmission(void) {
    return hal_i2c_end_transmission_bus(0);
}

uint8_t hal_i2c_end_transmission_bus(uint8_t bus) {
    mock_i2c_bus_state_t *st = i2c_state(bus);
    st->transaction_count++;
    hal_i2c_unlock_bus(bus);
    return st->busy ? 2 : 0;  // 2 = NACK on address (simulates device not responding)
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
    mock_i2c_bus_state_t *st = i2c_state(bus);
    hal_i2c_lock_bus(bus);
    (void)address;
    st->rx_len = 1;
    st->rx_pos = 0;
    st->transaction_count++;

    uint8_t received = 1;
    if (received != 1) {
        if (outReadOk != NULL) *outReadOk = false;
        hal_i2c_unlock_bus(bus);
        return 0;
    }
    st->read_byte_lock_depth_at_read = st->lock_depth;
    int raw = (st->rx_pos < st->rx_len) ? st->rx_buf[st->rx_pos++] : -1;
    hal_i2c_unlock_bus(bus);
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
    mock_i2c_bus_state_t *st = i2c_state(bus);
    hal_i2c_lock_bus(bus);
    (void)address;
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
    mock_i2c_bus_state_t *st = i2c_state(bus);
    return st->rx_len - st->rx_pos;
}

int hal_i2c_read(void) {
    return hal_i2c_read_bus(0);
}

int hal_i2c_read_bus(uint8_t bus) {
    mock_i2c_bus_state_t *st = i2c_state(bus);
    if (st->rx_pos < st->rx_len) return st->rx_buf[st->rx_pos++];
    return -1;
}

/* ── Mock helpers ─────────────────────────────────────────────────────── */

void hal_mock_i2c_inject_rx(const uint8_t *data, int len) {
    hal_mock_i2c_inject_rx_bus(0, data, len);
}

void hal_mock_i2c_inject_rx_bus(uint8_t bus, const uint8_t *data, int len) {
    mock_i2c_bus_state_t *st = i2c_state(bus);
    if (len > MOCK_I2C_BUF_SIZE) len = MOCK_I2C_BUF_SIZE;
    memcpy(st->rx_buf, data, len);
    st->rx_len = len;
    st->rx_pos = 0;
}

uint8_t hal_mock_i2c_get_last_addr(void) {
    return hal_mock_i2c_get_last_addr_bus(0);
}

uint8_t hal_mock_i2c_get_last_addr_bus(uint8_t bus) {
    return i2c_state(bus)->cur_addr;
}

int hal_mock_i2c_get_lock_depth_bus(uint8_t bus) {
    return i2c_state(bus)->lock_depth;
}

int hal_mock_i2c_get_lock_depth(void) {
    return hal_mock_i2c_get_lock_depth_bus(0);
}

int hal_mock_i2c_get_read_byte_lock_depth_bus(uint8_t bus) {
    return i2c_state(bus)->read_byte_lock_depth_at_read;
}

int hal_mock_i2c_get_read_byte_lock_depth(void) {
    return hal_mock_i2c_get_read_byte_lock_depth_bus(0);
}

void hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin) {
    hal_i2c_bus_clear_bus(0, sda_pin, scl_pin);
}

void hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin) {
    (void)sda_pin; (void)scl_pin;
    i2c_state(bus)->bus_clear_count++;
}

uint32_t hal_mock_i2c_get_bus_clear_count(void) {
    return hal_mock_i2c_get_bus_clear_count_bus(0);
}

uint32_t hal_mock_i2c_get_bus_clear_count_bus(uint8_t bus) {
    return i2c_state(bus)->bus_clear_count;
}

bool hal_mock_i2c_is_initialized_bus(uint8_t bus) {
    return i2c_state(bus)->initialized;
}

bool hal_mock_i2c_is_initialized(void) {
    return hal_mock_i2c_is_initialized_bus(0);
}

bool hal_i2c_is_busy(uint8_t address) {
    return hal_i2c_is_busy_bus(0, address);
}

bool hal_i2c_is_busy_bus(uint8_t bus, uint8_t address) {
    hal_i2c_begin_transmission_bus(bus, address);
    bool busy = i2c_state(bus)->busy;
    hal_i2c_end_transmission_bus(bus);
    return busy;
}

void hal_mock_i2c_set_busy(bool busy) {
    hal_mock_i2c_set_busy_bus(0, busy);
}

void hal_mock_i2c_set_busy_bus(uint8_t bus, bool busy) {
    i2c_state(bus)->busy = busy;
}

uint32_t hal_i2c_get_transaction_count(void) {
    return hal_i2c_get_transaction_count_bus(0);
}

uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus) {
    return i2c_state(bus)->transaction_count;
}
