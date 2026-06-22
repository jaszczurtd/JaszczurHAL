#pragma once

#include "../../../hal_config.h"

#ifdef HAL_ENABLE_DIGIPOT

#include "../../../hal_digipot.h"
#include "../../../hal_i2c.h"

#include <stddef.h>
#include <stdint.h>

typedef struct hal_digipot_ops {
  bool (*validate)(const hal_digipot_config_t *cfg);
  bool (*init)(hal_digipot_config_t *cfg);
  bool (*set_resistance)(const hal_digipot_config_t *cfg, uint32_t ohms);
  uint16_t (*step_count)(void);
} hal_digipot_ops_t;

#ifdef HAL_ENABLE_MCP401X
const hal_digipot_ops_t *hal_digipot_mcp401x_ops(void);
#endif

#ifdef HAL_ENABLE_MAX5395
const hal_digipot_ops_t *hal_digipot_max5395_ops(void);
#endif

/* I2C transaction helpers shared by chip implementations. */
static inline bool hal_digipot_i2c_write(uint8_t bus, uint8_t addr,
                                         const uint8_t *data, size_t n) {
  hal_i2c_begin_transmission_bus(bus, addr);
  for (size_t i = 0; i < n; ++i) {
    hal_i2c_write_bus(bus, data[i]);
  }
  return hal_i2c_end_transmission_bus(bus) == 0u;
}

/* Raw single-byte read with no register/command prefix. */
static inline bool hal_digipot_i2c_read_raw(uint8_t bus, uint8_t addr,
                                            uint8_t *out) {
  if (!out) {
    return false;
  }
  return hal_i2c_read_bytes_bus(bus, addr, out, 1u);
}

/* Command-then-read of one byte. */
static inline bool hal_digipot_i2c_read_cmd(uint8_t bus, uint8_t addr,
                                            uint8_t cmd, uint8_t *out) {
  if (!out) {
    return false;
  }
  return hal_i2c_write_read_bus(bus, addr, &cmd, 1u, out, 1u);
}

static inline uint8_t hal_digipot_scale_to_wiper_trunc(uint32_t value,
                                                       uint32_t full_scale,
                                                       uint16_t steps) {
  return (uint8_t)(value * (uint32_t)steps / full_scale);
}

static inline uint8_t
hal_digipot_scale_to_wiper_round_nearest(uint32_t value, uint32_t full_scale,
                                         uint16_t steps) {
  const uint32_t scaled = value * (uint32_t)steps;
  uint8_t wiper = (uint8_t)(scaled / full_scale);
  const uint32_t remainder = scaled % full_scale;
  wiper += ((remainder * 2u) > full_scale) ? 1u : 0u;
  return wiper;
}

#endif /* HAL_ENABLE_DIGIPOT */
