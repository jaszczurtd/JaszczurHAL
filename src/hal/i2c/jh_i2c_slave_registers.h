#pragma once

#include "hal/i2c/hal_i2c_slave.h"
#include <string.h>

/* Backend callers hold their register-map lock. */
typedef struct {
  bool valid;
#ifdef HAL_ENABLE_I2C_SLAVE_SNAPSHOT
  uint8_t bytes[HAL_I2C_SLAVE_REG_MAP_SIZE];
#endif
} jh_i2c_slave_snapshot_t;

static inline const uint8_t *
jh_i2c_slave_read_view(jh_i2c_slave_snapshot_t *snapshot,
                       const uint8_t *registers) {
#ifdef HAL_ENABLE_I2C_SLAVE_SNAPSHOT
  if (!snapshot->valid) {
    memcpy(snapshot->bytes, registers, HAL_I2C_SLAVE_REG_MAP_SIZE);
    snapshot->valid = true;
  }
  return snapshot->bytes;
#else
  (void)snapshot;
  return registers;
#endif
}

static inline hal_status_t jh_i2c_slave_write_block(uint8_t *registers,
                                                    uint8_t reg,
                                                    const uint8_t *data,
                                                    size_t count) {
  if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE ||
      count > HAL_I2C_SLAVE_REG_MAP_SIZE - (size_t)reg ||
      (data == NULL && count != 0U)) {
    return HAL_EINVAL;
  }
  if (count != 0U) {
    memcpy(registers + reg, data, count);
  }
  return HAL_OK;
}
