#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/i2c/hal_i2c.h"
#include "hal/i2c/hal_i2c_internal.h"
#include "hal_mock.h"

#include <string.h>

/* request_from() uses uint8_t count, so 255 is the maximum meaningful size. */
#define MOCK_I2C_BUF_SIZE 255

/* Write-frame capture: lets tests inspect exactly what a driver transmitted
 * (one frame = one begin_transmission..end_transmission). Sized for small
 * register-style writes; longer payloads are truncated to MOCK_I2C_WLOG_LEN. */
#define MOCK_I2C_WLOG_FRAMES 32
#define MOCK_I2C_WLOG_LEN 8

typedef struct {
  uint8_t rx_script[MOCK_I2C_BUF_SIZE];
  int rx_script_len;
  int rx_script_pos;
  uint8_t rx_buf[MOCK_I2C_BUF_SIZE];
  int rx_len;
  int rx_pos;
  uint8_t cur_addr;
  bool busy;
  bool scan_device_map_enabled;
  bool scan_device_present[128];
  bool initialized;
  uint32_t clock_hz;
  int lock_depth;
  int mutex_depth;
  uint32_t mutex_take_count;
  uint32_t mutex_give_count;
  int read_byte_lock_depth_at_read;
  uint32_t transaction_count;
  uint32_t bus_clear_count;
  /* Write-frame log (see MOCK_I2C_WLOG_* above). */
  uint8_t tx_buf[MOCK_I2C_WLOG_LEN];
  int tx_len;
  uint8_t wlog[MOCK_I2C_WLOG_FRAMES][MOCK_I2C_WLOG_LEN];
  int wlog_flen[MOCK_I2C_WLOG_FRAMES];
  int wlog_count;
} mock_i2c_bus_state_t;

static mock_i2c_bus_state_t s_i2c_state[2];

static inline bool i2c_bus_valid(uint8_t bus) { return bus <= 1u; }

static inline uint8_t i2c_bus_index(uint8_t bus) {
  HAL_ASSERT(bus <= 1u, "hal_i2c: invalid bus index");
  return (bus <= 1u) ? bus : 0u;
}

static inline mock_i2c_bus_state_t *i2c_state(uint8_t bus) {
  return &s_i2c_state[i2c_bus_index(bus)];
}

bool jh_hal_i2c_bus_is_initialized(uint8_t bus) {
  return i2c_bus_valid(bus) && i2c_state(bus)->initialized;
}

static void mock_i2c_load_rx(mock_i2c_bus_state_t *st, uint8_t count) {
  st->rx_len = count;
  st->rx_pos = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (st->rx_script_pos < st->rx_script_len) {
      st->rx_buf[i] = st->rx_script[st->rx_script_pos++];
    } else {
      st->rx_buf[i] = 0u;
    }
  }
}

static void mock_i2c_commit_write_frame(mock_i2c_bus_state_t *st) {
  if (st->wlog_count < MOCK_I2C_WLOG_FRAMES) {
    int n = (st->tx_len < MOCK_I2C_WLOG_LEN) ? st->tx_len : MOCK_I2C_WLOG_LEN;
    for (int i = 0; i < n; ++i) {
      st->wlog[st->wlog_count][i] = st->tx_buf[i];
    }
    st->wlog_flen[st->wlog_count] = n;
    st->wlog_count++;
  }
}

hal_status_t hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
  return hal_i2c_init_bus(0, sda_pin, scl_pin, clock_hz);
}

hal_status_t hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                              uint32_t clock_hz) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  mock_i2c_bus_state_t *st = i2c_state(bus);
  (void)sda_pin;
  (void)scl_pin;
  st->initialized = true;
  st->clock_hz = clock_hz;
  st->rx_script_len = 0;
  st->rx_script_pos = 0;
  st->rx_len = 0;
  st->rx_pos = 0;
  st->lock_depth = 0;
  st->mutex_depth = 0;
  st->mutex_take_count = 0;
  st->mutex_give_count = 0;
  st->read_byte_lock_depth_at_read = 0;
  st->transaction_count = 0;
  st->bus_clear_count = 0;
  st->scan_device_map_enabled = false;
  memset(st->scan_device_present, 0, sizeof(st->scan_device_present));
  st->tx_len = 0;
  st->wlog_count = 0;
  return HAL_OK;
}

