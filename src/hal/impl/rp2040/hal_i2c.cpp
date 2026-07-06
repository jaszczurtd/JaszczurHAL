#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_I2C

#include "../../hal_i2c.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"

#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <pico/error.h>
#include <pico/platform.h>
#include <pico/time.h>
#include <stdint.h>

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

#define RP2040_I2C_BUF_SIZE 255u
#define RP2040_I2C_TIMEOUT_US 100000u

typedef struct {
  uint8_t rx_buf[RP2040_I2C_BUF_SIZE];
  size_t rx_len;
  size_t rx_pos;
  uint8_t tx_buf[RP2040_I2C_BUF_SIZE];
  size_t tx_len;
  uint8_t cur_addr;
  uint8_t sda_pin;
  uint8_t scl_pin;
  uint32_t clock_hz;
  uint32_t actual_clock_hz;
  bool initialized;
  hal_mutex_t mutex;
  volatile uintptr_t lock_owner;
  volatile uint32_t lock_depth;
  volatile uint32_t transaction_count;
} i2c_bus_state_t;

static i2c_bus_state_t s_i2c[2] = {};

static inline bool i2c_bus_valid(uint8_t bus) { return bus <= 1u; }

static inline uint8_t i2c_bus_index(uint8_t bus) {
  HAL_ASSERT(bus <= 1u, "hal_i2c: invalid bus index");
  return (bus <= 1u) ? bus : 0u;
}

static inline i2c_inst_t *i2c_bus_hw(uint8_t bus) {
  return i2c_bus_index(bus) == 1u ? i2c1 : i2c0;
}

static inline i2c_bus_state_t *i2c_state(uint8_t bus) {
  return &s_i2c[i2c_bus_index(bus)];
}

static void i2c_ensure_mutex(uint8_t bus) {
  uint8_t idx = i2c_bus_index(bus);
  (void)jh_hal_mutex_create_once(&s_i2c[idx].mutex);
}

static uintptr_t i2c_current_owner_token(void) {
#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  return (task != NULL) ? (uintptr_t)task
                        : (UINTPTR_MAX - (uintptr_t)get_core_num());
#else
  return (uintptr_t)(get_core_num() + 1u);
#endif
}

static void i2c_lock_idx(uint8_t idx) {
  i2c_ensure_mutex(idx);
  const uintptr_t owner = i2c_current_owner_token();
  if ((s_i2c[idx].lock_depth > 0u) && (s_i2c[idx].lock_owner == owner)) {
    s_i2c[idx].lock_depth++;
    return;
  }

  hal_mutex_lock(s_i2c[idx].mutex);
  s_i2c[idx].lock_owner = owner;
  s_i2c[idx].lock_depth = 1u;
}

static void i2c_unlock_idx(uint8_t idx) {
  i2c_ensure_mutex(idx);
  const uintptr_t owner = i2c_current_owner_token();
  HAL_ASSERT((s_i2c[idx].lock_depth > 0u) && (s_i2c[idx].lock_owner == owner),
             "hal_i2c_unlock: bus is not locked by this context");
  if ((s_i2c[idx].lock_depth == 0u) || (s_i2c[idx].lock_owner != owner)) {
    return;
  }

  s_i2c[idx].lock_depth--;
  if (s_i2c[idx].lock_depth == 0u) {
    s_i2c[idx].lock_owner = 0u;
    hal_mutex_unlock(s_i2c[idx].mutex);
  }
}

static uint32_t i2c_normalize_clock(uint32_t clock_hz) {
  if (clock_hz == 0u) {
    return HAL_I2C_CLOCK_STANDARD_HZ;
  }
  if (clock_hz > HAL_I2C_CLOCK_FAST_PLUS_HZ) {
    return HAL_I2C_CLOCK_FAST_PLUS_HZ;
  }
  return clock_hz;
}

