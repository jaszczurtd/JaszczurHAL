#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_S3

#include "hal/core/hal_config.h"
#include "hal/core/jh_endian.h"
#ifdef HAL_ENABLE_SPI

#include "hal/core/hal_mutex_once.h"
#include "hal/spi/hal_spi.h"
#include "hal/spi/hal_spi_internal.h"
#include "hal/spi/hal_spi_settings.h"
#include "hal/system/hal_sync.h"
#include "jh_board_config.h"
#include "jh_esp32_status.h"

#include <driver/spi_master.h>
#include <esp_err.h>
#include <soc/spi_pins.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

constexpr size_t kSpiTransferChunkSize = 64u;

struct SpiBusState {
  bool initialized;
  bool transaction_active;
  uint8_t rx_pin;
  uint8_t tx_pin;
  uint8_t sck_pin;
  hal_spi_settings_t settings;
  spi_device_handle_t device;
  hal_mutex_t mutex;
};

SpiBusState s_spi[2] = {};

bool spi_bus_valid(uint8_t bus) { return bus <= 1u; }

uint8_t spi_bus_index(uint8_t bus) {
  HAL_ASSERT(spi_bus_valid(bus), "hal_spi: invalid bus index");
  return spi_bus_valid(bus) ? bus : 0u;
}

spi_host_device_t spi_host(uint8_t bus) {
  return spi_bus_index(bus) == 1u ? SPI3_HOST : SPI2_HOST;
}

bool spi_pin_accessible(uint8_t pin) {
  if (pin >= 64u) {
    return false;
  }
  const uint64_t bit = UINT64_C(1) << pin;
  const uint64_t available = (uint64_t)HAL_BOARD_GPIO_EXPOSED_MASK |
                             (uint64_t)HAL_BOARD_GPIO_SOFT_RESERVED_MASK;
  return (((uint64_t)HAL_TARGET_GPIO_VALID_MASK & bit) != 0u) &&
         ((available & bit) != 0u) &&
         (((uint64_t)HAL_BOARD_GPIO_HARD_RESERVED_MASK & bit) == 0u);
}

bool spi_output_pin_usable(uint8_t pin) {
  if (!spi_pin_accessible(pin)) {
    return false;
  }
  return (((uint64_t)HAL_TARGET_GPIO_INPUT_ONLY_MASK & (UINT64_C(1) << pin)) ==
          0u);
}

bool spi_pins_valid(uint8_t rx_pin, uint8_t tx_pin, uint8_t sck_pin) {
  return rx_pin != tx_pin && rx_pin != sck_pin && tx_pin != sck_pin &&
         spi_pin_accessible(rx_pin) && spi_output_pin_usable(tx_pin) &&
         spi_output_pin_usable(sck_pin);
}

void spi_default_pins(uint8_t bus, uint8_t *rx_pin, uint8_t *tx_pin,
                      uint8_t *sck_pin) {
  if (bus == 0u) {
    *rx_pin = (uint8_t)SPI2_IOMUX_PIN_NUM_MISO;
    *tx_pin = (uint8_t)SPI2_IOMUX_PIN_NUM_MOSI;
    *sck_pin = (uint8_t)SPI2_IOMUX_PIN_NUM_CLK;
    return;
  }

  /* ESP32-S3 SPI3 has no dedicated IOMUX pins. Keep its GPIO-matrix defaults
   * separate from the SPI2 IOMUX set so both HAL buses can coexist. */
  *rx_pin = 4u;
  *tx_pin = 5u;
  *sck_pin = 6u;
}

bool spi_settings_equal(const hal_spi_settings_t &left,
                        const hal_spi_settings_t &right) {
  return left.clock_hz == right.clock_hz && left.bit_order == right.bit_order &&
         left.data_mode == right.data_mode;
}

bool spi_clock_valid(uint32_t clock_hz) {
  return clock_hz > 0u && clock_hz <= (uint32_t)INT_MAX;
}

hal_status_t spi_ensure_mutex(uint8_t bus) {
  return jh_hal_mutex_create_once(&s_spi[spi_bus_index(bus)].mutex) != nullptr
             ? HAL_OK
             : HAL_ENOMEM;
}

hal_status_t spi_remove_device(SpiBusState &state) {
  if (state.device == nullptr) {
    return HAL_OK;
  }
  const esp_err_t error = spi_bus_remove_device(state.device);
  if (error == ESP_OK) {
    state.device = nullptr;
  }
  return jh_esp32_status_from_esp_err(error);
}

