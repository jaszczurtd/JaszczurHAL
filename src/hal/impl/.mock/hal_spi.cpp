#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#include "../../hal_spi.h"
#include "hal_mock.h"

#include <string.h>

#define MOCK_SPI_BUF_SIZE 512u

typedef struct {
  bool initialized;
  bool transaction_active;
  uint8_t rx_pin;
  uint8_t tx_pin;
  uint8_t sck_pin;
  hal_spi_settings_t settings;
  int lock_depth;
  uint8_t rx_script[MOCK_SPI_BUF_SIZE];
  size_t rx_len;
  size_t rx_pos;
  uint8_t tx_log[MOCK_SPI_BUF_SIZE];
  size_t tx_len;
  uint32_t transfer_count;
  uint32_t dma_write_count;
  bool fail_next_begin;
  bool fail_next_end;
  bool fail_next_write;
  bool fail_next_dma_write;
} mock_spi_bus_t;

static uint8_t s_last_bus = 0;
static mock_spi_bus_t s_spi[2] = {};

static inline uint8_t spi_bus_index(uint8_t bus) {
  HAL_ASSERT(bus <= 1u, "hal_spi: invalid bus index");
  return (bus <= 1u) ? bus : 0u;
}

static bool spi_settings_valid(const hal_spi_settings_t *settings) {
  return settings == nullptr || ((settings->bit_order == HAL_SPI_LSBFIRST ||
                                  settings->bit_order == HAL_SPI_MSBFIRST) &&
                                 settings->data_mode <= HAL_SPI_MODE3);
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

static void spi_log_tx(mock_spi_bus_t *st, uint8_t data) {
  if (st->tx_len < MOCK_SPI_BUF_SIZE) {
    st->tx_log[st->tx_len++] = data;
  }
}

static uint8_t spi_next_rx(mock_spi_bus_t *st) {
  if (st->rx_pos < st->rx_len) {
    return st->rx_script[st->rx_pos++];
  }
  return 0xFFu;
}

hal_status_t hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin,
                          uint8_t sck_pin) {
  if (bus > 1u) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  mock_spi_bus_t *st = &s_spi[idx];
  s_last_bus = idx;
  st->rx_pin = rx_pin;
  st->tx_pin = tx_pin;
  st->sck_pin = sck_pin;
  st->settings = spi_normalize_settings(nullptr);
  st->initialized = true;
  return HAL_OK;
}

void hal_spi_deinit(uint8_t bus) {
  s_spi[spi_bus_index(bus)].initialized = false;
}

void hal_spi_lock(uint8_t bus) {
  uint8_t idx = spi_bus_index(bus);
  s_spi[idx].lock_depth++;
}

void hal_spi_unlock(uint8_t bus) {
  uint8_t idx = spi_bus_index(bus);
  if (s_spi[idx].lock_depth > 0) {
    s_spi[idx].lock_depth--;
  }
}

