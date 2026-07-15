/*
 * IR decoding state machines and timing thresholds are based on
 * IRsmallDecoder v1.3.0 by Luis Carvalho. The RC5 transition-table decoder is
 * based on the RC5 Arduino Library by Guy Carpenter, Clearwater Software -
 * 2013, licensed under the BSD2 license. This implementation preserves the
 * protocol behavior while routing GPIO interrupts, timing and synchronization
 * through JaszczurHAL.
 */

#include "hal/hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/hal_config.h"
#if defined(HAL_ENABLE_IRSMALL_DECODER)

#include "hal/hal_irsmall_decoder.h"

#include "hal/hal_system.h"
#include "hal/impl/shared/hal_mutex_once.h"

#include <stddef.h>
#include <string.h>

#define IRSMALL_SLOT_NONE 0xFFu

#define IRSMALL_RC5_MIN_SHORT 444u
#define IRSMALL_RC5_MAX_SHORT 1333u
#define IRSMALL_RC5_MIN_LONG 1334u
#define IRSMALL_RC5_MAX_LONG 2222u

#define IRSMALL_RC5_EVENT_SHORTSPACE 0u
#define IRSMALL_RC5_EVENT_SHORTPULSE 2u
#define IRSMALL_RC5_EVENT_LONGSPACE 4u
#define IRSMALL_RC5_EVENT_LONGPULSE 6u

#define IRSMALL_RC5_STATE_START1 0u
#define IRSMALL_RC5_STATE_MID1 1u
#define IRSMALL_RC5_STATE_MID0 2u
#define IRSMALL_RC5_STATE_START0 3u

static hal_mutex_t s_irsmall_slots_mutex;
static hal_irsmall_decoder_t
    *s_irsmall_slots[HAL_IRSMALL_DECODER_MAX_INSTANCES];
static const uint8_t s_irsmall_rc5_transitions[4] = {0x01u, 0x91u, 0x9Bu,
                                                     0xFBu};

static uint8_t irsmall_byte32(uint32_t value, uint8_t index) {
  return (uint8_t)(value >> ((uint32_t)index * 8u));
}

static bool irsmall_valid_protocol(hal_irsmall_protocol_t protocol) {
  return protocol <= HAL_IRSMALL_PROTOCOL_SAMSUNG32;
}

static bool irsmall_ensure_dev_mutex(hal_irsmall_decoder_t *dev) {
  return (dev != NULL) && (jh_hal_mutex_create_once(&dev->mutex) != NULL);
}

static bool irsmall_ensure_slots_mutex(void) {
  return jh_hal_mutex_create_once(&s_irsmall_slots_mutex) != NULL;
}

static void irsmall_clear_fsm(hal_irsmall_decoder_t *dev) {
  volatile hal_irsmall_decoder_fsm_t *fsm = &dev->fsm;
  fsm->signal = 0u;
  fsm->first_code = 0u;
  fsm->last_bit_time = 0u;
  fsm->addr16 = 0u;
  fsm->bit_count = 0u;
  fsm->repeat_count = 0u;
  fsm->byte_index = 0u;
  fsm->frame_count = 0u;
  fsm->first_bit_count = 20u;
  fsm->cmd = 0u;
  fsm->possibly_held = false;
  fsm->prev_toggle = false;
  for (uint8_t i = 0u; i < 4u; ++i) {
    fsm->bytes[i] = 0u;
  }
}

static void irsmall_reset_isr_state(hal_irsmall_decoder_t *dev,
                                    uint32_t previous_time) {
  hal_critical_section_enter();
  irsmall_clear_fsm(dev);
  dev->previous_time = previous_time;
  if (dev->cfg.protocol == HAL_IRSMALL_PROTOCOL_RC5) {
    dev->state = IRSMALL_RC5_STATE_MID1;
    dev->fsm.bit_count = 1u;
    dev->fsm.signal = 1u;
  } else {
    dev->state = 0u;
  }
  hal_critical_section_exit();
}

static int irsmall_alloc_slot(hal_irsmall_decoder_t *dev) {
  if (!irsmall_ensure_slots_mutex()) {
    return -1;
  }

  hal_mutex_lock(s_irsmall_slots_mutex);
  int selected = -1;
  for (uint8_t i = 0u; i < HAL_IRSMALL_DECODER_MAX_INSTANCES; ++i) {
    if (s_irsmall_slots[i] == dev) {
      selected = (int)i;
      break;
    }
    if (selected < 0 && s_irsmall_slots[i] == NULL) {
      selected = (int)i;
    }
  }
  if (selected >= 0) {
    s_irsmall_slots[selected] = dev;
  }
  hal_mutex_unlock(s_irsmall_slots_mutex);
  return selected;
}

