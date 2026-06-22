#include "../../../hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474)

#include "../../../hal_config.h"
#ifdef HAL_ENABLE_DS18B20

#include "../../../hal_ds18b20.h"
#include "../../../hal_serial.h"
#include "../../../hal_sync.h"
#include "../../../hal_system.h"

#include "../onewire/onewire_driver.h"

#include <math.h>
#include <new>
#include <string.h>

#define DS18B20_CMD_START_CONVO (0x44u)
#define DS18B20_CMD_COPY_SCRATCH (0x48u)
#define DS18B20_CMD_READ_SCRATCH (0xBEu)
#define DS18B20_CMD_WRITE_SCRATCH (0x4Eu)
#define DS18B20_CMD_READ_POWER_SUPPLY (0xB4u)

#define DS18B20_SCRATCH_TEMP_LSB (0u)
#define DS18B20_SCRATCH_TEMP_MSB (1u)
#define DS18B20_SCRATCH_HIGH_ALARM (2u)
#define DS18B20_SCRATCH_LOW_ALARM (3u)
#define DS18B20_SCRATCH_CONFIG (4u)
#define DS18B20_SCRATCH_COUNT_REMAIN (6u)
#define DS18B20_SCRATCH_COUNT_PER_C (7u)
#define DS18B20_SCRATCH_CRC (8u)

#define DS18B20_FAMILY_DS18S20 (0x10u)
#define DS18B20_FAMILY_DS18B20 (0x28u)
#define DS18B20_FAMILY_DS1822 (0x22u)
#define DS18B20_FAMILY_DS1825 (0x3Bu)
#define DS18B20_FAMILY_DS28EA00 (0x42u)

#define DS18B20_CONFIG_9_BIT (0x1Fu)
#define DS18B20_CONFIG_10_BIT (0x3Fu)
#define DS18B20_CONFIG_11_BIT (0x5Fu)
#define DS18B20_CONFIG_12_BIT (0x7Fu)

#define DS18B20_DEVICE_DISCONNECTED_RAW (-7040)
#define DS18B20_DEVICE_FAULT_OPEN_RAW (-32512)
#define DS18B20_DEVICE_FAULT_GND_RAW (-32384)
#define DS18B20_DEVICE_FAULT_VDD_RAW (-32256)

enum ds18b20_state_t {
  DS18B20_STATE_IDLE = 0,
  DS18B20_STATE_CONVERTING,
};

struct hal_ds18b20_impl_s {
  bool in_use;
  uint8_t pin;
  bool use_rom;
  uint8_t rom[8];
  uint8_t address[8];
  hal_ds18b20_resolution_t resolution;
  uint32_t conversion_time_us;
  uint64_t conversion_deadline_us;
  ds18b20_state_t state;
  bool sample_valid;
  bool sample_fresh;
  float last_temp_c;
  bool parasite;
  uint8_t devices;
  uint8_t ds18_count;
  hal_mutex_t mutex;
  alignas(JHOneWire) uint8_t onewire_mem[sizeof(JHOneWire)];
};

static hal_ds18b20_impl_t s_pool[HAL_DS18B20_MAX_INSTANCES];

static inline JHOneWire *as_onewire(hal_ds18b20_impl_t *h) {
  return reinterpret_cast<JHOneWire *>(h->onewire_mem);
}

static void release_pool_slot(hal_ds18b20_impl_t *h) {
  hal_critical_section_enter();
  h->in_use = false;
  hal_critical_section_exit();
}

static hal_ds18b20_resolution_t
normalize_resolution(hal_ds18b20_resolution_t r) {
  switch (r) {
  case HAL_DS18B20_RES_9_BIT:
  case HAL_DS18B20_RES_10_BIT:
  case HAL_DS18B20_RES_11_BIT:
  case HAL_DS18B20_RES_12_BIT:
    return r;
  default:
    return HAL_DS18B20_RES_12_BIT;
  }
}

static uint32_t conversion_time_us_from_resolution(hal_ds18b20_resolution_t r) {
  switch (normalize_resolution(r)) {
  case HAL_DS18B20_RES_9_BIT:
    return 93750u;
  case HAL_DS18B20_RES_10_BIT:
    return 187500u;
  case HAL_DS18B20_RES_11_BIT:
    return 375000u;
  default:
    return 750000u;
  }
}

