#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/spi/hal_spi.h"
#include "hal/spi/hal_spi_internal.h"
#include "hal/spi/hal_spi_settings.h"
#include "hal/system/hal_sync.h"

#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>

typedef struct {
  bool initialized;
  bool transaction_active;
  uint8_t rx_pin;
  uint8_t tx_pin;
  uint8_t sck_pin;
  hal_spi_settings_t settings;
  hal_spi_settings_t applied_settings;
  uint32_t actual_clock_hz;
  uint8_t applied_data_bits;
  bool hw_configured;
  int dma_tx_channel;
  bool dma_tx_channel_claimed;
  bool dma_tx_active;
  hal_mutex_t mutex;
} hal_spi_bus_state_t;

static hal_spi_bus_state_t s_spi[2] = {};

static inline uint8_t spi_bus_index(uint8_t bus) {
  HAL_ASSERT(bus <= 1u, "hal_spi: invalid bus index");
  return (bus <= 1u) ? bus : 0u;
}

static inline spi_inst_t *spi_hw(uint8_t bus) {
  return spi_bus_index(bus) == 1u ? spi1 : spi0;
}

static void spi_default_pins(uint8_t idx, uint8_t *rx_pin, uint8_t *tx_pin,
                             uint8_t *sck_pin) {
  if (idx == 1u) {
    *rx_pin = 12u;
    *tx_pin = 11u;
    *sck_pin = 10u;
  } else {
    *rx_pin = 16u;
    *tx_pin = 19u;
    *sck_pin = 18u;
  }
}

static void spi_ensure_mutex(uint8_t bus) {
  const uint8_t idx = spi_bus_index(bus);
  (void)jh_hal_mutex_create_once(&s_spi[idx].mutex);
}

