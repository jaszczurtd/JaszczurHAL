#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_STM32G474_FDCAN)

#include "hal_can_stm32g474_fdcan.h"

#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"

#include <string.h>

#ifdef JH_STM32G474_HW
#include "port/stm32g474_fdcan_timing.h"
#include "port/stm32g474_regs.h"
#endif

#define STM32_FDCAN_STD_RX_PIN 11u /* PA11, AF9 */
#define STM32_FDCAN_STD_TX_PIN 12u /* PA12, AF9 */
#define STM32_FDCAN_AF 9u

static uint8_t dlc_to_fdcan(uint8_t dlc) { return (uint8_t)(dlc & 0x0Fu); }

static uint32_t fdcan_tx_header0(const hal_can_frame_t *frame) {
  if ((frame->flags & HAL_CAN_FRAME_EXTENDED) != 0u) {
    return (frame->id & HAL_CAN_EXT_ID_MASK) | (1u << 30) |
           (((frame->flags & HAL_CAN_FRAME_RTR) != 0u) ? (1u << 29) : 0u) |
           (((frame->flags & HAL_CAN_FRAME_ESI) != 0u) ? (1u << 31) : 0u);
  }
  return ((frame->id & HAL_CAN_STD_ID_MASK) << 18) |
         (((frame->flags & HAL_CAN_FRAME_RTR) != 0u) ? (1u << 29) : 0u) |
         (((frame->flags & HAL_CAN_FRAME_ESI) != 0u) ? (1u << 31) : 0u);
}

static uint32_t fdcan_tx_header1(const hal_can_frame_t *frame) {
  uint32_t h = (uint32_t)dlc_to_fdcan(frame->dlc) << 16;
  if ((frame->flags & HAL_CAN_FRAME_BRS) != 0u) {
    h |= 1u << 20;
  }
  if ((frame->flags & HAL_CAN_FRAME_FD) != 0u) {
    h |= 1u << 21;
  }
  return h;
}

static void fdcan_decode_header(uint32_t h0, uint32_t h1,
                                hal_can_frame_t *frame) {
  memset(frame, 0, sizeof(*frame));
  if ((h0 & (1u << 30)) != 0u) {
    frame->flags |= HAL_CAN_FRAME_EXTENDED;
    frame->id = h0 & HAL_CAN_EXT_ID_MASK;
  } else {
    frame->id = (h0 >> 18) & HAL_CAN_STD_ID_MASK;
  }
  if ((h0 & (1u << 29)) != 0u) {
    frame->flags |= HAL_CAN_FRAME_RTR;
  }
  if ((h0 & (1u << 31)) != 0u) {
    frame->flags |= HAL_CAN_FRAME_ESI;
  }
  if ((h1 & (1u << 21)) != 0u) {
    frame->flags |= HAL_CAN_FRAME_FD;
  }
  if ((h1 & (1u << 20)) != 0u) {
    frame->flags |= HAL_CAN_FRAME_BRS;
  }
  frame->dlc = (uint8_t)((h1 >> 16) & 0x0Fu);
  frame->len = hal_can_dlc_to_bytes(frame->dlc);
  if (frame->len > HAL_CAN_FD_MAX_DATA_LEN) {
    frame->len = HAL_CAN_FD_MAX_DATA_LEN;
  }
}

static bool fdcan_mode_valid(const hal_can_stm32g474_fdcan_t *fdcan,
                             hal_can_mode_t mode) {
  hal_can_mode_t supported = HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_LISTEN_ONLY |
                             HAL_CAN_MODE_ONE_SHOT | HAL_CAN_MODE_SLEEP;
  if (fdcan && fdcan->fd_capable) {
    supported |= HAL_CAN_MODE_FD;
  }
  if ((mode & ~supported) != 0u) {
    return false;
  }
  uint8_t ops = 0u;
  ops += (mode & HAL_CAN_MODE_LOOPBACK) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_LISTEN_ONLY) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_SLEEP) != 0u ? 1u : 0u;
  return ops <= 1u;
}