static uint8_t resolution_to_u8(hal_ds18b20_resolution_t r) {
  return (uint8_t)normalize_resolution(r);
}

static uint8_t resolution_to_config(uint8_t resolution) {
  switch (resolution) {
  case 12u:
    return DS18B20_CONFIG_12_BIT;
  case 11u:
    return DS18B20_CONFIG_11_BIT;
  case 10u:
    return DS18B20_CONFIG_10_BIT;
  case 9u:
  default:
    return DS18B20_CONFIG_9_BIT;
  }
}

static bool ds18b20_valid_family(const uint8_t *address) {
  if (address == NULL) {
    return false;
  }

  switch (address[0]) {
  case DS18B20_FAMILY_DS18S20:
  case DS18B20_FAMILY_DS18B20:
  case DS18B20_FAMILY_DS1822:
  case DS18B20_FAMILY_DS1825:
  case DS18B20_FAMILY_DS28EA00:
    return true;
  default:
    return false;
  }
}

static bool ds18b20_valid_address(const uint8_t *address) {
  if (address == NULL) {
    return false;
  }
  return JHOneWire::crc8(address, 7u) == address[7];
}

static bool scratchpad_is_all_zeros(const uint8_t *scratchpad, size_t len) {
  if (scratchpad == NULL) {
    return true;
  }

  for (size_t i = 0u; i < len; ++i) {
    if (scratchpad[i] != 0u) {
      return false;
    }
  }
  return true;
}

static bool ds18b20_read_scratchpad(hal_ds18b20_impl_t *h,
                                    const uint8_t *address,
                                    uint8_t scratchpad[9]) {
  JHOneWire *ow = as_onewire(h);
  int present = ow->reset();
  if (present == 0) {
    return false;
  }

  ow->select(address);
  ow->write(DS18B20_CMD_READ_SCRATCH);
  for (uint8_t i = 0u; i < 9u; ++i) {
    scratchpad[i] = ow->read();
  }

  present = ow->reset();
  return present == 1;
}

static bool ds18b20_is_connected(hal_ds18b20_impl_t *h, const uint8_t *address,
                                 uint8_t scratchpad[9]) {
  if (scratchpad == NULL) {
    uint8_t tmp[9] = {};
    return ds18b20_is_connected(h, address, tmp);
  }

  const bool ok = ds18b20_read_scratchpad(h, address, scratchpad);
  return ok && !scratchpad_is_all_zeros(scratchpad, 9u) &&
         (JHOneWire::crc8(scratchpad, 8u) == scratchpad[DS18B20_SCRATCH_CRC]);
}

static bool ds18b20_read_power_supply(hal_ds18b20_impl_t *h,
                                      const uint8_t *address) {
  JHOneWire *ow = as_onewire(h);
  bool parasite = false;

  ow->reset();
  if (address == NULL) {
    ow->skip();
  } else {
    ow->select(address);
  }

  ow->write(DS18B20_CMD_READ_POWER_SUPPLY);
  if (ow->read_bit() == 0u) {
    parasite = true;
  }
  ow->reset();
  return parasite;
}

static uint8_t ds18b20_get_resolution(hal_ds18b20_impl_t *h,
                                      const uint8_t *address) {
  if (address == NULL) {
    return 0u;
  }

  if (address[0] == DS18B20_FAMILY_DS18S20) {
    return 12u;
  }

  uint8_t scratchpad[9] = {};
  if (ds18b20_is_connected(h, address, scratchpad)) {
    if ((address[0] == DS18B20_FAMILY_DS1825) &&
        (scratchpad[DS18B20_SCRATCH_CONFIG] & 0x80u)) {
      return 12u;
    }

    switch (scratchpad[DS18B20_SCRATCH_CONFIG]) {
    case DS18B20_CONFIG_12_BIT:
      return 12u;
    case DS18B20_CONFIG_11_BIT:
      return 11u;
    case DS18B20_CONFIG_10_BIT:
      return 10u;
    case DS18B20_CONFIG_9_BIT:
      return 9u;
    default:
      break;
    }
  }

  return 0u;
}

static bool ds18b20_save_scratchpad(hal_ds18b20_impl_t *h,
                                    const uint8_t *address) {
  JHOneWire *ow = as_onewire(h);
  if (ow->reset() == 0u) {
    return false;
  }

  if (address == NULL) {
    ow->skip();
  } else {
    ow->select(address);
  }

  ow->write(DS18B20_CMD_COPY_SCRATCH, h->parasite ? 1u : 0u);
  hal_delay_ms(20u);
  return ow->reset() == 1u;
}