hal_status_t hal_i2c_set_clock(uint32_t clock_hz) {
  return hal_i2c_set_clock_bus(0, clock_hz);
}

hal_status_t hal_i2c_set_clock_bus(uint8_t bus, uint32_t clock_hz) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  i2c_state(bus)->clock_hz = clock_hz;
  return HAL_OK;
}

void hal_i2c_deinit(void) { hal_i2c_deinit_bus(0); }

void hal_i2c_deinit_bus(uint8_t bus) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  st->initialized = false;
  st->lock_depth = 0;
  st->mutex_depth = 0;
  st->mutex_take_count = 0;
  st->mutex_give_count = 0;
  st->read_byte_lock_depth_at_read = 0;
  st->rx_script_len = 0;
  st->rx_script_pos = 0;
  st->rx_len = 0;
  st->rx_pos = 0;
  st->cur_addr = 0;
  st->busy = false;
  st->tx_len = 0;
  st->wlog_count = 0;
}

void hal_i2c_lock(void) { hal_i2c_lock_bus(0); }

void hal_i2c_unlock(void) { hal_i2c_unlock_bus(0); }

void hal_i2c_lock_bus(uint8_t bus) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  if (st->lock_depth == 0) {
    st->mutex_depth = 1;
    st->mutex_take_count++;
  }
  st->lock_depth++;
}

void hal_i2c_unlock_bus(uint8_t bus) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  if (st->lock_depth > 0) {
    st->lock_depth--;
    if (st->lock_depth == 0) {
      st->mutex_depth = 0;
      st->mutex_give_count++;
    }
  }
}

void hal_i2c_begin_transmission(uint8_t address) {
  hal_i2c_begin_transmission_bus(0, address);
}

void hal_i2c_begin_transmission_bus(uint8_t bus, uint8_t address) {
  hal_i2c_lock_bus(bus);
  mock_i2c_bus_state_t *st = i2c_state(bus);
  st->cur_addr = address;
  st->tx_len = 0; /* start a fresh write frame */
}

size_t hal_i2c_write(uint8_t data) { return hal_i2c_write_bus(0, data); }

size_t hal_i2c_write_bus(uint8_t bus, uint8_t data) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  if (st->tx_len < MOCK_I2C_WLOG_LEN) {
    st->tx_buf[st->tx_len] = data;
  }
  st->tx_len++; /* count even past the cap so truncation is observable */
  return 1;
}

hal_status_t hal_i2c_end_transmission_ex(void) {
  return hal_i2c_end_transmission_bus_ex(0);
}

hal_status_t hal_i2c_end_transmission_bus_ex(uint8_t bus) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  mock_i2c_bus_state_t *st = i2c_state(bus);
  st->transaction_count++;
  mock_i2c_commit_write_frame(st);
  hal_i2c_unlock_bus(bus);
  if (st->scan_device_map_enabled && st->tx_len == 0 && st->cur_addr < 128u) {
    return st->scan_device_present[st->cur_addr] ? HAL_OK : HAL_EBUS;
  }
  return st->busy ? HAL_EBUS : HAL_OK;
}

