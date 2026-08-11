#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_I2C

#include "hal/i2c/hal_i2c.h"
#include "hal/i2c/hal_i2c_internal.h"

namespace {

bool bus_is_valid(uint8_t bus) { return bus <= 1u; }

uint8_t legacy_result(hal_status_t status) {
  switch (status) {
  case HAL_OK:
    return HAL_I2C_RESULT_OK;
  case HAL_ETIMEOUT:
    return HAL_I2C_ERROR_TIMEOUT;
  case HAL_EBUS:
    return HAL_I2C_ERROR_GENERIC;
  default:
    return HAL_I2C_ERROR_OTHER;
  }
}

} // namespace

uint8_t hal_i2c_end_transmission(void) {
  return legacy_result(hal_i2c_end_transmission_ex());
}

uint8_t hal_i2c_end_transmission_bus(uint8_t bus) {
  return legacy_result(hal_i2c_end_transmission_bus_ex(bus));
}

hal_status_t hal_i2c_scan(uint8_t *addresses, size_t capacity, size_t *outFound,
                          hal_i2c_scan_callback_t callback) {
  return hal_i2c_scan_bus(0u, addresses, capacity, outFound, callback);
}

hal_status_t hal_i2c_scan_bus(uint8_t bus, uint8_t *addresses, size_t capacity,
                              size_t *outFound,
                              hal_i2c_scan_callback_t callback) {
  if (!bus_is_valid(bus) || outFound == nullptr ||
      (addresses == nullptr && capacity > 0u)) {
    return HAL_EINVAL;
  }
  *outFound = 0u;
  if (!jh_hal_i2c_bus_is_initialized(bus)) {
    return HAL_EUNINIT;
  }

  for (uint8_t address = HAL_I2C_SCAN_FIRST_ADDRESS;
       address <= HAL_I2C_SCAN_LAST_ADDRESS; ++address) {
    if (callback != nullptr) {
      callback();
    }
    hal_i2c_begin_transmission_bus(bus, address);
    const hal_status_t probe_status = hal_i2c_end_transmission_bus_ex(bus);
    if (probe_status == HAL_OK) {
      if (*outFound < capacity) {
        addresses[*outFound] = address;
      }
      ++(*outFound);
    } else if (probe_status != HAL_EBUS) {
      return probe_status;
    }
  }

  return (addresses != nullptr && *outFound > capacity) ? HAL_EOVERFLOW
                                                        : HAL_OK;
}

uint8_t hal_i2c_write_byte(uint8_t address, uint8_t data, bool *outWriteOk) {
  return hal_i2c_write_byte_bus(0u, address, data, outWriteOk);
}

hal_status_t hal_i2c_write_byte_ex(uint8_t address, uint8_t data,
                                   bool *outWriteOk) {
  return hal_i2c_write_byte_bus_ex(0u, address, data, outWriteOk);
}

hal_status_t hal_i2c_write_byte_bus_ex(uint8_t bus, uint8_t address,
                                       uint8_t data, bool *outWriteOk) {
  if (!bus_is_valid(bus)) {
    if (outWriteOk != nullptr) {
      *outWriteOk = false;
    }
    return HAL_EINVAL;
  }
  hal_i2c_begin_transmission_bus(bus, address);
  const bool write_ok = hal_i2c_write_bus(bus, data) == 1u;
  if (outWriteOk != nullptr) {
    *outWriteOk = write_ok;
  }
  const hal_status_t transfer_status = hal_i2c_end_transmission_bus_ex(bus);
  return write_ok ? transfer_status : HAL_EIO;
}

uint8_t hal_i2c_write_byte_bus(uint8_t bus, uint8_t address, uint8_t data,
                               bool *outWriteOk) {
  return legacy_result(
      hal_i2c_write_byte_bus_ex(bus, address, data, outWriteOk));
}

uint8_t hal_i2c_read_byte(uint8_t address, bool *outReadOk) {
  return hal_i2c_read_byte_bus(0u, address, outReadOk);
}

hal_status_t hal_i2c_read_byte_ex(uint8_t address, uint8_t *outValue) {
  return hal_i2c_read_byte_bus_ex(0u, address, outValue);
}

hal_status_t hal_i2c_read_byte_bus_ex(uint8_t bus, uint8_t address,
                                      uint8_t *outValue) {
  if (outValue == nullptr) {
    return HAL_EINVAL;
  }
  *outValue = 0u;
  if (!bus_is_valid(bus)) {
    return HAL_EINVAL;
  }
  return hal_i2c_read_bytes_bus_ex(bus, address, outValue, 1u);
}

uint8_t hal_i2c_read_byte_bus(uint8_t bus, uint8_t address, bool *outReadOk) {
  uint8_t value = 0u;
  const hal_status_t status = hal_i2c_read_byte_bus_ex(bus, address, &value);
  if (outReadOk != nullptr) {
    *outReadOk = hal_status_to_bool(status);
  }
  return hal_status_to_bool(status) ? value : 0u;
}

bool hal_i2c_write_read(uint8_t address, const uint8_t *tx, size_t tx_len,
                        uint8_t *rx, size_t rx_len) {
  return hal_status_to_bool(
      hal_i2c_write_read_ex(address, tx, tx_len, rx, rx_len));
}

hal_status_t hal_i2c_write_read_ex(uint8_t address, const uint8_t *tx,
                                   size_t tx_len, uint8_t *rx, size_t rx_len) {
  return hal_i2c_write_read_bus_ex(0u, address, tx, tx_len, rx, rx_len);
}

bool hal_i2c_write_read_bus(uint8_t bus, uint8_t address, const uint8_t *tx,
                            size_t tx_len, uint8_t *rx, size_t rx_len) {
  return hal_status_to_bool(
      hal_i2c_write_read_bus_ex(bus, address, tx, tx_len, rx, rx_len));
}

bool hal_i2c_read_bytes(uint8_t address, uint8_t *rx, size_t rx_len) {
  return hal_status_to_bool(hal_i2c_read_bytes_ex(address, rx, rx_len));
}

hal_status_t hal_i2c_read_bytes_ex(uint8_t address, uint8_t *rx,
                                   size_t rx_len) {
  return hal_i2c_read_bytes_bus_ex(0u, address, rx, rx_len);
}

bool hal_i2c_read_bytes_bus(uint8_t bus, uint8_t address, uint8_t *rx,
                            size_t rx_len) {
  return hal_status_to_bool(
      hal_i2c_read_bytes_bus_ex(bus, address, rx, rx_len));
}

uint8_t hal_i2c_request_from(uint8_t address, uint8_t count) {
  uint8_t received = 0u;
  (void)hal_i2c_request_from_ex(address, count, &received);
  return received;
}

hal_status_t hal_i2c_request_from_ex(uint8_t address, uint8_t count,
                                     uint8_t *outReceived) {
  return hal_i2c_request_from_bus_ex(0u, address, count, outReceived);
}

uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count) {
  uint8_t received = 0u;
  (void)hal_i2c_request_from_bus_ex(bus, address, count, &received);
  return received;
}

#endif
