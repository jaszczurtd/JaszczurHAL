#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_S3

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_I2C

#include "hal/core/hal_mutex_once.h"
#include "hal/i2c/hal_i2c.h"
#include "hal/i2c/hal_i2c_internal.h"
#include "hal/system/hal_sync.h"
#include "jh_board_config.h"
#include "jh_esp32_status.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <stddef.h>
#include <stdint.h>

namespace {

constexpr size_t kI2cBufferSize = UINT8_MAX;
constexpr int kI2cTransferTimeoutMs = 100;
constexpr uint32_t kI2cBusClearDelayUs = 5u;
constexpr uint8_t kI2cBusClearPulses = 9u;
/* Device-handle cache capacity: sized for the full addressable space of the
 * widest addressing mode compiled in. 7-bit-only builds keep the original
 * 128-entry footprint; enabling HAL_ENABLE_I2C_10BIT trades RAM (2x
 * i2c_master_dev_handle_t per bus, ~8 KiB total on typical pointer size) for
 * full 10-bit device-handle caching. */
#ifdef HAL_ENABLE_I2C_10BIT
constexpr uint16_t kI2cAddressCount = 1024u;
#else
constexpr uint16_t kI2cAddressCount = 128u;
#endif

struct I2cBusState {
  uint8_t rx_buffer[kI2cBufferSize];
  size_t rx_length;
  size_t rx_position;
  uint8_t tx_buffer[kI2cBufferSize];
  size_t tx_length;
  hal_i2c_address_t current_address;
#ifdef HAL_ENABLE_I2C_10BIT
  hal_i2c_addr_mode_t addr_mode;
#endif
  uint8_t sda_pin;
  uint8_t scl_pin;
  uint32_t clock_hz;
  bool initialized;
  i2c_master_bus_handle_t bus_handle;
  i2c_master_dev_handle_t devices[kI2cAddressCount];
  hal_mutex_t mutex;
  volatile uintptr_t lock_owner;
  volatile uint32_t lock_depth;
  volatile uint32_t transaction_count;
};

I2cBusState s_i2c[2] = {};

bool i2c_bus_valid(uint8_t bus) { return bus <= 1u; }

uint8_t i2c_bus_index(uint8_t bus) {
  HAL_ASSERT(i2c_bus_valid(bus), "hal_i2c: invalid bus index");
  return i2c_bus_valid(bus) ? bus : 0u;
}

I2cBusState &i2c_state(uint8_t bus) { return s_i2c[i2c_bus_index(bus)]; }

bool i2c_address_valid(hal_i2c_address_t address) {
  return address < kI2cAddressCount;
}

bool i2c_pin_usable(uint8_t pin) {
  if (pin >= 64u) {
    return false;
  }
  const uint64_t bit = UINT64_C(1) << pin;
  const uint64_t accessible = (uint64_t)HAL_BOARD_GPIO_EXPOSED_MASK |
                              (uint64_t)HAL_BOARD_GPIO_SOFT_RESERVED_MASK;
  return (((uint64_t)HAL_TARGET_GPIO_VALID_MASK & bit) != 0u) &&
         ((accessible & bit) != 0u) &&
         (((uint64_t)HAL_TARGET_GPIO_INPUT_ONLY_MASK & bit) == 0u) &&
         (((uint64_t)HAL_BOARD_GPIO_HARD_RESERVED_MASK & bit) == 0u);
}

uint32_t i2c_normalize_clock(uint32_t clock_hz) {
  return clock_hz == 0u ? HAL_I2C_CLOCK_STANDARD_HZ : clock_hz;
}

hal_status_t i2c_status_from_err(esp_err_t error) {
  if (error == ESP_ERR_INVALID_RESPONSE) {
    return HAL_EBUS;
  }
  return jh_esp32_status_from_esp_err(error);
}

hal_status_t i2c_transfer_status(I2cBusState &state, esp_err_t error) {
  if (error == ESP_ERR_TIMEOUT && state.bus_handle != nullptr) {
    (void)i2c_master_bus_reset(state.bus_handle);
  }
  return error == ESP_ERR_NOT_FOUND ? HAL_EBUS : i2c_status_from_err(error);
}

uintptr_t i2c_current_owner_token() {
  const TaskHandle_t task = xTaskGetCurrentTaskHandle();
  return task != nullptr ? (uintptr_t)task : UINTPTR_MAX;
}

bool i2c_ensure_mutex(uint8_t bus) {
  return jh_hal_mutex_create_once(&i2c_state(bus).mutex) != nullptr;
}

bool i2c_lock_index(uint8_t index) {
  if (!i2c_ensure_mutex(index)) {
    return false;
  }

  I2cBusState &state = s_i2c[index];
  const uintptr_t owner = i2c_current_owner_token();
  const uint32_t depth = __atomic_load_n(&state.lock_depth, __ATOMIC_ACQUIRE);
  const uintptr_t active_owner =
      depth > 0u ? __atomic_load_n(&state.lock_owner, __ATOMIC_ACQUIRE) : 0u;
  if (depth > 0u && active_owner == owner) {
    HAL_ASSERT(depth < UINT32_MAX, "hal_i2c_lock: nesting depth overflow");
    if (depth == UINT32_MAX) {
      return false;
    }
    (void)__atomic_fetch_add(&state.lock_depth, 1u, __ATOMIC_RELAXED);
    return true;
  }

  hal_mutex_lock(state.mutex);
  __atomic_store_n(&state.lock_owner, owner, __ATOMIC_RELAXED);
  __atomic_store_n(&state.lock_depth, 1u, __ATOMIC_RELEASE);
  return true;
}

void i2c_unlock_index(uint8_t index) {
  I2cBusState &state = s_i2c[index];
  const uintptr_t owner = i2c_current_owner_token();
  const uint32_t depth = __atomic_load_n(&state.lock_depth, __ATOMIC_ACQUIRE);
  const uintptr_t active_owner =
      depth > 0u ? __atomic_load_n(&state.lock_owner, __ATOMIC_ACQUIRE) : 0u;
  HAL_ASSERT(depth > 0u && active_owner == owner,
             "hal_i2c_unlock: bus is not locked by this context");
  if (depth == 0u || active_owner != owner) {
    return;
  }

  if (depth > 1u) {
    (void)__atomic_fetch_sub(&state.lock_depth, 1u, __ATOMIC_RELEASE);
    return;
  }
  __atomic_store_n(&state.lock_depth, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&state.lock_owner, 0u, __ATOMIC_RELAXED);
  hal_mutex_unlock(state.mutex);
}

void i2c_clear_buffers(I2cBusState &state) {
  state.rx_length = 0u;
  state.rx_position = 0u;
  state.tx_length = 0u;
  state.current_address = 0u;
}

hal_status_t i2c_remove_devices(I2cBusState &state) {
  hal_status_t first_error = HAL_OK;
  for (i2c_master_dev_handle_t &device : state.devices) {
    if (device == nullptr) {
      continue;
    }
    const esp_err_t error = i2c_master_bus_rm_device(device);
    if (error == ESP_OK) {
      device = nullptr;
    } else if (first_error == HAL_OK) {
      first_error = i2c_status_from_err(error);
    }
  }
  return first_error;
}

hal_status_t i2c_deinit_locked(I2cBusState &state) {
  if (!state.initialized) {
    i2c_clear_buffers(state);
    return HAL_OK;
  }

  const hal_status_t devices_status = i2c_remove_devices(state);
  if (hal_status_is_error(devices_status)) {
    return devices_status;
  }
  const esp_err_t error = i2c_del_master_bus(state.bus_handle);
  if (error != ESP_OK) {
    return i2c_status_from_err(error);
  }

  state.bus_handle = nullptr;
  state.initialized = false;
  i2c_clear_buffers(state);
  return HAL_OK;
}

hal_status_t i2c_get_device(I2cBusState &state, hal_i2c_address_t address,
                            i2c_master_dev_handle_t *out_device) {
  if (out_device == nullptr || !i2c_address_valid(address)) {
    return HAL_EINVAL;
  }
  if (state.devices[address] == nullptr) {
    i2c_device_config_t config = {};
#ifdef HAL_ENABLE_I2C_10BIT
    config.dev_addr_length = (state.addr_mode == HAL_I2C_ADDR_MODE_10BIT)
                                 ? I2C_ADDR_BIT_LEN_10
                                 : I2C_ADDR_BIT_LEN_7;
#else
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
#endif
    config.device_address = address;
    config.scl_speed_hz = state.clock_hz;
    config.scl_wait_us = 0u;
    config.flags.disable_ack_check = 0u;
    const esp_err_t error = i2c_master_bus_add_device(state.bus_handle, &config,
                                                      &state.devices[address]);
    if (error != ESP_OK) {
      return i2c_status_from_err(error);
    }
  }
  *out_device = state.devices[address];
  return HAL_OK;
}

/* i2c_master_probe() zero-initialises the transaction's addr_length (see
 * i2c_master.c), i.e. it always probes as a 7-bit address regardless of the
 * value passed in - it is not usable for a 10-bit probe. Kept 7-bit-only;
 * see i2c_zero_byte_probe_locked() for the 10-bit substitute. */
hal_status_t i2c_probe_locked(I2cBusState &state, uint8_t address) {
  if (!i2c_address_valid(address)) {
    return HAL_EINVAL;
  }
  const esp_err_t error =
      i2c_master_probe(state.bus_handle, address, kI2cTransferTimeoutMs);
  return i2c_transfer_status(state, error);
}

#ifdef HAL_ENABLE_I2C_10BIT
/* ESP-IDF has no zero-length-write / address-only probe for a configured
 * device handle (i2c_master_transmit() rejects write_size == 0), so a 10-bit
 * "is the device present/ready" check is approximated with a 1-byte read
 * through the correctly-configured (dev_addr_length = I2C_ADDR_BIT_LEN_10)
 * device handle instead: NACK on the address phase still surfaces as an
 * error, at the cost of consuming and discarding one byte from the device.
 * This is an ESP32-S3-specific deviation from the true zero-byte probe the
 * mock/RP2040/STM32G474 backends perform; document it wherever
 * hal_i2c_is_busy_bus() is used against a 10-bit ESP32-S3 bus. */
hal_status_t i2c_probe_10bit_locked(I2cBusState &state,
                                    hal_i2c_address_t address) {
  i2c_master_dev_handle_t device = nullptr;
  hal_status_t status = i2c_get_device(state, address, &device);
  if (hal_status_is_error(status)) {
    return status;
  }
  uint8_t discard = 0u;
  const esp_err_t error =
      i2c_master_receive(device, &discard, 1u, kI2cTransferTimeoutMs);
  return i2c_transfer_status(state, error);
}
#endif

hal_status_t i2c_zero_byte_probe_locked(I2cBusState &state,
                                        hal_i2c_address_t address) {
#ifdef HAL_ENABLE_I2C_10BIT
  if (state.addr_mode == HAL_I2C_ADDR_MODE_10BIT) {
    return i2c_probe_10bit_locked(state, address);
  }
#endif
  if (!i2c_address_valid(address)) {
    return HAL_EINVAL;
  }
  return i2c_probe_locked(state, (uint8_t)address);
}

void i2c_count_transaction(I2cBusState &state) {
  __atomic_fetch_add(&state.transaction_count, 1u, __ATOMIC_RELAXED);
}

hal_status_t i2c_require_initialized(const I2cBusState &state) {
  if (state.initialized) {
    return HAL_OK;
  }
  HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
  return HAL_EUNINIT;
}

hal_status_t i2c_transfer_locked(I2cBusState &state, hal_i2c_address_t address,
                                 const uint8_t *tx, size_t tx_length,
                                 uint8_t *rx, size_t rx_length) {
  i2c_master_dev_handle_t device = nullptr;
  hal_status_t status = i2c_get_device(state, address, &device);
  if (hal_status_is_error(status)) {
    return status;
  }

  esp_err_t error = ESP_OK;
  if (tx_length > 0u && rx_length > 0u) {
    error = i2c_master_transmit_receive(device, tx, tx_length, rx, rx_length,
                                        kI2cTransferTimeoutMs);
    i2c_count_transaction(state);
    if (error == ESP_OK) {
      i2c_count_transaction(state);
    }
  } else if (tx_length > 0u) {
    error = i2c_master_transmit(device, tx, tx_length, kI2cTransferTimeoutMs);
    i2c_count_transaction(state);
  } else if (rx_length > 0u) {
    error = i2c_master_receive(device, rx, rx_length, kI2cTransferTimeoutMs);
    i2c_count_transaction(state);
  }
  return i2c_transfer_status(state, error);
}

void i2c_finish_bus_clear(uint64_t pin_mask) {
  gpio_config_t input_config = {};
  input_config.pin_bit_mask = pin_mask;
  input_config.mode = GPIO_MODE_INPUT;
  input_config.pull_up_en = GPIO_PULLUP_ENABLE;
  input_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  input_config.intr_type = GPIO_INTR_DISABLE;
  (void)gpio_config(&input_config);
}

} // namespace