hal_status_t spi_add_device(uint8_t bus) {
  SpiBusState &state = s_spi[spi_bus_index(bus)];
  if (state.device != nullptr) {
    return HAL_OK;
  }
  if (!spi_clock_valid(state.settings.clock_hz)) {
    return HAL_EINVAL;
  }

  spi_device_interface_config_t config = {};
  config.mode = state.settings.data_mode;
  config.clock_speed_hz = (int)state.settings.clock_hz;
  config.spics_io_num = -1;
  config.flags = state.settings.bit_order == HAL_SPI_LSBFIRST
                     ? SPI_DEVICE_BIT_LSBFIRST
                     : 0u;
  config.queue_size = 1;
  return jh_esp32_status_from_esp_err(
      spi_bus_add_device(spi_host(bus), &config, &state.device));
}

hal_status_t spi_deinit_bus(uint8_t bus) {
  SpiBusState &state = s_spi[spi_bus_index(bus)];
  if (!state.initialized) {
    state.transaction_active = false;
    return HAL_OK;
  }

  const hal_status_t device_status = spi_remove_device(state);
  if (hal_status_is_error(device_status)) {
    return device_status;
  }
  const esp_err_t error = spi_bus_free(spi_host(bus));
  if (error != ESP_OK) {
    return jh_esp32_status_from_esp_err(error);
  }
  state.initialized = false;
  state.transaction_active = false;
  return HAL_OK;
}

hal_status_t spi_ensure_initialized(uint8_t bus) {
  const uint8_t index = spi_bus_index(bus);
  if (!s_spi[index].initialized) {
    uint8_t rx_pin = 0u;
    uint8_t tx_pin = 0u;
    uint8_t sck_pin = 0u;
    spi_default_pins(index, &rx_pin, &tx_pin, &sck_pin);
    return hal_spi_init(index, rx_pin, tx_pin, sck_pin);
  }
  return spi_add_device(index);
}

hal_status_t spi_transmit(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                          size_t length) {
  spi_transaction_t transaction = {};
  transaction.length = length * 8u;
  transaction.rxlength = rx != nullptr ? length * 8u : 0u;
  transaction.tx_buffer = tx;
  transaction.rx_buffer = rx;
  return jh_esp32_status_from_esp_err(spi_device_polling_transmit(
      s_spi[spi_bus_index(bus)].device, &transaction));
}

hal_status_t spi_transfer_chunks(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                                 size_t length) {
  uint8_t temporary[kSpiTransferChunkSize];
  size_t offset = 0u;
  while (offset < length) {
    size_t chunk = length - offset;
    if (chunk > kSpiTransferChunkSize) {
      chunk = kSpiTransferChunkSize;
    }

    const uint8_t *chunk_tx = tx != nullptr ? tx + offset : temporary;
    uint8_t *chunk_rx = rx != nullptr ? rx + offset : nullptr;
    if (tx == nullptr) {
      memset(temporary, 0xFF, chunk);
    } else if (tx == rx) {
      memcpy(temporary, tx + offset, chunk);
      chunk_tx = temporary;
    }

    const hal_status_t status = spi_transmit(bus, chunk_tx, chunk_rx, chunk);
    if (hal_status_is_error(status)) {
      return status;
    }
    offset += chunk;
  }
  return HAL_OK;
}

} // namespace

hal_status_t hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin,
                          uint8_t sck_pin) {
  if (!spi_bus_valid(bus) || !spi_pins_valid(rx_pin, tx_pin, sck_pin)) {
    return HAL_EINVAL;
  }
  const uint8_t index = spi_bus_index(bus);
  hal_status_t status = spi_ensure_mutex(index);
  if (hal_status_is_error(status)) {
    return status;
  }

  status = spi_deinit_bus(index);
  if (hal_status_is_error(status)) {
    return status;
  }

  spi_bus_config_t config = {};
  config.mosi_io_num = tx_pin;
  config.miso_io_num = rx_pin;
  config.sclk_io_num = sck_pin;
  config.quadwp_io_num = -1;
  config.quadhd_io_num = -1;
  config.data4_io_num = -1;
  config.data5_io_num = -1;
  config.data6_io_num = -1;
  config.data7_io_num = -1;
  config.max_transfer_sz = (int)kSpiTransferChunkSize;
  config.flags = SPICOMMON_BUSFLAG_MASTER;
  config.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
  config.intr_flags = 0;

  esp_err_t error =
      spi_bus_initialize(spi_host(index), &config, SPI_DMA_CH_AUTO);
  if (error == ESP_ERR_NOT_FOUND) {
    error = spi_bus_initialize(spi_host(index), &config, SPI_DMA_DISABLED);
  }
  if (error != ESP_OK) {
    return jh_esp32_status_from_esp_err(error);
  }

  SpiBusState &state = s_spi[index];
  state.initialized = true;
  state.transaction_active = false;
  state.rx_pin = rx_pin;
  state.tx_pin = tx_pin;
  state.sck_pin = sck_pin;
  state.settings = spi_default_settings();
  state.device = nullptr;
  status = spi_add_device(index);
  if (hal_status_is_error(status)) {
    (void)spi_deinit_bus(index);
  }
  return status;
}

