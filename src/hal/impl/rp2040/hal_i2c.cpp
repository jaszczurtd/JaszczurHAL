#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_I2C

#include "hal/core/hal_mutex_once.h"
#include "hal/i2c/hal_i2c.h"
#include "hal/i2c/hal_i2c_internal.h"
#include "hal/system/hal_sync.h"

#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <pico/error.h>
#include <pico/platform.h>
#include <pico/time.h>
#include <stdint.h>

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

#define RP2040_I2C_BUF_SIZE 255u
#define RP2040_I2C_TIMEOUT_US 100000u

typedef struct {
  uint8_t rx_buf[RP2040_I2C_BUF_SIZE];
  size_t rx_len;
  size_t rx_pos;
  uint8_t tx_buf[RP2040_I2C_BUF_SIZE];
  size_t tx_len;
  hal_i2c_address_t cur_addr;
#ifdef HAL_ENABLE_I2C_10BIT
  hal_i2c_addr_mode_t addr_mode;
#endif
  uint8_t sda_pin;
  uint8_t scl_pin;
  uint32_t clock_hz;
  uint32_t actual_clock_hz;
  bool initialized;
  hal_mutex_t mutex;
  volatile uintptr_t lock_owner;
  volatile uint32_t lock_depth;
  volatile uint32_t transaction_count;
} i2c_bus_state_t;

static i2c_bus_state_t s_i2c[2] = {};

static inline bool i2c_bus_valid(uint8_t bus) { return bus <= 1u; }

bool jh_hal_i2c_bus_is_initialized(uint8_t bus) {
  return i2c_bus_valid(bus) && s_i2c[bus].initialized;
}

#ifdef HAL_ENABLE_I2C_10BIT
bool jh_hal_i2c_bus_is_10bit(uint8_t bus) {
  return i2c_bus_valid(bus) && s_i2c[bus].addr_mode == HAL_I2C_ADDR_MODE_10BIT;
}
#endif

static inline uint8_t i2c_bus_index(uint8_t bus) {
  HAL_ASSERT(bus <= 1u, "hal_i2c: invalid bus index");
  return (bus <= 1u) ? bus : 0u;
}

static inline i2c_inst_t *i2c_bus_hw(uint8_t bus) {
  return i2c_bus_index(bus) == 1u ? i2c1 : i2c0;
}

static inline i2c_bus_state_t *i2c_state(uint8_t bus) {
  return &s_i2c[i2c_bus_index(bus)];
}

static void i2c_ensure_mutex(uint8_t bus) {
  uint8_t idx = i2c_bus_index(bus);
  (void)jh_hal_mutex_create_once(&s_i2c[idx].mutex);
}

static uintptr_t i2c_current_owner_token(void) {
#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  return (task != NULL) ? (uintptr_t)task
                        : (UINTPTR_MAX - (uintptr_t)get_core_num());
#else
  return (uintptr_t)(get_core_num() + 1u);
#endif
}

static void i2c_lock_idx(uint8_t idx) {
  i2c_ensure_mutex(idx);
  const uintptr_t owner = i2c_current_owner_token();
  if ((s_i2c[idx].lock_depth > 0u) && (s_i2c[idx].lock_owner == owner)) {
    s_i2c[idx].lock_depth++;
    return;
  }

  hal_mutex_lock(s_i2c[idx].mutex);
  s_i2c[idx].lock_owner = owner;
  s_i2c[idx].lock_depth = 1u;
}

static void i2c_unlock_idx(uint8_t idx) {
  i2c_ensure_mutex(idx);
  const uintptr_t owner = i2c_current_owner_token();
  HAL_ASSERT((s_i2c[idx].lock_depth > 0u) && (s_i2c[idx].lock_owner == owner),
             "hal_i2c_unlock: bus is not locked by this context");
  if ((s_i2c[idx].lock_depth == 0u) || (s_i2c[idx].lock_owner != owner)) {
    return;
  }

  s_i2c[idx].lock_depth--;
  if (s_i2c[idx].lock_depth == 0u) {
    s_i2c[idx].lock_owner = 0u;
    hal_mutex_unlock(s_i2c[idx].mutex);
  }
}