static void irsmall_free_slot(hal_irsmall_decoder_t *dev) {
  if (!irsmall_ensure_slots_mutex()) {
    return;
  }

  hal_mutex_lock(s_irsmall_slots_mutex);
  for (uint8_t i = 0u; i < HAL_IRSMALL_DECODER_MAX_INSTANCES; ++i) {
    if (s_irsmall_slots[i] == dev) {
      s_irsmall_slots[i] = NULL;
    }
  }
  hal_mutex_unlock(s_irsmall_slots_mutex);
}

static hal_status_t irsmall_ready_status(hal_irsmall_decoder_t *dev) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }
  if (dev->mutex == NULL || !dev->initialized) {
    return HAL_EUNINIT;
  }
  return HAL_OK;
}

uint32_t hal_irsmall_decoder_timeout_us(hal_irsmall_protocol_t protocol) {
  switch (protocol) {
  case HAL_IRSMALL_PROTOCOL_NEC:
  case HAL_IRSMALL_PROTOCOL_NECX:
    return 126226u;
  case HAL_IRSMALL_PROTOCOL_RC5:
    return 106920u;
  case HAL_IRSMALL_PROTOCOL_SAMSUNG:
    return 33525u;
  case HAL_IRSMALL_PROTOCOL_SAMSUNG32:
    return 64125u;
  case HAL_IRSMALL_PROTOCOL_SIRC12:
  case HAL_IRSMALL_PROTOCOL_SIRC15:
  case HAL_IRSMALL_PROTOCOL_SIRC20:
    return 2160u;
  case HAL_IRSMALL_PROTOCOL_SIRC:
    return 37440u;
  default:
    return 0u;
  }
}

hal_gpio_irq_mode_t
hal_irsmall_decoder_irq_mode(hal_irsmall_protocol_t protocol) {
  switch (protocol) {
  case HAL_IRSMALL_PROTOCOL_SAMSUNG:
  case HAL_IRSMALL_PROTOCOL_SAMSUNG32:
    return HAL_GPIO_IRQ_FALLING;
  case HAL_IRSMALL_PROTOCOL_RC5:
    return HAL_GPIO_IRQ_CHANGE;
  case HAL_IRSMALL_PROTOCOL_NEC:
  case HAL_IRSMALL_PROTOCOL_NECX:
  case HAL_IRSMALL_PROTOCOL_SIRC12:
  case HAL_IRSMALL_PROTOCOL_SIRC15:
  case HAL_IRSMALL_PROTOCOL_SIRC20:
  case HAL_IRSMALL_PROTOCOL_SIRC:
  default:
    return HAL_GPIO_IRQ_RISING;
  }
}

/* Publishers run in ISR context; the consumer (irsmall_take_data) reads the
 * shared data under a critical section, so no extra guard is needed here. */
static void irsmall_publish(hal_irsmall_decoder_t *dev, uint16_t addr,
                            uint8_t cmd, uint8_t ext, bool key_held,
                            uint8_t bits) {
  dev->data.protocol = dev->cfg.protocol;
  dev->data.addr = addr;
  dev->data.cmd = cmd;
  dev->data.ext = ext;
  dev->data.key_held = key_held;
  dev->data.bits = bits;
  dev->data_available = true;
}

static void irsmall_publish_held(hal_irsmall_decoder_t *dev) {
  dev->data.key_held = true;
  dev->data_available = true;
}

