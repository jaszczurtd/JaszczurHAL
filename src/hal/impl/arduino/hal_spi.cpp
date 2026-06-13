#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_spi.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"
#include <SPI.h>

static hal_mutex_t s_spi_mutex[2] = {NULL, NULL};
static bool s_spi_initialized[2] = {false, false};

static inline uint8_t spi_bus_index(uint8_t bus) { return bus == 1 ? 1 : 0; }

static inline SPIClassRP2040 &spi_object(uint8_t bus) {
  return spi_bus_index(bus) == 1 ? SPI1 : SPI;
}

static SPISettings spi_make_settings(const hal_spi_settings_t *settings) {
  if (settings == nullptr) {
    return SPISettings(HAL_SPI_CLOCK_DEFAULT_HZ, MSBFIRST, SPI_MODE0);
  }
  uint32_t clock =
      settings->clock_hz ? settings->clock_hz : HAL_SPI_CLOCK_DEFAULT_HZ;
  BitOrder order =
      settings->bit_order == HAL_SPI_LSBFIRST ? LSBFIRST : MSBFIRST;
  uint8_t mode = SPI_MODE0;

  if ((settings->data_mode >= SPI_MODE0) &&
      (settings->data_mode <= SPI_MODE3)) {
    mode = static_cast<uint8_t>(settings->data_mode);
  }
  return SPISettings(clock, order, mode);
}

static void spi_ensure_mutex(uint8_t bus) {
  uint8_t idx = spi_bus_index(bus);
  (void)jh_hal_mutex_create_once(&s_spi_mutex[idx]);
}

void hal_spi_init(uint8_t bus, uint8_t rx_miso, uint8_t tx_mosi,
                  uint8_t sck_pin) {
  uint8_t idx = spi_bus_index(bus);
  spi_ensure_mutex(idx);
  SPIClassRP2040 &spi = spi_object(idx);
  spi.setRX(rx_miso);
  spi.setTX(tx_mosi);
  spi.setSCK(sck_pin);
  spi.begin(true); // true = controller (master) mode on RP2040
  s_spi_initialized[idx] = true;
}

void hal_spi_deinit(uint8_t bus) {
  uint8_t idx = spi_bus_index(bus);
  spi_object(idx).end();
  s_spi_initialized[idx] = false;
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

void hal_spi_begin_transaction(uint8_t bus,
                               const hal_spi_settings_t *settings) {
  uint8_t idx = spi_bus_index(bus);
  if (!s_spi_initialized[idx]) {
    spi_object(idx).begin(true);
    s_spi_initialized[idx] = true;
  }
  spi_object(idx).beginTransaction(spi_make_settings(settings));
}

void hal_spi_end_transaction(uint8_t bus) { spi_object(bus).endTransaction(); }

uint8_t hal_spi_transfer(uint8_t bus, uint8_t data) {
  uint8_t idx = spi_bus_index(bus);
  if (!s_spi_initialized[idx]) {
    spi_object(idx).begin(true);
    s_spi_initialized[idx] = true;
  }
  return spi_object(idx).transfer(data);
}

uint16_t hal_spi_transfer16(uint8_t bus, uint16_t data) {
  return spi_object(bus).transfer16(data);
}

void hal_spi_transfer_buffer(uint8_t bus, uint8_t *buffer, size_t len) {
  if (buffer == nullptr || len == 0u) {
    return;
  }
  spi_object(bus).transfer(buffer, len);
}

void hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                           size_t len) {
  if (len == 0u) {
    return;
  }
  SPIClassRP2040 &spi = spi_object(bus);
  for (size_t i = 0; i < len; ++i) {
    const uint8_t out = tx ? tx[i] : 0xFFu;
    const uint8_t in = spi.transfer(out);
    if (rx) {
      rx[i] = in;
    }
  }
}

void hal_spi_write(uint8_t bus, const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0u) {
    return;
  }
  hal_spi_transfer_txrx(bus, data, nullptr, len);
}
#endif // HAL_TARGET_IS_RP2040