#ifdef JH_STM32G474_HW
static volatile uint32_t *fdcan_mram_word(uint32_t word_offset) {
  return (volatile uint32_t *)(FDCAN_SRAM_BASE + (word_offset * 4u));
}

static void fdcan_gpio_af9(uint8_t pin) {
  const uint32_t port = (uint32_t)(pin >> 4);
  const uint32_t n = (uint32_t)(pin & 0x0Fu);
  if (port > 6u) {
    return;
  }
  RCC_AHB2ENR |= (1u << port);
  GPIO_MODER(port) =
      (GPIO_MODER(port) & ~(0x3u << (n * 2u))) | (GPIO_MODE_AF << (n * 2u));
  GPIO_OSPEEDR(port) |= 0x3u << (n * 2u);
  GPIO_PUPDR(port) &= ~(0x3u << (n * 2u));
  if (n < 8u) {
    GPIO_AFRL(port) =
        (GPIO_AFRL(port) & ~(0xFu << (n * 4u))) | (STM32_FDCAN_AF << (n * 4u));
  } else {
    const uint32_t p = n - 8u;
    GPIO_AFRH(port) =
        (GPIO_AFRH(port) & ~(0xFu << (p * 4u))) | (STM32_FDCAN_AF << (p * 4u));
  }
}

static bool fdcan_enter_init(void) {
  FDCAN_CCCR(FDCAN1_BASE) |= FDCAN_CCCR_INIT;
  for (uint32_t i = 0; i < FDCAN_POLL_TIMEOUT; ++i) {
    if ((FDCAN_CCCR(FDCAN1_BASE) & FDCAN_CCCR_INIT) != 0u) {
      FDCAN_CCCR(FDCAN1_BASE) |= FDCAN_CCCR_CCE;
      return true;
    }
  }
  return false;
}

static bool fdcan_leave_init(void) {
  FDCAN_CCCR(FDCAN1_BASE) &= ~FDCAN_CCCR_INIT;
  for (uint32_t i = 0; i < FDCAN_POLL_TIMEOUT; ++i) {
    if ((FDCAN_CCCR(FDCAN1_BASE) & FDCAN_CCCR_INIT) == 0u) {
      return true;
    }
  }
  return false;
}

static uint32_t fdcan_encode_nbtp(uint32_t bitrate_hz) {
  jh_stm32g474_fdcan_timing_t timing = {};
  if (!jh_stm32g474_fdcan_compute_timing(JH_G474_FDCAN_CLOCK_HZ, bitrate_hz,
                                         false, &timing)) {
    return 0u;
  }
  return jh_stm32g474_fdcan_encode_nbtp(&timing);
}

static uint32_t fdcan_encode_dbtp(uint32_t bitrate_hz) {
  jh_stm32g474_fdcan_timing_t timing = {};
  if (!jh_stm32g474_fdcan_compute_timing(JH_G474_FDCAN_CLOCK_HZ, bitrate_hz,
                                         true, &timing)) {
    return 0u;
  }
  return jh_stm32g474_fdcan_encode_dbtp(&timing);
}

static void fdcan_clear_mram(void) {
  volatile uint32_t *mram = fdcan_mram_word(0u);
  for (uint32_t i = 0; i < 256u; ++i) {
    mram[i] = 0u;
  }
}

static void fdcan_write_filter_config(uint8_t std_count, uint8_t ext_count,
                                      bool filters_active) {
  FDCAN_SIDFC(FDCAN1_BASE) =
      FDCAN_MRAM_STD_FILTER_WORD | ((uint32_t)std_count << 16);
  FDCAN_XIDFC(FDCAN1_BASE) =
      FDCAN_MRAM_EXT_FILTER_WORD | ((uint32_t)ext_count << 16);
  uint32_t rxgfc = 0u;
  if (filters_active) {
    rxgfc |= (3u << 4) | (3u << 2); /* reject non-matching std/ext frames */
  }
  FDCAN_RXGFC(FDCAN1_BASE) = rxgfc;
}