static uint32_t i2c_normalize_clock(uint32_t clock_hz) {
  if (clock_hz == 0u) {
    return HAL_I2C_CLOCK_STANDARD_HZ;
  }
  if (clock_hz > HAL_I2C_CLOCK_FAST_PLUS_HZ) {
    return HAL_I2C_CLOCK_FAST_PLUS_HZ;
  }
  return clock_hz;
}

static void i2c_hw_configure_pins(uint8_t sda_pin, uint8_t scl_pin) {
  gpio_set_function(sda_pin, GPIO_FUNC_I2C);
  gpio_set_function(scl_pin, GPIO_FUNC_I2C);
  gpio_pull_up(sda_pin);
  gpio_pull_up(scl_pin);
}

static void i2c_hw_init_bus(uint8_t idx) {
  i2c_bus_state_t *st = &s_i2c[idx];
  i2c_hw_configure_pins(st->sda_pin, st->scl_pin);
  st->actual_clock_hz = i2c_init(i2c_bus_hw(idx), st->clock_hz);
}

#ifdef HAL_ENABLE_I2C_10BIT
/* i2c_init() (called by i2c_hw_init_bus() above) assigns IC_CON outright
 * without the 10-bit bit, so every hal_i2c_init_bus[_10bit]() call already
 * leaves the controller in a clean 7-bit state; this only needs to OR the
 * 10-bit mode bit back in for a 10-bit init. Kept set for the controller's
 * lifetime per the addressing-mode contract (hal_i2c_address_t). */
static void i2c_hw_enable_10bit_mode(uint8_t idx) {
  i2c_hw_t *hw = i2c_get_hw(i2c_bus_hw(idx));
  hw->enable = 0;
  hw_write_masked(&hw->con, I2C_IC_CON_IC_10BITADDR_MASTER_BITS,
                  I2C_IC_CON_IC_10BITADDR_MASTER_BITS);
  hw->enable = 1;
}

/* Shared mutable state for i2c_10bit_write_timeout_us()/
 * i2c_10bit_read_timeout_us(): both adapt the same Pico SDK FIFO/abort/
 * timeout skeleton around a different per-byte body, so the skeleton's
 * setup and bookkeeping fields live here once instead of twice. */
struct i2c_10bit_xfer_ctx {
  i2c_hw_t *hw;
  absolute_time_t deadline;
  int ilen;
  int byte_ctr;
  bool abort;
  bool timeout;
  uint32_t abort_reason;
};

/* Program IC_TAR for this transfer and initialize the shared loop state.
 * IC_10BITADDR_MASTER is expected to already be set by
 * i2c_hw_enable_10bit_mode() for the controller's lifetime, so this only
 * programs IC_TAR per transfer, matching the 7-bit i2c_write_timeout_us()/
 * i2c_read_timeout_us() call sites it parallels. */
static i2c_10bit_xfer_ctx i2c_10bit_begin_xfer(i2c_inst_t *i2c, uint16_t addr,
                                               size_t len, uint timeout_us) {
  i2c_10bit_xfer_ctx ctx;
  ctx.hw = i2c_get_hw(i2c);
  ctx.hw->enable = 0;
  ctx.hw->tar = addr;
  ctx.hw->enable = 1;
  ctx.deadline = make_timeout_time_us(timeout_us);
  ctx.ilen = (int)len;
  ctx.byte_ctr = 0;
  ctx.abort = false;
  ctx.timeout = false;
  ctx.abort_reason = 0u;
  return ctx;
}

/* Adapted from the pinned Pico SDK's i2c_write_blocking_internal()/
 * i2c_read_blocking_internal() (third_party/pico-sdk/.../hardware_i2c/i2c.c,
 * BSD-3-Clause): same FIFO/abort/timeout handling, but addr is not limited to
 * uint8_t/0x7F - IC_TAR already carries the full 10-bit value in silicon
 * (I2C_IC_TAR_IC_TAR_BITS = 0x3FF) - and abort-reason checks recognise the
 * 10-bit address-phase NACK bits (ABRT_10ADDR1/2_NOACK) alongside the 7-bit
 * one. */