static void irsmall_decode_nec(hal_irsmall_decoder_t *dev, uint32_t duration) {
  const bool extended = (dev->cfg.protocol == HAL_IRSMALL_PROTOCOL_NECX);
  const uint16_t gap_min = extended ? 27956u : 34256u;
  const uint32_t gap_max = 136743u;
  const uint16_t rm_min = 1968u;
  const uint16_t rm_max = 3616u;
  const uint16_t lm_min = 3617u;
  const uint16_t lm_max = 6581u;
  const uint16_t m1_min = 1575u;
  const uint16_t m1_max = 2925u;
  const uint16_t m0_min = 787u;
  const uint8_t rpt_count = 2u;
  volatile hal_irsmall_decoder_fsm_t *fsm = &dev->fsm;

  switch (dev->state) {
  case 0u:
    if (duration > gap_min) {
      if (duration > gap_max) {
        fsm->possibly_held = false;
      }
      dev->state = 1u;
    } else {
      fsm->possibly_held = false;
    }
    break;
  case 1u:
    if (duration >= lm_min && duration <= lm_max) {
      fsm->bit_count = 0u;
      fsm->repeat_count = 0u;
      fsm->signal = 0u;
      dev->state = 2u;
    } else {
      if (fsm->possibly_held && duration >= rm_min && duration <= rm_max) {
        if (fsm->repeat_count < rpt_count) {
          fsm->repeat_count++;
        } else {
          irsmall_publish_held(dev);
        }
      }
      dev->state = 0u;
    }
    break;
  case 2u:
    if (duration < m0_min || duration > m1_max) {
      dev->state = 0u;
      break;
    }
    {
      uint32_t signal = fsm->signal >> 1u;
      if (duration >= m1_min) {
        signal |= 0x80000000u;
      }
      fsm->signal = signal;
      fsm->bit_count++;

      if (!extended && fsm->bit_count == 16u) {
        if (irsmall_byte32(signal, 2u) !=
            (uint8_t)~irsmall_byte32(signal, 3u)) {
          dev->state = 0u;
        }
      } else if (fsm->bit_count == 32u) {
        if (irsmall_byte32(signal, 2u) ==
            (uint8_t)~irsmall_byte32(signal, 3u)) {
          const uint16_t addr =
              extended
                  ? (uint16_t)(irsmall_byte32(signal, 0u) |
                               ((uint16_t)irsmall_byte32(signal, 1u) << 8u))
                  : irsmall_byte32(signal, 0u);
          irsmall_publish(dev, addr, irsmall_byte32(signal, 2u), 0u, false,
                          32u);
          fsm->possibly_held = true;
        }
        dev->state = 0u;
      }
    }
    break;
  default:
    dev->state = 0u;
    break;
  }
}

static void irsmall_decode_rc5_finish(hal_irsmall_decoder_t *dev) {
  const uint32_t rpt_period_max = 136550u;
  const uint8_t rpt_count = 2u;
  volatile hal_irsmall_decoder_fsm_t *fsm = &dev->fsm;
  const uint16_t signal = (uint16_t)fsm->signal;
  const bool toggle = (signal & 0x0800u) != 0u;

  if (fsm->last_bit_time != 0u &&
      ((uint32_t)(dev->previous_time - fsm->last_bit_time) < rpt_period_max) &&
      (fsm->prev_toggle == toggle)) {
    if (fsm->repeat_count < rpt_count) {
      fsm->repeat_count++;
    } else {
      irsmall_publish_held(dev);
    }
  } else {
    const uint8_t addr = (uint8_t)((signal & 0x07C0u) >> 6u);
    const uint8_t cmd =
        (uint8_t)((signal & 0x003Fu) | ((signal & 0x1000u) ? 0u : 0x40u));
    irsmall_publish(dev, addr, cmd, 0u, false, 14u);
    fsm->repeat_count = 0u;
  }

  fsm->prev_toggle = toggle;
  fsm->last_bit_time = dev->previous_time;
  fsm->signal = 0u;
  fsm->bit_count = 0u;
}

static void irsmall_rc5_reset_decoder(hal_irsmall_decoder_t *dev) {
  volatile hal_irsmall_decoder_fsm_t *fsm = &dev->fsm;
  dev->state = IRSMALL_RC5_STATE_MID1;
  fsm->bit_count = 1u;
  fsm->signal = 1u;
}

static void irsmall_decode_rc5_event(hal_irsmall_decoder_t *dev,
                                     uint8_t event) {
  volatile hal_irsmall_decoder_fsm_t *fsm = &dev->fsm;
  const uint8_t current_state = dev->state;
  if (current_state > IRSMALL_RC5_STATE_START0) {
    irsmall_rc5_reset_decoder(dev);
    return;
  }

  const uint8_t new_state =
      (uint8_t)((s_irsmall_rc5_transitions[current_state] >> event) & 0x03u);
  if (new_state == current_state) {
    irsmall_rc5_reset_decoder(dev);
    return;
  }

  dev->state = new_state;
  if (new_state == IRSMALL_RC5_STATE_MID0) {
    fsm->signal <<= 1u;
    fsm->bit_count++;
  } else if (new_state == IRSMALL_RC5_STATE_MID1) {
    fsm->signal = (fsm->signal << 1u) + 1u;
    fsm->bit_count++;
  }

  if (fsm->bit_count == 14u) {
    irsmall_decode_rc5_finish(dev);
  }
}

