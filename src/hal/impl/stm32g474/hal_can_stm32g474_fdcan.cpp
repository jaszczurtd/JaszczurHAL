#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_STM32G474_FDCAN)

#include "hal_can_stm32g474_fdcan.h"

#include "hal/core/jh_endian.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"

#include <string.h>

#ifdef JH_STM32G474_HW
#include "port/stm32g474_clock.h"
#include "port/stm32g474_fdcan_timing.h"
#include "port/stm32g474_gpio_af.h"
#include "port/stm32g474_nvic.h"
#include "port/stm32g474_regs.h"

/* ── Instances and pins (DS12288 Table 13) ──────────────────────────────── */

#define PIN(port, n) ((uint8_t)((port) * 16u + (n)))
#define PORT_A 0u
#define PORT_B 1u
#define PORT_D 3u

typedef struct {
  uint8_t instance; /* 0..2 */
  uint8_t pin;
  bool tx;
  uint8_t af;
} fdcan_pin_t;

static const fdcan_pin_t kPins[] = {
    {0u, PIN(PORT_A, 11u), false, 9u}, {0u, PIN(PORT_A, 12u), true, 9u},
    {0u, PIN(PORT_B, 8u), false, 9u},  {0u, PIN(PORT_B, 9u), true, 9u},
    {0u, PIN(PORT_D, 0u), false, 9u},  {0u, PIN(PORT_D, 1u), true, 9u},
    {1u, PIN(PORT_B, 12u), false, 9u}, {1u, PIN(PORT_B, 13u), true, 9u},
    {1u, PIN(PORT_B, 5u), false, 9u},  {1u, PIN(PORT_B, 6u), true, 9u},
    {2u, PIN(PORT_A, 8u), false, 11u}, {2u, PIN(PORT_A, 15u), true, 11u},
    {2u, PIN(PORT_B, 3u), false, 11u}, {2u, PIN(PORT_B, 4u), true, 11u},
};

/* Default RX/TX per instance: the pins of the NUCLEO CAN-FD HAT. */
static const uint8_t kDefaultRx[FDCAN_INSTANCE_COUNT] = {
    PIN(PORT_A, 11u), PIN(PORT_B, 12u), PIN(PORT_A, 8u)};
static const uint8_t kDefaultTx[FDCAN_INSTANCE_COUNT] = {
    PIN(PORT_A, 12u), PIN(PORT_B, 13u), PIN(PORT_B, 4u)};

/* Classic-API filter slots. */
#define LEGACY_SLOTS HAL_CAN_MAX_FILTERS

/* Standard filter element S0 and extended F0/F1 fields (RM0440 §44.3.8):
 * filter types, and the element configurations used (store in FIFO0,
 * reject). */
#define SFT_RANGE (0u << 30)
#define SFT_DUAL (1u << 30)
#define SFT_CLASSIC (2u << 30)
#define SFEC_POS 27u
#define EFEC_POS 29u
#define EFT_DUAL (1u << 30)
#define EFT_CLASSIC (2u << 30)
#define EFT_RANGE_NO_XIDAM (3u << 30)
#define ELEMENT_FIFO0 1u
#define ELEMENT_REJECT 3u

/* Owners of filter elements besides filter indices. */
#define OWNER_FREE HAL_CAN_FILTER_NONE
#define OWNER_ACCEPT_ALL 0xFEu

static_assert(HAL_CAN_STM32G474_FDCAN_STD_FILTERS == FDCAN_MRAM_STD_FILTERS &&
                  HAL_CAN_STM32G474_FDCAN_EXT_FILTERS == FDCAN_MRAM_EXT_FILTERS,
              "filter ownership tables must cover the element lists");

/* TX/RX element header bits (RM0440 Tables 411, 413). */
#define ELEM_ESI (1u << 31)
#define ELEM_XTD (1u << 30)
#define ELEM_RTR (1u << 29)
#define ELEM_FDF (1u << 21)
#define ELEM_BRS (1u << 20)
#define ELEM_DLC_POS 16u

/* Bounds of the INIT handshake and of the wake-up of an MCP2562FD-class
 * transceiver (tWAKE max 40 us). */
#define INIT_TIMEOUT_US 2000u
#define TRANSCEIVER_WAKE_US 40u

/* ISO 11898-1: the arbitration phase runs at 1 Mbit/s at most. */
#define MAX_NOMINAL_BITRATE_HZ 1000000u
/* Data bitrate the controller itself is rated for here (CiA 601-3). */
#define MAX_DATA_BITRATE_HZ 8000000u

/* Interrupts (RM0440 44.4.21-24): IR/IE bit positions, IT0 carries the RX
 * FIFO groups and IT1 every other group (ILS: SMSG, TFERR, MISC, BERR,
 * PERR). The NVIC priority leaves room for FreeRTOS-aware ISRs above. */
#define IR_RF0N (1u << 0)
#define IR_RF0L (1u << 2)
#define IR_RF1N (1u << 3)
#define IR_RF1L (1u << 5)
#define IR_TC (1u << 7)
#define IR_TCF (1u << 8)
#define IR_TFE (1u << 9)
#define IR_MRAF (1u << 14)
#define IR_EP (1u << 17)
#define IR_EW (1u << 18)
#define IR_BO (1u << 19)
#define IR_PEA (1u << 21)
#define IR_PED (1u << 22)
#define IE_USED                                                                \
  (IR_RF0N | IR_RF0L | IR_RF1N | IR_RF1L | IR_TC | IR_TCF | IR_TFE | IR_MRAF | \
   IR_EP | IR_EW | IR_BO)
/* A failed one-shot attempt sets TXBCF without raising TCF (seen on the
 * STM32G474); the protocol error it causes does interrupt. Lost
 * arbitration causes no error and is collected with the next interrupt or
 * when the FIFO runs empty (TFE). */
#define IE_ONE_SHOT (IR_PEA | IR_PED)
#define ILS_IT1_GROUPS 0x7Cu
#define ILE_BOTH_LINES 0x3u
#define TX_SLOT_BITS 0x7u
#define FDCAN_IRQ_PRIORITY 0x80u

/* RX FIFO element R1: filter index, "accepted without a match" and the
 * timestamp. TX element T1 and TX event E1: message marker and "store a TX
 * event". */
#define ELEM_FIDX_POS 24u
#define ELEM_FIDX_MASK (0x7Fu << ELEM_FIDX_POS)
#define ELEM_ANMF (1u << 31)
#define ELEM_TS_MASK 0xFFFFu
#define ELEM_MM_POS 24u
#define ELEM_EFC (1u << 23)
#define IR_TEFN (1u << 10)

/* Frame timestamps. By default the interrupt's entry time (hal_micros64()).
 * With HAL_CAN_STM32G474_TIMESTAMP_TIM3, TIM3 counts microseconds and every
 * instance captures it at the start of frame (TSCC.TSS external; RM0440
 * 44.4.8): received frames carry it in RXTS, sent ones in the TX event
 * FIFO. The 16-bit value is extended against hal_micros64(), which holds
 * while the interrupt serves a frame within 65 ms. TIM3 then belongs to
 * FDCAN: hal_pwm refuses its pins. */
#ifdef HAL_CAN_STM32G474_TIMESTAMP_TIM3
#define TIMESTAMP_TIM3 1
#else
#define TIMESTAMP_TIM3 0
#endif

/* What a TX FIFO element carries. */
enum { SLOT_FREE = 0u, SLOT_QUEUED = 1u, SLOT_SYNC = 2u };

static const uint8_t kIrqIt0[FDCAN_INSTANCE_COUNT] = {21u, 86u, 88u};
static const uint8_t kIrqIt1[FDCAN_INSTANCE_COUNT] = {22u, 87u, 89u};
static hal_can_stm32g474_fdcan_t *volatile s_isr_ctx[FDCAN_INSTANCE_COUNT];

static uint8_t s_open_mask; /* instances with a live handle */

static uint32_t reg_base(const hal_can_stm32g474_fdcan_t *fd) {
  return FDCAN1_BASE + ((uint32_t)fd->index * 0x400u);
}

static uint32_t rd(hal_can_stm32g474_fdcan_t *fd, uint32_t off) {
  return JH_REG32_RD(reg_base(fd) + off);
}

static void wr(hal_can_stm32g474_fdcan_t *fd, uint32_t off, uint32_t value) {
  JH_REG32_WR(reg_base(fd) + off, value);
}