static int i2c_10bit_write_timeout_us(i2c_inst_t *i2c, uint16_t addr,
                                      const uint8_t *src, size_t len,
                                      bool nostop, uint timeout_us) {
  if (len == 0u) {
    return 0;
  }
  i2c_10bit_xfer_ctx ctx = i2c_10bit_begin_xfer(i2c, addr, len, timeout_us);
  for (; ctx.byte_ctr < ctx.ilen; ++ctx.byte_ctr) {
    const bool first = ctx.byte_ctr == 0;
    const bool last = ctx.byte_ctr == ctx.ilen - 1;

    ctx.hw->data_cmd = (uint32_t)((first && i2c->restart_on_next) ? 1u : 0u)
                           << I2C_IC_DATA_CMD_RESTART_LSB |
                       (uint32_t)((last && !nostop) ? 1u : 0u)
                           << I2C_IC_DATA_CMD_STOP_LSB |
                       src[ctx.byte_ctr];

    while (!ctx.timeout &&
           !(ctx.hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_EMPTY_BITS)) {
      ctx.timeout = time_reached(ctx.deadline);
      ctx.abort |= ctx.timeout;
      tight_loop_contents();
    }

    if (!ctx.timeout) {
      ctx.abort_reason = ctx.hw->tx_abrt_source;
      if (ctx.abort_reason) {
        (void)ctx.hw->clr_tx_abrt;
        ctx.abort = true;
      }
      if (ctx.abort || (last && !nostop)) {
        while (!ctx.timeout &&
               !(ctx.hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS)) {
          ctx.timeout = time_reached(ctx.deadline);
          ctx.abort |= ctx.timeout;
          tight_loop_contents();
        }
        if (!ctx.timeout) {
          (void)ctx.hw->clr_stop_det;
        }
      }
    }
    if (ctx.abort) {
      break;
    }
  }

  int rval;
  if (ctx.abort) {
    if (ctx.timeout) {
      rval = PICO_ERROR_TIMEOUT;
    } else if (!ctx.abort_reason ||
               (ctx.abort_reason &
                (I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS |
                 I2C_IC_TX_ABRT_SOURCE_ABRT_10ADDR1_NOACK_BITS |
                 I2C_IC_TX_ABRT_SOURCE_ABRT_10ADDR2_NOACK_BITS))) {
      rval = PICO_ERROR_GENERIC;
    } else if (ctx.abort_reason &
               I2C_IC_TX_ABRT_SOURCE_ABRT_TXDATA_NOACK_BITS) {
      rval = ctx.byte_ctr;
    } else {
      rval = PICO_ERROR_GENERIC;
    }
  } else {
    rval = ctx.byte_ctr;
  }

  i2c->restart_on_next = nostop;
  return rval;
}

static int i2c_10bit_read_timeout_us(i2c_inst_t *i2c, uint16_t addr,
                                     uint8_t *dst, size_t len, bool nostop,
                                     uint timeout_us) {
  if (len == 0u) {
    return 0;
  }
  i2c_10bit_xfer_ctx ctx = i2c_10bit_begin_xfer(i2c, addr, len, timeout_us);
  for (; ctx.byte_ctr < ctx.ilen; ++ctx.byte_ctr) {
    const bool first = ctx.byte_ctr == 0;
    const bool last = ctx.byte_ctr == ctx.ilen - 1;

    while (!i2c_get_write_available(i2c)) {
      tight_loop_contents();
    }

    ctx.hw->data_cmd = (uint32_t)((first && i2c->restart_on_next) ? 1u : 0u)
                           << I2C_IC_DATA_CMD_RESTART_LSB |
                       (uint32_t)((last && !nostop) ? 1u : 0u)
                           << I2C_IC_DATA_CMD_STOP_LSB |
                       I2C_IC_DATA_CMD_CMD_BITS;

    do {
      ctx.abort_reason = ctx.hw->tx_abrt_source;
      if (ctx.hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) {
        ctx.abort = true;
        (void)ctx.hw->clr_tx_abrt;
      }
      ctx.timeout = time_reached(ctx.deadline);
      ctx.abort |= ctx.timeout;
    } while (!ctx.abort && !i2c_get_read_available(i2c));

    if (ctx.abort) {
      break;
    }
    dst[ctx.byte_ctr] = (uint8_t)ctx.hw->data_cmd;
  }

  int rval;
  if (ctx.abort) {
    if (ctx.timeout) {
      rval = PICO_ERROR_TIMEOUT;
    } else if (!ctx.abort_reason ||
               (ctx.abort_reason &
                (I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS |
                 I2C_IC_TX_ABRT_SOURCE_ABRT_10ADDR1_NOACK_BITS |
                 I2C_IC_TX_ABRT_SOURCE_ABRT_10ADDR2_NOACK_BITS))) {
      rval = PICO_ERROR_GENERIC;
    } else {
      rval = PICO_ERROR_GENERIC;
    }
  } else {
    rval = ctx.byte_ctr;
  }

  i2c->restart_on_next = nostop;
  return rval;
}
#endif /* HAL_ENABLE_I2C_10BIT */