static uint32_t fdcan_tx_elem_word(uint8_t idx) {
  return FDCAN_MRAM_TX_BUF_WORD + ((uint32_t)idx * FDCAN_MRAM_ELEM_WORDS_64);
}

static uint32_t fdcan_rx0_elem_word(uint8_t idx) {
  return FDCAN_MRAM_RX0_WORD + ((uint32_t)idx * FDCAN_MRAM_ELEM_WORDS_64);
}
#endif /* JH_STM32G474_HW */

bool hal_can_stm32g474_fdcan_init(hal_can_stm32g474_fdcan_t *fdcan,
                                  const hal_can_stm32g474_fdcan_config_t *cfg) {
  if (!fdcan || !cfg || cfg->arbitration_bitrate_hz == 0u) {
    return false;
  }
  memset(fdcan, 0, sizeof(*fdcan));
  fdcan->fd_capable = cfg->enable_fd;
  fdcan->one_shot = cfg->one_shot_tx;

#ifdef JH_STM32G474_HW
  const uint8_t rx_pin = cfg->rx_pin ? cfg->rx_pin : STM32_FDCAN_STD_RX_PIN;
  const uint8_t tx_pin = cfg->tx_pin ? cfg->tx_pin : STM32_FDCAN_STD_TX_PIN;
  fdcan_gpio_af9(rx_pin);
  fdcan_gpio_af9(tx_pin);

  RCC_CCIPR = (RCC_CCIPR & ~RCC_CCIPR_FDCANSEL_MASK) | RCC_CCIPR_FDCANSEL_PCLK1;
  RCC_APB1ENR1 |= RCC_APB1ENR1_FDCANEN;
  (void)RCC_APB1ENR1;

  if (!fdcan_enter_init()) {
    return false;
  }
  fdcan_clear_mram();

  const uint32_t nbtp = fdcan_encode_nbtp(cfg->arbitration_bitrate_hz);
  const uint32_t dbtp =
      fdcan_encode_dbtp(cfg->data_bitrate_hz ? cfg->data_bitrate_hz
                                             : cfg->arbitration_bitrate_hz);
  if (nbtp == 0u || dbtp == 0u) {
    return false;
  }
  FDCAN_NBTP(FDCAN1_BASE) = nbtp;
  FDCAN_DBTP(FDCAN1_BASE) = dbtp;
  FDCAN_TDCR(FDCAN1_BASE) = cfg->enable_fd ? (8u << 8) : 0u;
  FDCAN_XIDAM(FDCAN1_BASE) = HAL_CAN_EXT_ID_MASK;
  fdcan_write_filter_config(0u, 0u, false);
  FDCAN_RXF0C(FDCAN1_BASE) =
      FDCAN_MRAM_RX0_WORD | (FDCAN_MRAM_RX_FIFO0_ELEMS << 16);
  FDCAN_RXF1C(FDCAN1_BASE) = 0u;
  FDCAN_RXESC(FDCAN1_BASE) = FDCAN_ELEM_SIZE_64;
  FDCAN_TXEFC(FDCAN1_BASE) = 0u;
  FDCAN_TXBC(FDCAN1_BASE) =
      FDCAN_MRAM_TX_BUF_WORD | (FDCAN_MRAM_TX_BUF_ELEMS << 16);
  FDCAN_TXESC(FDCAN1_BASE) = FDCAN_ELEM_SIZE_64;
  FDCAN_IR(FDCAN1_BASE) = FDCAN_IR_ALL;
  FDCAN_IE(FDCAN1_BASE) = 0u;

  uint32_t cccr = FDCAN_CCCR_INIT | FDCAN_CCCR_CCE;
  if (cfg->enable_fd) {
    cccr |= FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE;
  }
  if (cfg->one_shot_tx) {
    cccr |= FDCAN_CCCR_DAR;
  }
  FDCAN_CCCR(FDCAN1_BASE) = cccr;
  if (!fdcan_leave_init()) {
    return false;
  }
#endif
  fdcan->initialized = true;
  return true;
}