static void i2c_hw_configure_pins(uint8_t sda_pin, uint8_t scl_pin) {
  gpio_set_function(sda_pin, GPIO_FUNC_I2C);
  gpio_set_function(scl_pin, GPIO_FUNC_I2C);
  gpio_pull_up(sda_pin);
  gpio_pull_up(scl_pin);
}

static void i2c_hw_init_bus(uint8_t idx) {
  i2c_bus_state_t *st = &s_i2c[idx];
  i2c_hw_configure_pins(st->sda_pin, st->scl_pin);
  st->actual_clock_hz = i2c_init(i2c_bus_hw(idx), st->clock_hz);
}

static bool i2c_ensure_initialized(uint8_t idx) {
  if (!s_i2c[idx].initialized) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return false;
  }
  return true;
}

static uint8_t i2c_result_from_write_rc(int rc, size_t expected_len) {
  if (rc == (int)expected_len) {
    return HAL_I2C_RESULT_OK;
  }
  if (rc == PICO_ERROR_TIMEOUT) {
    return HAL_I2C_ERROR_TIMEOUT;
  }
  if (rc == PICO_ERROR_GENERIC) {
    return HAL_I2C_ERROR_GENERIC;
  }
  return HAL_I2C_ERROR_OTHER;
}

static hal_status_t i2c_status_from_result(uint8_t result) {
  switch (result) {
  case HAL_I2C_RESULT_OK:
    return HAL_OK;
  case HAL_I2C_ERROR_TIMEOUT:
    return HAL_ETIMEOUT;
  case HAL_I2C_ERROR_GENERIC:
    return HAL_EBUS;
  case HAL_I2C_ERROR_OTHER:
  default:
    return HAL_EIO;
  }
}

static uint8_t i2c_probe_read(uint8_t idx, uint8_t address) {
  uint8_t dummy = 0u;
  int rc = i2c_read_timeout_us(i2c_bus_hw(idx), address, &dummy, 1u, false,
                               RP2040_I2C_TIMEOUT_US);
  if (rc == 1) {
    return HAL_I2C_RESULT_OK;
  }
  if (rc == PICO_ERROR_TIMEOUT) {
    return HAL_I2C_ERROR_TIMEOUT;
  }
  return HAL_I2C_ERROR_GENERIC;
}

hal_status_t hal_i2c_init_ex(uint8_t sda_pin, uint8_t scl_pin,
                             uint32_t clock_hz) {
  return hal_i2c_init_bus_ex(0, sda_pin, scl_pin, clock_hz);
}

void hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
  (void)hal_i2c_init_ex(sda_pin, scl_pin, clock_hz);
}

hal_status_t hal_i2c_init_bus_ex(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                                 uint32_t clock_hz) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  hal_i2c_init_bus(bus, sda_pin, scl_pin, clock_hz);
  return HAL_OK;
}

void hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                      uint32_t clock_hz) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_ensure_mutex(idx);

  i2c_bus_state_t *st = &s_i2c[idx];
  st->rx_len = 0u;
  st->rx_pos = 0u;
  st->tx_len = 0u;
  st->cur_addr = 0u;
  st->sda_pin = sda_pin;
  st->scl_pin = scl_pin;
  st->clock_hz = i2c_normalize_clock(clock_hz);
  st->actual_clock_hz = 0u;
  __atomic_store_n(&st->transaction_count, 0u, __ATOMIC_RELEASE);
  if (st->lock_depth == 0u) {
    st->lock_owner = 0u;
  }

  i2c_hw_init_bus(idx);
  st->initialized = true;
}

void hal_i2c_set_clock(uint32_t clock_hz) {
  (void)hal_i2c_set_clock_ex(clock_hz);
}

hal_status_t hal_i2c_set_clock_ex(uint32_t clock_hz) {
  return hal_i2c_set_clock_bus_ex(0, clock_hz);
}