static uint32_t ram_addr(const hal_can_stm32g474_fdcan_t *fd, uint32_t word) {
  return FDCAN_SRAM_BASE + ((uint32_t)fd->index * FDCAN_MRAM_STRIDE) +
         (word * 4u);
}

static uint32_t ram_rd(hal_can_stm32g474_fdcan_t *fd, uint32_t word) {
  return JH_REG32_RD(ram_addr(fd, word));
}

static void ram_wr(hal_can_stm32g474_fdcan_t *fd, uint32_t word,
                   uint32_t value) {
  JH_REG32_WR(ram_addr(fd, word), value);
}

static const fdcan_pin_t *find_pin(uint8_t index, uint8_t pin, bool tx) {
  for (size_t i = 0; i < COUNTOF(kPins); ++i) {
    if (kPins[i].instance == index && kPins[i].pin == pin &&
        kPins[i].tx == tx) {
      return &kPins[i];
    }
  }
  return NULL;
}

/* ── Controller state ───────────────────────────────────────────────────── */

/* Reading PSR resets LEC and DLEC to 7 ("no change"); keep the last codes. */
static uint32_t read_psr(hal_can_stm32g474_fdcan_t *fd) {
  const uint32_t psr = rd(fd, FDCAN_PSR);
  const uint8_t lec = (uint8_t)(psr & FDCAN_PSR_LEC_MASK);
  const uint8_t dlec =
      (uint8_t)((psr & FDCAN_PSR_DLEC_MASK) >> FDCAN_PSR_DLEC_POS);
  if (lec != 7u) {
    fd->last_error = lec;
  }
  if (dlec != 7u) {
    fd->last_data_error = dlec;
  }
  return psr;
}

static hal_can_state_t state_from_psr(uint32_t psr) {
  if ((psr & FDCAN_PSR_BO) != 0u) {
    return HAL_CAN_STATE_BUS_OFF;
  }
  if ((psr & FDCAN_PSR_EP) != 0u) {
    return HAL_CAN_STATE_ERROR_PASSIVE;
  }
  if ((psr & FDCAN_PSR_EW) != 0u) {
    return HAL_CAN_STATE_ERROR_WARNING;
  }
  return HAL_CAN_STATE_ERROR_ACTIVE;
}

/* HAL_OK once CCCR.INIT reads @p set, HAL_ETIMEOUT after INIT_TIMEOUT_US. */
static hal_status_t wait_init(hal_can_stm32g474_fdcan_t *fd, bool set) {
  const uint32_t started = hal_micros();
  for (;;) {
    const bool init = (rd(fd, FDCAN_CCCR) & FDCAN_CCCR_INIT) != 0u;
    if (init == set) {
      return HAL_OK;
    }
    if (hal_elapsed_u32(hal_micros(), started, INIT_TIMEOUT_US)) {
      return HAL_ETIMEOUT;
    }
  }
}

/* INIT stops bus activity; CCE unlocks the protected registers and resets the
 * TX request and FIFO status registers, which drops every pending frame. */
static hal_status_t enter_config(hal_can_stm32g474_fdcan_t *fd) {
  wr(fd, FDCAN_CCCR, rd(fd, FDCAN_CCCR) | FDCAN_CCCR_INIT);
  const hal_status_t st = wait_init(fd, true);
  if (st == HAL_OK) {
    wr(fd, FDCAN_CCCR, rd(fd, FDCAN_CCCR) | FDCAN_CCCR_CCE);
  } else {
    hal_derr_limited("can", "FDCAN%u did not enter INIT",
                     (unsigned)fd->index + 1u);
  }
  return st;
}

/* Clearing INIT also clears CCE; the node joins after 11 recessive bits. */
static hal_status_t leave_config(hal_can_stm32g474_fdcan_t *fd) {
  wr(fd, FDCAN_CCCR, rd(fd, FDCAN_CCCR) & ~FDCAN_CCCR_INIT);
  const hal_status_t st = wait_init(fd, false);
  if (st != HAL_OK) {
    hal_derr_limited("can", "FDCAN%u did not leave INIT",
                     (unsigned)fd->index + 1u);
  }
  return st;
}

static void transceiver_standby(hal_can_stm32g474_fdcan_t *fd, bool standby) {
  if (!fd->has_standby) {
    return;
  }
  hal_gpio_write(fd->standby_pin,
                 standby ? fd->standby_high : !fd->standby_high);
  if (!standby) {
    hal_delay_us(TRANSCEIVER_WAKE_US);
  }
}

/* A bus-off controller sets INIT itself; clearing it starts the recovery
 * (128 x 11 recessive bits), after which the error counters reset. */
static void recover_bus_off(hal_can_stm32g474_fdcan_t *fd) {
  if ((fd->mode & HAL_CAN_MODE_MANUAL_RECOVERY) != 0u) {
    return; /* waits for hal_can_recover() */
  }
  if ((read_psr(fd) & FDCAN_PSR_BO) != 0u &&
      (rd(fd, FDCAN_CCCR) & FDCAN_CCCR_INIT) != 0u) {
    (void)leave_config(fd);
  }
}

/* ── Frames ─────────────────────────────────────────────────────────────── */

static uint32_t frame_header0(const hal_can_frame_t *frame) {
  uint32_t h = 0u;
  if ((frame->flags & HAL_CAN_FRAME_EXTENDED) != 0u) {
    h = (frame->id & HAL_CAN_EXT_ID_MASK) | ELEM_XTD;
  } else {
    h = (frame->id & HAL_CAN_STD_ID_MASK) << 18;
  }
  if ((frame->flags & HAL_CAN_FRAME_RTR) != 0u) {
    h |= ELEM_RTR;
  }
  if ((frame->flags & HAL_CAN_FRAME_ESI) != 0u) {
    h |= ELEM_ESI;
  }
  return h;
}

static uint32_t frame_header1(const hal_can_frame_t *frame) {
  uint32_t h = (uint32_t)(frame->dlc & 0x0Fu) << ELEM_DLC_POS;
  if ((frame->flags & HAL_CAN_FRAME_BRS) != 0u) {
    h |= ELEM_BRS;
  }
  if ((frame->flags & HAL_CAN_FRAME_FD) != 0u) {
    h |= ELEM_FDF;
  }
  return h;
}

static void decode_header(uint32_t h0, uint32_t h1, hal_can_frame_t *frame) {
  memset(frame, 0, sizeof(*frame));
  if ((h0 & ELEM_XTD) != 0u) {
    frame->flags |= HAL_CAN_FRAME_EXTENDED;
    frame->id = h0 & HAL_CAN_EXT_ID_MASK;
  } else {
    frame->id = (h0 >> 18) & HAL_CAN_STD_ID_MASK;
  }
  if ((h0 & ELEM_RTR) != 0u) {
    frame->flags |= HAL_CAN_FRAME_RTR;
  }
  if ((h0 & ELEM_ESI) != 0u) {
    frame->flags |= HAL_CAN_FRAME_ESI;
  }
  if ((h1 & ELEM_FDF) != 0u) {
    frame->flags |= HAL_CAN_FRAME_FD;
  }
  if ((h1 & ELEM_BRS) != 0u) {
    frame->flags |= HAL_CAN_FRAME_BRS;
  }
  frame->dlc = (uint8_t)((h1 >> ELEM_DLC_POS) & 0x0Fu);
  /* A classic frame never carries more than 8 bytes, whatever its DLC. */
  if ((frame->flags & HAL_CAN_FRAME_FD) == 0u && frame->dlc > 8u) {
    frame->dlc = 8u;
  }
  frame->len = hal_can_dlc_to_bytes(frame->dlc);
}

/* Payload bytes are packed little-endian into the words after the header;
 * a last, partial word goes through a zero-padded buffer. */
static void write_payload(hal_can_stm32g474_fdcan_t *fd, uint32_t word,
                          const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i += 4u) {
    uint8_t bytes[4] = {0u, 0u, 0u, 0u};
    const uint8_t n = (uint8_t)(len - i) < 4u ? (uint8_t)(len - i) : 4u;
    memcpy(bytes, &data[i], n);
    ram_wr(fd, word + (uint32_t)(i / 4u), jh_load_le32(bytes));
  }
}

static void read_payload(hal_can_stm32g474_fdcan_t *fd, uint32_t word,
                         uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i += 4u) {
    uint8_t bytes[4];
    jh_store_le32(bytes, ram_rd(fd, word + (uint32_t)(i / 4u)));
    const uint8_t n = (uint8_t)(len - i) < 4u ? (uint8_t)(len - i) : 4u;
    memcpy(&data[i], bytes, n);
  }
}