static void ds18b20_write_scratchpad(hal_ds18b20_impl_t *h,
                                     const uint8_t *address,
                                     const uint8_t scratchpad[9]) {
  JHOneWire *ow = as_onewire(h);
  ow->reset();
  ow->select(address);
  ow->write(DS18B20_CMD_WRITE_SCRATCH);
  ow->write(scratchpad[DS18B20_SCRATCH_HIGH_ALARM]);
  ow->write(scratchpad[DS18B20_SCRATCH_LOW_ALARM]);

  if (address[0] != DS18B20_FAMILY_DS18S20) {
    ow->write(scratchpad[DS18B20_SCRATCH_CONFIG]);
  }

  (void)ds18b20_save_scratchpad(h, address);
}

static bool ds18b20_set_resolution(hal_ds18b20_impl_t *h,
                                   const uint8_t *address, uint8_t resolution) {
  if (address == NULL) {
    return false;
  }

  bool success = false;

  if (address[0] == DS18B20_FAMILY_DS18S20) {
    success = true;
  } else {
    if (resolution < 9u) {
      resolution = 9u;
    } else if (resolution > 12u) {
      resolution = 12u;
    }

    uint8_t scratchpad[9] = {};
    if (ds18b20_is_connected(h, address, scratchpad)) {
      const uint8_t new_value = resolution_to_config(resolution);
      if (scratchpad[DS18B20_SCRATCH_CONFIG] != new_value) {
        scratchpad[DS18B20_SCRATCH_CONFIG] = new_value;
        ds18b20_write_scratchpad(h, address, scratchpad);
      }
      success = true;
    }
  }

  if (success) {
    h->resolution = (hal_ds18b20_resolution_t)resolution;
  }

  return success;
}

static void refresh_conversion_timing(hal_ds18b20_impl_t *h) {
  const uint8_t sensor_resolution = ds18b20_get_resolution(h, h->address);
  if (sensor_resolution >= 9u && sensor_resolution <= 12u) {
    h->resolution = (hal_ds18b20_resolution_t)sensor_resolution;
  }
  h->conversion_time_us = conversion_time_us_from_resolution(h->resolution);
}

static void ds18b20_begin_scan(hal_ds18b20_impl_t *h) {
  JHOneWire *ow = as_onewire(h);
  uint8_t address[8] = {};

  for (uint8_t retry = 0u; retry < 3u; ++retry) {
    ow->reset_search();
    h->devices = 0u;
    h->ds18_count = 0u;

    hal_delay_ms(50u);

    while (ow->search(address, true)) {
      if (ds18b20_valid_address(address)) {
        h->devices++;

        if (ds18b20_valid_family(address)) {
          h->ds18_count++;

          if (!h->parasite && ds18b20_read_power_supply(h, address)) {
            h->parasite = true;
          }
        }
      }
    }

    if (h->devices > 0u) {
      break;
    }
  }
}

static bool ds18b20_get_address(hal_ds18b20_impl_t *h, uint8_t *address,
                                uint8_t index) {
  if (address == NULL || index >= h->devices) {
    return false;
  }

  uint8_t depth = 0u;
  JHOneWire *ow = as_onewire(h);
  ow->reset_search();

  while ((depth <= index) && ow->search(address, true)) {
    if ((depth == index) && ds18b20_valid_address(address)) {
      return true;
    }
    ++depth;
  }

  return false;
}

static bool resolve_sensor_address(hal_ds18b20_impl_t *h) {
  if (h->use_rom) {
    if (!ds18b20_valid_address(h->rom) || !ds18b20_valid_family(h->rom) ||
        !ds18b20_is_connected(h, h->rom, NULL)) {
      return false;
    }
    memcpy(h->address, h->rom, sizeof(h->address));
    return true;
  }

  ds18b20_begin_scan(h);
  if (h->ds18_count == 0u) {
    return false;
  }

  uint8_t discovered[8] = {};
  if (!ds18b20_get_address(h, discovered, 0u) ||
      !ds18b20_valid_family(discovered) ||
      !ds18b20_is_connected(h, discovered, NULL)) {
    return false;
  }

  memcpy(h->address, discovered, sizeof(h->address));

  if (h->ds18_count > 1u) {
    hal_derr("hal_ds18b20_init: multiple sensors on pin %u; using first "
             "discovered address",
             (unsigned)h->pin);
  }

  return true;
}