bool jh_hal_i2c_bus_is_initialized(uint8_t bus) {
  return i2c_bus_valid(bus) && s_i2c[bus].initialized;
}

#ifdef HAL_ENABLE_I2C_10BIT
bool jh_hal_i2c_bus_is_10bit(uint8_t bus) {
  return i2c_bus_valid(bus) && s_i2c[bus].addr_mode == HAL_I2C_ADDR_MODE_10BIT;
}
#endif

static hal_status_t esp32_i2c_init_bus_common(uint8_t bus, uint8_t sda_pin,
                                              uint8_t scl_pin,
                                              uint32_t clock_hz) {
  if (!i2c_bus_valid(bus) || sda_pin == scl_pin || !i2c_pin_usable(sda_pin) ||
      !i2c_pin_usable(scl_pin)) {
    return HAL_EINVAL;
  }
  const uint8_t index = i2c_bus_index(bus);
  if (!i2c_lock_index(index)) {
    return HAL_ENOMEM;
  }

  I2cBusState &state = s_i2c[index];
  hal_status_t status = i2c_deinit_locked(state);
  if (hal_status_is_error(status)) {
    i2c_unlock_index(index);
    return status;
  }

  i2c_master_bus_config_t config = {};
  config.i2c_port = (i2c_port_num_t)index;
  config.sda_io_num = (gpio_num_t)sda_pin;
  config.scl_io_num = (gpio_num_t)scl_pin;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7u;
  config.intr_priority = 0;
  config.trans_queue_depth = 0u;
  config.flags.enable_internal_pullup = 1u;
  config.flags.allow_pd = 0u;

  const esp_err_t error = i2c_new_master_bus(&config, &state.bus_handle);
  if (error != ESP_OK) {
    state.bus_handle = nullptr;
    i2c_unlock_index(index);
    return i2c_status_from_err(error);
  }

  state.sda_pin = sda_pin;
  state.scl_pin = scl_pin;
  state.clock_hz = i2c_normalize_clock(clock_hz);
  state.initialized = true;
  i2c_clear_buffers(state);
  __atomic_store_n(&state.transaction_count, 0u, __ATOMIC_RELEASE);
  i2c_unlock_index(index);
  return HAL_OK;
}