void hal_can_stm32g474_fdcan_deinit(hal_can_stm32g474_fdcan_t *fdcan) {
  if (!fdcan) {
    return;
  }
#ifdef JH_STM32G474_HW
  (void)fdcan_enter_init();
#endif
  fdcan->initialized = false;
}

bool hal_can_stm32g474_fdcan_send_frame(hal_can_stm32g474_fdcan_t *fdcan,
                                        const hal_can_frame_t *frame) {
  if (!fdcan || !fdcan->initialized || !hal_can_validate_frame(frame)) {
    return false;
  }
  if ((frame->flags & HAL_CAN_FRAME_FD) != 0u && !fdcan->fd_capable) {
    return false;
  }

#ifdef JH_STM32G474_HW
  uint32_t pending = FDCAN_TXBRP(FDCAN1_BASE);
  uint8_t idx = 0xFFu;
  for (uint8_t i = 0; i < FDCAN_MRAM_TX_BUF_ELEMS; ++i) {
    if ((pending & (1u << i)) == 0u) {
      idx = i;
      break;
    }
  }
  if (idx == 0xFFu) {
    return false;
  }

  volatile uint32_t *elem = fdcan_mram_word(fdcan_tx_elem_word(idx));
  elem[0] = fdcan_tx_header0(frame);
  elem[1] = fdcan_tx_header1(frame);
  const uint8_t bytes =
      ((frame->flags & HAL_CAN_FRAME_RTR) != 0u) ? 0u : frame->len;
  for (uint8_t i = 0; i < bytes; ++i) {
    volatile uint8_t *payload = (volatile uint8_t *)&elem[2];
    payload[i] = frame->data[i];
  }
  FDCAN_TXBAR(FDCAN1_BASE) = 1u << idx;
  for (uint32_t spin = 0; spin < FDCAN_POLL_TIMEOUT; ++spin) {
    if ((FDCAN_TXBTO(FDCAN1_BASE) & (1u << idx)) != 0u) {
      return true;
    }
    if ((FDCAN_TXBCF(FDCAN1_BASE) & (1u << idx)) != 0u) {
      return false;
    }
  }
  return false;
#else
  return false;
#endif
}

bool hal_can_stm32g474_fdcan_send(hal_can_stm32g474_fdcan_t *fdcan, uint32_t id,
                                  uint8_t len, const uint8_t *data) {
  if (len > 0u && data == NULL) {
    return false;
  }
  if (len > HAL_CAN_MAX_DATA_LEN) {
    len = HAL_CAN_MAX_DATA_LEN;
  }
  hal_can_frame_t frame = {};
  frame.id = id & HAL_CAN_STD_ID_MASK;
  frame.dlc = len;
  frame.len = len;
  if (len > 0u) {
    memcpy(frame.data, data, len);
  }
  return hal_can_stm32g474_fdcan_send_frame(fdcan, &frame);
}

bool hal_can_stm32g474_fdcan_available(hal_can_stm32g474_fdcan_t *fdcan) {
  if (!fdcan || !fdcan->initialized) {
    return false;
  }
#ifdef JH_STM32G474_HW
  return (FDCAN_RXF0S(FDCAN1_BASE) & FDCAN_RXF0S_F0FL_MASK) != 0u;
#else
  return false;
#endif
}

