#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_CAN

#include "hal/can/hal_can.h"
#include "hal/core/hal_array.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"

#if HAL_BOARD_CAN_CHANNEL_COUNT > 0
/* One row per HAL_BOARD_CAN_CHANNELS entry of the board profile. */
typedef struct {
  hal_can_backend_t backend;
  uint8_t instance;
  uint8_t rx_pin;
  uint8_t tx_pin;
  uint8_t standby_pin;
  bool standby_high;
  uint32_t max_bitrate_hz;
} jh_can_board_channel_t;

#define JH_CAN_BOARD_ROW(backend, instance, rx, tx, standby, standby_high,     \
                         max_hz)                                               \
  {HAL_CAN_BACKEND_##backend, instance, rx, tx, standby,                       \
   (standby_high) != 0,       max_hz},
static const jh_can_board_channel_t kBoardChannels[] = {
    HAL_BOARD_CAN_CHANNELS(JH_CAN_BOARD_ROW)};
#undef JH_CAN_BOARD_ROW

static void board_fdcan_config(const jh_can_board_channel_t *c,
                               hal_can_config_t *cfg) {
  hal_can_stm32g474_fdcan_config_t *f = &cfg->stm32g474_fdcan;
  f->instance = c->instance;
  f->rx_pin = c->rx_pin;
  f->tx_pin = c->tx_pin;
  f->has_standby = c->standby_pin != HAL_BOARD_DEVICE_PIN_NONE;
  f->standby_pin = f->has_standby ? c->standby_pin : 0u;
  f->standby_high = c->standby_high;
  f->arbitration_bitrate_hz = 500000u;
  f->data_bitrate_hz =
      c->max_bitrate_hz < 2000000u ? c->max_bitrate_hz : 2000000u;
  f->transceiver_max_bitrate_hz = c->max_bitrate_hz;
  f->enable_fd = true;
}
#endif

hal_status_t hal_can_board_config(uint8_t channel, hal_can_config_t *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }
#if HAL_BOARD_CAN_CHANNEL_COUNT > 0
  if (channel >= COUNTOF(kBoardChannels)) {
    return HAL_ENOENT;
  }
  const jh_can_board_channel_t *c = &kBoardChannels[channel];
  hal_can_config_t cfg = {};
  cfg.backend = c->backend;
  switch (c->backend) {
  case HAL_CAN_BACKEND_STM32G474_FDCAN:
    board_fdcan_config(c, &cfg);
    break;
  default:
    return HAL_EUNSUPPORTED;
  }
  *out = cfg;
  return HAL_OK;
#else
  (void)channel;
  return HAL_ENOENT;
#endif
}

hal_status_t hal_can_create_with_retry(const hal_can_config_t *cfg,
                                       uint8_t int_pin, void (*isr)(void),
                                       int max_retries,
                                       void (*retry_idle)(void),
                                       hal_can_t *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }
  *out = NULL;
  hal_status_t st = HAL_EINVAL;
  for (int attempt = 0; attempt <= max_retries; attempt++) {
    st = hal_can_create(cfg, out);
    if (st == HAL_OK) {
      if (int_pin != HAL_CAN_NO_INT_PIN) {
        hal_gpio_set_mode(int_pin, HAL_GPIO_INPUT);
        if (isr) {
          hal_gpio_attach_interrupt(int_pin, isr, HAL_GPIO_IRQ_FALLING);
        }
      }
      return HAL_OK;
    }

    hal_derr_limited("can", "init failed (attempt %d/%d): %s", attempt + 1,
                     max_retries + 1, hal_status_to_string(st));

    if (attempt < max_retries) {
      if (retry_idle) {
        retry_idle();
      }
      hal_delay_ms(SECOND);
    }
  }
  return st;
}

hal_status_t hal_can_process_all(hal_can_t h, hal_can_frame_cb_t cb,
                                 uint32_t *delivered) {
  uint32_t count = 0u;
  hal_status_t st = (h != NULL && cb != NULL) ? HAL_OK : HAL_EINVAL;
  while (st == HAL_OK) {
    uint32_t id = 0u;
    uint8_t len = 0u;
    uint8_t buf[HAL_CAN_MAX_DATA_LEN];
    st = hal_can_receive(h, &id, &len, buf);
    if (st == HAL_OK && id != 0u && len > 0u) {
      cb(id, len, buf);
      count++;
    }
  }
  if (delivered != NULL) {
    *delivered = count;
  }
  return st == HAL_EAGAIN ? HAL_OK : st;
}

uint8_t hal_can_dlc_to_bytes(uint8_t dlc) {
  static const uint8_t fd_lengths[16] = {0, 1,  2,  3,  4,  5,  6,  7,
                                         8, 12, 16, 20, 24, 32, 48, 64};
  if (dlc >= (uint8_t)COUNTOF(fd_lengths)) {
    return 0;
  }
  return fd_lengths[dlc];
}

uint8_t hal_can_bytes_to_dlc(uint8_t bytes) {
  if (bytes <= 8u) {
    return bytes;
  }
  if (bytes <= 12u) {
    return 9u;
  }
  if (bytes <= 16u) {
    return 10u;
  }
  if (bytes <= 20u) {
    return 11u;
  }
  if (bytes <= 24u) {
    return 12u;
  }
  if (bytes <= 32u) {
    return 13u;
  }
  if (bytes <= 48u) {
    return 14u;
  }
  if (bytes <= 64u) {
    return 15u;
  }
  return HAL_CAN_DLC_INVALID;
}