static void irsmall_decode_rc5(hal_irsmall_decoder_t *dev, uint32_t duration) {
  const bool signal = hal_gpio_read(dev->cfg.input_pin);
  uint8_t event = 0u;

  if (duration >= IRSMALL_RC5_MIN_SHORT && duration <= IRSMALL_RC5_MAX_SHORT) {
    event =
        signal ? IRSMALL_RC5_EVENT_SHORTPULSE : IRSMALL_RC5_EVENT_SHORTSPACE;
  } else if (duration >= IRSMALL_RC5_MIN_LONG &&
             duration <= IRSMALL_RC5_MAX_LONG) {
    event = signal ? IRSMALL_RC5_EVENT_LONGPULSE : IRSMALL_RC5_EVENT_LONGSPACE;
  } else {
    irsmall_rc5_reset_decoder(dev);
    return;
  }

  irsmall_decode_rc5_event(dev, event);
}

static uint8_t irsmall_sirc_basic_bits(hal_irsmall_protocol_t protocol) {
  if (protocol == HAL_IRSMALL_PROTOCOL_SIRC12) {
    return 12u;
  }
  if (protocol == HAL_IRSMALL_PROTOCOL_SIRC15) {
    return 15u;
  }
  return 20u;
}

static uint16_t irsmall_sirc_basic_gap_min(uint8_t bits) {
  return (uint16_t)((75u - (4u + (3u * bits))) * 600u * 8u / 10u);
}

static void irsmall_decode_sirc_basic(hal_irsmall_decoder_t *dev,
                                      uint32_t duration) {
  const uint8_t bits = irsmall_sirc_basic_bits(dev->cfg.protocol);
  const uint16_t gap_min = irsmall_sirc_basic_gap_min(bits);
  const uint16_t m1_max = 2100u;
  const uint16_t m1_min = 1500u;
  const uint16_t m0_min = 900u;
  volatile hal_irsmall_decoder_fsm_t *fsm = &dev->fsm;

  switch (dev->state) {
  case 0u:
    if (duration > gap_min) {
      fsm->bit_count = 0u;
      fsm->signal = 0u;
      dev->state = 1u;
    }
    break;
  case 1u:
    if (duration < m0_min || duration > m1_max) {
      dev->state = 0u;
      break;
    }
    {
      uint32_t signal = fsm->signal >> 1u;
      if (duration >= m1_min) {
        signal |= (bits == 20u) ? 0x80000000u : 0x00008000u;
      }
      fsm->signal = signal;
      fsm->bit_count++;
      if (fsm->bit_count == bits) {
        if (bits == 12u) {
          const uint32_t adjusted = signal >> 3u;
          irsmall_publish(dev, irsmall_byte32(adjusted, 1u),
                          (uint8_t)(irsmall_byte32(adjusted, 0u) >> 1u), 0u,
                          false, 12u);
        } else if (bits == 15u) {
          irsmall_publish(dev, irsmall_byte32(signal, 1u),
                          (uint8_t)(irsmall_byte32(signal, 0u) >> 1u), 0u,
                          false, 15u);
        } else {
          const uint8_t ext = irsmall_byte32(signal, 3u);
          const uint32_t adjusted = (signal & 0x00FFFFFFu) >> 3u;
          irsmall_publish(dev, irsmall_byte32(adjusted, 2u),
                          (uint8_t)(irsmall_byte32(adjusted, 1u) >> 1u), ext,
                          false, 20u);
        }
        dev->state = 0u;
      }
    }
    break;
  default:
    dev->state = 0u;
    break;
  }
}

static void irsmall_decode_sirc_multi_frame(hal_irsmall_decoder_t *dev,
                                            uint8_t bit_count,
                                            uint32_t signal) {
  if (bit_count == 12u) {
    const uint32_t adjusted = signal >> 3u;
    irsmall_publish(dev, irsmall_byte32(adjusted, 3u),
                    (uint8_t)(irsmall_byte32(adjusted, 2u) >> 1u), 0u, false,
                    12u);
  } else if (bit_count == 15u) {
    irsmall_publish(dev, irsmall_byte32(signal, 3u),
                    (uint8_t)(irsmall_byte32(signal, 2u) >> 1u), 0u, false,
                    15u);
  } else {
    const uint8_t ext = irsmall_byte32(signal, 3u);
    const uint32_t adjusted = (signal & 0x00FFFFFFu) >> 3u;
    irsmall_publish(dev, irsmall_byte32(adjusted, 2u),
                    (uint8_t)(irsmall_byte32(adjusted, 1u) >> 1u), ext, false,
                    20u);
  }
}