static bool i2c_ensure_initialized(uint8_t idx) {
  if (!s_i2c[idx].initialized) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return false;
  }
  return true;
}

static uint8_t i2c_result_from_write_rc(int rc, size_t expected_len) {
  if (rc == (int)expected_len) {
    return HAL_I2C_RESULT_OK;
  }
  if (rc == PICO_ERROR_TIMEOUT) {
    return HAL_I2C_ERROR_TIMEOUT;
  }
  if (rc == PICO_ERROR_GENERIC) {
    return HAL_I2C_ERROR_GENERIC;
  }
  return HAL_I2C_ERROR_OTHER;
}

static hal_status_t i2c_status_from_result(uint8_t result) {
  switch (result) {
  case HAL_I2C_RESULT_OK:
    return HAL_OK;
  case HAL_I2C_ERROR_TIMEOUT:
    return HAL_ETIMEOUT;
  case HAL_I2C_ERROR_GENERIC:
    return HAL_EBUS;
  case HAL_I2C_ERROR_OTHER:
  default:
    return HAL_EIO;
  }
}

static uint8_t i2c_probe_read(uint8_t idx, uint8_t address) {
  uint8_t dummy = 0u;
  int rc = i2c_read_timeout_us(i2c_bus_hw(idx), address, &dummy, 1u, false,
                               RP2040_I2C_TIMEOUT_US);
  if (rc == 1) {
    return HAL_I2C_RESULT_OK;
  }
  if (rc == PICO_ERROR_TIMEOUT) {
    return HAL_I2C_ERROR_TIMEOUT;
  }
  return HAL_I2C_ERROR_GENERIC;
}

#ifdef HAL_ENABLE_I2C_10BIT
static uint8_t i2c_probe_read_10bit(uint8_t idx, hal_i2c_address_t address) {
  uint8_t dummy = 0u;
  int rc = i2c_10bit_read_timeout_us(i2c_bus_hw(idx), address, &dummy, 1u,
                                     false, RP2040_I2C_TIMEOUT_US);
  if (rc == 1) {
    return HAL_I2C_RESULT_OK;
  }
  if (rc == PICO_ERROR_TIMEOUT) {
    return HAL_I2C_ERROR_TIMEOUT;
  }
  return HAL_I2C_ERROR_GENERIC;
}
#endif

static hal_status_t i2c_init_bus_common(uint8_t bus, uint8_t sda_pin,
                                        uint8_t scl_pin, uint32_t clock_hz) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  uint8_t idx = i2c_bus_index(bus);
  i2c_ensure_mutex(idx);

  i2c_bus_state_t *st = &s_i2c[idx];
  st->rx_len = 0u;
  st->rx_pos = 0u;
  st->tx_len = 0u;
  st->cur_addr = 0u;
  st->sda_pin = sda_pin;
  st->scl_pin = scl_pin;
  st->clock_hz = i2c_normalize_clock(clock_hz);
  st->actual_clock_hz = 0u;
  __atomic_store_n(&st->transaction_count, 0u, __ATOMIC_RELEASE);
  if (st->lock_depth == 0u) {
    st->lock_owner = 0u;
  }

  i2c_hw_init_bus(idx);
  st->initialized = true;
  return HAL_OK;
}

