#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_EEPROM

#include "hal/core/jh_endian.h"
#include "hal/storage/jh_eeprom_provider.h"

#ifdef HAL_ENABLE_I2C
#include "hal/i2c/hal_i2c.h"
#include "hal/system/hal_system.h"
#endif

#include <string.h>

namespace {

constexpr uint16_t kDeviceSize = 32768u;

#ifdef HAL_ENABLE_I2C
uint8_t s_i2c_addr = EEPROM_I2C_ADDRESS;

void notify(hal_eeprom_progress_callback_t progress, void *ctx) {
  if (progress != nullptr) {
    progress(ctx);
  }
}
#endif

hal_status_t initialize(const jh_eeprom_provider_config_t *config,
                        jh_eeprom_provider_info_t *out_info) {
  if (config == nullptr || out_info == nullptr ||
      config->requested_type != HAL_EEPROM_AT24C256) {
    return HAL_EINVAL;
  }
#ifndef HAL_ENABLE_I2C
  return HAL_ECONFIG;
#else
  s_i2c_addr = config->i2c_addr != 0u ? config->i2c_addr : EEPROM_I2C_ADDRESS;
  out_info->type = HAL_EEPROM_AT24C256;
  out_info->size = kDeviceSize;
  out_info->erase_size = 1u;
  out_info->program_size = 1u;
  out_info->append_size = 1u;
  return HAL_OK;
#endif
}

#ifdef HAL_ENABLE_I2C
hal_status_t wait_ready(hal_eeprom_progress_callback_t progress, void *ctx) {
  uint32_t waited_us = 0u;
  while (hal_i2c_is_busy(s_i2c_addr)) {
    if (waited_us >= HAL_AT24C256_WRITE_TIMEOUT_US) {
      return HAL_ETIMEOUT;
    }
    hal_delay_us(HAL_AT24C256_ACK_POLL_US);
    waited_us += HAL_AT24C256_ACK_POLL_US;
    notify(progress, ctx);
  }
  return HAL_OK;
}

hal_status_t write_page(uint16_t addr, const uint8_t *data, uint8_t len,
                        hal_eeprom_progress_callback_t progress, void *ctx) {
  if (data == nullptr || len == 0u || addr >= kDeviceSize ||
      static_cast<uint32_t>(addr) + len > kDeviceSize) {
    return HAL_EINVAL;
  }

  hal_i2c_begin_transmission(s_i2c_addr);
  uint8_t address[2];
  jh_store_be16(address, addr);
  bool queued = hal_i2c_write(address[0]) == 1u;
  queued = queued && hal_i2c_write(address[1]) == 1u;
  for (uint8_t i = 0u; queued && i < len; ++i) {
    queued = hal_i2c_write(data[i]) == 1u;
  }
  const hal_status_t transfer = hal_i2c_end_transmission_ex();
  if (!queued || transfer != HAL_OK) {
    return HAL_EIO;
  }
  return wait_ready(progress, ctx);
}
#endif

hal_status_t write_bytes(uint16_t addr, const uint8_t *data, uint16_t len,
                         hal_eeprom_progress_callback_t progress, void *ctx) {
  if ((data == nullptr && len > 0u) ||
      static_cast<uint32_t>(addr) + len > kDeviceSize) {
    return HAL_EINVAL;
  }
#ifndef HAL_ENABLE_I2C
  (void)progress;
  (void)ctx;
  return HAL_ECONFIG;
#else
  uint16_t written = 0u;
  while (written < len) {
    const uint16_t current = static_cast<uint16_t>(addr + written);
    const uint8_t page_remaining = static_cast<uint8_t>(
        HAL_AT24C256_PAGE_SIZE - (current % HAL_AT24C256_PAGE_SIZE));
    uint16_t chunk = static_cast<uint16_t>(len - written);
    if (chunk > page_remaining) {
      chunk = page_remaining;
    }
    if (chunk > HAL_AT24C256_PAGE_SIZE) {
      chunk = HAL_AT24C256_PAGE_SIZE;
    }
    const hal_status_t status = write_page(
        current, data + written, static_cast<uint8_t>(chunk), progress, ctx);
    if (status != HAL_OK) {
      return status;
    }
    written = static_cast<uint16_t>(written + chunk);
    notify(progress, ctx);
  }
  return HAL_OK;
#endif
}

hal_status_t read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  if ((out == nullptr && len > 0u) ||
      static_cast<uint32_t>(addr) + len > kDeviceSize) {
    return HAL_EINVAL;
  }
#ifndef HAL_ENABLE_I2C
  if (out != nullptr && len > 0u) {
    memset(out, 0, len);
  }
  return HAL_ECONFIG;
#else
  while (len > 0u) {
    const uint8_t chunk =
        len > UINT8_MAX ? UINT8_MAX : static_cast<uint8_t>(len);
    uint8_t address[2];
    jh_store_be16(address, addr);
    if (hal_i2c_write_read_ex(s_i2c_addr, address, sizeof(address), out,
                              chunk) != HAL_OK) {
      return HAL_EIO;
    }
    addr = static_cast<uint16_t>(addr + chunk);
    out += chunk;
    len = static_cast<uint16_t>(len - chunk);
  }
  return HAL_OK;
#endif
}

hal_status_t provider_read(uint16_t addr, uint8_t *out, uint16_t len) {
  return read_bytes(addr, out, len);
}

