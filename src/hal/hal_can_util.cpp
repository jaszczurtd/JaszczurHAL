#include "hal_config.h"
#ifdef HAL_ENABLE_CAN

#include "hal_can.h"
#include "hal_gpio.h"
#include "hal_serial.h"
#include "hal_system.h"

hal_can_t hal_can_create_with_retry(const hal_can_config_t *cfg,
                                    uint8_t int_pin, void (*isr)(void),
                                    int max_retries, void (*retry_idle)(void)) {
  for (int attempt = 0; attempt <= max_retries; attempt++) {
    hal_can_t h = hal_can_create(cfg);
    if (h) {
      if (int_pin != HAL_CAN_NO_INT_PIN) {
        hal_gpio_set_mode(int_pin, HAL_GPIO_INPUT);
        if (isr) {
          hal_gpio_attach_interrupt(int_pin, isr, HAL_GPIO_IRQ_FALLING);
        }
      }
      return h;
    }

    hal_derr_limited("can", "init failed (attempt %d/%d)", attempt + 1,
                     max_retries + 1);

    if (attempt < max_retries) {
      if (retry_idle) {
        retry_idle();
      }
      hal_delay_ms(SECOND);
    }
  }
  return NULL;
}

int hal_can_process_all(hal_can_t h, hal_can_frame_cb_t cb) {
  if (!h || !cb)
    return 0;

  int count = 0;
  uint32_t id;
  uint8_t len;
  uint8_t buf[HAL_CAN_MAX_DATA_LEN];

  while (hal_can_receive(h, &id, &len, buf)) {
    if (id != 0 && len > 0) {
      cb(id, len, buf);
      count++;
    }
  }
  return count;
}

uint8_t hal_can_dlc_to_bytes(uint8_t dlc) {
  static const uint8_t fd_lengths[16] = {0, 1,  2,  3,  4,  5,  6,  7,
                                         8, 12, 16, 20, 24, 32, 48, 64};
  if (dlc >= (uint8_t)(sizeof(fd_lengths) / sizeof(fd_lengths[0]))) {
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

bool hal_can_validate_frame(const hal_can_frame_t *frame) {
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

bool hal_can_validate_filter(const hal_can_filter_t *filter) {
  if (!filter) {
    return false;
  }
  if ((filter->flags & ~HAL_CAN_FILTER_EXTENDED) != 0u) {
    return false;
  }
  const uint32_t id_mask = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u
                               ? HAL_CAN_EXT_ID_MASK
                               : HAL_CAN_STD_ID_MASK;
  return filter->id <= id_mask && filter->mask <= id_mask;
}

bool hal_can_frame_matches_filter(const hal_can_frame_t *frame,
                                  const hal_can_filter_t *filter) {
  if (!hal_can_validate_frame(frame) || !hal_can_validate_filter(filter)) {
    return false;
  }
  const bool frame_ext = (frame->flags & HAL_CAN_FRAME_EXTENDED) != 0u;
  const bool filter_ext = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  if (frame_ext != filter_ext) {
    return false;
  }
  return (frame->id & filter->mask) == (filter->id & filter->mask);
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
