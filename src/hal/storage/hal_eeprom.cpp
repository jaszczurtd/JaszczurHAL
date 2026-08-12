#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_EEPROM

#include "hal/core/hal_mutex_once.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/jh_eeprom_provider.h"
#include "hal/system/hal_sync.h"

#include <string.h>

namespace {

const jh_eeprom_provider_ops_t *s_provider = nullptr;
uint16_t s_size = 0u;
hal_mutex_t s_eeprom_mutex = nullptr;
hal_eeprom_progress_callback_t s_progress_callback = nullptr;
void *s_progress_ctx = nullptr;

hal_mutex_t eeprom_mutex() { return jh_hal_mutex_create_once(&s_eeprom_mutex); }

uint16_t clipped_len(uint16_t addr, uint16_t len) {
  if (addr >= s_size) {
    return 0u;
  }
  const uint16_t remaining = static_cast<uint16_t>(s_size - addr);
  return len <= remaining ? len : remaining;
}

hal_status_t range_status(uint16_t addr, uint16_t len) {
  if (s_provider == nullptr || s_size == 0u) {
    return HAL_EUNINIT;
  }
  if (len == 0u) {
    return HAL_OK;
  }
  if (addr >= s_size ||
      static_cast<uint32_t>(addr) + len > static_cast<uint32_t>(s_size)) {
    return HAL_EOVERFLOW;
  }
  return HAL_OK;
}

hal_status_t combine_status(hal_status_t range, hal_status_t provider_status) {
  if (range == HAL_EUNINIT) {
    return range;
  }
  return provider_status == HAL_OK ? range : provider_status;
}

hal_status_t write_clipped(uint16_t addr, const uint8_t *data, uint16_t len) {
  if (len == 0u) {
    return HAL_OK;
  }
  return s_provider != nullptr
             ? s_provider->write(addr, data, len, s_progress_callback,
                                 s_progress_ctx)
             : HAL_EUNINIT;
}

hal_status_t read_clipped(uint16_t addr, uint8_t *out, uint16_t len) {
  if (len == 0u) {
    return HAL_OK;
  }
  return s_provider != nullptr ? s_provider->read(addr, out, len) : HAL_EUNINIT;
}

int32_t decode_int(const uint8_t raw[4]) {
  return static_cast<int32_t>(static_cast<uint32_t>(raw[0]) |
                              (static_cast<uint32_t>(raw[1]) << 8u) |
                              (static_cast<uint32_t>(raw[2]) << 16u) |
                              (static_cast<uint32_t>(raw[3]) << 24u));
}

} // namespace

hal_status_t hal_eeprom_init(hal_eeprom_type_t type, uint16_t size,
                             uint8_t i2c_addr) {
  if (type < HAL_EEPROM_DEFAULT || type > HAL_EEPROM_STM32_FLASH) {
    return HAL_EINVAL;
  }

  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  const jh_eeprom_provider_ops_t *provider = jh_eeprom_provider_get_ops(type);
  if (provider == nullptr || provider->initialize == nullptr ||
      provider->read == nullptr || provider->write == nullptr ||
      provider->commit == nullptr || provider->reset == nullptr) {
    s_provider = nullptr;
    s_size = 0u;
    hal_mutex_unlock(mutex);
    return HAL_ECONFIG;
  }

  const jh_eeprom_provider_config_t config = {type, size, i2c_addr};
  jh_eeprom_provider_info_t info = {};
  const hal_status_t status = provider->initialize(&config, &info);
  if (status == HAL_OK && info.size > 0u) {
    s_provider = provider;
    s_size = info.size;
  } else {
    s_provider = nullptr;
    s_size = 0u;
  }
  hal_mutex_unlock(mutex);
  return status == HAL_OK && info.size == 0u ? HAL_EINVAL : status;
}