bool hal_can_stm32g474_fdcan_receive_frame(hal_can_stm32g474_fdcan_t *fdcan,
                                           hal_can_frame_t *frame) {
  if (!fdcan || !fdcan->initialized || !frame) {
    return false;
  }
#ifdef JH_STM32G474_HW
  const uint32_t status = FDCAN_RXF0S(FDCAN1_BASE);
  if ((status & FDCAN_RXF0S_F0FL_MASK) == 0u) {
    return false;
  }
  const uint8_t idx =
      (uint8_t)((status & FDCAN_RXF0S_F0GI_MASK) >> FDCAN_RXF0S_F0GI_POS);
  volatile uint32_t *elem = fdcan_mram_word(fdcan_rx0_elem_word(idx));
  fdcan_decode_header(elem[0], elem[1], frame);
  if ((frame->flags & HAL_CAN_FRAME_RTR) == 0u && frame->len > 0u) {
    volatile uint8_t *payload = (volatile uint8_t *)&elem[2];
    for (uint8_t i = 0; i < frame->len; ++i) {
      frame->data[i] = payload[i];
    }
  }
  FDCAN_RXF0A(FDCAN1_BASE) = idx & FDCAN_RXF0A_F0AI_MASK;
  return hal_can_validate_frame(frame);
#else
  return false;
#endif
}

bool hal_can_stm32g474_fdcan_receive(hal_can_stm32g474_fdcan_t *fdcan,
                                     uint32_t *id, uint8_t *len,
                                     uint8_t *data) {
  if (!id || !len || !data) {
    return false;
  }
  hal_can_frame_t frame = {};
  if (!hal_can_stm32g474_fdcan_receive_frame(fdcan, &frame)) {
    return false;
  }
  if ((frame.flags & HAL_CAN_FRAME_FD) != 0u ||
      frame.len > HAL_CAN_MAX_DATA_LEN) {
    return false;
  }
  *id = frame.id;
  *len = frame.len;
  if (frame.len > 0u) {
    memcpy(data, frame.data, frame.len);
  }
  return true;
}

bool hal_can_stm32g474_fdcan_set_filter(hal_can_stm32g474_fdcan_t *fdcan,
                                        uint8_t index,
                                        const hal_can_filter_t *filter) {
  if (!fdcan || !fdcan->initialized || index >= HAL_CAN_MAX_FILTERS ||
      !hal_can_validate_filter(filter)) {
    return false;
  }
#ifdef JH_STM32G474_HW
  const bool ext = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  if (!fdcan_enter_init()) {
    return false;
  }
  if (ext) {
    volatile uint32_t *elem =
        fdcan_mram_word(FDCAN_MRAM_EXT_FILTER_WORD + ((uint32_t)index * 2u));
    elem[0] = (1u << 29) | (filter->id & HAL_CAN_EXT_ID_MASK);
    elem[1] = (2u << 30) | (filter->mask & HAL_CAN_EXT_ID_MASK);
    if ((uint8_t)(index + 1u) > fdcan->ext_filter_count) {
      fdcan->ext_filter_count = (uint8_t)(index + 1u);
    }
  } else {
    volatile uint32_t *elem =
        fdcan_mram_word(FDCAN_MRAM_STD_FILTER_WORD + index);
    elem[0] = (2u << 30) | (1u << 27) |
              ((filter->mask & HAL_CAN_STD_ID_MASK) << 16) |
              (filter->id & HAL_CAN_STD_ID_MASK);
    if ((uint8_t)(index + 1u) > fdcan->std_filter_count) {
      fdcan->std_filter_count = (uint8_t)(index + 1u);
    }
  }
  fdcan_write_filter_config(fdcan->std_filter_count, fdcan->ext_filter_count,
                            true);
  return fdcan_leave_init();
#else
  (void)index;
  (void)filter;
  return false;
#endif
}

bool hal_can_stm32g474_fdcan_set_std_filters(hal_can_stm32g474_fdcan_t *fdcan,
                                             uint32_t id0, uint32_t id1) {
  hal_can_filter_t f0 = {id0 & HAL_CAN_STD_ID_MASK, HAL_CAN_STD_ID_MASK, 0u};
  hal_can_filter_t f1 = {id1 & HAL_CAN_STD_ID_MASK, HAL_CAN_STD_ID_MASK, 0u};
  return hal_can_stm32g474_fdcan_set_filter(fdcan, 0u, &f0) &&
         hal_can_stm32g474_fdcan_set_filter(fdcan, 1u, &f1);
}