/* The rules of hal_can_validate_frame(). */
static bool frame_valid(const hal_can_frame_t *frame) {
  if (!frame) {
    return false;
  }
  const uint8_t supported_flags = HAL_CAN_FRAME_EXTENDED | HAL_CAN_FRAME_RTR |
                                  HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS |
                                  HAL_CAN_FRAME_ESI;
  if ((frame->flags & (uint8_t)~supported_flags) != 0u) {
    return false;
  }
  if ((frame->flags & (HAL_CAN_FRAME_BRS | HAL_CAN_FRAME_ESI)) != 0u &&
      (frame->flags & HAL_CAN_FRAME_FD) == 0u) {
    return false;
  }
  if ((frame->flags & HAL_CAN_FRAME_FD) != 0u &&
      (frame->flags & HAL_CAN_FRAME_RTR) != 0u) {
    return false;
  }
  if ((frame->flags & HAL_CAN_FRAME_EXTENDED) != 0u) {
    if (frame->id > HAL_CAN_EXT_ID_MASK) {
      return false;
    }
  } else if (frame->id > HAL_CAN_STD_ID_MASK) {
    return false;
  }
  if (frame->dlc > 15u) {
    return false;
  }
  if (hal_can_dlc_to_bytes(frame->dlc) != frame->len) {
    return false;
  }
  if ((frame->flags & HAL_CAN_FRAME_FD) == 0u &&
      frame->len > HAL_CAN_MAX_DATA_LEN) {
    return false;
  }
  return frame->len <= HAL_CAN_FD_MAX_DATA_LEN;
}

hal_status_t hal_can_validate_frame(const hal_can_frame_t *frame) {
  return frame_valid(frame) ? HAL_OK : HAL_EINVAL;
}

/* The rules of hal_can_validate_filter_ex(). */
static bool filter_ex_valid(const hal_can_filter_ex_t *filter) {
  if (!filter || filter->type > HAL_CAN_FILTER_DUAL ||
      filter->action > HAL_CAN_FILTER_REJECT ||
      (filter->flags & ~HAL_CAN_FILTER_EXTENDED) != 0u) {
    return false;
  }
  const uint32_t id_mask = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u
                               ? HAL_CAN_EXT_ID_MASK
                               : HAL_CAN_STD_ID_MASK;
  if (filter->id1 > id_mask || filter->id2 > id_mask) {
    return false;
  }
  return filter->type != HAL_CAN_FILTER_RANGE || filter->id1 <= filter->id2;
}

hal_status_t hal_can_validate_filter_ex(const hal_can_filter_ex_t *filter) {
  return filter_ex_valid(filter) ? HAL_OK : HAL_EINVAL;
}

/* Condition of a valid filter on a valid frame. */
static bool filter_ex_matches(const hal_can_frame_t *frame,
                              const hal_can_filter_ex_t *filter) {
  const bool frame_ext = (frame->flags & HAL_CAN_FRAME_EXTENDED) != 0u;
  const bool filter_ext = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  if (frame_ext != filter_ext) {
    return false;
  }
  switch (filter->type) {
  case HAL_CAN_FILTER_MASK:
    return (frame->id & filter->id2) == (filter->id1 & filter->id2);
  case HAL_CAN_FILTER_RANGE:
    return filter->id1 <= frame->id && frame->id <= filter->id2;
  default:
    return frame->id == filter->id1 || frame->id == filter->id2;
  }
}

hal_status_t hal_can_frame_matches_filter_ex(const hal_can_frame_t *frame,
                                             const hal_can_filter_ex_t *filter,
                                             bool *matches) {
  if (matches == NULL || !frame_valid(frame) || !filter_ex_valid(filter)) {
    return HAL_EINVAL;
  }
  *matches = filter_ex_matches(frame, filter);
  return HAL_OK;
}

/* A classic filter is a mask filter that accepts. */
static hal_can_filter_ex_t classic_as_ex(const hal_can_filter_t *filter) {
  hal_can_filter_ex_t ex = {};
  ex.type = HAL_CAN_FILTER_MASK;
  ex.action = HAL_CAN_FILTER_ACCEPT;
  ex.flags = filter->flags;
  ex.id1 = filter->id;
  ex.id2 = filter->mask;
  return ex;
}

hal_status_t hal_can_validate_filter(const hal_can_filter_t *filter) {
  if (!filter) {
    return HAL_EINVAL;
  }
  const hal_can_filter_ex_t ex = classic_as_ex(filter);
  return hal_can_validate_filter_ex(&ex);
}

hal_status_t hal_can_frame_matches_filter(const hal_can_frame_t *frame,
                                          const hal_can_filter_t *filter,
                                          bool *matches) {
  if (!filter) {
    return HAL_EINVAL;
  }
  const hal_can_filter_ex_t ex = classic_as_ex(filter);
  return hal_can_frame_matches_filter_ex(frame, &ex, matches);
}

uint8_t hal_can_encode_temp_i8(float temp_c) {
  int32_t value = (int32_t)temp_c;

  if (value < -128) {
    value = -128;
  }
  if (value > 127) {
    value = 127;
  }

  return (uint8_t)(int8_t)value;
}

#endif /* HAL_ENABLE_CAN */