static uint8_t spi_reverse8(uint8_t v) {
  v = (uint8_t)(((v & 0xF0u) >> 4) | ((v & 0x0Fu) << 4));
  v = (uint8_t)(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
  v = (uint8_t)(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
  return v;
}

static uint16_t spi_reverse16(uint16_t v) {
  return (uint16_t)((uint16_t)spi_reverse8((uint8_t)(v & 0xFFu)) << 8 |
                    spi_reverse8((uint8_t)(v >> 8)));
}

static spi_cpol_t spi_cpol_from_mode(uint8_t mode) {
  return (mode & 0x2u) ? SPI_CPOL_1 : SPI_CPOL_0;
}

static spi_cpha_t spi_cpha_from_mode(uint8_t mode) {
  return (mode & 0x1u) ? SPI_CPHA_1 : SPI_CPHA_0;
}

static void spi_apply_settings(uint8_t idx, uint data_bits) {
  hal_spi_bus_state_t *st = &s_spi[idx];
  spi_inst_t *hw = spi_hw(idx);

  if (st->hw_configured && st->applied_data_bits == data_bits &&
      st->applied_settings.clock_hz == st->settings.clock_hz &&
      st->applied_settings.data_mode == st->settings.data_mode) {
    return;
  }

  if (!st->hw_configured ||
      st->applied_settings.clock_hz != st->settings.clock_hz) {
    st->actual_clock_hz = spi_set_baudrate(hw, st->settings.clock_hz);
  }
  spi_set_format(hw, data_bits, spi_cpol_from_mode(st->settings.data_mode),
                 spi_cpha_from_mode(st->settings.data_mode), SPI_MSB_FIRST);
  st->applied_settings = st->settings;
  st->applied_data_bits = (uint8_t)data_bits;
  st->hw_configured = true;
}

static void spi_hw_init(uint8_t idx) {
  hal_spi_bus_state_t *st = &s_spi[idx];
  spi_inst_t *hw = spi_hw(idx);

  gpio_set_function(st->rx_pin, GPIO_FUNC_SPI);
  gpio_set_function(st->tx_pin, GPIO_FUNC_SPI);
  gpio_set_function(st->sck_pin, GPIO_FUNC_SPI);
  spi_init(hw, st->settings.clock_hz);
  st->hw_configured = false;
  spi_apply_settings(idx, 8u);
}

static hal_status_t spi_ensure_initialized(uint8_t idx) {
  if (!s_spi[idx].initialized) {
    uint8_t rx = 0u;
    uint8_t tx = 0u;
    uint8_t sck = 0u;
    spi_default_pins(idx, &rx, &tx, &sck);
    return hal_spi_init(idx, rx, tx, sck);
  }
  return HAL_OK;
}

static uint8_t spi_transfer8_fast(uint8_t idx, uint8_t data) {
  spi_hw_t *hw = spi_get_hw(spi_hw(idx));
  while (!(hw->sr & SPI_SSPSR_TNF_BITS)) {
  }
  hw->dr = data;
  while (!(hw->sr & SPI_SSPSR_RNE_BITS)) {
  }
  return (uint8_t)hw->dr;
}

static void spi_wait_idle_and_drain_rx(uint8_t idx) {
  spi_hw_t *hw = spi_get_hw(spi_hw(idx));
  while (hw->sr & SPI_SSPSR_BSY_BITS) {
  }
  while (hw->sr & SPI_SSPSR_RNE_BITS) {
    (void)hw->dr;
  }
}

static bool spi_claim_dma_tx_channel(uint8_t idx) {
  hal_spi_bus_state_t *st = &s_spi[idx];
  if (st->dma_tx_channel_claimed) {
    return true;
  }

  const int channel = dma_claim_unused_channel(false);
  if (channel < 0) {
    return false;
  }

  st->dma_tx_channel = channel;
  st->dma_tx_channel_claimed = true;
  return true;
}

hal_status_t hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin,
                          uint8_t sck_pin) {
  if (bus > 1u) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  hal_spi_bus_state_t *st = &s_spi[idx];

  spi_ensure_mutex(idx);
  if (st->mutex == nullptr) {
    return HAL_ENOMEM;
  }
  st->rx_pin = rx_pin;
  st->tx_pin = tx_pin;
  st->sck_pin = sck_pin;
  st->settings = spi_default_settings();
  st->applied_settings = spi_default_settings();
  st->applied_data_bits = 0u;
  st->hw_configured = false;
  st->dma_tx_channel = st->dma_tx_channel_claimed ? st->dma_tx_channel : -1;
  st->transaction_active = false;
  st->initialized = true;
  spi_hw_init(idx);
  return HAL_OK;
}

void hal_spi_deinit(uint8_t bus) {
  const uint8_t idx = spi_bus_index(bus);
  (void)hal_spi_write_dma_async_wait(idx);
  if (s_spi[idx].dma_tx_channel_claimed) {
    dma_channel_unclaim(s_spi[idx].dma_tx_channel);
    s_spi[idx].dma_tx_channel = -1;
    s_spi[idx].dma_tx_channel_claimed = false;
  }
  spi_deinit(spi_hw(idx));
  s_spi[idx].hw_configured = false;
  s_spi[idx].applied_data_bits = 0u;
  s_spi[idx].transaction_active = false;
  s_spi[idx].initialized = false;
}

void hal_spi_lock(uint8_t bus) {
  const uint8_t idx = spi_bus_index(bus);
  spi_ensure_mutex(idx);
  hal_mutex_lock(s_spi[idx].mutex);
}

void hal_spi_unlock(uint8_t bus) {
  const uint8_t idx = spi_bus_index(bus);
  spi_ensure_mutex(idx);
  hal_mutex_unlock(s_spi[idx].mutex);
}

hal_status_t hal_spi_begin_transaction(uint8_t bus,
                                       const hal_spi_settings_t *settings) {
  if (bus > 1u || !spi_settings_valid(settings)) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  const hal_status_t status = spi_ensure_initialized(idx);
  if (hal_status_is_error(status)) {
    return status;
  }
  s_spi[idx].settings = spi_normalize_settings(settings);
  s_spi[idx].transaction_active = true;
  spi_apply_settings(idx, 8u);
  return HAL_OK;
}

hal_status_t hal_spi_end_transaction(uint8_t bus) {
  if (bus > 1u) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  const hal_status_t status = hal_spi_write_dma_async_wait_ex(idx);
  if (hal_status_is_error(status)) {
    return status;
  }
  if (s_spi[idx].initialized) {
    spi_wait_idle_and_drain_rx(idx);
  }
  s_spi[idx].transaction_active = false;
  return HAL_OK;
}

hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                 uint8_t *out_received) {
  if (bus > 1u || out_received == nullptr) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  const hal_status_t status = spi_ensure_initialized(idx);
  if (hal_status_is_error(status)) {
    return status;
  }
  spi_apply_settings(idx, 8u);

  const bool lsb_first = s_spi[idx].settings.bit_order == HAL_SPI_LSBFIRST;
  uint8_t tx = lsb_first ? spi_reverse8(data) : data;
  uint8_t rx = spi_transfer8_fast(idx, tx);
  *out_received = lsb_first ? spi_reverse8(rx) : rx;
  return HAL_OK;
}