hal_status_t
hal_eeprom_set_progress_callback(hal_eeprom_progress_callback_t callback,
                                 void *ctx) {
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  s_progress_callback = callback;
  s_progress_ctx = ctx;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_eeprom_write_byte(uint16_t addr, uint8_t val) {
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  const hal_status_t range = range_status(addr, 1u);
  const hal_status_t provider_status =
      write_clipped(addr, &val, clipped_len(addr, 1u));
  hal_mutex_unlock(mutex);
  return combine_status(range, provider_status);
}

uint8_t hal_eeprom_read_byte(uint16_t addr) {
  uint8_t value = 0u;
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  (void)read_clipped(addr, &value, clipped_len(addr, 1u));
  hal_mutex_unlock(mutex);
  return value;
}

hal_status_t hal_eeprom_read_byte_ex(uint16_t addr, uint8_t *out_val) {
  if (out_val == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  const hal_status_t range = range_status(addr, 1u);
  const hal_status_t provider_status =
      range == HAL_OK ? read_clipped(addr, out_val, 1u) : HAL_OK;
  hal_mutex_unlock(mutex);
  return combine_status(range, provider_status);
}

hal_status_t hal_eeprom_write_int(uint16_t addr, int32_t val) {
  const uint8_t raw[4] = {
      static_cast<uint8_t>((val >> 0) & 0xff),
      static_cast<uint8_t>((val >> 8) & 0xff),
      static_cast<uint8_t>((val >> 16) & 0xff),
      static_cast<uint8_t>((val >> 24) & 0xff),
  };
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  const hal_status_t range = range_status(addr, sizeof(raw));
  const hal_status_t provider_status =
      write_clipped(addr, raw, clipped_len(addr, sizeof(raw)));
  hal_mutex_unlock(mutex);
  return combine_status(range, provider_status);
}

int32_t hal_eeprom_read_int(uint16_t addr) {
  uint8_t raw[4] = {};
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  (void)read_clipped(addr, raw, clipped_len(addr, sizeof(raw)));
  hal_mutex_unlock(mutex);
  return decode_int(raw);
}

hal_status_t hal_eeprom_read_int_ex(uint16_t addr, int32_t *out_val) {
  if (out_val == nullptr) {
    return HAL_EINVAL;
  }
  uint8_t raw[4] = {};
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  const hal_status_t range = range_status(addr, sizeof(raw));
  const hal_status_t provider_status =
      range == HAL_OK ? read_clipped(addr, raw, sizeof(raw)) : HAL_OK;
  if (provider_status == HAL_OK && range == HAL_OK) {
    *out_val = decode_int(raw);
  }
  hal_mutex_unlock(mutex);
  return combine_status(range, provider_status);
}

hal_status_t hal_eeprom_write_bytes(uint16_t addr, const uint8_t *data,
                                    uint16_t len) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  const hal_status_t range = range_status(addr, len);
  const hal_status_t provider_status =
      write_clipped(addr, data, clipped_len(addr, len));
  hal_mutex_unlock(mutex);
  return combine_status(range, provider_status);
}

hal_status_t hal_eeprom_read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  const hal_status_t range = range_status(addr, len);
  const uint16_t readable = clipped_len(addr, len);
  const hal_status_t provider_status = read_clipped(addr, out, readable);
  memset(out + readable, 0, static_cast<size_t>(len - readable));
  hal_mutex_unlock(mutex);
  return combine_status(range, provider_status);
}

hal_status_t hal_eeprom_commit(void) {
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  const hal_status_t status =
      s_provider != nullptr
          ? s_provider->commit(s_progress_callback, s_progress_ctx)
          : HAL_EUNINIT;
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_eeprom_reset(void) {
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  const hal_status_t status =
      s_provider != nullptr
          ? s_provider->reset(s_progress_callback, s_progress_ctx)
          : HAL_EUNINIT;
  hal_mutex_unlock(mutex);
  return status;
}

uint16_t hal_eeprom_size(void) {
  uint16_t size = 0u;
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  size = s_size;
  hal_mutex_unlock(mutex);
  return size;
}

hal_status_t hal_eeprom_size_ex(uint16_t *out_size) {
  if (out_size == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  *out_size = s_size;
  const hal_status_t status =
      s_provider != nullptr && s_size > 0u ? HAL_OK : HAL_EUNINIT;
  hal_mutex_unlock(mutex);
  return status;
}

void jh_eeprom_mock_reset_facade(void) {
  hal_mutex_t mutex = eeprom_mutex();
  hal_mutex_lock(mutex);
  s_provider = nullptr;
  s_size = 0u;
  s_progress_callback = nullptr;
  s_progress_ctx = nullptr;
  hal_mutex_unlock(mutex);
}

#endif /* HAL_ENABLE_EEPROM */