bool hal_can_stm32g474_fdcan_set_mode(hal_can_stm32g474_fdcan_t *fdcan,
                                      hal_can_mode_t mode) {
  if (!fdcan || !fdcan->initialized || !fdcan_mode_valid(fdcan, mode)) {
    return false;
  }
#ifdef JH_STM32G474_HW
  if (!fdcan_enter_init()) {
    return false;
  }
  uint32_t cccr = FDCAN_CCCR_INIT | FDCAN_CCCR_CCE;
  uint32_t test = FDCAN_TEST(FDCAN1_BASE) & ~FDCAN_TEST_LBCK;
  if ((mode & HAL_CAN_MODE_FD) != 0u) {
    cccr |= FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE;
  }
  if ((mode & HAL_CAN_MODE_ONE_SHOT) != 0u || fdcan->one_shot) {
    cccr |= FDCAN_CCCR_DAR;
  }
  if ((mode & HAL_CAN_MODE_LISTEN_ONLY) != 0u) {
    cccr |= FDCAN_CCCR_MON;
  }
  if ((mode & HAL_CAN_MODE_LOOPBACK) != 0u) {
    cccr |= FDCAN_CCCR_TEST;
    test |= FDCAN_TEST_LBCK;
  }
  FDCAN_TEST(FDCAN1_BASE) = test;
  FDCAN_CCCR(FDCAN1_BASE) = cccr;
  if ((mode & HAL_CAN_MODE_SLEEP) != 0u) {
    return true;
  }
  return fdcan_leave_init();
#else
  return false;
#endif
}

bool hal_can_stm32g474_fdcan_start(hal_can_stm32g474_fdcan_t *fdcan,
                                   hal_can_mode_t mode) {
  return hal_can_stm32g474_fdcan_set_mode(fdcan, mode);
}

bool hal_can_stm32g474_fdcan_stop(hal_can_stm32g474_fdcan_t *fdcan) {
  if (!fdcan || !fdcan->initialized) {
    return false;
  }
#ifdef JH_STM32G474_HW
  return fdcan_enter_init();
#else
  return false;
#endif
}

bool hal_can_stm32g474_fdcan_get_state(hal_can_stm32g474_fdcan_t *fdcan,
                                       bool started, hal_can_state_t *state) {
  if (!fdcan || !state) {
    return false;
  }
  if (!started) {
    *state = HAL_CAN_STATE_STOPPED;
    return true;
  }
#ifdef JH_STM32G474_HW
  const uint32_t psr = FDCAN_PSR(FDCAN1_BASE);
  if ((psr & FDCAN_PSR_BO) != 0u) {
    *state = HAL_CAN_STATE_BUS_OFF;
  } else if ((psr & FDCAN_PSR_EP) != 0u) {
    *state = HAL_CAN_STATE_ERROR_PASSIVE;
  } else if ((psr & FDCAN_PSR_EW) != 0u) {
    *state = HAL_CAN_STATE_ERROR_WARNING;
  } else {
    *state = HAL_CAN_STATE_ERROR_ACTIVE;
  }
  return true;
#else
  *state = HAL_CAN_STATE_STOPPED;
  return true;
#endif
}

bool hal_can_stm32g474_fdcan_get_error_counters(
    hal_can_stm32g474_fdcan_t *fdcan, hal_can_error_counters_t *counters) {
  if (!fdcan || !counters) {
    return false;
  }
#ifdef JH_STM32G474_HW
  const uint32_t ecr = FDCAN_ECR(FDCAN1_BASE);
  counters->tx = (uint8_t)(ecr & FDCAN_ECR_TEC_MASK);
  counters->rx = (uint8_t)((ecr & FDCAN_ECR_REC_MASK) >> 8);
#else
  counters->tx = 0u;
  counters->rx = 0u;
#endif
  return true;
}

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_STM32G474_FDCAN */
#endif /* HAL_TARGET_IS_STM32G474 */