/* ── Filters ──────────────────────────────────────────────────────────────
 * The lists are sized once at init (LSS/LSE are protected); elements are
 * written while the controller runs. Each element records which filter
 * (API index) owns it, so a received frame reports the filter that matched
 * and a slot can be reprogrammed or removed. Accepting frames no filter
 * matches uses an accept-all element in the last position of its list. */

static uint8_t *owner_of(hal_can_stm32g474_fdcan_t *fd, bool ext) {
  return ext ? fd->ext_owner : fd->std_owner;
}

static uint32_t list_size(bool ext) {
  return ext ? FDCAN_MRAM_EXT_FILTERS : FDCAN_MRAM_STD_FILTERS;
}

static uint32_t element_action(const hal_can_filter_ex_t *f) {
  return f->action == HAL_CAN_FILTER_REJECT ? ELEMENT_REJECT : ELEMENT_FIFO0;
}

/* Write element @p elem of the list @p f belongs to. An extended element is
 * disabled while its two words change, so it never matches half-written. */
static void write_element(hal_can_stm32g474_fdcan_t *fd, uint32_t elem,
                          const hal_can_filter_ex_t *f) {
  static const uint32_t kStdType[] = {SFT_CLASSIC, SFT_RANGE, SFT_DUAL};
  static const uint32_t kExtType[] = {EFT_CLASSIC, EFT_RANGE_NO_XIDAM,
                                      EFT_DUAL};
  if ((f->flags & HAL_CAN_FILTER_EXTENDED) == 0u) {
    ram_wr(fd, FDCAN_MRAM_STD_FILTER_WORD + elem,
           kStdType[f->type] | (element_action(f) << SFEC_POS) |
               ((f->id1 & HAL_CAN_STD_ID_MASK) << 16) |
               (f->id2 & HAL_CAN_STD_ID_MASK));
    return;
  }
  const uint32_t word = FDCAN_MRAM_EXT_FILTER_WORD + (elem * 2u);
  ram_wr(fd, word, 0u);
  ram_wr(fd, word + 1u, kExtType[f->type] | (f->id2 & HAL_CAN_EXT_ID_MASK));
  ram_wr(fd, word,
         (element_action(f) << EFEC_POS) | (f->id1 & HAL_CAN_EXT_ID_MASK));
}

static void clear_element(hal_can_stm32g474_fdcan_t *fd, bool ext,
                          uint32_t elem) {
  if (ext) {
    ram_wr(fd, FDCAN_MRAM_EXT_FILTER_WORD + (elem * 2u), 0u);
  } else {
    ram_wr(fd, FDCAN_MRAM_STD_FILTER_WORD + elem, 0u);
  }
  owner_of(fd, ext)[elem] = OWNER_FREE;
}

/* Lowest free element of a list; the last one stays reserved while it
 * accepts unmatched frames. */
static bool free_element(hal_can_stm32g474_fdcan_t *fd, bool ext,
                         uint32_t *elem) {
  const uint8_t *owner = owner_of(fd, ext);
  for (uint32_t i = 0; i < list_size(ext); ++i) {
    if (owner[i] == OWNER_FREE) {
      *elem = i;
      return true;
    }
  }
  return false;
}

/* Element of filter @p index, if it has one. */
static bool find_owner(hal_can_stm32g474_fdcan_t *fd, uint8_t index, bool *ext,
                       uint32_t *elem) {
  for (uint32_t list = 0; list < 2u; ++list) {
    const uint8_t *owner = owner_of(fd, list != 0u);
    for (uint32_t i = 0; i < list_size(list != 0u); ++i) {
      if (owner[i] == index) {
        *ext = list != 0u;
        *elem = i;
        return true;
      }
    }
  }
  return false;
}