hal_status_t provider_write(uint16_t addr, const uint8_t *data, uint16_t len,
                            hal_eeprom_progress_callback_t progress,
                            void *ctx) {
  return write_bytes(addr, data, len, progress, ctx);
}

hal_status_t commit(hal_eeprom_progress_callback_t progress, void *ctx) {
  (void)progress;
  (void)ctx;
  return HAL_OK;
}

hal_status_t verify_region(uint16_t addr, const uint8_t *expected,
                           uint16_t len) {
  uint8_t chunk[32];
  uint16_t checked = 0u;
  while (checked < len) {
    uint16_t size = static_cast<uint16_t>(len - checked);
    if (size > sizeof(chunk)) {
      size = sizeof(chunk);
    }
    const hal_status_t status =
        read_bytes(static_cast<uint16_t>(addr + checked), chunk, size);
    if (status != HAL_OK) {
      return status;
    }
    if (memcmp(chunk, expected + checked, size) != 0) {
      return HAL_EIO;
    }
    checked = static_cast<uint16_t>(checked + size);
  }
  return HAL_OK;
}

hal_status_t fill_region(uint32_t addr, uint32_t len, uint8_t value,
                         hal_eeprom_progress_callback_t progress, void *ctx) {
  uint8_t pattern[HAL_AT24C256_PAGE_SIZE];
  memset(pattern, value, sizeof(pattern));
  const uint32_t end = addr + len;
  while (addr < end) {
    uint32_t chunk = end - addr;
    if (chunk > sizeof(pattern)) {
      chunk = sizeof(pattern);
    }
    const hal_status_t status =
        write_bytes(static_cast<uint16_t>(addr), pattern,
                    static_cast<uint16_t>(chunk), progress, ctx);
    if (status != HAL_OK) {
      return status;
    }
    addr += chunk;
  }
  return HAL_OK;
}

hal_status_t replace_region(uint16_t addr, const uint8_t *data, uint16_t len,
                            uint16_t publish_size,
                            hal_eeprom_progress_callback_t progress,
                            void *ctx) {
  if (data == nullptr || publish_size == 0u || publish_size >= len ||
      static_cast<uint32_t>(addr) + len > kDeviceSize) {
    return HAL_EINVAL;
  }

  const uint16_t body_size = static_cast<uint16_t>(len - publish_size);
  hal_status_t status = fill_region(addr, publish_size, 0u, progress, ctx);
  if (status == HAL_OK) {
    status = write_bytes(static_cast<uint16_t>(addr + publish_size),
                         data + publish_size, body_size, progress, ctx);
  }
  if (status == HAL_OK) {
    status = verify_region(static_cast<uint16_t>(addr + publish_size),
                           data + publish_size, body_size);
  }
  if (status == HAL_OK) {
    status = write_bytes(addr, data, publish_size, progress, ctx);
  }
  return status == HAL_OK ? verify_region(addr, data, len) : status;
}

hal_status_t reset(hal_eeprom_progress_callback_t progress, void *ctx) {
  return fill_region(0u, kDeviceSize, 0u, progress, ctx);
}

hal_status_t region_erased(uint16_t addr, uint16_t len, bool *out_erased) {
  if (out_erased == nullptr ||
      static_cast<uint32_t>(addr) + len > kDeviceSize) {
    return HAL_EINVAL;
  }
  *out_erased = false;
  uint8_t chunk[32];
  uint16_t checked = 0u;
  while (checked < len) {
    uint16_t size = static_cast<uint16_t>(len - checked);
    if (size > sizeof(chunk)) {
      size = sizeof(chunk);
    }
    const hal_status_t status =
        read_bytes(static_cast<uint16_t>(addr + checked), chunk, size);
    if (status != HAL_OK) {
      return status;
    }
    for (uint16_t index = 0u; index < size; index++) {
      if (chunk[index] != 0xFFu) {
        return HAL_OK;
      }
    }
    checked = static_cast<uint16_t>(checked + size);
  }
  *out_erased = true;
  return HAL_OK;
}

/* An EEPROM needs no erase; append keeps the flash rule that the range must
 * read 0xFF first, so the KV log behaves the same on every medium. */
hal_status_t append(uint16_t addr, const uint8_t *data, uint16_t len,
                    hal_eeprom_progress_callback_t progress, void *ctx) {
  if (data == nullptr || len == 0u ||
      static_cast<uint32_t>(addr) + len > kDeviceSize) {
    return HAL_EINVAL;
  }
  bool erased = false;
  hal_status_t status = region_erased(addr, len, &erased);
  if (status == HAL_OK && !erased) {
    status = HAL_ESTATE;
  }
  if (status == HAL_OK) {
    status = write_bytes(addr, data, len, progress, ctx);
  }
  return status == HAL_OK ? verify_region(addr, data, len) : status;
}

hal_status_t erase(uint16_t addr, uint16_t len,
                   hal_eeprom_progress_callback_t progress, void *ctx) {
  if (static_cast<uint32_t>(addr) + len > kDeviceSize) {
    return HAL_EINVAL;
  }
  return fill_region(addr, len, 0xFFu, progress, ctx);
}

const jh_eeprom_provider_ops_t kProvider = {
    initialize, provider_read, provider_write, commit,       replace_region,
    reset,      append,        erase,          region_erased};

} // namespace

const jh_eeprom_provider_ops_t *jh_at24c256_provider_get_ops(void) {
  return &kProvider;
}

#endif /* HAL_ENABLE_EEPROM */
