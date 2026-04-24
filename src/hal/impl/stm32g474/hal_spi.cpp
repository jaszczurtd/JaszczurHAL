#include "../../hal_spi.h"
#include "../../hal_sync.h"

typedef struct {
    bool initialized;
    uint8_t rx_pin;
    uint8_t tx_pin;
    uint8_t sck_pin;
    hal_mutex_t mutex;
} hal_spi_bus_state_t;

static hal_spi_bus_state_t s_spi[2] = {};

static inline uint8_t spi_bus_index(uint8_t bus) {
    return bus == 1u ? 1u : 0u;
}

static void spi_ensure_mutex(uint8_t bus) {
    hal_spi_bus_state_t *st = &s_spi[spi_bus_index(bus)];
    if (st->mutex == nullptr) {
        hal_critical_section_enter();
        if (st->mutex == nullptr) {
            st->mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

void hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin, uint8_t sck_pin) {
    uint8_t idx = spi_bus_index(bus);
    hal_spi_bus_state_t *st = &s_spi[idx];

    spi_ensure_mutex(idx);
    st->rx_pin = rx_pin;
    st->tx_pin = tx_pin;
    st->sck_pin = sck_pin;
    st->initialized = true;
}

void hal_spi_lock(uint8_t bus) {
    uint8_t idx = spi_bus_index(bus);
    spi_ensure_mutex(idx);
    hal_mutex_lock(s_spi[idx].mutex);
}

void hal_spi_unlock(uint8_t bus) {
    uint8_t idx = spi_bus_index(bus);
    spi_ensure_mutex(idx);
    hal_mutex_unlock(s_spi[idx].mutex);
}