static void irsmall_decode_sirc_multi(hal_irsmall_decoder_t *dev,
                                      uint32_t duration) {
  const uint16_t gap_max = 33840u;
  const uint16_t gap_min = 5280u;
  const uint16_t m1_max = 2100u;
  const uint16_t m1_min = 1500u;
  const uint16_t m0_min = 900u;
  const uint8_t rpt_count = 5u;
  volatile hal_irsmall_decoder_fsm_t *fsm = &dev->fsm;

  switch (dev->state) {
  case 0u:
    if (duration >= gap_min) {
      if (duration > gap_max) {
        fsm->possibly_held = false;
      }
      fsm->bit_count = 0u;
      fsm->signal = 0u;
      fsm->frame_count = 1u;
      dev->state = 1u;
    } else {
      fsm->possibly_held = false;
    }
    break;
  case 1u:
    if (duration < m0_min || duration > m1_max) {
      if (fsm->frame_count == 3u) {
        dev->state = 0u;
      } else if (duration < gap_min || duration > gap_max) {
        dev->state = 0u;
      } else if (fsm->frame_count == 1u) {
        if (fsm->bit_count == 12u || fsm->bit_count == 15u ||
            fsm->bit_count == 20u) {
          fsm->first_bit_count = fsm->bit_count;
          fsm->bit_count = 0u;
          fsm->first_code = fsm->signal;
          fsm->signal = 0u;
          fsm->frame_count = 2u;
        } else {
          dev->state = 0u;
        }
      } else if (fsm->signal == fsm->first_code) {
        fsm->bit_count = 0u;
        fsm->signal = 0u;
        fsm->frame_count = 3u;
      } else {
        dev->state = 0u;
      }
    } else {
      uint32_t signal = fsm->signal >> 1u;
      if (duration >= m1_min) {
        signal |= 0x80000000u;
      }
      fsm->signal = signal;
      fsm->bit_count++;

      if (fsm->frame_count == 3u) {
        if (fsm->bit_count == fsm->first_bit_count) {
          if (signal == fsm->first_code) {
            irsmall_decode_sirc_multi_frame(dev, fsm->bit_count, signal);
            fsm->possibly_held = true;
          }
          fsm->repeat_count = 0u;
          dev->state = 0u;
        }
      } else if (fsm->frame_count == 1u && fsm->possibly_held &&
                 fsm->bit_count == fsm->first_bit_count &&
                 signal == fsm->first_code) {
        if (fsm->repeat_count < rpt_count) {
          fsm->repeat_count++;
        } else {
          irsmall_publish_held(dev);
        }
        dev->state = 0u;
      }
    }
    break;
  default:
    dev->state = 0u;
    break;
  }
}

static void irsmall_decode_samsung(hal_irsmall_decoder_t *dev,
                                   uint32_t duration) {
  const uint16_t lm_max = 9900u;
  const uint16_t lm_min = 8100u;
  const uint16_t m1_max = 3262u;
  const uint16_t m1_min = 1838u;
  const uint16_t m0_min = 413u;
  const uint32_t gap_max = 32210u;
  const uint16_t gap_min = 1165u;
  const uint8_t rpt_count = 3u;
  volatile hal_irsmall_decoder_fsm_t *fsm = &dev->fsm;

  switch (dev->state) {
  case 0u:
    if (duration > gap_min) {
      if (duration > gap_max) {
        fsm->possibly_held = false;
      }
      dev->state = 1u;
    } else {
      fsm->possibly_held = false;
    }
    break;
  case 1u:
    if (duration >= lm_min && duration <= lm_max) {
      fsm->bit_count = 0u;
      fsm->cmd = 0u;
      fsm->addr16 = 0u;
      dev->state = 2u;
    } else {
      dev->state = 0u;
    }
    break;
  case 2u:
    if (duration < m0_min || duration > m1_max) {
      dev->state = 0u;
      break;
    }
    {
      uint8_t cmd = (uint8_t)(fsm->cmd >> 1u);
      if (duration >= m1_min) {
        cmd |= 0x80u;
      }
      fsm->cmd = cmd;
      fsm->bit_count++;

      if (fsm->bit_count == 8u) {
        fsm->addr16 = cmd;
      } else if (fsm->bit_count == 12u) {
        cmd = (uint8_t)(cmd >> 4u);
        fsm->cmd = cmd;
        fsm->addr16 = (uint16_t)(fsm->addr16 | ((uint16_t)cmd << 8u));
      } else if (fsm->bit_count == 20u) {
        if (fsm->possibly_held && cmd == dev->data.cmd) {
          if (fsm->repeat_count < rpt_count) {
            fsm->repeat_count++;
          } else {
            irsmall_publish_held(dev);
          }
        } else {
          irsmall_publish(dev, fsm->addr16, cmd, 0u, false, 20u);
          fsm->possibly_held = true;
          fsm->repeat_count = 0u;
        }
        dev->state = 0u;
      }
    }
    break;
  default:
    dev->state = 0u;
    break;
  }
}