static hal_status_t place_filter(hal_can_stm32g474_fdcan_t *fd, uint8_t index,
                                 const hal_can_filter_ex_t *f) {
  const bool ext = (f->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  uint32_t elem = 0u;
  if (!free_element(fd, ext, &elem)) {
    return HAL_ENOMEM;
  }
  owner_of(fd, ext)[elem] = index;
  write_element(fd, elem, f);
  return HAL_OK;
}

/* Accept-all element in the last position of a list, or free it. */
static hal_status_t accept_unmatched(hal_can_stm32g474_fdcan_t *fd, bool ext,
                                     bool on) {
  const uint32_t last = list_size(ext) - 1u;
  uint8_t *owner = owner_of(fd, ext);
  if (!on) {
    if (owner[last] == OWNER_ACCEPT_ALL) {
      clear_element(fd, ext, last);
    }
    return HAL_OK;
  }
  if (owner[last] == OWNER_ACCEPT_ALL) {
    return HAL_OK;
  }
  if (owner[last] != OWNER_FREE) {
    return HAL_ENOMEM;
  }
  const hal_can_filter_ex_t all = {
      HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT,
      ext ? (uint8_t)HAL_CAN_FILTER_EXTENDED : (uint8_t)0u, 0u, 0u};
  owner[last] = OWNER_ACCEPT_ALL;
  write_element(fd, last, &all);
  return HAL_OK;
}

/* Initial state: no filters, everything accepted, remote frames too. */
static void reset_filters(hal_can_stm32g474_fdcan_t *fd) {
  memset(fd->std_owner, OWNER_FREE, sizeof(fd->std_owner));
  memset(fd->ext_owner, OWNER_FREE, sizeof(fd->ext_owner));
  (void)accept_unmatched(fd, false, true);
  (void)accept_unmatched(fd, true, true);
}

/* Filter index a received element's FIDX stands for. */
static uint8_t filter_of(hal_can_stm32g474_fdcan_t *fd, bool ext,
                         uint32_t fidx) {
  if (fidx >= list_size(ext)) {
    return HAL_CAN_FILTER_NONE;
  }
  const uint8_t owner = owner_of(fd, ext)[fidx];
  return owner < OWNER_ACCEPT_ALL ? owner : HAL_CAN_FILTER_NONE;
}

/* One RX FIFO element with its filter index; the accept-all elements and
 * frames accepted without a match report HAL_CAN_FILTER_NONE. */
/* Full time of a 16-bit TIM3 capture, @p now being hal_micros64() read
 * shortly after it. */
static uint64_t extend_sof(uint32_t captured, uint64_t now) {
  const uint16_t age =
      (uint16_t)((uint16_t)TIM_CNT(TIM3_BASE) - (uint16_t)captured);
  return now - age;
}

static void read_rx_element(hal_can_stm32g474_fdcan_t *fd, uint32_t elem,
                            hal_can_frame_t *frame, hal_can_rx_info_t *info,
                            uint64_t now) {
  const uint32_t r0 = ram_rd(fd, elem);
  const uint32_t r1 = ram_rd(fd, elem + 1u);
  decode_header(r0, r1, frame);
  if ((frame->flags & HAL_CAN_FRAME_RTR) == 0u) {
    read_payload(fd, elem + 2u, frame->data, frame->len);
  }
  if (info != NULL) {
    info->timestamp_us =
        TIMESTAMP_TIM3 ? extend_sof(r1 & ELEM_TS_MASK, now) : now;
    info->filter_index =
        (r1 & ELEM_ANMF) != 0u
            ? HAL_CAN_FILTER_NONE
            : filter_of(fd, (frame->flags & HAL_CAN_FRAME_EXTENDED) != 0u,
                        (r1 & ELEM_FIDX_MASK) >> ELEM_FIDX_POS);
  }
}

/* @p tx_event asks for a TX event element carrying the element index. */
static void write_tx_element(hal_can_stm32g474_fdcan_t *fd, uint32_t idx,
                             const hal_can_frame_t *frame, bool tx_event) {
  const uint32_t elem =
      FDCAN_MRAM_TX_BUF_WORD + (idx * FDCAN_MRAM_ELEM_WORDS_64);
  ram_wr(fd, elem, frame_header0(frame));
  ram_wr(fd, elem + 1u,
         frame_header1(frame) |
             (tx_event ? (ELEM_EFC | (idx << ELEM_MM_POS)) : 0u));
  if ((frame->flags & HAL_CAN_FRAME_RTR) == 0u) {
    write_payload(fd, elem + 2u, frame->data, frame->len);
  }
}

/* ── Queued operation ───────────────────────────────────────────────────────
 * Once attached, the interrupt moves received frames into queues->rx, ends
 * queued frames with an event when their TX element reports success (TXBTO)
 * or a failed one-shot attempt (TXBCF), refills the TX FIFO from queues->tx
 * and reports state changes. Task-side code that touches the TX elements or
 * posts events runs with interrupts masked, so the rings keep one producer
 * and one consumer. */

static void post_tx_outcome(hal_can_stm32g474_fdcan_t *fd, uint32_t tag,
                            uint8_t reason, uint64_t timestamp_us) {
  jh_can_queues_post_tx(fd->queues, tag, jh_can_tx_reason_status(reason),
                        reason, timestamp_us);
}

/* SOF times of sent queued frames from the TX event FIFO (TIM3 mode). */
static void drain_tx_events(hal_can_stm32g474_fdcan_t *fd, uint64_t now) {
  for (;;) {
    const uint32_t status = rd(fd, FDCAN_TXEFS);
    if ((status & FDCAN_TXEFS_EFFL_MASK) == 0u) {
      break;
    }
    const uint32_t get =
        (status & FDCAN_TXEFS_EFGI_MASK) >> FDCAN_TXEFS_EFGI_POS;
    const uint32_t e1 = ram_rd(fd, FDCAN_MRAM_TX_EVENT_WORD + (get * 2u) + 1u);
    const uint32_t idx = e1 >> ELEM_MM_POS;
    if (idx < HAL_CAN_STM32G474_FDCAN_TX_SLOTS) {
      fd->slot_sof_us[idx] = extend_sof(e1 & ELEM_TS_MASK, now);
      fd->slot_sof_valid[idx] = true;
    }
    wr(fd, FDCAN_TXEFA, get);
  }
}

/* Queued frames the controller has finished with. In TIM3 mode a sent
 * frame waits for its TX event, which carries its start-of-frame time. */
static void finish_slots(hal_can_stm32g474_fdcan_t *fd, uint64_t now) {
  const uint32_t done = rd(fd, FDCAN_TXBTO);
  const uint32_t failed = rd(fd, FDCAN_TXBCF);
  for (uint32_t idx = 0; idx < HAL_CAN_STM32G474_FDCAN_TX_SLOTS; ++idx) {
    const uint32_t bit = 1u << idx;
    if (fd->slot_kind[idx] != SLOT_QUEUED || ((done | failed) & bit) == 0u) {
      continue;
    }
    if ((done & bit) != 0u && TIMESTAMP_TIM3 && !fd->slot_sof_valid[idx]) {
      continue;
    }
    fd->slot_kind[idx] = SLOT_FREE;
    fd->slot_sof_valid[idx] = false;
    post_tx_outcome(fd, fd->slot_tag[idx],
                    (done & bit) != 0u ? HAL_CAN_TX_DONE : HAL_CAN_TX_FAILED,
                    TIMESTAMP_TIM3 ? fd->slot_sof_us[idx] : now);
  }
}

/* The controller dropped its TX elements (INIT + CCE): every queued frame in
 * them, and with @p ring_too every frame still waiting, ends with @p reason. */
static void cancel_queued(hal_can_stm32g474_fdcan_t *fd, uint8_t reason,
                          bool ring_too) {
  if (fd->queues == NULL) {
    return;
  }
  for (uint32_t idx = 0; idx < HAL_CAN_STM32G474_FDCAN_TX_SLOTS; ++idx) {
    if (fd->slot_kind[idx] == SLOT_QUEUED) {
      fd->slot_kind[idx] = SLOT_FREE;
      fd->slot_sof_valid[idx] = false;
      post_tx_outcome(fd, fd->slot_tag[idx], reason, 0u);
    }
  }
  jh_can_tx_entry_t entry;
  while (ring_too && jh_spsc_ring_pop(&fd->queues->tx, &entry)) {
    post_tx_outcome(fd, entry.tag, reason, 0u);
  }
}

/* Move waiting frames into free TX elements while the node is on the bus.
 * An element a blocking send still owns stops the refill: the FIFO hands out
 * elements in order and its waiter must see the result first. */
static void refill_tx(hal_can_stm32g474_fdcan_t *fd, uint64_t now) {
  if (fd->queues == NULL) {
    return;
  }
  finish_slots(fd, now);
  if ((rd(fd, FDCAN_CCCR) & FDCAN_CCCR_INIT) != 0u) {
    return;
  }
  const jh_can_tx_entry_t *entry;
  while ((entry = static_cast<const jh_can_tx_entry_t *>(
              jh_spsc_ring_peek(&fd->queues->tx))) != NULL) {
    const uint32_t fifo = rd(fd, FDCAN_TXFQS);
    if ((fifo & FDCAN_TXFQS_TFQF) != 0u) {
      break;
    }
    const uint32_t idx =
        (fifo & FDCAN_TXFQS_TFQPI_MASK) >> FDCAN_TXFQS_TFQPI_POS;
    if (fd->slot_kind[idx] != SLOT_FREE) {
      break;
    }
    write_tx_element(fd, idx, &entry->frame, TIMESTAMP_TIM3 != 0);
    fd->slot_kind[idx] = SLOT_QUEUED;
    fd->slot_tag[idx] = entry->tag;
    jh_spsc_ring_drop(&fd->queues->tx);
    wr(fd, FDCAN_TXBAR, 1u << idx);
  }
}

static void drain_rx(hal_can_stm32g474_fdcan_t *fd, uint32_t fifo, bool lost,
                     uint64_t now) {
  jh_can_queues_t *q = fd->queues;
  if (lost) {
    jh_can_count(&q->counters.rx_hw_lost);
  }
  const uint32_t status_off = fifo != 0u ? FDCAN_RXF1S : FDCAN_RXF0S;
  const uint32_t ack_off = fifo != 0u ? FDCAN_RXF1A : FDCAN_RXF0A;
  const uint32_t base = fifo != 0u ? FDCAN_MRAM_RX1_WORD : FDCAN_MRAM_RX0_WORD;
  for (;;) {
    const uint32_t status = rd(fd, status_off);
    if ((status & FDCAN_RXFS_FL_MASK) == 0u) {
      break;
    }
    const uint32_t idx = (status & FDCAN_RXFS_GI_MASK) >> FDCAN_RXFS_GI_POS;
    jh_can_rx_entry_t entry = {};
    read_rx_element(fd, base + (idx * FDCAN_MRAM_ELEM_WORDS_64), &entry.frame,
                    &entry.info, now);
    wr(fd, ack_off, idx & FDCAN_RXFA_AI_MASK);
    if (jh_spsc_ring_push(&q->rx, &entry)) {
      jh_can_count(&q->counters.rx_frames);
    } else {
      jh_can_count(&q->counters.rx_queue_overflow);
    }
  }
}

/* MRAF: the controller dropped a received frame, or aborted a transmission
 * because the TX handler could not read the message RAM in time; then it is
 * in restricted operation mode and sends nothing until ASM is cleared
 * (RM0440). The HAL never sets ASM itself, so a set ASM is the controller's;
 * clearing it lets the waiting frames go out again. */
static void end_restricted_operation(hal_can_stm32g474_fdcan_t *fd) {
  const uint32_t cccr = rd(fd, FDCAN_CCCR);
  if ((cccr & FDCAN_CCCR_ASM) != 0u) {
    wr(fd, FDCAN_CCCR, cccr & ~FDCAN_CCCR_ASM);
  }
}

/* Without interrupts the task watches for MRAF itself. */
static void poll_ram_access_failure(hal_can_stm32g474_fdcan_t *fd) {
  if (fd->queues == NULL && (rd(fd, FDCAN_IR) & IR_MRAF) != 0u) {
    wr(fd, FDCAN_IR, IR_MRAF);
    fd->ram_access_failures++;
    end_restricted_operation(fd);
  }
}

/* EP, EW or BO changed. Entering bus-off drops every pending frame and,
 * unless recovery is manual, starts the recovery at once. */
static void state_changed(hal_can_stm32g474_fdcan_t *fd) {
  const hal_can_state_t state = state_from_psr(read_psr(fd));
  if (state == HAL_CAN_STATE_BUS_OFF &&
      fd->reported_state != HAL_CAN_STATE_BUS_OFF) {
    jh_can_count(&fd->queues->counters.bus_off_count);
    /* The controller set INIT itself; CCE resets the TX requests. */
    wr(fd, FDCAN_CCCR, rd(fd, FDCAN_CCCR) | FDCAN_CCCR_CCE);
    cancel_queued(fd, HAL_CAN_TX_BUS_OFF, true);
    if ((fd->mode & HAL_CAN_MODE_MANUAL_RECOVERY) == 0u) {
      wr(fd, FDCAN_CCCR, rd(fd, FDCAN_CCCR) & ~FDCAN_CCCR_INIT);
    }
  }
  if (state != fd->reported_state) {
    jh_can_event_t event = {};
    const uint32_t ecr = rd(fd, FDCAN_ECR);
    event.kind = JH_CAN_EVENT_STATE;
    event.state = state;
    event.counters.tx = (uint8_t)(ecr & FDCAN_ECR_TEC_MASK);
    event.counters.rx = (uint8_t)((ecr & FDCAN_ECR_REC_MASK) >> 8);
    jh_can_queues_post(fd->queues, &event);
    fd->reported_state = state;
  }
}

static void fdcan_isr(hal_can_stm32g474_fdcan_t *fd) {
  if (fd == NULL || fd->queues == NULL) {
    return;
  }
  const uint64_t now = hal_micros64();
  /* Clear first, so a frame arriving while draining raises it again. */
  const uint32_t ir = rd(fd, FDCAN_IR) & (IE_USED | IE_ONE_SHOT | IR_TEFN);
  wr(fd, FDCAN_IR, ir);
  if ((ir & IR_MRAF) != 0u) {
    jh_can_count(&fd->queues->counters.ram_access_failures);
    end_restricted_operation(fd); /* first, so the controller sends again */
  }
  if ((ir & (IR_RF0N | IR_RF0L)) != 0u) {
    drain_rx(fd, 0u, (ir & IR_RF0L) != 0u, now);
  }
  if ((ir & (IR_RF1N | IR_RF1L)) != 0u) {
    drain_rx(fd, 1u, (ir & IR_RF1L) != 0u, now);
  }
  if (TIMESTAMP_TIM3) {
    drain_tx_events(fd, now);
  }
  if ((ir & (IR_EP | IR_EW | IR_BO)) != 0u) {
    state_changed(fd);
  }
  refill_tx(fd, now);
  jh_can_queues_notify(fd->queues);
}

extern "C" {
void FDCAN1_IT0_IRQHandler(void) { fdcan_isr(s_isr_ctx[0]); }
void FDCAN1_IT1_IRQHandler(void) { fdcan_isr(s_isr_ctx[0]); }
void FDCAN2_IT0_IRQHandler(void) { fdcan_isr(s_isr_ctx[1]); }
void FDCAN2_IT1_IRQHandler(void) { fdcan_isr(s_isr_ctx[1]); }
void FDCAN3_IT0_IRQHandler(void) { fdcan_isr(s_isr_ctx[2]); }
void FDCAN3_IT1_IRQHandler(void) { fdcan_isr(s_isr_ctx[2]); }
}

static uint32_t interrupts_for(hal_can_mode_t mode) {
  return IE_USED | (TIMESTAMP_TIM3 ? IR_TEFN : 0u) |
         ((mode & HAL_CAN_MODE_ONE_SHOT) != 0u ? IE_ONE_SHOT : 0u);
}

/* Stop the interrupts of an attached instance. */
static void detach(hal_can_stm32g474_fdcan_t *fd) {
  if (fd->queues == NULL) {
    return;
  }
  jh_stm32g474_nvic_disable(kIrqIt0[fd->index]);
  jh_stm32g474_nvic_disable(kIrqIt1[fd->index]);
  wr(fd, FDCAN_ILE, 0u);
  wr(fd, FDCAN_IE, 0u);
  s_isr_ctx[fd->index] = NULL;
  fd->queues = NULL;
}

/* ── Provider ───────────────────────────────────────────────────────────── */

static hal_can_stm32g474_fdcan_t *ctx_of(void *ctx) {
  return static_cast<hal_can_stm32g474_fdcan_t *>(ctx);
}

static hal_status_t program_timing(hal_can_stm32g474_fdcan_t *fd,
                                   const hal_can_stm32g474_fdcan_config_t *c) {
  jh_stm32g474_fdcan_timing_t nominal = {};
  hal_status_t st = jh_stm32g474_fdcan_compute_timing(
      JH_G474_FDCAN_CLOCK_HZ, c->arbitration_bitrate_hz, false,
      c->arbitration_sample_point_permille, 0u, &nominal);
  if (st != HAL_OK) {
    hal_derr_limited("can", "FDCAN%u: no timing for %lu bit/s",
                     (unsigned)fd->index + 1u,
                     (unsigned long)c->arbitration_bitrate_hz);
    return st;
  }
  wr(fd, FDCAN_NBTP, jh_stm32g474_fdcan_encode_nbtp(&nominal));
  if (!c->enable_fd) {
    return HAL_OK;
  }
  const uint32_t data_hz =
      c->data_bitrate_hz ? c->data_bitrate_hz : c->arbitration_bitrate_hz;
  jh_stm32g474_fdcan_timing_t data = {};
  st = jh_stm32g474_fdcan_compute_timing(JH_G474_FDCAN_CLOCK_HZ, data_hz, true,
                                         c->data_sample_point_permille,
                                         nominal.prescaler, &data);
  if (st != HAL_OK) {
    hal_derr_limited("can", "FDCAN%u: no data timing for %lu bit/s",
                     (unsigned)fd->index + 1u, (unsigned long)data_hz);
    return st;
  }
  uint8_t tdco = 0u;
  if (c->tdc_mode == HAL_CAN_TDC_MANUAL) {
    tdco = c->tdc_offset;
  } else if (c->tdc_mode == HAL_CAN_TDC_AUTO) {
    tdco = jh_stm32g474_fdcan_tdc_offset(&data);
  }
  wr(fd, FDCAN_DBTP, jh_stm32g474_fdcan_encode_dbtp(&data, tdco != 0u));
  wr(fd, FDCAN_TDCR, (uint32_t)(tdco & 0x7Fu) << FDCAN_TDCR_TDCO_POS);
  return HAL_OK;
}

static hal_status_t check_config(const hal_can_stm32g474_fdcan_config_t *c,
                                 uint8_t index, uint8_t *rx_af,
                                 uint8_t *tx_af) {
  if (index >= FDCAN_INSTANCE_COUNT || c->arbitration_bitrate_hz == 0u ||
      c->arbitration_bitrate_hz > MAX_NOMINAL_BITRATE_HZ) {
    return HAL_EINVAL;
  }
  const uint8_t rx = c->rx_pin ? c->rx_pin : kDefaultRx[index];
  const uint8_t tx = c->tx_pin ? c->tx_pin : kDefaultTx[index];
  const fdcan_pin_t *rx_pin = find_pin(index, rx, false);
  const fdcan_pin_t *tx_pin = find_pin(index, tx, true);
  if (rx_pin == NULL || tx_pin == NULL) {
    hal_derr_limited("can", "FDCAN%u: pins %u/%u not routable",
                     (unsigned)index + 1u, (unsigned)rx, (unsigned)tx);
    return HAL_EINVAL;
  }
  const uint32_t data_hz = (c->enable_fd && c->data_bitrate_hz)
                               ? c->data_bitrate_hz
                               : c->arbitration_bitrate_hz;
  if (data_hz < c->arbitration_bitrate_hz) {
    return HAL_EINVAL;
  }
  if (c->transceiver_max_bitrate_hz != 0u &&
      data_hz > c->transceiver_max_bitrate_hz) {
    hal_derr_limited("can", "FDCAN%u: %lu bit/s above the transceiver limit",
                     (unsigned)index + 1u, (unsigned long)data_hz);
    return HAL_EINVAL;
  }
  if (c->tdc_mode > HAL_CAN_TDC_MANUAL ||
      (c->tdc_mode == HAL_CAN_TDC_MANUAL &&
       (c->tdc_offset == 0u || c->tdc_offset > 127u))) {
    return HAL_EINVAL;
  }
  *rx_af = rx_pin->af;
  *tx_af = tx_pin->af;
  return HAL_OK;
}

/* Longest blocking send: 20 worst-case classic frames at the nominal rate,
 * at least 5 ms. */
static uint32_t send_timeout_us(uint32_t nominal_hz) {
  const uint64_t us = (20ull * 160ull * 1000000ull) / nominal_hz;
  return us < 5000ull ? 5000u : (uint32_t)us;
}

static hal_status_t fdcan_apply_mode(void *ctx, hal_can_mode_t mode);

/* TIM3 as a free-running microsecond counter for SOF timestamps; shared by
 * the instances and left running. */
static void start_timestamp_timer(void) {
  if ((TIM_CR1(TIM3_BASE) & TIM_CR1_CEN) != 0u) {
    return;
  }
  RCC_APB1ENR1 |= RCC_APB1ENR1_TIM3EN;
  (void)RCC_APB1ENR1;
  TIM_PSC(TIM3_BASE) = (JH_G474_TIMCLK1_HZ / 1000000u) - 1u;
  TIM_ARR(TIM3_BASE) = 0xFFFFu;
  TIM_EGR(TIM3_BASE) = TIM_EGR_UG;
  TIM_CR1(TIM3_BASE) = TIM_CR1_CEN;
}

/* Message RAM, bit timing, filter lists and TX FIFO of an instance held in
 * INIT + CCE. */
static hal_status_t configure(hal_can_stm32g474_fdcan_t *fd,
                              const hal_can_stm32g474_fdcan_config_t *c) {
  for (uint32_t w = 0; w < FDCAN_MRAM_WORDS; ++w) {
    ram_wr(fd, w, 0u);
  }
  const hal_status_t st = program_timing(fd, c);
  if (st != HAL_OK) {
    return st;
  }
  wr(fd, FDCAN_XIDAM, HAL_CAN_EXT_ID_MASK);
  wr(fd, FDCAN_RXGFC,
     ((uint32_t)FDCAN_MRAM_STD_FILTERS << FDCAN_RXGFC_LSS_POS) |
         ((uint32_t)FDCAN_MRAM_EXT_FILTERS << FDCAN_RXGFC_LSE_POS) |
         FDCAN_RXGFC_ANFS_REJECT | FDCAN_RXGFC_ANFE_REJECT);
  reset_filters(fd);
  if (TIMESTAMP_TIM3) {
    start_timestamp_timer();
    wr(fd, FDCAN_TSCC, FDCAN_TSCC_TSS_EXTERNAL);
  }
  wr(fd, FDCAN_TXBC, 0u); /* TX FIFO: frames leave in the order queued */
  wr(fd, FDCAN_IR, FDCAN_IR_ALL);
  wr(fd, FDCAN_IE, 0u);
  return HAL_OK;
}

/* Take the instance off the bus and free it; the kernel clock, shared by all
 * three instances, stops with the last one. */
static void release(hal_can_stm32g474_fdcan_t *fd) {
  detach(fd);
  (void)enter_config(fd);
  transceiver_standby(fd, true);
  s_open_mask = (uint8_t)(s_open_mask & ~(1u << fd->index));
  if (s_open_mask == 0u) {
    RCC_APB1ENR1 &= ~RCC_APB1ENR1_FDCANEN;
  }
  fd->initialized = false;
}

static hal_status_t fdcan_init(void *ctx, const hal_can_config_t *cfg,
                               jh_can_caps_t *caps, hal_can_mode_t *mode) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  const hal_can_stm32g474_fdcan_config_t *c = &cfg->stm32g474_fdcan;
  const uint8_t index = c->instance ? (uint8_t)(c->instance - 1u) : 0u;
  uint8_t rx_af = 0u;
  uint8_t tx_af = 0u;
  hal_status_t st = check_config(c, index, &rx_af, &tx_af);
  if (st != HAL_OK) {
    return st;
  }
  if ((s_open_mask & (1u << index)) != 0u) {
    return HAL_EBUSY;
  }

  memset(fd, 0, sizeof(*fd));
  fd->index = index;
  fd->fd_capable = c->enable_fd;
  fd->has_standby = c->has_standby;
  fd->standby_pin = c->standby_pin;
  fd->standby_high = c->standby_high;
  fd->tx_timeout_us = send_timeout_us(c->arbitration_bitrate_hz);
  if (fd->has_standby) {
    hal_gpio_set_mode(fd->standby_pin, fd->standby_high ? HAL_GPIO_OUTPUT_HIGH
                                                        : HAL_GPIO_OUTPUT_LOW);
  }
  jh_stm32g474_gpio_set_af(c->rx_pin ? c->rx_pin : kDefaultRx[index], rx_af);
  jh_stm32g474_gpio_set_af(c->tx_pin ? c->tx_pin : kDefaultTx[index], tx_af);

  /* One bus clock and one reset bit serve all three instances; the kernel
   * clock (JH_G474_FDCAN_CLOCK_HZ) is selected by the clock tree. */
  s_open_mask = (uint8_t)(s_open_mask | (1u << index));
  RCC_APB1ENR1 |= RCC_APB1ENR1_FDCANEN;
  (void)RCC_APB1ENR1;

  st = enter_config(fd);
  if (st == HAL_OK) {
    st = configure(fd, c);
  }
  if (st == HAL_OK) {
    caps->modes = HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_EXTERNAL_LOOPBACK |
                  HAL_CAN_MODE_LISTEN_ONLY | HAL_CAN_MODE_ONE_SHOT |
                  HAL_CAN_MODE_SLEEP | HAL_CAN_MODE_MANUAL_RECOVERY |
                  (c->enable_fd ? HAL_CAN_MODE_FD : HAL_CAN_MODE_NORMAL);
    caps->legacy_filters = LEGACY_SLOTS;
    hal_can_caps_t *pub = &caps->public_caps;
    pub->std_filters = FDCAN_MRAM_STD_FILTERS;
    pub->ext_filters = FDCAN_MRAM_EXT_FILTERS;
    pub->tx_slots = HAL_CAN_STM32G474_FDCAN_TX_SLOTS;
    pub->features = HAL_CAN_CAP_TX_EVENTS | HAL_CAN_CAP_RX_QUEUE |
                    HAL_CAN_CAP_MANUAL_RECOVERY;
    if (TIMESTAMP_TIM3) {
      pub->features |= HAL_CAN_CAP_TIMESTAMP_HW;
    }
    pub->core_clock_hz = JH_G474_FDCAN_CLOCK_HZ;
    pub->max_nominal_bitrate_hz = MAX_NOMINAL_BITRATE_HZ;
    pub->max_data_bitrate_hz = MAX_NOMINAL_BITRATE_HZ;
    if (c->enable_fd) {
      pub->features |= HAL_CAN_CAP_FD;
      pub->max_data_bitrate_hz =
          c->transceiver_max_bitrate_hz != 0u &&
                  c->transceiver_max_bitrate_hz < MAX_DATA_BITRATE_HZ
              ? c->transceiver_max_bitrate_hz
              : MAX_DATA_BITRATE_HZ;
    }
    *mode = (c->one_shot_tx ? HAL_CAN_MODE_ONE_SHOT : HAL_CAN_MODE_NORMAL) |
            (c->enable_fd ? HAL_CAN_MODE_FD : HAL_CAN_MODE_NORMAL);
    st = fdcan_apply_mode(ctx, *mode);
  }
  if (st != HAL_OK) {
    release(fd);
    return st;
  }
  fd->initialized = true;
  return HAL_OK;
}