hal_status_t hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
  return hal_i2c_init_bus(0u, sda_pin, scl_pin, clock_hz);
}

hal_status_t hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                              uint32_t clock_hz) {
  const hal_status_t status =
      esp32_i2c_init_bus_common(bus, sda_pin, scl_pin, clock_hz);
#ifdef HAL_ENABLE_I2C_10BIT
  if (status == HAL_OK) {
    i2c_state(bus).addr_mode = HAL_I2C_ADDR_MODE_7BIT;
  }
#endif
  return status;
}

#ifdef HAL_ENABLE_I2C_10BIT
hal_status_t hal_i2c_init_10bit(uint8_t sda_pin, uint8_t scl_pin,
                                uint32_t clock_hz) {
  return hal_i2c_init_bus_10bit(0u, sda_pin, scl_pin, clock_hz);
}

hal_status_t hal_i2c_init_bus_10bit(uint8_t bus, uint8_t sda_pin,
                                    uint8_t scl_pin, uint32_t clock_hz) {
  const hal_status_t status =
      esp32_i2c_init_bus_common(bus, sda_pin, scl_pin, clock_hz);
  if (status == HAL_OK) {
    i2c_state(bus).addr_mode = HAL_I2C_ADDR_MODE_10BIT;
  }
  return status;
}