hal_status_t hal_spi_begin_transaction(uint8_t bus,
                                       const hal_spi_settings_t *settings) {
  if (bus > 1u || !spi_settings_valid(settings)) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  if (s_spi[idx].fail_next_begin) {
    s_spi[idx].fail_next_begin = false;
    return HAL_EIO;
  }
  if (!s_spi[idx].initialized) {
    const hal_status_t status = hal_spi_init(idx, 0u, 0u, 0u);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
  s_last_bus = idx;
  s_spi[idx].settings = spi_normalize_settings(settings);
  s_spi[idx].transaction_active = true;
  return HAL_OK;
}

hal_status_t hal_spi_end_transaction(uint8_t bus) {
  if (bus > 1u) {
    return HAL_EINVAL;
  }
  mock_spi_bus_t *st = &s_spi[spi_bus_index(bus)];
  st->transaction_active = false;
  if (st->fail_next_end) {
    st->fail_next_end = false;
    return HAL_EIO;
  }
  return HAL_OK;
}

hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                 uint8_t *out_received) {
  if (bus > 1u || out_received == nullptr) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  if (!s_spi[idx].initialized) {
    const hal_status_t status = hal_spi_init(idx, 0u, 0u, 0u);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
  mock_spi_bus_t *st = &s_spi[idx];
  spi_log_tx(st, data);
  st->transfer_count++;
  *out_received = spi_next_rx(st);
  return HAL_OK;
}

uint8_t hal_spi_transfer(uint8_t bus, uint8_t data) {
  uint8_t received = 0xFFu;
  (void)hal_spi_transfer_ex(bus, data, &received);
  return received;
}

hal_status_t hal_spi_transfer16_ex(uint8_t bus, uint16_t data,
                                   uint16_t *out_received) {
  if (bus > 1u || out_received == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_spi[bus].initialized) {
    const hal_status_t init_status = hal_spi_init(bus, 0u, 0u, 0u);
    if (hal_status_is_error(init_status)) {
      return init_status;
    }
  }
  mock_spi_bus_t *st = &s_spi[spi_bus_index(bus)];
  uint16_t in = 0u;
  uint8_t first = 0u;
  uint8_t second = 0u;
  hal_status_t status;
  if (st->settings.bit_order == HAL_SPI_LSBFIRST) {
    status = hal_spi_transfer_ex(bus, (uint8_t)(data & 0xFFu), &first);
    if (hal_status_is_error(status)) {
      return status;
    }
    status = hal_spi_transfer_ex(bus, (uint8_t)(data >> 8), &second);
    in = (uint16_t)(first | ((uint16_t)second << 8));
  } else {
    status = hal_spi_transfer_ex(bus, (uint8_t)(data >> 8), &first);
    if (hal_status_is_error(status)) {
      return status;
    }
    status = hal_spi_transfer_ex(bus, (uint8_t)(data & 0xFFu), &second);
    in = (uint16_t)(((uint16_t)first << 8) | second);
  }
  if (hal_status_is_error(status)) {
    return status;
  }
  *out_received = in;
  return HAL_OK;
}

uint16_t hal_spi_transfer16(uint8_t bus, uint16_t data) {
  uint16_t received = 0xFFFFu;
  (void)hal_spi_transfer16_ex(bus, data, &received);
  return received;
}

hal_status_t hal_spi_transfer_buffer(uint8_t bus, uint8_t *buffer, size_t len) {
  if (bus > 1u || (len > 0u && buffer == nullptr)) {
    return HAL_EINVAL;
  }
  return hal_spi_transfer_txrx(bus, buffer, buffer, len);
}

hal_status_t hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                                   size_t len) {
  if (bus > 1u || (len > 0u && tx == nullptr && rx == nullptr)) {
    return HAL_EINVAL;
  }
  for (size_t i = 0; i < len; ++i) {
    const uint8_t out = tx ? tx[i] : 0xFFu;
    uint8_t in = 0u;
    const hal_status_t status = hal_spi_transfer_ex(bus, out, &in);
    if (hal_status_is_error(status)) {
      return status;
    }
    if (rx) {
      rx[i] = in;
    }
  }
  return HAL_OK;
}

hal_status_t hal_spi_write(uint8_t bus, const uint8_t *data, size_t len) {
  if (bus > 1u || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  mock_spi_bus_t *st = &s_spi[spi_bus_index(bus)];
  if (st->fail_next_write) {
    st->fail_next_write = false;
    return HAL_EIO;
  }
  return hal_spi_transfer_txrx(bus, data, nullptr, len);
}

hal_status_t hal_spi_write_dma_ex(uint8_t bus, const uint8_t *data,
                                  size_t len) {
  hal_status_t status = hal_spi_write_dma_async_start_ex(bus, data, len);
  if (hal_status_is_error(status)) {
    return status;
  }
  return hal_spi_write_dma_async_wait_ex(bus);
}

bool hal_spi_write_dma(uint8_t bus, const uint8_t *data, size_t len) {
  return hal_status_to_bool(hal_spi_write_dma_ex(bus, data, len));
}

hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus, const uint8_t *data,
                                              size_t len) {
  if (bus > 1u || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  if (len == 0u) {
    return HAL_OK;
  }
  s_spi[idx].dma_write_count++;
  if (s_spi[idx].fail_next_dma_write) {
    s_spi[idx].fail_next_dma_write = false;
    return HAL_EIO;
  }
  return hal_spi_write(bus, data, len);
}

bool hal_spi_write_dma_async_start(uint8_t bus, const uint8_t *data,
                                   size_t len) {
  return hal_status_to_bool(hal_spi_write_dma_async_start_ex(bus, data, len));
}

bool hal_spi_write_dma_async_busy(uint8_t bus) {
  (void)bus;
  return false;
}

hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus) {
  return bus <= 1u ? HAL_OK : HAL_EINVAL;
}