hal_status_t jh_hal_spi_transfer16_provider(uint8_t bus, uint16_t data,
                                            uint16_t *out_received) {
  const uint8_t idx = spi_bus_index(bus);
  const hal_status_t status = spi_ensure_initialized(idx);
  if (hal_status_is_error(status)) {
    return status;
  }
  spi_apply_settings(idx, 16u);

  const bool lsb_first = s_spi[idx].settings.bit_order == HAL_SPI_LSBFIRST;
  uint16_t tx = lsb_first ? spi_reverse16(data) : data;
  uint16_t rx = 0xFFFFu;
  const int transferred =
      spi_write16_read16_blocking(spi_hw(idx), &tx, &rx, 1u);
  spi_apply_settings(idx, 8u);
  if (transferred != 1) {
    return HAL_EIO;
  }
  *out_received = lsb_first ? spi_reverse16(rx) : rx;
  return HAL_OK;
}

hal_status_t hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                                   size_t len) {
  if (bus > 1u || (len > 0u && tx == nullptr && rx == nullptr)) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }

  const uint8_t idx = spi_bus_index(bus);
  const hal_status_t init_status = spi_ensure_initialized(idx);
  if (hal_status_is_error(init_status)) {
    return init_status;
  }
  spi_apply_settings(idx, 8u);

  if (s_spi[idx].settings.bit_order == HAL_SPI_LSBFIRST) {
    for (size_t i = 0u; i < len; ++i) {
      const uint8_t out = tx ? tx[i] : 0xFFu;
      uint8_t tx_byte = spi_reverse8(out);
      uint8_t rx_byte = spi_transfer8_fast(idx, tx_byte);
      if (rx) {
        rx[i] = spi_reverse8(rx_byte);
      }
    }
    return HAL_OK;
  }

  spi_inst_t *hw = spi_hw(idx);
  int transferred = 0;
  if (tx && rx) {
    transferred = spi_write_read_blocking(hw, tx, rx, len);
  } else if (tx) {
    transferred = spi_write_blocking(hw, tx, len);
  } else if (rx) {
    transferred = spi_read_blocking(hw, 0xFFu, rx, len);
  }
  return transferred >= 0 && (size_t)transferred == len ? HAL_OK : HAL_EIO;
}

hal_status_t jh_hal_spi_write_provider(uint8_t bus, const uint8_t *data,
                                       size_t len) {
  return hal_spi_transfer_txrx(bus, data, nullptr, len);
}

hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus, const uint8_t *data,
                                              size_t len) {
  if (bus > 1u || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }

  const uint8_t idx = spi_bus_index(bus);
  const hal_status_t status = spi_ensure_initialized(idx);
  if (hal_status_is_error(status)) {
    return status;
  }
  spi_apply_settings(idx, 8u);

  if (s_spi[idx].dma_tx_active) {
    return HAL_EBUSY;
  }

  if (s_spi[idx].settings.bit_order == HAL_SPI_LSBFIRST) {
    return hal_spi_write(bus, data, len);
  }

  if (!spi_claim_dma_tx_channel(idx)) {
    return hal_spi_write(bus, data, len);
  }

  spi_inst_t *hw = spi_hw(idx);
  spi_hw_t *regs = spi_get_hw(hw);
  dma_channel_config config =
      dma_channel_get_default_config(s_spi[idx].dma_tx_channel);
  channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
  channel_config_set_read_increment(&config, true);
  channel_config_set_write_increment(&config, false);
  channel_config_set_dreq(&config, spi_get_dreq(hw, true));

  dma_channel_configure(s_spi[idx].dma_tx_channel, &config, &regs->dr, data,
                        len, true);
  s_spi[idx].dma_tx_active = true;
  return HAL_OK;
}

bool hal_spi_write_dma_async_busy(uint8_t bus) {
  const uint8_t idx = spi_bus_index(bus);
  if (!s_spi[idx].dma_tx_active || !s_spi[idx].dma_tx_channel_claimed) {
    return false;
  }
  if (dma_channel_is_busy(s_spi[idx].dma_tx_channel)) {
    return true;
  }
  spi_wait_idle_and_drain_rx(idx);
  s_spi[idx].dma_tx_active = false;
  return false;
}

hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus) {
  if (bus > 1u) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  if (!s_spi[idx].dma_tx_active || !s_spi[idx].dma_tx_channel_claimed) {
    return HAL_OK;
  }
  dma_channel_wait_for_finish_blocking(s_spi[idx].dma_tx_channel);
  spi_wait_idle_and_drain_rx(idx);
  s_spi[idx].dma_tx_active = false;
  return HAL_OK;
}

#endif // HAL_TARGET_IS_RP