hal_i2c_addr_mode_t hal_i2c_get_addr_mode(void) {
  return hal_i2c_get_addr_mode_bus(0u);
}

hal_i2c_addr_mode_t hal_i2c_get_addr_mode_bus(uint8_t bus) {
  return i2c_state(bus).addr_mode;
}
#endif /* HAL_ENABLE_I2C_10BIT */

hal_status_t hal_i2c_get_clock(uint32_t *out_clock_hz) {
  return hal_i2c_get_clock_bus(0u, out_clock_hz);
}

hal_status_t hal_i2c_get_clock_bus(uint8_t bus, uint32_t *out_clock_hz) {
  if (!i2c_bus_valid(bus) || out_clock_hz == nullptr) {
    return HAL_EINVAL;
  }
  *out_clock_hz = i2c_state(bus).clock_hz;
  return HAL_OK;
}

hal_status_t hal_i2c_set_clock(uint32_t clock_hz) {
  return hal_i2c_set_clock_bus(0u, clock_hz);
}

hal_status_t hal_i2c_set_clock_bus(uint8_t bus, uint32_t clock_hz) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  const uint8_t index = i2c_bus_index(bus);
  if (!i2c_lock_index(index)) {
    return HAL_ENOMEM;
  }

  I2cBusState &state = s_i2c[index];
  hal_status_t status = HAL_OK;
  if (state.initialized) {
    status = i2c_remove_devices(state);
  }
  if (hal_status_is_ok(status)) {
    state.clock_hz = i2c_normalize_clock(clock_hz);
  }
  i2c_unlock_index(index);
  return status;
}