hal_status_t hal_i2c_write_read_bus_ex(uint8_t bus, uint8_t address,
                                       const uint8_t *tx, size_t tx_len,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) || (tx_len > 0u && tx == NULL) ||
      (rx_len > 0u && rx == NULL) || tx_len > MOCK_I2C_BUF_SIZE ||
      rx_len > MOCK_I2C_BUF_SIZE) {
    return HAL_EINVAL;
  }
  mock_i2c_bus_state_t *st = i2c_state(bus);
  hal_i2c_lock_bus(bus);
  st->cur_addr = address;
  st->tx_len = 0;
  for (size_t i = 0; i < tx_len; ++i) {
    if (hal_i2c_write_bus(bus, tx[i]) != 1u) {
      hal_i2c_unlock_bus(bus);
      return HAL_EIO;
    }
  }
  st->transaction_count++;
  mock_i2c_commit_write_frame(st);
  if (st->busy) {
    hal_i2c_unlock_bus(bus);
    return HAL_EBUS;
  }

  if (rx_len == 0u) {
    hal_i2c_unlock_bus(bus);
    return HAL_OK;
  }

  mock_i2c_load_rx(st, (uint8_t)rx_len);
  st->transaction_count++;
  for (size_t i = 0; i < rx_len; ++i) {
    if (st->rx_pos >= st->rx_len) {
      hal_i2c_unlock_bus(bus);
      return HAL_EBUS;
    }
    st->read_byte_lock_depth_at_read = st->lock_depth;
    rx[i] = st->rx_buf[st->rx_pos++];
  }
  hal_i2c_unlock_bus(bus);
  return HAL_OK;
}

static bool i2c_read_bytes_bus_impl(uint8_t bus, uint8_t address, uint8_t *rx,
                                    size_t rx_len);

hal_status_t hal_i2c_read_bytes_bus_ex(uint8_t bus, uint8_t address,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) || (rx_len > 0u && rx == NULL) ||
      rx_len > MOCK_I2C_BUF_SIZE) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(i2c_read_bytes_bus_impl(bus, address, rx, rx_len),
                              HAL_EBUS);
}

static bool i2c_read_bytes_bus_impl(uint8_t bus, uint8_t address, uint8_t *rx,
                                    size_t rx_len) {
  if ((rx_len > 0u && rx == NULL) || rx_len > MOCK_I2C_BUF_SIZE) {
    return false;
  }
  if (rx_len == 0u) {
    return true;
  }

  mock_i2c_bus_state_t *st = i2c_state(bus);
  hal_i2c_lock_bus(bus);
  (void)address;
  mock_i2c_load_rx(st, (uint8_t)rx_len);
  st->transaction_count++;
  for (size_t i = 0; i < rx_len; ++i) {
    if (st->rx_pos >= st->rx_len) {
      hal_i2c_unlock_bus(bus);
      return false;
    }
    st->read_byte_lock_depth_at_read = st->lock_depth;
    rx[i] = st->rx_buf[st->rx_pos++];
  }
  hal_i2c_unlock_bus(bus);
  return true;
}

static uint8_t i2c_request_from_bus_impl(uint8_t bus, uint8_t address,
                                         uint8_t count);

hal_status_t hal_i2c_request_from_bus_ex(uint8_t bus, uint8_t address,
                                         uint8_t count, uint8_t *outReceived) {
  if (outReceived == NULL || !i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  *outReceived = i2c_request_from_bus_impl(bus, address, count);
  return (*outReceived == count) ? HAL_OK : HAL_EBUS;
}

static uint8_t i2c_request_from_bus_impl(uint8_t bus, uint8_t address,
                                         uint8_t count) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  hal_i2c_lock_bus(bus);
  (void)address;
  mock_i2c_load_rx(st, count);
  st->transaction_count++;
  hal_i2c_unlock_bus(bus);
  return count;
}

int hal_i2c_available(void) { return hal_i2c_available_bus(0); }

int hal_i2c_available_bus(uint8_t bus) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  return st->rx_len - st->rx_pos;
}

int hal_i2c_read(void) { return hal_i2c_read_bus(0); }

int hal_i2c_read_bus(uint8_t bus) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  if (st->rx_pos < st->rx_len)
    return st->rx_buf[st->rx_pos++];
  return -1;
}

/* ── Mock helpers ─────────────────────────────────────────────────────── */

void hal_mock_i2c_inject_rx(const uint8_t *data, int len) {
  hal_mock_i2c_inject_rx_bus(0, data, len);
}

void hal_mock_i2c_inject_rx_bus(uint8_t bus, const uint8_t *data, int len) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  if (len < 0)
    len = 0;
  if (len > MOCK_I2C_BUF_SIZE)
    len = MOCK_I2C_BUF_SIZE;
  if (len > 0 && data != NULL) {
    memcpy(st->rx_script, data, len);
  }
  st->rx_script_len = len;
  st->rx_script_pos = 0;
  st->rx_len = 0;
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