static void irsmall_decode_samsung32(hal_irsmall_decoder_t *dev,
                                     uint32_t duration) {
  const uint16_t lm_max = 9900u;
  const uint16_t lm_min = 8100u;
  const uint16_t m1_max = 3262u;
  const uint16_t m1_min = 1838u;
  const uint16_t m0_min = 413u;
  const uint32_t gap_max = 76835u;
  const uint16_t gap_min = 31165u;
  const uint8_t rpt_count = 2u;
  volatile hal_irsmall_decoder_fsm_t *fsm = &dev->fsm;

  switch (dev->state) {
  case 0u:
    if (duration > gap_min) {
      if (duration > gap_max) {
        fsm->possibly_held = false;
      }
      dev->state = 1u;
    } else {
      fsm->possibly_held = false;
    }
    break;
  case 1u:
    if (duration >= lm_min && duration <= lm_max) {
      fsm->bit_count = 0u;
      fsm->byte_index = 0u;
      for (uint8_t i = 0u; i < 4u; ++i) {
        fsm->bytes[i] = 0u;
      }
      dev->state = 2u;
    } else {
      dev->state = 0u;
    }
    break;
  case 2u:
    if (duration < m0_min || duration > m1_max) {
      dev->state = 0u;
      break;
    }
    {
      uint8_t value = (uint8_t)(fsm->bytes[fsm->byte_index] >> 1u);
      if (duration >= m1_min) {
        value |= 0x80u;
      }
      fsm->bytes[fsm->byte_index] = value;
      fsm->bit_count++;

      if (fsm->bit_count == 8u || fsm->bit_count == 16u ||
          fsm->bit_count == 24u) {
        fsm->byte_index++;
      } else if (fsm->bit_count == 32u) {
        dev->state = 0u;
        if (fsm->bytes[0] == fsm->bytes[1] &&
            fsm->bytes[2] == (uint8_t)~fsm->bytes[3]) {
          if (fsm->possibly_held && fsm->bytes[2] == dev->data.cmd) {
            if (fsm->repeat_count < rpt_count) {
              fsm->repeat_count++;
            } else {
              irsmall_publish_held(dev);
            }
          } else {
            irsmall_publish(dev, fsm->bytes[0], fsm->bytes[2], 0u, false, 32u);
            fsm->possibly_held = true;
            fsm->repeat_count = 0u;
          }
        }
      }
    }
    break;
  default:
    dev->state = 0u;
    break;
  }
}

static void irsmall_decode_edge(hal_irsmall_decoder_t *dev) {
  const uint32_t now = hal_micros();
  const uint32_t duration = now - dev->previous_time;
  dev->previous_time = now;

  switch (dev->cfg.protocol) {
  case HAL_IRSMALL_PROTOCOL_NEC:
  case HAL_IRSMALL_PROTOCOL_NECX:
    irsmall_decode_nec(dev, duration);
    break;
  case HAL_IRSMALL_PROTOCOL_RC5:
    irsmall_decode_rc5(dev, duration);
    break;
  case HAL_IRSMALL_PROTOCOL_SIRC12:
  case HAL_IRSMALL_PROTOCOL_SIRC15:
  case HAL_IRSMALL_PROTOCOL_SIRC20:
    irsmall_decode_sirc_basic(dev, duration);
    break;
  case HAL_IRSMALL_PROTOCOL_SIRC:
    irsmall_decode_sirc_multi(dev, duration);
    break;
  case HAL_IRSMALL_PROTOCOL_SAMSUNG:
    irsmall_decode_samsung(dev, duration);
    break;
  case HAL_IRSMALL_PROTOCOL_SAMSUNG32:
    irsmall_decode_samsung32(dev, duration);
    break;
  default:
    dev->state = 0u;
    break;
  }
}