void hal_i2c_deinit(void) { hal_i2c_deinit_bus(0u); }

void hal_i2c_deinit_bus(uint8_t bus) {
  const uint8_t index = i2c_bus_index(bus);
  if (!i2c_lock_index(index)) {
    return;
  }
  (void)i2c_deinit_locked(s_i2c[index]);
  i2c_unlock_index(index);
}

void hal_i2c_lock(void) { hal_i2c_lock_bus(0u); }

void hal_i2c_lock_bus(uint8_t bus) { (void)i2c_lock_index(i2c_bus_index(bus)); }

void hal_i2c_unlock(void) { hal_i2c_unlock_bus(0u); }

void hal_i2c_unlock_bus(uint8_t bus) { i2c_unlock_index(i2c_bus_index(bus)); }

void hal_i2c_begin_transmission(hal_i2c_address_t address) {
  hal_i2c_begin_transmission_bus(0u, address);
}

void hal_i2c_begin_transmission_bus(uint8_t bus, hal_i2c_address_t address) {
  const uint8_t index = i2c_bus_index(bus);
  if (!i2c_lock_index(index)) {
    return;
  }
  (void)i2c_require_initialized(s_i2c[index]);
  s_i2c[index].current_address = address;
  s_i2c[index].tx_length = 0u;
}

size_t hal_i2c_write(uint8_t data) { return hal_i2c_write_bus(0u, data); }

size_t hal_i2c_write_bus(uint8_t bus, uint8_t data) {
  I2cBusState &state = i2c_state(bus);
  if (state.tx_length >= kI2cBufferSize) {
    return 0u;
  }
  state.tx_buffer[state.tx_length++] = data;
  return 1u;
}

