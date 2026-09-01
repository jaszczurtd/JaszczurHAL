#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_EEPROM

#include "hal/storage/jh_eeprom_provider.h"
#include "hal_mock.h"

#include <string.h>

namespace {

uint8_t s_memory[MOCK_EEPROM_BUF_SIZE] = {};
hal_eeprom_type_t s_type = HAL_EEPROM_AT24C256;
uint16_t s_size = 0u;
bool s_committed = false;
uint32_t s_write_count = 0u;
hal_status_t s_io_status = HAL_OK;
hal_status_t s_commit_status = HAL_OK;
hal_mock_eeprom_replace_fail_phase_t s_replace_fail_phase =
    HAL_MOCK_EEPROM_REPLACE_FAIL_NONE;

uint16_t selected_size(const jh_eeprom_provider_config_t *config) {
  if (config->requested_type == HAL_EEPROM_AT24C256) {
    return static_cast<uint16_t>(MOCK_EEPROM_BUF_SIZE);
  }
  return config->requested_size;
}

hal_status_t initialize(const jh_eeprom_provider_config_t *config,
                        jh_eeprom_provider_info_t *out_info) {
  if (config == nullptr || out_info == nullptr) {
    return HAL_EINVAL;
  }
  if (s_io_status != HAL_OK) {
    return s_io_status;
  }
  const uint16_t size = selected_size(config);
  if (size > MOCK_EEPROM_BUF_SIZE) {
    return HAL_EINVAL;
  }
  s_type = config->requested_type;
  s_size = size;
  s_committed = false;
  s_write_count = 0u;
  memset(s_memory, 0, sizeof(s_memory));
  out_info->type = s_type;
  out_info->size = s_size;
  return HAL_OK;
}

bool range_valid(uint16_t addr, uint16_t len) {
  return addr <= s_size && len <= static_cast<uint16_t>(s_size - addr);
}

hal_status_t read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  if (s_io_status != HAL_OK) {
    return s_io_status;
  }
  if ((out == nullptr && len > 0u) || !range_valid(addr, len)) {
    return HAL_EINVAL;
  }
  if (len > 0u) {
    memcpy(out, s_memory + addr, len);
  }
  return HAL_OK;
}

hal_status_t write_bytes(uint16_t addr, const uint8_t *data, uint16_t len,
                         hal_eeprom_progress_callback_t progress, void *ctx) {
  (void)progress;
  (void)ctx;
  if (s_io_status != HAL_OK) {
    return s_io_status;
  }
  if ((data == nullptr && len > 0u) || !range_valid(addr, len)) {
    return HAL_EINVAL;
  }
  if (len > 0u) {
    memcpy(s_memory + addr, data, len);
    s_write_count += len;
  }
  return HAL_OK;
}

void notify(hal_eeprom_progress_callback_t progress, void *ctx) {
  if (progress != nullptr) {
    progress(ctx);
  }
}

hal_status_t commit(hal_eeprom_progress_callback_t progress, void *ctx) {
  if (s_io_status != HAL_OK) {
    return s_io_status;
  }
  if (s_commit_status != HAL_OK) {
    return s_commit_status;
  }
  s_committed = true;
  notify(progress, ctx);
  return HAL_OK;
}

hal_status_t replace_region(uint16_t addr, const uint8_t *data, uint16_t len,
                            uint16_t publish_size,
                            hal_eeprom_progress_callback_t progress,
                            void *ctx) {
  if (s_io_status != HAL_OK) {
    return s_io_status;
  }
  if (s_commit_status != HAL_OK) {
    return s_commit_status;
  }
  if (data == nullptr || publish_size == 0u || publish_size >= len ||
      !range_valid(addr, len)) {
    return HAL_EINVAL;
  }

  memset(s_memory + addr, 0, publish_size);
  s_write_count += publish_size;
  notify(progress, ctx);
  if (s_replace_fail_phase == HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_INVALIDATE) {
    return HAL_EIO;
  }

  const uint16_t body_size = static_cast<uint16_t>(len - publish_size);
  memcpy(s_memory + addr + publish_size, data + publish_size, body_size);
  s_write_count += body_size;
  notify(progress, ctx);
  if (s_replace_fail_phase == HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_BODY) {
    return HAL_EIO;
  }
  if (memcmp(s_memory + addr + publish_size, data + publish_size, body_size) !=
      0) {
    return HAL_EIO;
  }
  if (s_replace_fail_phase == HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_VERIFY) {
    return HAL_EIO;
  }

  memcpy(s_memory + addr, data, publish_size);
  s_write_count += publish_size;
  s_committed = true;
  notify(progress, ctx);
  if (s_replace_fail_phase == HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_PUBLISH) {
    return HAL_EIO;
  }
  return memcmp(s_memory + addr, data, len) == 0 ? HAL_OK : HAL_EIO;
}

hal_status_t reset(hal_eeprom_progress_callback_t progress, void *ctx) {
  if (s_io_status != HAL_OK) {
    return s_io_status;
  }
  memset(s_memory, 0, sizeof(s_memory));
  s_committed = true;
  notify(progress, ctx);
  return HAL_OK;
}

const jh_eeprom_provider_ops_t kProvider = {
    initialize, read_bytes, write_bytes, commit, replace_region, reset};

} // namespace

const jh_eeprom_provider_ops_t *
jh_eeprom_provider_get_ops(hal_eeprom_type_t type) {
  return type >= HAL_EEPROM_DEFAULT && type <= HAL_EEPROM_STM32_FLASH
             ? &kProvider
             : nullptr;
}

uint8_t hal_mock_eeprom_get_byte(uint16_t addr) {
  return addr < s_size ? s_memory[addr] : 0u;
}

hal_eeprom_type_t hal_mock_eeprom_get_type(void) { return s_type; }

bool hal_mock_eeprom_was_committed(void) { return s_committed; }

void hal_mock_eeprom_clear_committed_flag(void) { s_committed = false; }

uint32_t hal_mock_eeprom_get_write_count(void) { return s_write_count; }

void hal_mock_eeprom_clear_write_count(void) { s_write_count = 0u; }

void hal_mock_eeprom_set_io_status(hal_status_t status) {
  s_io_status = status;
}

void hal_mock_eeprom_set_commit_status(hal_status_t status) {
  s_commit_status = status;
}

void hal_mock_eeprom_set_replace_fail_phase(
    hal_mock_eeprom_replace_fail_phase_t phase) {
  s_replace_fail_phase = phase;
}

void hal_mock_eeprom_reset(void) {
  memset(s_memory, 0, sizeof(s_memory));
  s_type = HAL_EEPROM_AT24C256;
  s_size = 0u;
  s_committed = false;
  s_write_count = 0u;
  s_io_status = HAL_OK;
  s_commit_status = HAL_OK;
  s_replace_fail_phase = HAL_MOCK_EEPROM_REPLACE_FAIL_NONE;
  jh_eeprom_mock_reset_facade();
}

#endif /* HAL_ENABLE_EEPROM */
#endif /* HAL_TARGET_IS_MOCK */