void hal_spi_deinit(uint8_t bus) { (void)spi_deinit_bus(spi_bus_index(bus)); }

void hal_spi_lock(uint8_t bus) {
  const uint8_t index = spi_bus_index(bus);
  if (hal_status_is_ok(spi_ensure_mutex(index))) {
    hal_mutex_lock(s_spi[index].mutex);
  }
}

void hal_spi_unlock(uint8_t bus) {
  const uint8_t index = spi_bus_index(bus);
  if (hal_status_is_ok(spi_ensure_mutex(index))) {
    hal_mutex_unlock(s_spi[index].mutex);
  }
}

hal_status_t hal_spi_begin_transaction(uint8_t bus,
                                       const hal_spi_settings_t *settings) {
  if (!spi_bus_valid(bus) || !spi_settings_valid(settings)) {
    return HAL_EINVAL;
  }
  const hal_spi_settings_t normalized = spi_normalize_settings(settings);
  if (!spi_clock_valid(normalized.clock_hz)) {
    return HAL_EINVAL;
  }

  const uint8_t index = spi_bus_index(bus);
  hal_status_t status = spi_ensure_initialized(index);
  if (hal_status_is_error(status)) {
    return status;
  }

  SpiBusState &state = s_spi[index];
  if (!spi_settings_equal(state.settings, normalized)) {
    status = spi_remove_device(state);
    if (hal_status_is_error(status)) {
      return status;
    }
    state.settings = normalized;
    status = spi_add_device(index);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
  state.transaction_active = true;
  return HAL_OK;
}

hal_status_t hal_spi_end_transaction(uint8_t bus) {
  if (!spi_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  s_spi[spi_bus_index(bus)].transaction_active = false;
  return HAL_OK;
}

hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                 uint8_t *out_received) {
  if (!spi_bus_valid(bus) || out_received == nullptr) {
    return HAL_EINVAL;
  }
  const uint8_t index = spi_bus_index(bus);
  const hal_status_t status = spi_ensure_initialized(index);
  if (hal_status_is_error(status)) {
    return status;
  }

  spi_transaction_t transaction = {};
  transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  transaction.length = 8u;
  transaction.rxlength = 8u;
  transaction.tx_data[0] = data;
  const esp_err_t error =
      spi_device_polling_transmit(s_spi[index].device, &transaction);
  if (error == ESP_OK) {
    *out_received = transaction.rx_data[0];
  }
  return jh_esp32_status_from_esp_err(error);
}

hal_status_t jh_hal_spi_transfer16_provider(uint8_t bus, uint16_t data,
                                            uint16_t *out_received) {
  if (!spi_bus_valid(bus) || out_received == nullptr) {
    return HAL_EINVAL;
  }
  const uint8_t index = spi_bus_index(bus);
  hal_status_t status = spi_ensure_initialized(index);
  if (hal_status_is_error(status)) {
    return status;
  }

  const bool lsb_first = s_spi[index].settings.bit_order == HAL_SPI_LSBFIRST;
  uint8_t tx[2];
  if (lsb_first) {
    jh_store_le16(tx, data);
  } else {
    jh_store_be16(tx, data);
  }
  uint8_t rx[2] = {};
  status = spi_transfer_chunks(index, tx, rx, sizeof(tx));
  if (hal_status_is_error(status)) {
    return status;
  }
  *out_received = lsb_first ? jh_load_le16(rx) : jh_load_be16(rx);
  return HAL_OK;
}

hal_status_t hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                                   size_t len) {
  if (!spi_bus_valid(bus) || (len > 0u && tx == nullptr && rx == nullptr)) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }
  const uint8_t index = spi_bus_index(bus);
  const hal_status_t status = spi_ensure_initialized(index);
  if (hal_status_is_error(status)) {
    return status;
  }
  return spi_transfer_chunks(index, tx, rx, len);
}

hal_status_t jh_hal_spi_write_provider(uint8_t bus, const uint8_t *data,
                                       size_t len) {
  return hal_spi_transfer_txrx(bus, data, nullptr, len);
}

hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus, const uint8_t *data,
                                              size_t len) {
  if (!spi_bus_valid(bus) || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  /* The ESP-IDF bus is DMA-enabled when a channel is available. This backend
   * uses the API-permitted synchronous fallback so completion owns no hidden
   * descriptor or caller buffer after this function returns. */
  return hal_spi_write(bus, data, len);
}

bool hal_spi_write_dma_async_busy(uint8_t bus) {
  (void)bus;
  return false;
}

hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus) {
  return spi_bus_valid(bus) ? HAL_OK : HAL_EINVAL;
}

#endif /* HAL_ENABLE_SPI */
#endif /* HAL_TARGET_IS_ESP32_S3 */