hal_status_t hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
  return hal_i2c_init_bus(0, sda_pin, scl_pin, clock_hz);
}

hal_status_t hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                              uint32_t clock_hz) {
  const hal_status_t status =
      i2c_init_bus_common(bus, sda_pin, scl_pin, clock_hz);
#ifdef HAL_ENABLE_I2C_10BIT
  if (status == HAL_OK) {
    s_i2c[i2c_bus_index(bus)].addr_mode = HAL_I2C_ADDR_MODE_7BIT;
  }
#endif
  return status;
}

#ifdef HAL_ENABLE_I2C_10BIT
hal_status_t hal_i2c_init_10bit(uint8_t sda_pin, uint8_t scl_pin,
                                uint32_t clock_hz) {
  return hal_i2c_init_bus_10bit(0, sda_pin, scl_pin, clock_hz);
}

hal_status_t hal_i2c_init_bus_10bit(uint8_t bus, uint8_t sda_pin,
                                    uint8_t scl_pin, uint32_t clock_hz) {
  const hal_status_t status =
      i2c_init_bus_common(bus, sda_pin, scl_pin, clock_hz);
  if (status == HAL_OK) {
    const uint8_t idx = i2c_bus_index(bus);
    s_i2c[idx].addr_mode = HAL_I2C_ADDR_MODE_10BIT;
    i2c_hw_enable_10bit_mode(idx);
  }
  return status;
}

hal_i2c_addr_mode_t hal_i2c_get_addr_mode(void) {
  return hal_i2c_get_addr_mode_bus(0);
}

hal_i2c_addr_mode_t hal_i2c_get_addr_mode_bus(uint8_t bus) {
  return s_i2c[i2c_bus_index(bus)].addr_mode;
}
#endif /* HAL_ENABLE_I2C_10BIT */

hal_status_t hal_i2c_get_clock(uint32_t *out_clock_hz) {
  return hal_i2c_get_clock_bus(0, out_clock_hz);
}

hal_status_t hal_i2c_get_clock_bus(uint8_t bus, uint32_t *out_clock_hz) {
  if (!i2c_bus_valid(bus) || out_clock_hz == NULL) {
    return HAL_EINVAL;
  }
  *out_clock_hz = s_i2c[i2c_bus_index(bus)].clock_hz;
  return HAL_OK;
}

hal_status_t hal_i2c_set_clock(uint32_t clock_hz) {
  return hal_i2c_set_clock_bus(0, clock_hz);
}

hal_status_t hal_i2c_set_clock_bus(uint8_t bus, uint32_t clock_hz) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  uint8_t idx = i2c_bus_index(bus);
  i2c_ensure_mutex(idx);
  i2c_lock_idx(idx);
  i2c_bus_state_t *st = &s_i2c[idx];
  st->clock_hz = i2c_normalize_clock(clock_hz);
  if (st->initialized) {
    st->actual_clock_hz = i2c_set_baudrate(i2c_bus_hw(idx), st->clock_hz);
  }
  i2c_unlock_idx(idx);
  return HAL_OK;
}

void hal_i2c_deinit(void) { hal_i2c_deinit_bus(0); }

void hal_i2c_deinit_bus(uint8_t bus) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_deinit(i2c_bus_hw(idx));
  s_i2c[idx].initialized = false;
  s_i2c[idx].rx_len = 0u;
  s_i2c[idx].rx_pos = 0u;
  s_i2c[idx].tx_len = 0u;
  s_i2c[idx].cur_addr = 0u;
}

void hal_i2c_lock(void) { hal_i2c_lock_bus(0); }

void hal_i2c_lock_bus(uint8_t bus) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_lock_idx(idx);
}

void hal_i2c_unlock(void) { hal_i2c_unlock_bus(0); }

void hal_i2c_unlock_bus(uint8_t bus) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_unlock_idx(idx);
}

void hal_i2c_begin_transmission(hal_i2c_address_t address) {
  hal_i2c_begin_transmission_bus(0, address);
}