static bool ds18b20_request_temperature(hal_ds18b20_impl_t *h,
                                        const uint8_t *address) {
  const uint8_t resolution = ds18b20_get_resolution(h, address);
  if (resolution == 0u) {
    return false;
  }

  JHOneWire *ow = as_onewire(h);
  ow->reset();
  ow->select(address);
  ow->write(DS18B20_CMD_START_CONVO, h->parasite ? 1u : 0u);
  return true;
}

static int32_t ds18b20_calculate_temperature(const uint8_t *address,
                                             const uint8_t scratchpad[9]) {
  int32_t fp_temperature = 0;

  int32_t neg = 0x0;
  if (scratchpad[DS18B20_SCRATCH_TEMP_MSB] & 0x80u) {
    neg = 0xFFF80000;
  }

  if ((address[0] == DS18B20_FAMILY_DS1825) &&
      (scratchpad[DS18B20_SCRATCH_CONFIG] & 0x80u)) {
    if (scratchpad[DS18B20_SCRATCH_TEMP_LSB] & 1u) {
      if (scratchpad[DS18B20_SCRATCH_HIGH_ALARM] & 1u) {
        return DS18B20_DEVICE_FAULT_OPEN_RAW;
      } else if ((scratchpad[DS18B20_SCRATCH_HIGH_ALARM] >> 1u) & 1u) {
        return DS18B20_DEVICE_FAULT_GND_RAW;
      } else if ((scratchpad[DS18B20_SCRATCH_HIGH_ALARM] >> 2u) & 1u) {
        return DS18B20_DEVICE_FAULT_VDD_RAW;
      } else {
        return DS18B20_DEVICE_DISCONNECTED_RAW;
      }
    }

    fp_temperature =
        (((int32_t)scratchpad[DS18B20_SCRATCH_TEMP_MSB]) << 11) |
        (((int32_t)scratchpad[DS18B20_SCRATCH_TEMP_LSB] & 0xFC) << 3) | neg;
  } else {
    fp_temperature = (((int16_t)scratchpad[DS18B20_SCRATCH_TEMP_MSB]) << 11) |
                     (((int16_t)scratchpad[DS18B20_SCRATCH_TEMP_LSB]) << 3) |
                     neg;
  }

  if ((address[0] == DS18B20_FAMILY_DS18S20) &&
      (scratchpad[DS18B20_SCRATCH_COUNT_PER_C] != 0u)) {
    fp_temperature = (((fp_temperature & 0xFFF0) << 3) - 32 +
                      (((scratchpad[DS18B20_SCRATCH_COUNT_PER_C] -
                         scratchpad[DS18B20_SCRATCH_COUNT_REMAIN])
                        << 7) /
                       scratchpad[DS18B20_SCRATCH_COUNT_PER_C])) |
                     neg;
  }

  return fp_temperature;
}

static float ds18b20_raw_to_celsius(int32_t raw) {
  if (raw <= DS18B20_DEVICE_DISCONNECTED_RAW) {
    return -127.0f;
  }
  return (float)raw * 0.0078125f;
}

static float ds18b20_get_temp_c(hal_ds18b20_impl_t *h, const uint8_t *address) {
  uint8_t scratchpad[9] = {};
  if (ds18b20_is_connected(h, address, scratchpad)) {
    return ds18b20_raw_to_celsius(
        ds18b20_calculate_temperature(address, scratchpad));
  }
  return -127.0f;
}

