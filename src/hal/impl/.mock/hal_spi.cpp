#include "../../hal_spi.h"
#include "hal_mock.h"

static bool    s_initialized = false;
static uint8_t s_bus         = 0;
static uint8_t s_rx_pin      = 0;
static uint8_t s_tx_pin      = 0;
static uint8_t s_sck_pin     = 0;
static int     s_lock_depth[2] = {0, 0};

static inline uint8_t spi_bus_index(uint8_t bus) {
    return bus == 1 ? 1 : 0;
}

void hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin, uint8_t sck_pin) {
    s_bus         = spi_bus_index(bus);
    s_rx_pin      = rx_pin;
    s_tx_pin      = tx_pin;
    s_sck_pin     = sck_pin;
    s_initialized = true;
}

void hal_spi_lock(uint8_t bus) {
    uint8_t idx = spi_bus_index(bus);
    s_lock_depth[idx]++;
}

void hal_spi_unlock(uint8_t bus) {
    uint8_t idx = spi_bus_index(bus);
    if (s_lock_depth[idx] > 0) {
        s_lock_depth[idx]--;
    }
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

bool    hal_mock_spi_is_initialized(void) { return s_initialized; }
uint8_t hal_mock_spi_get_bus(void)        { return s_bus;         }
uint8_t hal_mock_spi_get_rx_pin(void)     { return s_rx_pin;      }
uint8_t hal_mock_spi_get_tx_pin(void)     { return s_tx_pin;      }
uint8_t hal_mock_spi_get_sck_pin(void)    { return s_sck_pin;     }
int     hal_mock_spi_get_lock_depth(uint8_t bus) {
    return s_lock_depth[spi_bus_index(bus)];
}

void hal_mock_spi_reset(void) {
    s_initialized = false;
    s_bus = s_rx_pin = s_tx_pin = s_sck_pin = 0;
    s_lock_depth[0] = 0;
    s_lock_depth[1] = 0;
}