hal_status_t hal_i2c_set_clock_bus_ex(uint8_t bus, uint32_t clock_hz) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  hal_i2c_set_clock_bus(bus, clock_hz);
  return HAL_OK;
}

void hal_i2c_set_clock_bus(uint8_t bus, uint32_t clock_hz) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_ensure_mutex(idx);
  i2c_lock_idx(idx);
  i2c_bus_state_t *st = &s_i2c[idx];
  st->clock_hz = i2c_normalize_clock(clock_hz);
  if (st->initialized) {
    st->actual_clock_hz = i2c_set_baudrate(i2c_bus_hw(idx), st->clock_hz);
  }
  i2c_unlock_idx(idx);
}

void hal_i2c_deinit(void) { hal_i2c_deinit_bus(0); }

void hal_i2c_deinit_bus(uint8_t bus) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_deinit(i2c_bus_hw(idx));
  s_i2c[idx].initialized = false;
  s_i2c[idx].rx_len = 0u;
  s_i2c[idx].rx_pos = 0u;
  s_i2c[idx].tx_len = 0u;
  s_i2c[idx].cur_addr = 0u;
}

void hal_i2c_lock(void) { hal_i2c_lock_bus(0); }

void hal_i2c_lock_bus(uint8_t bus) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_lock_idx(idx);
}

void hal_i2c_unlock(void) { hal_i2c_unlock_bus(0); }

void hal_i2c_unlock_bus(uint8_t bus) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_unlock_idx(idx);
}

void hal_i2c_begin_transmission(uint8_t address) {
  hal_i2c_begin_transmission_bus(0, address);
}

void hal_i2c_begin_transmission_bus(uint8_t bus, uint8_t address) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_lock_idx(idx);
  (void)i2c_ensure_initialized(idx);
  s_i2c[idx].cur_addr = address;
  s_i2c[idx].tx_len = 0u;
}

size_t hal_i2c_write(uint8_t data) { return hal_i2c_write_bus(0, data); }

size_t hal_i2c_write_bus(uint8_t bus, uint8_t data) {
  i2c_bus_state_t *st = i2c_state(bus);
  if (st->tx_len >= RP2040_I2C_BUF_SIZE) {
    return 0u;
  }
  st->tx_buf[st->tx_len++] = data;
  return 1u;
}

uint8_t hal_i2c_end_transmission(void) {
  return hal_i2c_end_transmission_bus(0);
}

hal_status_t hal_i2c_end_transmission_ex(void) {
  return hal_i2c_end_transmission_bus_ex(0);
}

hal_status_t hal_i2c_end_transmission_bus_ex(uint8_t bus) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  return i2c_status_from_result(hal_i2c_end_transmission_bus(bus));
}

uint8_t hal_i2c_end_transmission_bus(uint8_t bus) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_bus_state_t *st = &s_i2c[idx];
  uint8_t result = HAL_I2C_ERROR_TIMEOUT;
  if (st->initialized) {
    if (st->tx_len == 0u) {
      result = i2c_probe_read(idx, st->cur_addr);
    } else {
      int rc = i2c_write_timeout_us(i2c_bus_hw(idx), st->cur_addr, st->tx_buf,
                                    st->tx_len, false, RP2040_I2C_TIMEOUT_US);
      result = i2c_result_from_write_rc(rc, st->tx_len);
    }
  }
  st->tx_len = 0u;
  __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);
  i2c_unlock_idx(idx);
  return result;
}

uint8_t hal_i2c_write_byte(uint8_t address, uint8_t data, bool *outWriteOk) {
  return hal_i2c_write_byte_bus(0, address, data, outWriteOk);
}

hal_status_t hal_i2c_write_byte_ex(uint8_t address, uint8_t data,
                                   bool *outWriteOk) {
  return hal_i2c_write_byte_bus_ex(0, address, data, outWriteOk);
}