void hal_i2c_begin_transmission_bus(uint8_t bus, hal_i2c_address_t address) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_lock_idx(idx);
  (void)i2c_ensure_initialized(idx);
  s_i2c[idx].cur_addr = address;
  s_i2c[idx].tx_len = 0u;
}

size_t hal_i2c_write(uint8_t data) { return hal_i2c_write_bus(0, data); }

size_t hal_i2c_write_bus(uint8_t bus, uint8_t data) {
  i2c_bus_state_t *st = i2c_state(bus);
  if (st->tx_len >= RP2040_I2C_BUF_SIZE) {
    return 0u;
  }
  st->tx_buf[st->tx_len++] = data;
  return 1u;
}

hal_status_t hal_i2c_end_transmission_ex(void) {
  return hal_i2c_end_transmission_bus_ex(0);
}

hal_status_t hal_i2c_end_transmission_bus_ex(uint8_t bus) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  uint8_t idx = i2c_bus_index(bus);
  i2c_bus_state_t *st = &s_i2c[idx];
  if (hal_status_is_error(jh_hal_i2c_validate_address(bus, st->cur_addr))) {
    st->tx_len = 0u;
    i2c_unlock_idx(idx);
    return HAL_EINVAL;
  }
  uint8_t result = HAL_I2C_ERROR_TIMEOUT;
  if (st->initialized) {
#ifdef HAL_ENABLE_I2C_10BIT
    if (st->addr_mode == HAL_I2C_ADDR_MODE_10BIT) {
      if (st->tx_len == 0u) {
        result = i2c_probe_read_10bit(idx, st->cur_addr);
      } else {
        int rc = i2c_10bit_write_timeout_us(i2c_bus_hw(idx), st->cur_addr,
                                            st->tx_buf, st->tx_len, false,
                                            RP2040_I2C_TIMEOUT_US);
        result = i2c_result_from_write_rc(rc, st->tx_len);
      }
    } else
#endif
        if (st->tx_len == 0u) {
      result = i2c_probe_read(idx, (uint8_t)st->cur_addr);
    } else {
      int rc = i2c_write_timeout_us(i2c_bus_hw(idx), (uint8_t)st->cur_addr,
                                    st->tx_buf, st->tx_len, false,
                                    RP2040_I2C_TIMEOUT_US);
      result = i2c_result_from_write_rc(rc, st->tx_len);
    }
  }
  st->tx_len = 0u;
  __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);
  i2c_unlock_idx(idx);
  return i2c_status_from_result(result);
}

hal_status_t hal_i2c_write_read_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                       const uint8_t *tx, size_t tx_len,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) || (tx_len > 0u && tx == NULL) ||
      (rx_len > 0u && rx == NULL) || tx_len > RP2040_I2C_BUF_SIZE ||
      rx_len > RP2040_I2C_BUF_SIZE) {
    return HAL_EINVAL;
  }
  if (hal_status_is_error(jh_hal_i2c_validate_address(bus, address))) {
    return HAL_EINVAL;
  }
  if (tx_len == 0u) {
    return hal_i2c_read_bytes_bus_ex(bus, address, rx, rx_len);
  }
  const uint8_t idx = i2c_bus_index(bus);
  if (!s_i2c[idx].initialized) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return HAL_EUNINIT;
  }
  i2c_lock_idx(idx);
  if (!i2c_ensure_initialized(idx)) {
    i2c_unlock_idx(idx);
    return HAL_EUNINIT;
  }

  int written;
  int got = 0;
#ifdef HAL_ENABLE_I2C_10BIT
  if (s_i2c[idx].addr_mode == HAL_I2C_ADDR_MODE_10BIT) {
    written = i2c_10bit_write_timeout_us(i2c_bus_hw(idx), address, tx, tx_len,
                                         rx_len > 0u, RP2040_I2C_TIMEOUT_US);
  } else