static void irsmall_isr_slot(uint8_t slot) {
  if (slot >= HAL_IRSMALL_DECODER_MAX_INSTANCES) {
    return;
  }
  hal_irsmall_decoder_t *dev = s_irsmall_slots[slot];
  if (dev == NULL || !dev->initialized || !dev->enabled) {
    return;
  }
  irsmall_decode_edge(dev);
}

static void irsmall_isr0(void) { irsmall_isr_slot(0u); }
static void irsmall_isr1(void) { irsmall_isr_slot(1u); }
static void irsmall_isr2(void) { irsmall_isr_slot(2u); }
static void irsmall_isr3(void) { irsmall_isr_slot(3u); }

static void (*const s_irsmall_callbacks[HAL_IRSMALL_DECODER_MAX_INSTANCES])(
    void) = {
    irsmall_isr0,
    irsmall_isr1,
    irsmall_isr2,
    irsmall_isr3,
};

static void irsmall_check_timeout(hal_irsmall_decoder_t *dev) {
  if (!dev->cfg.timeout_enabled) {
    return;
  }
  if (dev->cfg.protocol == HAL_IRSMALL_PROTOCOL_RC5) {
    if (dev->fsm.bit_count <= 1u) {
      return;
    }
  } else if (dev->state == 0u) {
    return;
  }

  uint32_t prev = 0u;
  hal_critical_section_enter();
  prev = dev->previous_time;
  hal_critical_section_exit();

  if ((uint32_t)(hal_micros() - prev) >=
      hal_irsmall_decoder_timeout_us(dev->cfg.protocol)) {
    irsmall_reset_isr_state(dev, hal_micros());
  }
}

hal_irsmall_decoder_config_t
hal_irsmall_decoder_default_config(uint8_t input_pin,
                                   hal_irsmall_protocol_t protocol) {
  hal_irsmall_decoder_config_t cfg = {
      protocol,
      input_pin,
      true,
      HAL_IRQ_PRIORITY_DEFAULT,
  };
  return cfg;
}

bool hal_irsmall_decoder_init(hal_irsmall_decoder_t *dev,
                              const hal_irsmall_decoder_config_t *cfg) {
  return hal_status_to_bool(hal_irsmall_decoder_init_ex(dev, cfg));
}

hal_status_t
hal_irsmall_decoder_init_ex(hal_irsmall_decoder_t *dev,
                            const hal_irsmall_decoder_config_t *cfg) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }

  if (dev->initialized) {
    hal_irsmall_decoder_deinit(dev);
  }
  if (!irsmall_ensure_dev_mutex(dev)) {
    return HAL_ENOMEM;
  }

  const hal_irsmall_decoder_config_t effective =
      (cfg != NULL)
          ? *cfg
          : hal_irsmall_decoder_default_config(0u, HAL_IRSMALL_PROTOCOL_NEC);
  if (!irsmall_valid_protocol(effective.protocol)) {
    return HAL_EINVAL;
  }

  const int slot = irsmall_alloc_slot(dev);
  if (slot < 0) {
    return HAL_ENOMEM;
  }

  hal_gpio_set_mode(effective.input_pin, HAL_GPIO_INPUT_PULLUP);

  hal_mutex_lock(dev->mutex);
  dev->cfg = effective;
  dev->initialized = true;
  dev->enabled = true;
  dev->data_available = false;
  dev->state = 0u;
  dev->previous_time = UINT32_MAX;
  dev->slot_index = (uint8_t)slot;
  dev->data.protocol = effective.protocol;
  dev->data.addr = 0u;
  dev->data.cmd = 0u;
  dev->data.ext = 0u;
  dev->data.key_held = false;
  dev->data.bits = 0u;
  irsmall_clear_fsm(dev);
  if (effective.protocol == HAL_IRSMALL_PROTOCOL_RC5) {
    dev->state = IRSMALL_RC5_STATE_MID1;
    dev->previous_time = hal_micros();
    dev->fsm.bit_count = 1u;
    dev->fsm.signal = 1u;
  }
  hal_mutex_unlock(dev->mutex);

  hal_gpio_attach_interrupt(effective.input_pin, s_irsmall_callbacks[slot],
                            hal_irsmall_decoder_irq_mode(effective.protocol));
  hal_gpio_set_irq_priority(effective.irq_priority);
  return HAL_OK;
}