hal_status_t hal_i2c_write_byte_bus_ex(uint8_t bus, uint8_t address,
                                       uint8_t data, bool *outWriteOk) {
  if (!i2c_bus_valid(bus)) {
    if (outWriteOk != NULL) {
      *outWriteOk = false;
    }
    return HAL_EINVAL;
  }
  bool write_ok = false;
  uint8_t result = hal_i2c_write_byte_bus(bus, address, data, &write_ok);
  if (outWriteOk != NULL) {
    *outWriteOk = write_ok;
  }
  if (!write_ok) {
    return HAL_EIO;
  }
  return i2c_status_from_result(result);
}

uint8_t hal_i2c_write_byte_bus(uint8_t bus, uint8_t address, uint8_t data,
                               bool *outWriteOk) {
  hal_i2c_begin_transmission_bus(bus, address);
  size_t written = hal_i2c_write_bus(bus, data);
  if (outWriteOk != NULL) {
    *outWriteOk = (written == 1u);
  }
  return hal_i2c_end_transmission_bus(bus);
}

uint8_t hal_i2c_read_byte(uint8_t address, bool *outReadOk) {
  return hal_i2c_read_byte_bus(0, address, outReadOk);
}

hal_status_t hal_i2c_read_byte_ex(uint8_t address, uint8_t *outValue) {
  return hal_i2c_read_byte_bus_ex(0, address, outValue);
}

hal_status_t hal_i2c_read_byte_bus_ex(uint8_t bus, uint8_t address,
                                      uint8_t *outValue) {
  if (outValue == NULL) {
    return HAL_EINVAL;
  }
  *outValue = 0u;
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  return hal_i2c_read_bytes_bus_ex(bus, address, outValue, 1u);
}

uint8_t hal_i2c_read_byte_bus(uint8_t bus, uint8_t address, bool *outReadOk) {
  uint8_t value = 0u;
  bool ok = hal_i2c_read_bytes_bus(bus, address, &value, 1u);
  if (outReadOk != NULL) {
    *outReadOk = ok;
  }
  return ok ? value : 0u;
}

bool hal_i2c_write_read(uint8_t address, const uint8_t *tx, size_t tx_len,
                        uint8_t *rx, size_t rx_len) {
  return hal_status_to_bool(
      hal_i2c_write_read_ex(address, tx, tx_len, rx, rx_len));
}

hal_status_t hal_i2c_write_read_ex(uint8_t address, const uint8_t *tx,
                                   size_t tx_len, uint8_t *rx, size_t rx_len) {
  return hal_i2c_write_read_bus_ex(0, address, tx, tx_len, rx, rx_len);
}

hal_status_t hal_i2c_write_read_bus_ex(uint8_t bus, uint8_t address,
                                       const uint8_t *tx, size_t tx_len,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) || (tx_len > 0u && tx == NULL) ||
      (rx_len > 0u && rx == NULL) || tx_len > RP2040_I2C_BUF_SIZE ||
      rx_len > RP2040_I2C_BUF_SIZE) {
    return HAL_EINVAL;
  }
  if (tx_len == 0u) {
    return hal_i2c_read_bytes_bus_ex(bus, address, rx, rx_len);
  }
  const uint8_t idx = i2c_bus_index(bus);
  if (!s_i2c[idx].initialized) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return HAL_EUNINIT;
  }
  return hal_status_from_bool(
      hal_i2c_write_read_bus(bus, address, tx, tx_len, rx, rx_len), HAL_EBUS);
}