static void fdcan_deinit(void *ctx) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  if (fd->initialized) {
    release(fd);
  }
}

static hal_status_t fdcan_apply_mode(void *ctx, hal_can_mode_t mode) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  hal_status_t st = enter_config(fd);
  if (st != HAL_OK) {
    return st;
  }
  /* Entering configuration dropped the TX elements. Frames still waiting
   * end too: they were queued for the old mode (an FD frame cannot go out on
   * a classic channel). */
  hal_critical_section_enter();
  cancel_queued(fd, HAL_CAN_TX_STOPPED, true);
  fd->mode = mode;
  if (fd->queues != NULL) {
    wr(fd, FDCAN_IE, interrupts_for(mode));
  }
  hal_critical_section_exit();
  uint32_t cccr =
      rd(fd, FDCAN_CCCR) &
      ~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE | FDCAN_CCCR_DAR | FDCAN_CCCR_MON |
        FDCAN_CCCR_TEST | FDCAN_CCCR_ASM | FDCAN_CCCR_PXHD | FDCAN_CCCR_EFBI);
  if ((mode & HAL_CAN_MODE_FD) != 0u) {
    cccr |= FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE;
  }
  if ((mode & HAL_CAN_MODE_ONE_SHOT) != 0u) {
    cccr |= FDCAN_CCCR_DAR;
  }
  if ((mode & (HAL_CAN_MODE_LISTEN_ONLY | HAL_CAN_MODE_LOOPBACK)) != 0u) {
    cccr |= FDCAN_CCCR_MON; /* internal loopback = LBCK + MON */
  }
  const bool loop =
      (mode & (HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_EXTERNAL_LOOPBACK)) != 0u;
  if (loop) {
    cccr |= FDCAN_CCCR_TEST;
  }
  /* TEST is writable only once CCCR.TEST is set, and clears with it. */
  wr(fd, FDCAN_CCCR, cccr);
  if (loop) {
    wr(fd, FDCAN_TEST, FDCAN_TEST_LBCK);
  }
  if ((mode & HAL_CAN_MODE_SLEEP) != 0u) {
    transceiver_standby(fd, true);
    return HAL_OK;
  }
  transceiver_standby(fd, false);
  st = leave_config(fd);
  if (st != HAL_OK) {
    return st;
  }
  hal_critical_section_enter();
  refill_tx(fd, hal_micros64());
  hal_critical_section_exit();
  return HAL_OK;
}