void hal_irsmall_decoder_deinit(hal_irsmall_decoder_t *dev) {
  if (dev == NULL || dev->mutex == NULL) {
    return;
  }

  hal_mutex_t mutex = dev->mutex;
  uint8_t pin = dev->cfg.input_pin;
  hal_mutex_lock(mutex);
  dev->initialized = false;
  dev->enabled = false;
  dev->data_available = false;
  dev->state = 0u;
  hal_mutex_unlock(mutex);

  hal_gpio_detach_interrupt(pin);
  irsmall_free_slot(dev);
  hal_mutex_destroy(mutex);
  dev->mutex = NULL;
  dev->slot_index = IRSMALL_SLOT_NONE;
}

hal_status_t hal_irsmall_decoder_enable(hal_irsmall_decoder_t *dev) {
  hal_status_t status = irsmall_ready_status(dev);
  if (!hal_status_is_ok(status)) {
    return status;
  }

  hal_mutex_lock(dev->mutex);
  dev->enabled = true;
  hal_mutex_unlock(dev->mutex);

  hal_gpio_set_mode(dev->cfg.input_pin, HAL_GPIO_INPUT_PULLUP);
  hal_gpio_attach_interrupt(dev->cfg.input_pin,
                            s_irsmall_callbacks[dev->slot_index],
                            hal_irsmall_decoder_irq_mode(dev->cfg.protocol));
  hal_gpio_set_irq_priority(dev->cfg.irq_priority);
  return hal_irsmall_decoder_reset(dev);
}

hal_status_t hal_irsmall_decoder_disable(hal_irsmall_decoder_t *dev) {
  hal_status_t status = irsmall_ready_status(dev);
  if (!hal_status_is_ok(status)) {
    return status;
  }

  hal_mutex_lock(dev->mutex);
  dev->enabled = false;
  hal_mutex_unlock(dev->mutex);
  hal_gpio_detach_interrupt(dev->cfg.input_pin);
  return HAL_OK;
}

hal_status_t hal_irsmall_decoder_reset(hal_irsmall_decoder_t *dev) {
  hal_status_t status = irsmall_ready_status(dev);
  if (!hal_status_is_ok(status)) {
    return status;
  }

  hal_mutex_lock(dev->mutex);
  irsmall_reset_isr_state(dev, hal_micros());
  hal_mutex_unlock(dev->mutex);
  return HAL_OK;
}

static bool irsmall_take_data(hal_irsmall_decoder_t *dev,
                              hal_irsmall_decoder_data_t *out) {
  irsmall_check_timeout(dev);

  /* The decoded frame is shared with the GPIO ISR. Reading data_available and
   * copying the multi-field struct must be atomic with respect to the ISR's
   * publish, otherwise a preempting ISR can interleave a new frame between our
   * field reads and produce a torn result (e.g. new addr with old cmd). */
  bool had_data = false;
  hal_critical_section_enter();
  if (dev->data_available) {
    had_data = true;
    if (out != NULL) {
      out->protocol = dev->data.protocol;
      out->addr = dev->data.addr;
      out->cmd = dev->data.cmd;
      out->ext = dev->data.ext;
      out->key_held = dev->data.key_held;
      out->bits = dev->data.bits;
    }
    dev->data_available = false;
  }
  hal_critical_section_exit();

  return had_data;
}

bool hal_irsmall_decoder_data_available(hal_irsmall_decoder_t *dev,
                                        hal_irsmall_decoder_data_t *out) {
  bool available = false;
  (void)hal_irsmall_decoder_data_available_ex(dev, out, &available);
  return available;
}

hal_status_t
hal_irsmall_decoder_data_available_ex(hal_irsmall_decoder_t *dev,
                                      hal_irsmall_decoder_data_t *out,
                                      bool *out_available) {
  if (out_available == NULL) {
    return HAL_EINVAL;
  }
  *out_available = false;
  hal_status_t status = irsmall_ready_status(dev);
  if (!hal_status_is_ok(status)) {
    return status;
  }
  hal_mutex_lock(dev->mutex);
  const bool available = irsmall_take_data(dev, out);
  hal_mutex_unlock(dev->mutex);
  *out_available = available;
  return HAL_OK;
}

bool hal_irsmall_decoder_has_data(hal_irsmall_decoder_t *dev) {
  bool available = false;
  (void)hal_irsmall_decoder_has_data_ex(dev, &available);
  return available;
}

hal_status_t hal_irsmall_decoder_has_data_ex(hal_irsmall_decoder_t *dev,
                                             bool *out_has_data) {
  return hal_irsmall_decoder_data_available_ex(dev, NULL, out_has_data);
}

#endif /* HAL_ENABLE_IRSMALL_DECODER */
#endif /* supported target */
