#pragma once
#include <stddef.h>
#include <stdint.h>

typedef unsigned int uint;
struct i2c_hw_t {
  uint32_t intr_stat, clr_tx_abrt, clr_start_det, clr_stop_det;
  uint32_t clr_rd_req, clr_intr, intr_mask;
};
struct i2c_inst_t {
  i2c_hw_t hw;
  uint8_t received, transmitted;
  bool rx_ready;
  uint baudrate;
};
extern i2c_inst_t jh_test_i2c[2];
#define i2c0 (&jh_test_i2c[0])
#define i2c1 (&jh_test_i2c[1])
#define I2C0_IRQ 0U
#define I2C_IC_INTR_STAT_R_RX_FULL_BITS (1U << 0)
#define I2C_IC_INTR_STAT_R_RD_REQ_BITS (1U << 1)
#define I2C_IC_INTR_STAT_R_TX_ABRT_BITS (1U << 2)
#define I2C_IC_INTR_STAT_R_START_DET_BITS (1U << 3)
#define I2C_IC_INTR_STAT_R_STOP_DET_BITS (1U << 4)
#define I2C_IC_INTR_MASK_M_RX_FULL_BITS I2C_IC_INTR_STAT_R_RX_FULL_BITS
#define I2C_IC_INTR_MASK_M_RD_REQ_BITS I2C_IC_INTR_STAT_R_RD_REQ_BITS
#define I2C_IC_INTR_MASK_M_TX_ABRT_BITS I2C_IC_INTR_STAT_R_TX_ABRT_BITS
#define I2C_IC_INTR_MASK_M_START_DET_BITS I2C_IC_INTR_STAT_R_START_DET_BITS
#define I2C_IC_INTR_MASK_M_STOP_DET_BITS I2C_IC_INTR_STAT_R_STOP_DET_BITS
#define I2C_IC_INTR_MASK_RESET 0U
inline i2c_hw_t *i2c_get_hw(i2c_inst_t *i2c) { return &i2c->hw; }
inline uint i2c_init(i2c_inst_t *i2c, uint clock) {
  i2c->baudrate = clock;
  return clock;
}
inline uint i2c_set_baudrate(i2c_inst_t *i2c, uint clock) {
  i2c->baudrate = clock;
  return clock;
}
inline void i2c_deinit(i2c_inst_t *) {}
inline void i2c_set_slave_mode(i2c_inst_t *, bool, uint8_t) {}
inline uint i2c_get_read_available(i2c_inst_t *i2c) { return i2c->rx_ready; }
inline uint8_t i2c_read_byte_raw(i2c_inst_t *i2c) {
  i2c->rx_ready = false;
  return i2c->received;
}
inline void i2c_write_byte_raw(i2c_inst_t *i2c, uint8_t value) {
  i2c->transmitted = value;
}
/* Master transfers: every addressed device acknowledges. */
inline int i2c_write_timeout_us(i2c_inst_t *, uint8_t, const uint8_t *,
                                size_t len, bool, uint) {
  return (int)len;
}
inline int i2c_read_timeout_us(i2c_inst_t *, uint8_t, uint8_t *dst, size_t len,
                               bool, uint) {
  for (size_t index = 0u; index < len; ++index) {
    dst[index] = 0u;
  }
  return (int)len;
}