static hal_status_t fdcan_stop(void *ctx) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  const hal_status_t st = enter_config(fd);
  if (st != HAL_OK) {
    return st;
  }
  hal_critical_section_enter();
  cancel_queued(fd, HAL_CAN_TX_STOPPED, true);
  hal_critical_section_exit();
  transceiver_standby(fd, true);
  return HAL_OK;
}

/* Drop every pending frame: INIT + CCE resets the request register, which
 * RM0440 prefers over TXBCR cancellation for the TX FIFO. Leaving INIT
 * afterwards also starts the recovery of a bus-off node, so a node under
 * HAL_CAN_MODE_MANUAL_RECOVERY stays in INIT until hal_can_recover(). */
static void drop_pending(hal_can_stm32g474_fdcan_t *fd, uint8_t reason) {
  if (enter_config(fd) == HAL_OK) {
    hal_critical_section_enter();
    cancel_queued(fd, reason, reason == HAL_CAN_TX_BUS_OFF);
    hal_critical_section_exit();
    const bool hold = reason == HAL_CAN_TX_BUS_OFF &&
                      (fd->mode & HAL_CAN_MODE_MANUAL_RECOVERY) != 0u;
    if (!hold) {
      (void)leave_config(fd);
    }
  }
}

/* Wait for the frame in TX buffer @p idx to leave or fail. A frame reported
 * as failed is never left queued, so it cannot go out later behind the
 * caller's back. */