hal_ds18b20_t hal_ds18b20_init(const hal_ds18b20_config_t *cfg) {
  if (!cfg) {
    return NULL;
  }

  hal_critical_section_enter();
  int slot = -1;
  for (int i = 0; i < HAL_DS18B20_MAX_INSTANCES; ++i) {
    if (!s_pool[i].in_use) {
      slot = i;
      s_pool[i].in_use = true;
      break;
    }
  }
  hal_critical_section_exit();

  HAL_ASSERT(
      slot >= 0,
      "hal_ds18b20: pool exhausted - increase HAL_DS18B20_MAX_INSTANCES");
  if (slot < 0) {
    return NULL;
  }

  hal_ds18b20_impl_t *h = &s_pool[slot];
  h->pin = cfg->data_pin;
  h->use_rom = cfg->use_rom;
  memset(h->rom, 0, sizeof(h->rom));
  memset(h->address, 0, sizeof(h->address));
  memcpy(h->rom, cfg->rom_code, sizeof(h->rom));
  h->resolution = normalize_resolution(cfg->resolution_hint);
  h->conversion_time_us = conversion_time_us_from_resolution(h->resolution);
  h->conversion_deadline_us = 0u;
  h->state = DS18B20_STATE_IDLE;
  h->sample_valid = false;
  h->sample_fresh = false;
  h->last_temp_c = NAN;
  h->parasite = false;
  h->devices = 0u;
  h->ds18_count = 0u;
  h->mutex = NULL;

  h->mutex = hal_mutex_create();
  if (!h->mutex) {
    release_pool_slot(h);
    return NULL;
  }

  bool onewire_ready = false;
  new (h->onewire_mem) JHOneWire(h->pin);
  onewire_ready = true;

  if (!resolve_sensor_address(h)) {
    hal_derr("hal_ds18b20_init: sensor not found on pin %u", (unsigned)h->pin);
    if (onewire_ready) {
      as_onewire(h)->~JHOneWire();
    }
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    release_pool_slot(h);
    return NULL;
  }

  const uint8_t requested_resolution = resolution_to_u8(h->resolution);
  if (!ds18b20_set_resolution(h, h->address, requested_resolution)) {
    hal_derr("hal_ds18b20_init: setResolution(%u-bit) failed on pin %u; using "
             "fallback timing",
             (unsigned)requested_resolution, (unsigned)h->pin);
  }

  refresh_conversion_timing(h);
  return h;
}

void hal_ds18b20_deinit(hal_ds18b20_t h) {
  if (!h) {
    return;
  }

  hal_mutex_lock(h->mutex);
  as_onewire(h)->~JHOneWire();

  hal_mutex_t m = h->mutex;
  h->mutex = NULL;
  hal_mutex_unlock(m);
  hal_mutex_destroy(m);
  release_pool_slot(h);
}

bool hal_ds18b20_request(hal_ds18b20_t h) {
  if (!h) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  if (h->state == DS18B20_STATE_CONVERTING) {
    hal_mutex_unlock(h->mutex);
    return false;
  }

  if (!ds18b20_is_connected(h, h->address, NULL)) {
    hal_mutex_unlock(h->mutex);
    return false;
  }

  if (!ds18b20_request_temperature(h, h->address)) {
    hal_mutex_unlock(h->mutex);
    return false;
  }

  h->conversion_deadline_us = hal_micros64() + h->conversion_time_us;
  h->state = DS18B20_STATE_CONVERTING;
  hal_mutex_unlock(h->mutex);
  return true;
}

void hal_ds18b20_poll(hal_ds18b20_t h) {
  if (!h) {
    return;
  }

  hal_mutex_lock(h->mutex);
  if (h->state != DS18B20_STATE_CONVERTING) {
    hal_mutex_unlock(h->mutex);
    return;
  }

  if (hal_micros64() < h->conversion_deadline_us) {
    hal_mutex_unlock(h->mutex);
    return;
  }

  const float temp_c = ds18b20_get_temp_c(h, h->address);
  if (!isnan(temp_c) && temp_c >= -55.0f && temp_c <= 125.0f) {
    h->last_temp_c = temp_c;
    h->sample_valid = true;
    h->sample_fresh = true;
    refresh_conversion_timing(h);
  }

  h->state = DS18B20_STATE_IDLE;
  hal_mutex_unlock(h->mutex);
}

bool hal_ds18b20_is_busy(hal_ds18b20_t h) {
  if (!h) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  const bool busy = (h->state == DS18B20_STATE_CONVERTING);
  hal_mutex_unlock(h->mutex);
  return busy;
}

bool hal_ds18b20_take_latest(hal_ds18b20_t h, float *temp_c, bool *fresh) {
  if (!h || !temp_c) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  if (!h->sample_valid) {
    hal_mutex_unlock(h->mutex);
    return false;
  }

  *temp_c = h->last_temp_c;
  if (fresh) {
    *fresh = h->sample_fresh;
  }
  h->sample_fresh = false;
  hal_mutex_unlock(h->mutex);
  return true;
}

#endif /* HAL_ENABLE_DS18B20 */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