hal_status_t hal_i2c_end_transmission_ex(void) {
  return hal_i2c_end_transmission_bus_ex(0u);
}

hal_status_t hal_i2c_end_transmission_bus_ex(uint8_t bus) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  const uint8_t index = i2c_bus_index(bus);
  I2cBusState &state = s_i2c[index];
  hal_status_t status = i2c_require_initialized(state);
  if (hal_status_is_ok(status)) {
    status = jh_hal_i2c_validate_address(bus, state.current_address);
  }
  if (hal_status_is_ok(status)) {
    if (state.tx_length == 0u) {
      status = i2c_zero_byte_probe_locked(state, state.current_address);
      i2c_count_transaction(state);
    } else {
      status =
          i2c_transfer_locked(state, state.current_address, state.tx_buffer,
                              state.tx_length, nullptr, 0u);
    }
  }
  state.tx_length = 0u;
  i2c_unlock_index(index);
  return status;
}

hal_status_t hal_i2c_write_read_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                       const uint8_t *tx, size_t tx_len,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) ||
      hal_status_is_error(jh_hal_i2c_validate_address(bus, address)) ||
      (tx_len > 0u && tx == nullptr) || (rx_len > 0u && rx == nullptr) ||
      tx_len > kI2cBufferSize || rx_len > kI2cBufferSize) {
    return HAL_EINVAL;
  }
  if (tx_len == 0u) {
    return hal_i2c_read_bytes_bus_ex(bus, address, rx, rx_len);
  }

  const uint8_t index = i2c_bus_index(bus);
  if (!i2c_lock_index(index)) {
    return HAL_ENOMEM;
  }
  I2cBusState &state = s_i2c[index];
  hal_status_t status = i2c_require_initialized(state);
  if (hal_status_is_ok(status)) {
    status = i2c_transfer_locked(state, address, tx, tx_len, rx, rx_len);
  }
  i2c_unlock_index(index);
  return status;
}

hal_status_t hal_i2c_read_bytes_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) ||
      hal_status_is_error(jh_hal_i2c_validate_address(bus, address)) ||
      (rx_len > 0u && rx == nullptr) || rx_len > kI2cBufferSize) {
    return HAL_EINVAL;
  }
  if (rx_len == 0u) {
    return HAL_OK;
  }

  const uint8_t index = i2c_bus_index(bus);
  if (!i2c_lock_index(index)) {
    return HAL_ENOMEM;
  }
  I2cBusState &state = s_i2c[index];
  hal_status_t status = i2c_require_initialized(state);
  if (hal_status_is_ok(status)) {
    status = i2c_transfer_locked(state, address, nullptr, 0u, rx, rx_len);
  }
  state.rx_length = 0u;
  state.rx_position = 0u;
  i2c_unlock_index(index);
  return status;
}

hal_status_t hal_i2c_request_from_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                         uint8_t count, uint8_t *out_received) {
  if (!i2c_bus_valid(bus) ||
      hal_status_is_error(jh_hal_i2c_validate_address(bus, address)) ||
      out_received == nullptr) {
    return HAL_EINVAL;
  }
  *out_received = 0u;
  const uint8_t index = i2c_bus_index(bus);
  if (!i2c_lock_index(index)) {
    return HAL_ENOMEM;
  }
  I2cBusState &state = s_i2c[index];
  hal_status_t status = i2c_require_initialized(state);
  if (hal_status_is_ok(status) && count > 0u) {
    status = i2c_transfer_locked(state, address, nullptr, 0u, state.rx_buffer,
                                 count);
  } else if (hal_status_is_ok(status)) {
    i2c_count_transaction(state);
  }
  if (hal_status_is_ok(status)) {
    state.rx_length = count;
    state.rx_position = 0u;
    *out_received = count;
  } else {
    state.rx_length = 0u;
    state.rx_position = 0u;
  }
  i2c_unlock_index(index);
  return status;
}

int hal_i2c_available(void) { return hal_i2c_available_bus(0u); }