static hal_status_t wait_sent(hal_can_stm32g474_fdcan_t *fd, uint32_t idx) {
  const uint32_t bit = 1u << idx;
  const uint32_t started = hal_micros();
  for (;;) {
    poll_ram_access_failure(fd);
    if ((rd(fd, FDCAN_TXBTO) & bit) != 0u) {
      return HAL_OK;
    }
    if ((rd(fd, FDCAN_TXBCF) & bit) != 0u) {
      return HAL_EIO; /* one-shot attempt failed */
    }
    if ((read_psr(fd) & FDCAN_PSR_BO) != 0u) {
      drop_pending(fd, HAL_CAN_TX_BUS_OFF);
      return HAL_EBUS;
    }
    if (hal_elapsed_u32(hal_micros(), started, fd->tx_timeout_us)) {
      drop_pending(fd, HAL_CAN_TX_FAILED);
      return HAL_ETIMEOUT;
    }
  }
}

static hal_status_t fdcan_send_frame(void *ctx, const hal_can_frame_t *frame) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  poll_ram_access_failure(fd);
  if ((read_psr(fd) & FDCAN_PSR_BO) != 0u) {
    recover_bus_off(fd);
    return HAL_EBUS; /* nothing is queued while the node is off the bus */
  }
  /* The element is ours until the result is seen; the interrupt neither
   * reports nor reuses it meanwhile. */
  hal_critical_section_enter();
  const uint32_t fifo = rd(fd, FDCAN_TXFQS);
  const uint32_t idx = (fifo & FDCAN_TXFQS_TFQPI_MASK) >> FDCAN_TXFQS_TFQPI_POS;
  if ((fifo & FDCAN_TXFQS_TFQF) != 0u || fd->slot_kind[idx] != SLOT_FREE) {
    hal_critical_section_exit();
    return HAL_EBUSY;
  }
  write_tx_element(fd, idx, frame, false);
  fd->slot_kind[idx] = SLOT_SYNC;
  wr(fd, FDCAN_TXBAR, 1u << idx);
  hal_critical_section_exit();
  const hal_status_t st = wait_sent(fd, idx);
  hal_critical_section_enter();
  if (fd->slot_kind[idx] == SLOT_SYNC) {
    fd->slot_kind[idx] = SLOT_FREE;
  }
  refill_tx(fd, hal_micros64());
  hal_critical_section_exit();
  return st;
}

static hal_status_t fdcan_receive_frame(void *ctx, hal_can_frame_t *frame) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  recover_bus_off(fd);
  poll_ram_access_failure(fd);
  const uint32_t status = rd(fd, FDCAN_RXF0S);
  if ((status & FDCAN_RXFS_RFL) != 0u) {
    fd->rx_hw_lost++;
    wr(fd, FDCAN_IR, 1u << 2); /* IR.RF0L clears RXF0S.RF0L */
  }
  if ((status & FDCAN_RXFS_FL_MASK) == 0u) {
    return HAL_EAGAIN;
  }
  const uint32_t idx = (status & FDCAN_RXFS_GI_MASK) >> FDCAN_RXFS_GI_POS;
  read_rx_element(fd, FDCAN_MRAM_RX0_WORD + (idx * FDCAN_MRAM_ELEM_WORDS_64),
                  frame, NULL, 0u);
  wr(fd, FDCAN_RXF0A, idx & FDCAN_RXFA_AI_MASK);
  return HAL_OK;
}

static hal_status_t fdcan_available(void *ctx) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  return (rd(fd, FDCAN_RXF0S) & FDCAN_RXFS_FL_MASK) != 0u ? HAL_OK : HAL_EAGAIN;
}

/* A classic slot is an accepting mask filter in any free element; the first
 * one rejects unmatched frames unless a policy was chosen. Nothing changes
 * when the new filter has no room: a slot keeps its element if the ID kind
 * stays, otherwise it needs a free one in the other list (or the accept-all
 * element the first filter frees). */
static hal_status_t fdcan_set_filter(void *ctx, uint8_t index,
                                     const hal_can_filter_t *filter) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  const bool ext = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  bool old_ext = false;
  uint32_t old_elem = 0u;
  const bool had = find_owner(fd, index, &old_ext, &old_elem);
  const bool in_place = had && old_ext == ext;
  uint32_t free_elem = 0u;
  const bool frees_last =
      !fd->policy_set &&
      owner_of(fd, ext)[list_size(ext) - 1u] == OWNER_ACCEPT_ALL;
  if (!in_place && !free_element(fd, ext, &free_elem) && !frees_last) {
    return HAL_ENOMEM;
  }
  if (!fd->policy_set) {
    (void)accept_unmatched(fd, false, false);
    (void)accept_unmatched(fd, true, false);
    fd->policy_set = true;
  }
  hal_can_filter_ex_t ex = {};
  ex.type = HAL_CAN_FILTER_MASK;
  ex.action = HAL_CAN_FILTER_ACCEPT;
  ex.flags = filter->flags;
  ex.id1 = filter->id;
  ex.id2 = filter->mask;
  if (in_place) {
    write_element(fd, old_elem, &ex);
    return HAL_OK;
  }
  if (had) {
    clear_element(fd, old_ext, old_elem);
  }
  return place_filter(fd, index, &ex);
}

static hal_status_t
fdcan_add_filter(void *ctx, const hal_can_filter_ex_t *filter, uint8_t *index) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  uint8_t candidate = HAL_CAN_FILTER_FIRST_ADDED;
  bool ext = false;
  uint32_t elem = 0u;
  while (find_owner(fd, candidate, &ext, &elem)) {
    candidate++;
  }
  const hal_status_t st = place_filter(fd, candidate, filter);
  if (st == HAL_OK) {
    *index = candidate;
  }
  return st;
}

static hal_status_t fdcan_remove_filter(void *ctx, uint8_t index) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  bool ext = false;
  uint32_t elem = 0u;
  if (index >= OWNER_ACCEPT_ALL || !find_owner(fd, index, &ext, &elem)) {
    return HAL_ENOENT;
  }
  clear_element(fd, ext, elem);
  return HAL_OK;
}

