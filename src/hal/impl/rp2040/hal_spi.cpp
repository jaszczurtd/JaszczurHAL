#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040

#include "../../hal_config.h"
#include "../../hal_spi.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"

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

static hal_spi_settings_t spi_default_settings(void) {
  hal_spi_settings_t s = {HAL_SPI_CLOCK_DEFAULT_HZ, HAL_SPI_MSBFIRST,
                          HAL_SPI_MODE0};
  return s;
}

static hal_spi_settings_t
spi_normalize_settings(const hal_spi_settings_t *settings) {
  hal_spi_settings_t s = settings ? *settings : spi_default_settings();
  if (s.clock_hz == 0u) {
    s.clock_hz = HAL_SPI_CLOCK_DEFAULT_HZ;
  }
  if (s.bit_order != HAL_SPI_LSBFIRST) {
    s.bit_order = HAL_SPI_MSBFIRST;
  }
  if (s.data_mode > HAL_SPI_MODE3) {
    s.data_mode = HAL_SPI_MODE0;
  }
  return s;
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

static void spi_ensure_initialized(uint8_t idx) {
  if (!s_spi[idx].initialized) {
    uint8_t rx = 0u;
    uint8_t tx = 0u;
    uint8_t sck = 0u;
    spi_default_pins(idx, &rx, &tx, &sck);
    hal_spi_init(idx, rx, tx, sck);
  }
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

void hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin,
                  uint8_t sck_pin) {
  const uint8_t idx = spi_bus_index(bus);
  hal_spi_bus_state_t *st = &s_spi[idx];

  spi_ensure_mutex(idx);
  st->rx_pin = rx_pin;
  st->tx_pin = tx_pin;
  st->sck_pin = sck_pin;
  st->settings = spi_default_settings();
  st->applied_settings = spi_default_settings();
  st->applied_data_bits = 0u;
  st->hw_configured = false;
  st->transaction_active = false;
  st->initialized = true;
  spi_hw_init(idx);
}

void hal_spi_deinit(uint8_t bus) {
  const uint8_t idx = spi_bus_index(bus);
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

void hal_spi_begin_transaction(uint8_t bus,
                               const hal_spi_settings_t *settings) {
  const uint8_t idx = spi_bus_index(bus);
  spi_ensure_initialized(idx);
  s_spi[idx].settings = spi_normalize_settings(settings);
  s_spi[idx].transaction_active = true;
  spi_apply_settings(idx, 8u);
}

void hal_spi_end_transaction(uint8_t bus) {
  const uint8_t idx = spi_bus_index(bus);
  if (s_spi[idx].initialized) {
    spi_hw_t *hw = spi_get_hw(spi_hw(idx));
    while (hw->sr & SPI_SSPSR_BSY_BITS) {
    }
  }
  s_spi[idx].transaction_active = false;
}

uint8_t hal_spi_transfer(uint8_t bus, uint8_t data) {
  const uint8_t idx = spi_bus_index(bus);
  spi_ensure_initialized(idx);
  spi_apply_settings(idx, 8u);

  const bool lsb_first = s_spi[idx].settings.bit_order == HAL_SPI_LSBFIRST;
  uint8_t tx = lsb_first ? spi_reverse8(data) : data;
  uint8_t rx = spi_transfer8_fast(idx, tx);
  return lsb_first ? spi_reverse8(rx) : rx;
}

uint16_t hal_spi_transfer16(uint8_t bus, uint16_t data) {
  const uint8_t idx = spi_bus_index(bus);
  spi_ensure_initialized(idx);
  spi_apply_settings(idx, 16u);

  const bool lsb_first = s_spi[idx].settings.bit_order == HAL_SPI_LSBFIRST;
  uint16_t tx = lsb_first ? spi_reverse16(data) : data;
  uint16_t rx = 0xFFFFu;
  (void)spi_write16_read16_blocking(spi_hw(idx), &tx, &rx, 1u);
  spi_apply_settings(idx, 8u);
  return lsb_first ? spi_reverse16(rx) : rx;
}

void hal_spi_transfer_buffer(uint8_t bus, uint8_t *buffer, size_t len) {
  if (buffer == nullptr || len == 0u) {
    return;
  }
  hal_spi_transfer_txrx(bus, buffer, buffer, len);
}

void hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                           size_t len) {
  if (len == 0u) {
    return;
  }

  const uint8_t idx = spi_bus_index(bus);
  spi_ensure_initialized(idx);
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
    return;
  }

  spi_inst_t *hw = spi_hw(idx);
  if (tx && rx) {
    (void)spi_write_read_blocking(hw, tx, rx, len);
  } else if (tx) {
    (void)spi_write_blocking(hw, tx, len);
  } else if (rx) {
    (void)spi_read_blocking(hw, 0xFFu, rx, len);
  } else {
    for (size_t i = 0u; i < len; ++i) {
      const uint8_t dummy = 0xFFu;
      (void)spi_write_blocking(hw, &dummy, 1u);
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
