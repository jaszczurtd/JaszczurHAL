#include "../../hal_spi.h"
#include "../../hal_sync.h"
#include <SPI.h>

static hal_mutex_t s_spi_mutex[2] = {NULL, NULL};

static inline uint8_t spi_bus_index(uint8_t bus) {
    return bus == 1 ? 1 : 0;
}

static void spi_ensure_mutex(uint8_t bus) {
    uint8_t idx = spi_bus_index(bus);
    if (!s_spi_mutex[idx]) {
        hal_critical_section_enter();
        if (!s_spi_mutex[idx]) {
            s_spi_mutex[idx] = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

void hal_spi_init(uint8_t bus, uint8_t rx_miso, uint8_t tx_mosi, uint8_t sck_pin) {
    uint8_t idx = spi_bus_index(bus);
    spi_ensure_mutex(idx);
    if (idx == 1) {
        SPI1.setRX(rx_miso);
        SPI1.setTX(tx_mosi);
        SPI1.setSCK(sck_pin);
        SPI1.begin(true);
    } else {
        SPI.setRX(rx_miso);
        SPI.setTX(tx_mosi);
        SPI.setSCK(sck_pin);
        SPI.begin(true);  // true = controller (master) mode on RP2040
    }
}

void hal_spi_lock(uint8_t bus) {
    uint8_t idx = spi_bus_index(bus);
    spi_ensure_mutex(idx);
    hal_mutex_lock(s_spi_mutex[idx]);
}

void hal_spi_unlock(uint8_t bus) {
    uint8_t idx = spi_bus_index(bus);
    spi_ensure_mutex(idx);
    hal_mutex_unlock(s_spi_mutex[idx]);
}