/* Unmatched frames through the accept-all elements, at any time. Remote
 * frames through RXGFC, which only a stopped controller accepts. */
static hal_status_t fdcan_set_unmatched_policy(void *ctx, bool accept_std,
                                               bool accept_ext,
                                               bool accept_rtr) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  const uint32_t reject_rtr = FDCAN_RXGFC_RRFS | FDCAN_RXGFC_RRFE;
  const uint32_t rxgfc = rd(fd, FDCAN_RXGFC);
  const bool rtr_change = ((rxgfc & reject_rtr) == 0u) != accept_rtr;
  if (rtr_change && (rd(fd, FDCAN_CCCR) & FDCAN_CCCR_INIT) == 0u) {
    return HAL_EBUSY;
  }
  /* Both positions must be available before anything changes. */
  const uint8_t std_last = fd->std_owner[FDCAN_MRAM_STD_FILTERS - 1u];
  const uint8_t ext_last = fd->ext_owner[FDCAN_MRAM_EXT_FILTERS - 1u];
  if ((accept_std && std_last != OWNER_FREE && std_last != OWNER_ACCEPT_ALL) ||
      (accept_ext && ext_last != OWNER_FREE && ext_last != OWNER_ACCEPT_ALL)) {
    return HAL_ENOMEM;
  }
  (void)accept_unmatched(fd, false, accept_std);
  (void)accept_unmatched(fd, true, accept_ext);
  if (rtr_change) {
    wr(fd, FDCAN_CCCR, rd(fd, FDCAN_CCCR) | FDCAN_CCCR_CCE);
    wr(fd, FDCAN_RXGFC,
       accept_rtr ? (rxgfc & ~reject_rtr) : (rxgfc | reject_rtr));
  }
  fd->policy_set = true;
  return HAL_OK;
}

static hal_status_t fdcan_get_state(void *ctx, hal_can_state_t *state) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  *state = state_from_psr(read_psr(fd));
  if (*state == HAL_CAN_STATE_BUS_OFF) {
    recover_bus_off(fd);
  }
  return HAL_OK;
}

static hal_status_t fdcan_get_error_counters(void *ctx,
                                             hal_can_error_counters_t *c) {
  const uint32_t ecr = rd(ctx_of(ctx), FDCAN_ECR);
  c->tx = (uint8_t)(ecr & FDCAN_ECR_TEC_MASK);
  c->rx = (uint8_t)((ecr & FDCAN_ECR_REC_MASK) >> 8);
  return HAL_OK;
}

static hal_status_t fdcan_attach(void *ctx, jh_can_queues_t *queues) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  hal_critical_section_enter();
  fd->queues = queues;
  for (uint32_t idx = 0; idx < HAL_CAN_STM32G474_FDCAN_TX_SLOTS; ++idx) {
    fd->slot_kind[idx] = SLOT_FREE;
  }
  fd->reported_state = state_from_psr(read_psr(fd));
  s_isr_ctx[fd->index] = fd;
  wr(fd, FDCAN_ILS, ILS_IT1_GROUPS);
  wr(fd, FDCAN_TXBTIE, TX_SLOT_BITS);
  wr(fd, FDCAN_TXBCIE, TX_SLOT_BITS);
  wr(fd, FDCAN_IR, FDCAN_IR_ALL);
  wr(fd, FDCAN_IE, interrupts_for(fd->mode));
  wr(fd, FDCAN_ILE, ILE_BOTH_LINES);
  hal_critical_section_exit();
  jh_stm32g474_nvic_enable(kIrqIt0[fd->index], FDCAN_IRQ_PRIORITY);
  jh_stm32g474_nvic_enable(kIrqIt1[fd->index], FDCAN_IRQ_PRIORITY);
  return HAL_OK;
}

static void fdcan_kick_tx(void *ctx) {
  hal_critical_section_enter();
  refill_tx(ctx_of(ctx), hal_micros64());
  hal_critical_section_exit();
}

static hal_status_t fdcan_get_status(void *ctx, hal_can_status_t *status) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  const uint32_t psr = read_psr(fd);
  const uint32_t ecr = rd(fd, FDCAN_ECR);
  status->state = state_from_psr(psr);
  status->tec = (uint8_t)(ecr & FDCAN_ECR_TEC_MASK);
  status->rec = (uint8_t)((ecr & FDCAN_ECR_REC_MASK) >> 8);
  status->last_error = fd->last_error;
  status->last_data_error = fd->last_data_error;
  status->tdc_value =
      (uint8_t)((psr & FDCAN_PSR_TDCV_MASK) >> FDCAN_PSR_TDCV_POS);
  status->rx_hw_lost = fd->rx_hw_lost;
  status->ram_access_failures = fd->ram_access_failures;
  return HAL_OK;
}

/* Manual recovery: leaving INIT starts the 128 x 11 recessive-bit wait. */
static hal_status_t fdcan_recover(void *ctx) {
  hal_can_stm32g474_fdcan_t *fd = ctx_of(ctx);
  if ((read_psr(fd) & FDCAN_PSR_BO) == 0u) {
    return HAL_OK;
  }
  return leave_config(fd);
}

#else /* !JH_STM32G474_HW: host sanity build of the target library */

static hal_status_t fdcan_init(void *ctx, const hal_can_config_t *cfg,
                               jh_can_caps_t *caps, hal_can_mode_t *mode) {
  (void)ctx;
  (void)cfg;
  (void)caps;
  (void)mode;
  return HAL_EUNSUPPORTED;
}
static void fdcan_deinit(void *ctx) { (void)ctx; }
static hal_status_t fdcan_apply_mode(void *ctx, hal_can_mode_t mode) {
  (void)ctx;
  (void)mode;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_stop(void *ctx) {
  (void)ctx;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_send_frame(void *ctx, const hal_can_frame_t *frame) {
  (void)ctx;
  (void)frame;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_receive_frame(void *ctx, hal_can_frame_t *frame) {
  (void)ctx;
  (void)frame;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_available(void *ctx) {
  (void)ctx;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_set_filter(void *ctx, uint8_t index,
                                     const hal_can_filter_t *filter) {
  (void)ctx;
  (void)index;
  (void)filter;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_get_state(void *ctx, hal_can_state_t *state) {
  (void)ctx;
  (void)state;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_get_error_counters(void *ctx,
                                             hal_can_error_counters_t *c) {
  (void)ctx;
  (void)c;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_attach(void *ctx, jh_can_queues_t *queues) {
  (void)ctx;
  (void)queues;
  return HAL_EUNSUPPORTED;
}
static void fdcan_kick_tx(void *ctx) { (void)ctx; }
static hal_status_t fdcan_get_status(void *ctx, hal_can_status_t *status) {
  (void)ctx;
  (void)status;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_recover(void *ctx) {
  (void)ctx;
  return HAL_EUNSUPPORTED;
}
static hal_status_t
fdcan_add_filter(void *ctx, const hal_can_filter_ex_t *filter, uint8_t *index) {
  (void)ctx;
  (void)filter;
  (void)index;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_remove_filter(void *ctx, uint8_t index) {
  (void)ctx;
  (void)index;
  return HAL_EUNSUPPORTED;
}
static hal_status_t fdcan_set_unmatched_policy(void *ctx, bool accept_std,
                                               bool accept_ext,
                                               bool accept_rtr) {
  (void)ctx;
  (void)accept_std;
  (void)accept_ext;
  (void)accept_rtr;
  return HAL_EUNSUPPORTED;
}

#endif /* JH_STM32G474_HW */

const jh_can_provider_t jh_can_stm32g474_fdcan_provider = {
    HAL_CAN_BACKEND_STM32G474_FDCAN,
    fdcan_init,
    fdcan_deinit,
    fdcan_apply_mode,
    fdcan_stop,
    fdcan_send_frame,
    fdcan_receive_frame,
    fdcan_available,
    fdcan_set_filter,
    fdcan_get_state,
    fdcan_get_error_counters,
    NULL,
    NULL,
    NULL,
    fdcan_attach,
    fdcan_kick_tx,
    fdcan_get_status,
    fdcan_recover,
    fdcan_add_filter,
    fdcan_remove_filter,
    fdcan_set_unmatched_policy};

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_STM32G474_FDCAN */
#endif /* HAL_TARGET_IS_STM32G474 */