#endif
  {
    written = i2c_write_timeout_us(i2c_bus_hw(idx), (uint8_t)address, tx,
                                   tx_len, rx_len > 0u, RP2040_I2C_TIMEOUT_US);
  }
  __atomic_fetch_add(&s_i2c[idx].transaction_count, 1u, __ATOMIC_RELAXED);
  if (written != (int)tx_len) {
    i2c_unlock_idx(idx);
    return (written == PICO_ERROR_TIMEOUT) ? HAL_ETIMEOUT : HAL_EBUS;
  }

  if (rx_len > 0u) {
#ifdef HAL_ENABLE_I2C_10BIT
    if (s_i2c[idx].addr_mode == HAL_I2C_ADDR_MODE_10BIT) {
      got = i2c_10bit_read_timeout_us(i2c_bus_hw(idx), address, rx, rx_len,
                                      false, RP2040_I2C_TIMEOUT_US);
    } else
#endif
    {
      got = i2c_read_timeout_us(i2c_bus_hw(idx), (uint8_t)address, rx, rx_len,
                                false, RP2040_I2C_TIMEOUT_US);
    }
    __atomic_fetch_add(&s_i2c[idx].transaction_count, 1u, __ATOMIC_RELAXED);
    if (got != (int)rx_len) {
      i2c_unlock_idx(idx);
      return (got == PICO_ERROR_TIMEOUT) ? HAL_ETIMEOUT : HAL_EBUS;
    }
  }

  i2c_unlock_idx(idx);
  return HAL_OK;
}

static bool i2c_read_bytes_bus_impl(uint8_t bus, hal_i2c_address_t address,
                                    uint8_t *rx, size_t rx_len);

hal_status_t hal_i2c_read_bytes_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) || (rx_len > 0u && rx == NULL) ||
      rx_len > RP2040_I2C_BUF_SIZE) {
    return HAL_EINVAL;
  }
  if (hal_status_is_error(jh_hal_i2c_validate_address(bus, address))) {
    return HAL_EINVAL;
  }
  if (rx_len == 0u) {
    return HAL_OK;
  }
  const uint8_t idx = i2c_bus_index(bus);
  if (!s_i2c[idx].initialized) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return HAL_EUNINIT;
  }
  return hal_status_from_bool(i2c_read_bytes_bus_impl(bus, address, rx, rx_len),
                              HAL_EBUS);
}

static bool i2c_read_bytes_bus_impl(uint8_t bus, hal_i2c_address_t address,
                                    uint8_t *rx, size_t rx_len) {
  if ((rx_len > 0u && rx == NULL) || rx_len > RP2040_I2C_BUF_SIZE) {
    return false;
  }
  if (rx_len == 0u) {
    return true;
  }

  uint8_t idx = i2c_bus_index(bus);
  i2c_lock_idx(idx);
  if (!i2c_ensure_initialized(idx)) {
    i2c_unlock_idx(idx);
    return false;
  }
  int got;
#ifdef HAL_ENABLE_I2C_10BIT
  if (s_i2c[idx].addr_mode == HAL_I2C_ADDR_MODE_10BIT) {
    got = i2c_10bit_read_timeout_us(i2c_bus_hw(idx), address, rx, rx_len, false,
                                    RP2040_I2C_TIMEOUT_US);
  } else
#endif
  {
    got = i2c_read_timeout_us(i2c_bus_hw(idx), (uint8_t)address, rx, rx_len,
                              false, RP2040_I2C_TIMEOUT_US);
  }
  s_i2c[idx].rx_len = 0u;
  s_i2c[idx].rx_pos = 0u;
  __atomic_fetch_add(&s_i2c[idx].transaction_count, 1u, __ATOMIC_RELAXED);
  i2c_unlock_idx(idx);
  return got == (int)rx_len;
}

static uint8_t i2c_request_from_bus_impl(uint8_t bus, hal_i2c_address_t address,
                                         uint8_t count);

hal_status_t hal_i2c_request_from_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                         uint8_t count, uint8_t *outReceived) {
  if (outReceived == NULL || !i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  *outReceived = 0u;
  if (hal_status_is_error(jh_hal_i2c_validate_address(bus, address))) {
    return HAL_EINVAL;
  }
  const uint8_t idx = i2c_bus_index(bus);
  if (!s_i2c[idx].initialized && count > 0u) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return HAL_EUNINIT;
  }
  *outReceived = i2c_request_from_bus_impl(bus, address, count);
  return (*outReceived == count) ? HAL_OK : HAL_EBUS;
}

