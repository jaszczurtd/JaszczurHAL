#include "pn532.h"
#include "pn532_frame_read_cache.h"

#ifdef HAL_ENABLE_I2C

#include "hal/system/hal_system.h"

#include <cstring>

static constexpr uint8_t PN532_I2C_READY = 0x01;
/* The HAL accepts at most UINT8_MAX bytes per I2C read. One byte is the PN532
 * ready marker, leaving this many frame bytes in the same transaction. */
static constexpr size_t PN532_I2C_MAX_FRAME_READ_SIZE = UINT8_MAX - 1u;

static JH_PN532_FRAME_READ_CACHE s_i2c_frame_cache;

PN532_I2C::PN532_I2C(uint8_t resetPin, uint8_t address, uint8_t bus)
    : _resetPin(resetPin), _address(address), _bus(bus) {}

PN532_I2C::~PN532_I2C() { (void)s_i2c_frame_cache.release(this); }

hal_status_t PN532_I2C::begin() {
  const hal_status_t status = s_i2c_frame_cache.release(this);
  if (status != HAL_OK) {
    return status;
  }
  if (_resetPin != PN532_UNUSED_PIN) {
    hal_gpio_set_mode(_resetPin, HAL_GPIO_OUTPUT_HIGH);
    hal_gpio_write(_resetPin, false);
    hal_delay_ms(10);
    hal_gpio_write(_resetPin, true);
    hal_delay_ms(400);
  }
  return HAL_OK;
}

hal_status_t PN532_I2C::wakeup() {
  static const uint8_t wakeup_frame[] = {0x00};
  return writeCommand(wakeup_frame, sizeof(wakeup_frame));
}

hal_status_t PN532_I2C::isReady(bool *ready) {
  if (ready == NULL) {
    return HAL_EINVAL;
  }

  uint8_t value = 0;
  hal_i2c_lock_bus(_bus);
  const hal_status_t status =
      hal_i2c_read_bytes_bus_ex(_bus, _address, &value, sizeof(value));
  hal_i2c_unlock_bus(_bus);
  if (status != HAL_OK) {
    return status;
  }

  *ready = (value == PN532_I2C_READY);
  return HAL_OK;
}

hal_status_t PN532_I2C::writeCommand(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0) {
    return HAL_EINVAL;
  }
  hal_status_t status = s_i2c_frame_cache.release(this);
  if (status != HAL_OK) {
    return status;
  }

  hal_i2c_lock_bus(_bus);
  hal_i2c_begin_transmission_bus(_bus, _address);
  status = HAL_OK;
  for (size_t i = 0; i < len; ++i) {
    if (hal_i2c_write_bus(_bus, data[i]) != 1u && status == HAL_OK) {
      status = HAL_EIO;
    }
  }
  const hal_status_t end_status = hal_i2c_end_transmission_bus_ex(_bus);
  if (status == HAL_OK) {
    status = end_status;
  }
  hal_i2c_unlock_bus(_bus);
  return status;
}

hal_status_t PN532_I2C::readData(uint8_t *data, size_t len) {
  if (data == NULL || len == 0) {
    return HAL_EINVAL;
  }
  if (len > PN532_PACKETBUFFER_SIZE) {
    return HAL_EOVERFLOW;
  }

  bool cached = false;
  hal_status_t status = s_i2c_frame_cache.read(this, data, len, &cached);
  if (status != HAL_OK || cached) {
    return status;
  }

  const size_t frame_read_size =
      len == JH_PN532_FRAME_HEADER_SIZE ? PN532_I2C_MAX_FRAME_READ_SIZE : len;

  /* NXP UM0701-02 section 6.2.4 requires the response to be read before STOP:
   * stopping after a five-byte header discards the rest. Prefetch the largest
   * transaction supported by the HAL and serve the logical tail from cache. */
  uint8_t buffer[UINT8_MAX] = {};
  hal_i2c_lock_bus(_bus);
  status =
      hal_i2c_read_bytes_bus_ex(_bus, _address, buffer, frame_read_size + 1u);
  hal_i2c_unlock_bus(_bus);
  if (status != HAL_OK) {
    return status;
  }
  if (buffer[0] != PN532_I2C_READY) {
    return HAL_EAGAIN;
  }

  std::memcpy(data, buffer + 1, len);
  if (frame_read_size != len) {
    const size_t declared_frame_size =
        (size_t)buffer[1u + 3u] + JH_PN532_FRAME_OVERHEAD;
    /* For a declared frame above the HAL's I2C transaction limit, STOP has
     * already discarded the uncached tail. The parser still consumes that
     * logical tail before returning HAL_EOVERFLOW. */
    const bool unread_tail_was_discarded =
        declared_frame_size > frame_read_size;
    status = s_i2c_frame_cache.activate(this, buffer + 1, frame_read_size,
                                        unread_tail_was_discarded);
  }
  return status;
}

#endif