int hal_mock_i2c_get_mutex_depth_bus(uint8_t bus) {
  return i2c_state(bus)->mutex_depth;
}

int hal_mock_i2c_get_mutex_depth(void) {
  return hal_mock_i2c_get_mutex_depth_bus(0);
}

uint32_t hal_mock_i2c_get_mutex_take_count_bus(uint8_t bus) {
  return i2c_state(bus)->mutex_take_count;
}

uint32_t hal_mock_i2c_get_mutex_take_count(void) {
  return hal_mock_i2c_get_mutex_take_count_bus(0);
}

uint32_t hal_mock_i2c_get_mutex_give_count_bus(uint8_t bus) {
  return i2c_state(bus)->mutex_give_count;
}

uint32_t hal_mock_i2c_get_mutex_give_count(void) {
  return hal_mock_i2c_get_mutex_give_count_bus(0);
}

int hal_mock_i2c_get_read_byte_lock_depth_bus(uint8_t bus) {
  return i2c_state(bus)->read_byte_lock_depth_at_read;
}

int hal_mock_i2c_get_read_byte_lock_depth(void) {
  return hal_mock_i2c_get_read_byte_lock_depth_bus(0);
}

hal_status_t hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin) {
  return hal_i2c_bus_clear_bus(0, sda_pin, scl_pin);
}

hal_status_t hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  (void)sda_pin;
  (void)scl_pin;
  i2c_state(bus)->bus_clear_count++;
  return HAL_OK;
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

uint32_t hal_mock_i2c_get_clock_hz_bus(uint8_t bus) {
  return i2c_state(bus)->clock_hz;
}

uint32_t hal_mock_i2c_get_clock_hz(void) {
  return hal_mock_i2c_get_clock_hz_bus(0);
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

void hal_mock_i2c_set_busy(bool busy) { hal_mock_i2c_set_busy_bus(0, busy); }

void hal_mock_i2c_set_busy_bus(uint8_t bus, bool busy) {
  i2c_state(bus)->busy = busy;
}

void hal_mock_i2c_set_device_present(uint8_t address, bool present) {
  hal_mock_i2c_set_device_present_bus(0, address, present);
}

void hal_mock_i2c_set_device_present_bus(uint8_t bus, uint8_t address,
                                         bool present) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  if (address < 128u) {
    st->scan_device_map_enabled = true;
    st->scan_device_present[address] = present;
  }
}

/* ── Write-frame log inspection ───────────────────────────────────────── */

void hal_mock_i2c_reset_write_log(void) { hal_mock_i2c_reset_write_log_bus(0); }

void hal_mock_i2c_reset_write_log_bus(uint8_t bus) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  st->wlog_count = 0;
  st->tx_len = 0;
}

int hal_mock_i2c_get_write_frame_count(void) {
  return hal_mock_i2c_get_write_frame_count_bus(0);
}

int hal_mock_i2c_get_write_frame_count_bus(uint8_t bus) {
  return i2c_state(bus)->wlog_count;
}

int hal_mock_i2c_get_write_frame(int index, uint8_t *out, int max) {
  return hal_mock_i2c_get_write_frame_bus(0, index, out, max);
}

int hal_mock_i2c_get_write_frame_bus(uint8_t bus, int index, uint8_t *out,
                                     int max) {
  mock_i2c_bus_state_t *st = i2c_state(bus);
  if (index < 0 || index >= st->wlog_count) {
    return -1;
  }
  int n = st->wlog_flen[index];
  if (out != NULL) {
    int copy = (n < max) ? n : max;
    for (int i = 0; i < copy; ++i) {
      out[i] = st->wlog[index][i];
    }
  }
  return n;
}

uint32_t hal_i2c_get_transaction_count(void) {
  return hal_i2c_get_transaction_count_bus(0);
}

uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus) {
  return i2c_state(bus)->transaction_count;
}
#endif // HAL_TARGET_IS_MOCK