int hal_i2c_available_bus(uint8_t bus) {
  const I2cBusState &state = i2c_state(bus);
  return state.rx_position < state.rx_length
             ? (int)(state.rx_length - state.rx_position)
             : 0;
}

int hal_i2c_read(void) { return hal_i2c_read_bus(0u); }

int hal_i2c_read_bus(uint8_t bus) {
  I2cBusState &state = i2c_state(bus);
  if (state.rx_position >= state.rx_length) {
    return -1;
  }
  return state.rx_buffer[state.rx_position++];
}

bool hal_i2c_is_busy(hal_i2c_address_t address) {
  return hal_i2c_is_busy_bus(0u, address);
}

bool hal_i2c_is_busy_bus(uint8_t bus, hal_i2c_address_t address) {
  if (!i2c_bus_valid(bus) ||
      hal_status_is_error(jh_hal_i2c_validate_address(bus, address))) {
    return true;
  }
  hal_i2c_begin_transmission_bus(bus, address);
  return hal_i2c_end_transmission_bus_ex(bus) != HAL_OK;
}

uint32_t hal_i2c_get_transaction_count(void) {
  return hal_i2c_get_transaction_count_bus(0u);
}

uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus) {
  return __atomic_load_n(&s_i2c[i2c_bus_index(bus)].transaction_count,
                         __ATOMIC_ACQUIRE);
}

hal_status_t hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin) {
  return hal_i2c_bus_clear_bus(0u, sda_pin, scl_pin);
}

hal_status_t hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin) {
  if (!i2c_bus_valid(bus) || sda_pin == scl_pin || !i2c_pin_usable(sda_pin) ||
      !i2c_pin_usable(scl_pin)) {
    return HAL_EINVAL;
  }
  const uint8_t index = i2c_bus_index(bus);
  if (!i2c_lock_index(index)) {
    return HAL_ENOMEM;
  }
  if (s_i2c[index].initialized) {
    i2c_unlock_index(index);
    return HAL_ESTATE;
  }

  const uint64_t pins = (UINT64_C(1) << sda_pin) | (UINT64_C(1) << scl_pin);
  esp_err_t error = gpio_set_level((gpio_num_t)sda_pin, 1u);
  if (error == ESP_OK) {
    error = gpio_set_level((gpio_num_t)scl_pin, 1u);
  }

  gpio_config_t config = {};
  config.pin_bit_mask = pins;
  config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  if (error == ESP_OK) {
    error = gpio_config(&config);
  }
  if (error != ESP_OK) {
    i2c_finish_bus_clear(pins);
    i2c_unlock_index(index);
    return i2c_status_from_err(error);
  }

  esp_rom_delay_us(kI2cBusClearDelayUs);
  for (uint8_t pulse = 0u; pulse < kI2cBusClearPulses; ++pulse) {
    if (gpio_get_level((gpio_num_t)sda_pin) != 0) {
      break;
    }
    (void)gpio_set_level((gpio_num_t)scl_pin, 0u);
    esp_rom_delay_us(kI2cBusClearDelayUs);
    (void)gpio_set_level((gpio_num_t)scl_pin, 1u);
    esp_rom_delay_us(kI2cBusClearDelayUs);
  }

  (void)gpio_set_level((gpio_num_t)sda_pin, 0u);
  esp_rom_delay_us(kI2cBusClearDelayUs);
  (void)gpio_set_level((gpio_num_t)scl_pin, 1u);
  esp_rom_delay_us(kI2cBusClearDelayUs);
  (void)gpio_set_level((gpio_num_t)sda_pin, 1u);
  esp_rom_delay_us(kI2cBusClearDelayUs);

  const bool lines_released = gpio_get_level((gpio_num_t)sda_pin) != 0 &&
                              gpio_get_level((gpio_num_t)scl_pin) != 0;
  i2c_finish_bus_clear(pins);
  i2c_unlock_index(index);
  return lines_released ? HAL_OK : HAL_EBUS;
}

#endif /* HAL_ENABLE_I2C */
#endif /* HAL_TARGET_IS_ESP32_S3 */