bool hal_i2c_write_read_bus(uint8_t bus, uint8_t address, const uint8_t *tx,
                            size_t tx_len, uint8_t *rx, size_t rx_len) {
  if ((tx_len > 0u && tx == NULL) || (rx_len > 0u && rx == NULL) ||
      tx_len > RP2040_I2C_BUF_SIZE || rx_len > RP2040_I2C_BUF_SIZE) {
    return false;
  }
  if (tx_len == 0u) {
    return hal_i2c_read_bytes_bus(bus, address, rx, rx_len);
  }

  uint8_t idx = i2c_bus_index(bus);
  i2c_lock_idx(idx);
  if (!i2c_ensure_initialized(idx)) {
    i2c_unlock_idx(idx);
    return false;
  }

  int written = i2c_write_timeout_us(i2c_bus_hw(idx), address, tx, tx_len,
                                     rx_len > 0u, RP2040_I2C_TIMEOUT_US);
  bool ok = written == (int)tx_len;
  __atomic_fetch_add(&s_i2c[idx].transaction_count, 1u, __ATOMIC_RELAXED);

  if (ok && rx_len > 0u) {
    int got = i2c_read_timeout_us(i2c_bus_hw(idx), address, rx, rx_len, false,
                                  RP2040_I2C_TIMEOUT_US);
    ok = got == (int)rx_len;
    __atomic_fetch_add(&s_i2c[idx].transaction_count, 1u, __ATOMIC_RELAXED);
  }

  i2c_unlock_idx(idx);
  return ok;
}

bool hal_i2c_read_bytes(uint8_t address, uint8_t *rx, size_t rx_len) {
  return hal_status_to_bool(hal_i2c_read_bytes_ex(address, rx, rx_len));
}

hal_status_t hal_i2c_read_bytes_ex(uint8_t address, uint8_t *rx,
                                   size_t rx_len) {
  return hal_i2c_read_bytes_bus_ex(0, address, rx, rx_len);
}

hal_status_t hal_i2c_read_bytes_bus_ex(uint8_t bus, uint8_t address,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) || (rx_len > 0u && rx == NULL) ||
      rx_len > RP2040_I2C_BUF_SIZE) {
    return HAL_EINVAL;
  }
  if (rx_len == 0u) {
    return HAL_OK;
  }
  const uint8_t idx = i2c_bus_index(bus);
  if (!s_i2c[idx].initialized) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return HAL_EUNINIT;
  }
  return hal_status_from_bool(hal_i2c_read_bytes_bus(bus, address, rx, rx_len),
                              HAL_EBUS);
}

bool hal_i2c_read_bytes_bus(uint8_t bus, uint8_t address, uint8_t *rx,
                            size_t rx_len) {
  if ((rx_len > 0u && rx == NULL) || rx_len > RP2040_I2C_BUF_SIZE) {
    return false;
  }
  if (rx_len == 0u) {
    return true;
  }

  uint8_t idx = i2c_bus_index(bus);
  i2c_lock_idx(idx);
  if (!i2c_ensure_initialized(idx)) {
    i2c_unlock_idx(idx);
    return false;
  }
  int got = i2c_read_timeout_us(i2c_bus_hw(idx), address, rx, rx_len, false,
                                RP2040_I2C_TIMEOUT_US);
  s_i2c[idx].rx_len = 0u;
  s_i2c[idx].rx_pos = 0u;
  __atomic_fetch_add(&s_i2c[idx].transaction_count, 1u, __ATOMIC_RELAXED);
  i2c_unlock_idx(idx);
  return got == (int)rx_len;
}

uint8_t hal_i2c_request_from(uint8_t address, uint8_t count) {
  return hal_i2c_request_from_bus(0, address, count);
}

hal_status_t hal_i2c_request_from_ex(uint8_t address, uint8_t count,
                                     uint8_t *outReceived) {
  return hal_i2c_request_from_bus_ex(0, address, count, outReceived);
}

hal_status_t hal_i2c_request_from_bus_ex(uint8_t bus, uint8_t address,
                                         uint8_t count, uint8_t *outReceived) {
  if (outReceived == NULL || !i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  *outReceived = 0u;
  const uint8_t idx = i2c_bus_index(bus);
  if (!s_i2c[idx].initialized && count > 0u) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return HAL_EUNINIT;
  }
  *outReceived = hal_i2c_request_from_bus(bus, address, count);
  return (*outReceived == count) ? HAL_OK : HAL_EBUS;
}

uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_bus_state_t *st = &s_i2c[idx];
  i2c_lock_idx(idx);
  if (!i2c_ensure_initialized(idx)) {
    i2c_unlock_idx(idx);
    return 0u;
  }

  int got = 0;
  if (count > 0u) {
    got = i2c_read_timeout_us(i2c_bus_hw(idx), address, st->rx_buf, count,
                              false, RP2040_I2C_TIMEOUT_US);
    if (got < 0) {
      got = 0;
    }
  }
  st->rx_len = (size_t)got;
  st->rx_pos = 0u;
  __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);
  i2c_unlock_idx(idx);
  return (uint8_t)got;
}

int hal_i2c_available(void) { return hal_i2c_available_bus(0); }

int hal_i2c_available_bus(uint8_t bus) {
  i2c_bus_state_t *st = i2c_state(bus);
  return (st->rx_pos < st->rx_len) ? (int)(st->rx_len - st->rx_pos) : 0;
}

int hal_i2c_read(void) { return hal_i2c_read_bus(0); }

int hal_i2c_read_bus(uint8_t bus) {
  i2c_bus_state_t *st = i2c_state(bus);
  if (st->rx_pos >= st->rx_len) {
    return -1;
  }
  return st->rx_buf[st->rx_pos++];
}

bool hal_i2c_is_busy(uint8_t address) {
  return hal_i2c_is_busy_bus(0, address);
}

bool hal_i2c_is_busy_bus(uint8_t bus, uint8_t address) {
  hal_i2c_begin_transmission_bus(bus, address);
  return hal_i2c_end_transmission_bus(bus) != 0u;
}

uint32_t hal_i2c_get_transaction_count(void) {
  return hal_i2c_get_transaction_count_bus(0);
}

uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus) {
  return __atomic_load_n(&s_i2c[i2c_bus_index(bus)].transaction_count,
                         __ATOMIC_ACQUIRE);
}

void hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin) {
  (void)hal_i2c_bus_clear_ex(sda_pin, scl_pin);
}

hal_status_t hal_i2c_bus_clear_ex(uint8_t sda_pin, uint8_t scl_pin) {
  return hal_i2c_bus_clear_bus_ex(0, sda_pin, scl_pin);
}

hal_status_t hal_i2c_bus_clear_bus_ex(uint8_t bus, uint8_t sda_pin,
                                      uint8_t scl_pin) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  hal_i2c_bus_clear_bus(bus, sda_pin, scl_pin);
  return HAL_OK;
}

void hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin) {
  (void)bus;

  gpio_init(sda_pin);
  gpio_init(scl_pin);
  gpio_set_dir(sda_pin, GPIO_IN);
  gpio_pull_up(sda_pin);
  gpio_set_dir(scl_pin, GPIO_OUT);
  gpio_put(scl_pin, true);
  gpio_pull_up(scl_pin);
  busy_wait_us(5u);

  for (uint8_t i = 0u; i < 9u; ++i) {
    if (gpio_get(sda_pin)) {
      break;
    }
    gpio_put(scl_pin, false);
    busy_wait_us(5u);
    gpio_put(scl_pin, true);
    busy_wait_us(5u);
  }

  gpio_set_dir(sda_pin, GPIO_OUT);
  gpio_put(sda_pin, false);
  busy_wait_us(5u);
  gpio_put(scl_pin, true);
  busy_wait_us(5u);
  gpio_put(sda_pin, true);
  busy_wait_us(5u);

  gpio_set_dir(sda_pin, GPIO_IN);
  gpio_pull_up(sda_pin);
  gpio_set_dir(scl_pin, GPIO_IN);
  gpio_pull_up(scl_pin);
}

#endif /* HAL_ENABLE_I2C */
#endif // HAL_TARGET_IS_RP2040