static uint8_t i2c_request_from_bus_impl(uint8_t bus, hal_i2c_address_t address,
                                         uint8_t count) {
  uint8_t idx = i2c_bus_index(bus);
  i2c_bus_state_t *st = &s_i2c[idx];
  i2c_lock_idx(idx);
  if (!i2c_ensure_initialized(idx)) {
    i2c_unlock_idx(idx);
    return 0u;
  }

  int got = 0;
  if (count > 0u) {
#ifdef HAL_ENABLE_I2C_10BIT
    if (st->addr_mode == HAL_I2C_ADDR_MODE_10BIT) {
      got = i2c_10bit_read_timeout_us(i2c_bus_hw(idx), address, st->rx_buf,
                                      count, false, RP2040_I2C_TIMEOUT_US);
    } else
#endif
    {
      got = i2c_read_timeout_us(i2c_bus_hw(idx), (uint8_t)address, st->rx_buf,
                                count, false, RP2040_I2C_TIMEOUT_US);
    }
    if (got < 0) {
      got = 0;
    }
  }
  st->rx_len = (size_t)got;
  st->rx_pos = 0u;
  __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);
  i2c_unlock_idx(idx);
  return (uint8_t)got;
}

int hal_i2c_available(void) { return hal_i2c_available_bus(0); }

int hal_i2c_available_bus(uint8_t bus) {
  i2c_bus_state_t *st = i2c_state(bus);
  return (st->rx_pos < st->rx_len) ? (int)(st->rx_len - st->rx_pos) : 0;
}

int hal_i2c_read(void) { return hal_i2c_read_bus(0); }

int hal_i2c_read_bus(uint8_t bus) {
  i2c_bus_state_t *st = i2c_state(bus);
  if (st->rx_pos >= st->rx_len) {
    return -1;
  }
  return st->rx_buf[st->rx_pos++];
}

bool hal_i2c_is_busy(hal_i2c_address_t address) {
  return hal_i2c_is_busy_bus(0, address);
}

bool hal_i2c_is_busy_bus(uint8_t bus, hal_i2c_address_t address) {
  hal_i2c_begin_transmission_bus(bus, address);
  return hal_i2c_end_transmission_bus(bus) != 0u;
}

uint32_t hal_i2c_get_transaction_count(void) {
  return hal_i2c_get_transaction_count_bus(0);
}

uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus) {
  return __atomic_load_n(&s_i2c[i2c_bus_index(bus)].transaction_count,
                         __ATOMIC_ACQUIRE);
}

hal_status_t hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin) {
  return hal_i2c_bus_clear_bus(0, sda_pin, scl_pin);
}

hal_status_t hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  (void)bus;

  gpio_init(sda_pin);
  gpio_init(scl_pin);
  gpio_set_dir(sda_pin, GPIO_IN);
  gpio_pull_up(sda_pin);
  gpio_set_dir(scl_pin, GPIO_OUT);
  gpio_put(scl_pin, true);
  gpio_pull_up(scl_pin);
  busy_wait_us(5u);

  for (uint8_t i = 0u; i < 9u; ++i) {
    if (gpio_get(sda_pin)) {
      break;
    }
    gpio_put(scl_pin, false);
    busy_wait_us(5u);
    gpio_put(scl_pin, true);
    busy_wait_us(5u);
  }

  gpio_set_dir(sda_pin, GPIO_OUT);
  gpio_put(sda_pin, false);
  busy_wait_us(5u);
  gpio_put(scl_pin, true);
  busy_wait_us(5u);
  gpio_put(sda_pin, true);
  busy_wait_us(5u);

  gpio_set_dir(sda_pin, GPIO_IN);
  gpio_pull_up(sda_pin);
  gpio_set_dir(scl_pin, GPIO_IN);
  gpio_pull_up(scl_pin);
  return HAL_OK;
}

#endif /* HAL_ENABLE_I2C */
#endif // HAL_TARGET_IS_RP