bool hal_spi_write_dma_async_wait(uint8_t bus) {
  return hal_status_to_bool(hal_spi_write_dma_async_wait_ex(bus));
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

bool hal_mock_spi_is_initialized(void) { return s_spi[s_last_bus].initialized; }
uint8_t hal_mock_spi_get_bus(void) { return s_last_bus; }
uint8_t hal_mock_spi_get_rx_pin(void) { return s_spi[s_last_bus].rx_pin; }
uint8_t hal_mock_spi_get_tx_pin(void) { return s_spi[s_last_bus].tx_pin; }
uint8_t hal_mock_spi_get_sck_pin(void) { return s_spi[s_last_bus].sck_pin; }
int hal_mock_spi_get_lock_depth(uint8_t bus) {
  return s_spi[spi_bus_index(bus)].lock_depth;
}

bool hal_mock_spi_transaction_active(uint8_t bus) {
  return s_spi[spi_bus_index(bus)].transaction_active;
}

uint32_t hal_mock_spi_get_clock_hz(uint8_t bus) {
  return s_spi[spi_bus_index(bus)].settings.clock_hz;
}

uint8_t hal_mock_spi_get_bit_order(uint8_t bus) {
  return s_spi[spi_bus_index(bus)].settings.bit_order;
}

uint8_t hal_mock_spi_get_data_mode(uint8_t bus) {
  return s_spi[spi_bus_index(bus)].settings.data_mode;
}

uint32_t hal_mock_spi_get_transfer_count(uint8_t bus) {
  return s_spi[spi_bus_index(bus)].transfer_count;
}

uint32_t hal_mock_spi_get_dma_write_count(uint8_t bus) {
  return s_spi[spi_bus_index(bus)].dma_write_count;
}

void hal_mock_spi_fail_next_write(uint8_t bus, bool fail) {
  s_spi[spi_bus_index(bus)].fail_next_write = fail;
}

void hal_mock_spi_fail_next_begin(uint8_t bus, bool fail) {
  s_spi[spi_bus_index(bus)].fail_next_begin = fail;
}

void hal_mock_spi_fail_next_end(uint8_t bus, bool fail) {
  s_spi[spi_bus_index(bus)].fail_next_end = fail;
}

void hal_mock_spi_fail_next_dma_write(uint8_t bus, bool fail) {
  s_spi[spi_bus_index(bus)].fail_next_dma_write = fail;
}

void hal_mock_spi_push_rx(uint8_t bus, const uint8_t *data, size_t len) {
  mock_spi_bus_t *st = &s_spi[spi_bus_index(bus)];
  st->rx_len = 0u;
  st->rx_pos = 0u;
  if (data == NULL) {
    return;
  }
  if (len > MOCK_SPI_BUF_SIZE) {
    len = MOCK_SPI_BUF_SIZE;
  }
  memcpy(st->rx_script, data, len);
  st->rx_len = len;
}

size_t hal_mock_spi_get_tx(uint8_t bus, uint8_t *out, size_t max_len) {
  mock_spi_bus_t *st = &s_spi[spi_bus_index(bus)];
  const size_t n = st->tx_len < max_len ? st->tx_len : max_len;
  if (out != NULL && n > 0u) {
    memcpy(out, st->tx_log, n);
  }
  return st->tx_len;
}

void hal_mock_spi_reset(void) {
  s_last_bus = 0;
  for (uint8_t i = 0; i < 2u; ++i) {
    memset(&s_spi[i], 0, sizeof(s_spi[i]));
    s_spi[i].settings = spi_default_settings();
  }
}
#endif // HAL_TARGET_IS_MOCK
