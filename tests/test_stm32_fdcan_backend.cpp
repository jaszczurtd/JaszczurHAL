// The STM32G474 native FDCAN provider on a modelled controller. Register
// offsets, element layout and the protection rules come from RM0440 §44 and
// are written out here instead of being taken from port/stm32g474_regs.h, so
// a wrong map or a write the hardware would ignore fails these tests.

#include "hal/can/jh_can_provider.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/impl/stm32g474/hal_can_stm32g474_fdcan.h"
#include "hal/impl/stm32g474/port/stm32g474_fdcan_timing.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"
#include "jh_stm32g474_host_regs.h"
#include "utils/unity.h"

#include <stdint.h>
#include <string.h>
#include <vector>

namespace {

struct Event {
  enum Kind { GPIO_MODE, GPIO_WRITE, DELAY_US, CCCR } kind;
  uint32_t a;
  uint32_t b;
};

std::vector<Event> s_events;
uint32_t s_now_us;
int s_critical_depth; /* interrupts masked while > 0 */

} // namespace

// Platform services the provider calls, recorded for the tests. The facade
// entry points only satisfy hal_can_util.cpp, linked for the DLC helpers.
extern "C" {
hal_status_t hal_can_create(const hal_can_config_t *, hal_can_t *out) {
  *out = nullptr;
  return HAL_EUNSUPPORTED;
}
hal_status_t hal_can_receive(hal_can_t, uint32_t *, uint8_t *, uint8_t *) {
  return HAL_EAGAIN;
}
void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
  s_events.push_back({Event::GPIO_MODE, pin, (uint32_t)mode});
}
void hal_gpio_write(uint8_t pin, bool high) {
  s_events.push_back({Event::GPIO_WRITE, pin, high ? 1u : 0u});
}
void hal_gpio_attach_interrupt(uint8_t, void (*)(void), hal_gpio_irq_mode_t) {}
void hal_delay_ms(uint32_t ms) { s_now_us += ms * 1000u; }
void hal_delay_us(uint32_t us) {
  s_now_us += us;
  s_events.push_back({Event::DELAY_US, us, 0u});
}
uint32_t hal_micros(void) {
  s_now_us += 10u;
  return s_now_us;
}
uint64_t hal_micros64(void) { return s_now_us; }
void hal_derr_limited(const char *, const char *, ...) {}
void hal_critical_section_enter(void) { ++s_critical_depth; }
void hal_critical_section_exit(void) { --s_critical_depth; }
}

namespace {

constexpr uintptr_t kBase[3] = {0x40006400u, 0x40006800u, 0x40006C00u};
constexpr uintptr_t kRam[3] = {0x4000A400u, 0x4000A750u, 0x4000AAA0u};
constexpr uint32_t kRamWords = 0x350u / 4u;

constexpr uintptr_t kDbtp = 0x00Cu;
constexpr uintptr_t kTest = 0x010u;
constexpr uintptr_t kCccr = 0x018u;
constexpr uintptr_t kNbtp = 0x01Cu;
constexpr uintptr_t kTscc = 0x020u;
constexpr uintptr_t kEcr = 0x040u;
constexpr uintptr_t kPsr = 0x044u;
constexpr uintptr_t kTdcr = 0x048u;
constexpr uintptr_t kIr = 0x050u;
constexpr uintptr_t kRxgfc = 0x080u;
constexpr uintptr_t kXidam = 0x084u;
constexpr uintptr_t kHpms = 0x088u;
constexpr uintptr_t kRxf0s = 0x090u;
constexpr uintptr_t kRxf0a = 0x094u;
constexpr uintptr_t kTxbc = 0x0C0u;
constexpr uintptr_t kTxfqs = 0x0C4u;
constexpr uintptr_t kTxbrp = 0x0C8u;
constexpr uintptr_t kTxbar = 0x0CCu;
constexpr uintptr_t kTxbcr = 0x0D0u;
constexpr uintptr_t kTxbto = 0x0D4u;
constexpr uintptr_t kTxbcf = 0x0D8u;
constexpr uintptr_t kIe = 0x054u;
constexpr uintptr_t kIls = 0x058u;
constexpr uintptr_t kIle = 0x05Cu;
constexpr uintptr_t kTxbtie = 0x0DCu;
constexpr uintptr_t kTxbcie = 0x0E0u;
constexpr uintptr_t kTxefs = 0x0E4u;
constexpr uintptr_t kTxefa = 0x0E8u;
constexpr uint32_t kTxEventWord = 0x260u / 4u;
#ifdef HAL_CAN_STM32G474_TIMESTAMP_TIM3
/* TIM3 (RM0440 29.5): CR1, PSC, ARR, CNT. */
constexpr uintptr_t kTim3Cr1 = 0x40000400u, kTim3Cnt = 0x40000424u,
                    kTim3Psc = 0x40000428u, kTim3Arr = 0x4000042Cu;
constexpr bool kTim3 = true;
#else
constexpr bool kTim3 = false;
#endif
/* IR bits and NVIC (RM0440 Table 412, PM0214). */
constexpr uint32_t IR_RF0N = 1u << 0, IR_RF0L = 1u << 2, IR_TC = 1u << 7,
                   IR_TCF = 1u << 8, IR_TFE = 1u << 9, IR_TEFN = 1u << 10,
                   IR_MRAF = 1u << 14, IR_BO = 1u << 19, IR_PEA = 1u << 21,
                   IR_PED = 1u << 22;
constexpr uintptr_t kNvicIser = 0xE000E100u, kNvicIcer = 0xE000E180u,
                    kNvicIpr = 0xE000E400u;

constexpr uint32_t INIT = 1u << 0, CCE = 1u << 1, ASM = 1u << 2, MON = 1u << 5,
                   DAR = 1u << 6, TEST = 1u << 7, FDOE = 1u << 8,
                   BRSE = 1u << 9, PXHD = 1u << 12, EFBI = 1u << 13;
constexpr uint32_t LBCK = 1u << 4;

constexpr uint32_t kStdFilterWord = 0u;
constexpr uint32_t kExtFilterWord = 0x070u / 4u;
constexpr uint32_t kRxFifo0Word = 0x0B0u / 4u;
constexpr uint32_t kTxBufferWord = 0x278u / 4u;
constexpr uint32_t kElementWords = 18u;
constexpr uint32_t kSentinel = 0xA5A5A5A5u;

/* Pins: port * 16 + n. */
constexpr uint8_t PA8 = 8u, PA11 = 11u, PA12 = 12u, PB4 = 20u, PB12 = 28u,
                  PB13 = 29u, PC7 = 39u;
constexpr uintptr_t kGpioA = 0x48000000u, kGpioB = 0x48000400u;
constexpr uintptr_t kRccApb1enr1 = 0x40021058u;
constexpr uint32_t kFdcanEn = 1u << 25;

volatile uint32_t &cell(uintptr_t address) {
  return *jh_stm32g474_host_reg32(address);
}
volatile uint32_t &reg(int inst, uintptr_t offset) {
  return cell(kBase[inst] + offset);
}
volatile uint32_t &ram(int inst, uint32_t word) {
  return cell(kRam[inst] + (word * 4u));
}

/* ── Controller model ─────────────────────────────────────────────────── */

struct Model {
  bool stall_tx = false;       /* frames stay pending, nothing completes */
  bool bus_off_tx = false;     /* sending drives the node bus-off */
  bool hold_tx_events = false; /* TX events wait for release_tx_events() */
  uint32_t sof16 = 0u;         /* TIM3 value captured at the next SOF */
  std::vector<uint32_t> held_events[3];
  bool fail_tx = false; /* one-shot attempts fail (TXBCF) */
  /* The next request finds the message RAM too slow: MRAF, restricted
   * operation (ASM), the request stays pending. */
  bool tx_ram_failure = false;
  int ignored_writes = 0;   /* writes the hardware would have dropped */
  int txbcr_writes = 0;     /* cancellation RM0440 does not intend for FIFO */
  int init_entries[3] = {}; /* INIT 0 -> 1 transitions */
  std::vector<std::vector<uint32_t>> sent[3]; /* TX elements taken */
  std::vector<std::pair<uintptr_t, uint32_t>> ram_writes; /* in order */
} g;

int instance_of(uintptr_t address, uintptr_t *offset) {
  for (int i = 0; i < 3; ++i) {
    if (address >= kBase[i] && address < kBase[i] + 0x400u) {
      *offset = address - kBase[i];
      return i;
    }
  }
  return -1;
}

bool is_protected(uintptr_t off) {
  return off == kDbtp || off == kNbtp || off == kTscc || off == kTdcr ||
         off == kRxgfc || off == kXidam || off == kTxbc;
}

/* Reset of the TX/RX status registers when CCE becomes set (RM0440 §44.3.2). */
void cce_reset(int i) {
  reg(i, kHpms) = 0u;
  reg(i, kRxf0s) = 0u;
  reg(i, kTxfqs) = 3u; /* TFFL = 3, put/get index 0 */
  reg(i, kTxbrp) = 0u;
  reg(i, kTxbto) = 0u;
  reg(i, kTxbcf) = 0u;
}

void leave_restricted(int i);

void write_cccr(int i, uint32_t value, uint32_t *c) {
  const uint32_t old = *c;
  const bool open = (old & INIT) && (old & CCE);
  uint32_t res = (old & ~INIT) | (value & INIT);
  if ((res & INIT) == 0u) {
    res &= ~CCE;
  } else if (old & INIT) {
    res = (res & ~CCE) | (value & CCE);
  }
  /* TEST, MON, ASM: set only while INIT and CCE, cleared any time. */
  for (uint32_t bit : {TEST, MON, ASM}) {
    if ((value & bit) && !(old & bit) && !open) {
      g.ignored_writes++;
    } else {
      res = (res & ~bit) | (value & bit);
    }
  }
  for (uint32_t bit : {DAR, FDOE, BRSE, PXHD, EFBI}) {
    if (((value ^ old) & bit) && !open) {
      g.ignored_writes++;
    } else {
      res = (res & ~bit) | (value & bit);
    }
  }
  if (!(res & TEST)) {
    reg(i, kTest) = 0u;
  }
  if ((res & INIT) && !(old & INIT)) {
    g.init_entries[i]++;
  }
  if ((res & CCE) && !(old & CCE)) {
    cce_reset(i);
  }
  const bool left_restricted = (old & ASM) && !(res & ASM);
  *c = res;
  if (left_restricted && !g.stall_tx) {
    leave_restricted(i);
  }
  s_events.push_back({Event::CCCR, (uint32_t)i, res});
}

/* TX event FIFO (3 elements): EFFL 2:0, EFGI 9:8, EFPI 17:16. */
void push_tx_event(int i, uint32_t e1) {
  const uint32_t s = reg(i, kTxefs);
  const uint32_t fill = s & 7u;
  const uint32_t put = ((s >> 8) + fill) % 3u;
  ram(i, kTxEventWord + put * 2u + 1u) = e1;
  reg(i, kTxefs) = (s & ~7u) | (fill + 1u);
  reg(i, kIr) |= IR_TEFN;
}

#ifdef HAL_CAN_STM32G474_TIMESTAMP_TIM3
void release_tx_events(int i) {
  for (uint32_t e1 : g.held_events[i]) {
    push_tx_event(i, e1);
  }
  g.held_events[i].clear();
}
#endif

/* Element @p b went out: TXBTO, TC when enabled, a TX event when asked. */
void finish_request(int i, uint32_t b) {
  reg(i, kTxbto) |= 1u << b;
  reg(i, kTxbcf) &= ~(1u << b);
  if ((reg(i, kTxbtie) & (1u << b)) != 0u) {
    reg(i, kIr) |= IR_TC;
  }
  const uint32_t t1 = (uint32_t)ram(i, kTxBufferWord + b * kElementWords + 1u);
  if ((t1 & (1u << 23)) != 0u) { /* EFC: store a TX event */
    const uint32_t e1 = (t1 & 0xFF000000u) | (1u << 22) | (g.sof16 & 0xFFFFu);
    if (g.hold_tx_events) {
      g.held_events[i].push_back(e1);
    } else {
      push_tx_event(i, e1);
    }
  }
}

/* A request the controller keeps pending: TXBRP and one element less free. */
void keep_pending(int i, uint32_t b, uint32_t *fqs) {
  reg(i, kTxbrp) |= 1u << b;
  const uint32_t free_level = (*fqs & 7u) - 1u;
  *fqs = (*fqs & ~7u) | free_level;
  if (free_level == 0u) {
    *fqs |= 1u << 21; /* TFQF */
  }
}

void take_tx(int i, uint32_t requests) {
  uint32_t fqs = reg(i, kTxfqs);
  for (uint32_t b = 0; b < 3u; ++b) {
    if (!(requests & (1u << b))) {
      continue;
    }
    std::vector<uint32_t> elem;
    for (uint32_t w = 0; w < kElementWords; ++w) {
      elem.push_back((uint32_t)ram(i, kTxBufferWord + b * kElementWords + w));
    }
    g.sent[i].push_back(elem);
    uint32_t put = (fqs >> 16) & 3u;
    put = (put + 1u) % 3u;
    fqs = (fqs & ~(3u << 16)) | (put << 16);
    if (g.bus_off_tx) {
      /* Bus-off: INIT set by the controller, the request stays pending
       * and goes out after the recovery. */
      reg(i, kTxbrp) |= 1u << b;
      reg(i, kPsr) |= 1u << 7;
      reg(i, kCccr) |= INIT;
      reg(i, kIr) |= IR_BO;
    } else if (g.tx_ram_failure || (reg(i, kCccr) & ASM) != 0u) {
      /* Restricted operation sends nothing until ASM is cleared. */
      if (g.tx_ram_failure) {
        g.tx_ram_failure = false;
        reg(i, kIr) |= IR_MRAF;
        reg(i, kCccr) |= ASM;
      }
      keep_pending(i, b, &fqs);
    } else if (g.stall_tx) {
      keep_pending(i, b, &fqs);
    } else if (g.fail_tx) {
      /* A failed one-shot attempt sets TXBCF but, on the G474, no TCF; the
       * error that ended it is a protocol error. */
      reg(i, kTxbcf) |= 1u << b;
      reg(i, kTxbto) &= ~(1u << b);
      reg(i, kIr) |= IR_PEA;
    } else {
      finish_request(i, b);
    }
  }
  reg(i, kTxfqs) = fqs;
}

/* Clearing ASM lets the requests held in restricted operation go out. */
void leave_restricted(int i) {
  uint32_t fqs = reg(i, kTxfqs);
  for (uint32_t b = 0; b < 3u; ++b) {
    if ((reg(i, kTxbrp) & (1u << b)) == 0u) {
      continue;
    }
    reg(i, kTxbrp) &= ~(1u << b);
    fqs = ((fqs & ~7u) | ((fqs & 7u) + 1u)) & ~(1u << 21);
    finish_request(i, b);
  }
  reg(i, kTxfqs) = fqs;
}

void write_hook(uintptr_t address, uint32_t value, uint32_t *c) {
  uintptr_t off = 0u;
  const int i = instance_of(address, &off);
  if (i < 0) {
    if (address >= kRam[0] && address < kRam[2] + 0x350u) {
      g.ram_writes.emplace_back(address, value);
    }
    *c = value;
    return;
  }
  const uint32_t cccr = reg(i, kCccr);
  const bool open = (cccr & INIT) && (cccr & CCE);
  if (off == kCccr) {
    write_cccr(i, value, c);
  } else if (is_protected(off) && !open) {
    g.ignored_writes++;
  } else if (off == kTxbcr) {
    g.txbcr_writes++;
    *c = value;
  } else if (off == kTest) {
    if (cccr & TEST) {
      *c = value;
    } else {
      g.ignored_writes++;
    }
  } else if (off == kIr) {
    *c &= ~value; /* write 1 to clear */
    if ((value & (1u << 2)) != 0u) {
      reg(i, kRxf0s) &= ~(1u << 25); /* RXF0S.RF0L mirrors IR.RF0L */
    }
  } else if (off == kTxbar) {
    if (cccr & CCE) {
      g.ignored_writes++;
    } else {
      take_tx(i, value);
    }
    *c = 0u;
  } else if (off == kTxefa) {
    uint32_t st = reg(i, kTxefs);
    const uint32_t get = (st >> 8) & 3u;
    if ((st & 7u) != 0u && (value & 3u) == get) {
      st = (st & ~(3u << 8)) | (((get + 1u) % 3u) << 8);
      st = (st & ~7u) | ((st & 7u) - 1u);
      reg(i, kTxefs) = st;
    }
    *c = value;
  } else if (off == kRxf0a) {
    uint32_t s = reg(i, kRxf0s);
    const uint32_t get = (s >> 8) & 3u;
    if ((s & 0xFu) != 0u && (value & 7u) == get) {
      s = (s & ~(3u << 8)) | (((get + 1u) % 3u) << 8);
      s = (s & ~0xFu) | ((s & 0xFu) - 1u);
      reg(i, kRxf0s) = s;
    }
    *c = value;
  } else {
    *c = value;
  }
}

/* A frame arriving in RX FIFO0 of instance @p i; a full FIFO loses it. */
void rx_arrive(int i, uint32_t h0, uint32_t h1,
               const std::vector<uint32_t> &payload) {
  uint32_t s = reg(i, kRxf0s);
  const uint32_t fill = s & 0xFu; /* F0FL is 4 bits on the G4 */
  if (fill >= 3u) {
    reg(i, kRxf0s) = s | (1u << 25);
    reg(i, kIr) |= IR_RF0L;
    return;
  }
  const uint32_t put = (((s >> 8) & 3u) + fill) % 3u;
  const uint32_t word = kRxFifo0Word + put * kElementWords;
  ram(i, word) = h0;
  ram(i, word + 1u) = h1;
  for (size_t w = 0; w < payload.size(); ++w) {
    ram(i, word + 2u + (uint32_t)w) = payload[w];
  }
  reg(i, kRxf0s) = (s & ~0xFu) | (fill + 1u);
  reg(i, kIr) |= IR_RF0N;
}

/* ── Fixture ──────────────────────────────────────────────────────────── */

constexpr uintptr_t kForeignOffsets[] = {0x0A0u, 0x0A8u, 0x0B0u, 0x0F0u};

const jh_can_provider_t &P = jh_can_stm32g474_fdcan_provider;
hal_can_stm32g474_fdcan_t s_ctx[3];
bool s_open[3];
jh_can_caps_t s_caps;
hal_can_mode_t s_mode;

hal_can_config_t config(uint8_t instance, uint32_t arb, uint32_t data,
                        bool fd) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_STM32G474_FDCAN;
  cfg.stm32g474_fdcan.instance = instance;
  cfg.stm32g474_fdcan.arbitration_bitrate_hz = arb;
  cfg.stm32g474_fdcan.data_bitrate_hz = data;
  cfg.stm32g474_fdcan.enable_fd = fd;
  return cfg;
}

hal_status_t open(int slot, const hal_can_config_t &cfg) {
  const hal_status_t st = P.init(&s_ctx[slot], &cfg, &s_caps, &s_mode);
  s_open[slot] = st == HAL_OK;
  return st;
}

hal_status_t open_default(int slot) {
  return open(slot, config(1u, 500000u, 2000000u, true));
}

uint32_t gpio_afr(uintptr_t port, uint8_t pin) {
  const uintptr_t reg_addr = port + (pin < 8u ? 0x20u : 0x24u);
  return (cell(reg_addr) >> ((pin % 8u) * 4u)) & 0xFu;
}

int last_event_index(Event::Kind kind, uint32_t a, uint32_t b) {
  for (int k = (int)s_events.size() - 1; k >= 0; --k) {
    if (s_events[(size_t)k].kind == kind && s_events[(size_t)k].a == a &&
        s_events[(size_t)k].b == b) {
      return k;
    }
  }
  return -1;
}

int last_cccr_without_init(int inst) {
  for (int k = (int)s_events.size() - 1; k >= 0; --k) {
    const Event &e = s_events[(size_t)k];
    if (e.kind == Event::CCCR && e.a == (uint32_t)inst && !(e.b & INIT)) {
      return k;
    }
  }
  return -1;
}

} // namespace

void setUp(void) {
  jh_stm32g474_host_regs_reset();
  jh_stm32g474_host_regs_set_hooks(nullptr, write_hook);
  g = Model();
  s_events.clear();
  s_now_us = 0u;
  memset(s_open, 0, sizeof(s_open));
  for (int i = 0; i < 3; ++i) {
    reg(i, kCccr) = INIT; /* reset values */
    reg(i, kTxfqs) = 3u;
  }
  /* M_CAN registers the G4 does not have (RXF0C, RXF0A, RXF1C, TXEFC at
   * their M_CAN offsets) and the RAM of the neighbouring instance must stay
   * untouched; FDCAN1 RAM starts dirty. */
  for (uintptr_t off : kForeignOffsets) {
    cell(kBase[0] + off) = kSentinel;
  }
  for (uint32_t w = 0; w < kRamWords; ++w) {
    ram(0, w) = kSentinel;
  }
  ram(1, 0u) = kSentinel;
}

void tearDown(void) {
  for (int k = 0; k < 3; ++k) {
    if (s_open[k]) {
      P.deinit(&s_ctx[k]);
    }
  }
}

/* ── Init and layout ──────────────────────────────────────────────────── */

void test_init_programs_the_g4_layout_without_ignored_writes(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));

  TEST_ASSERT_EQUAL_INT(0, g.ignored_writes);
  for (uint32_t w = 0; w < kRamWords; ++w) {
    if (w == kStdFilterWord + 27u || w == kExtFilterWord + 14u ||
        w == kExtFilterWord + 15u) {
      continue;
    }
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, ram(0, w), "message RAM not cleared");
  }
  TEST_ASSERT_EQUAL_HEX32(kSentinel, ram(1, 0u));
  for (uintptr_t off : kForeignOffsets) {
    TEST_ASSERT_EQUAL_HEX32(kSentinel, cell(kBase[0] + off));
  }
  /* LSS 28, LSE 8, non-matching standard and extended frames rejected. */
  TEST_ASSERT_EQUAL_HEX32((8u << 24) | (28u << 16) | (2u << 4) | (2u << 2),
                          reg(0, kRxgfc));
  /* Accept-all elements last in each list: classic filter, mask 0, FIFO0. */
  TEST_ASSERT_EQUAL_HEX32((2u << 30) | (1u << 27),
                          ram(0, kStdFilterWord + 27u));
  TEST_ASSERT_EQUAL_HEX32(1u << 29, ram(0, kExtFilterWord + 14u));
  TEST_ASSERT_EQUAL_HEX32(2u << 30, ram(0, kExtFilterWord + 15u));
  TEST_ASSERT_EQUAL_HEX32(0x1FFFFFFFu, reg(0, kXidam));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTxbc));
  /* Running, FD with BRS, edge filtering and protocol exception left off. */
  TEST_ASSERT_EQUAL_HEX32(FDOE | BRSE, reg(0, kCccr));
  TEST_ASSERT_TRUE((cell(kRccApb1enr1) & kFdcanEn) != 0u);
}

void test_each_instance_uses_its_own_registers_and_message_ram(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(1, config(2u, 500000u, 0u, false)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(2, config(3u, 500000u, 0u, false)));

  TEST_ASSERT_EQUAL_HEX32((8u << 24) | (28u << 16) | (2u << 4) | (2u << 2),
                          reg(1, kRxgfc));
  TEST_ASSERT_EQUAL_HEX32((8u << 24) | (28u << 16) | (2u << 4) | (2u << 2),
                          reg(2, kRxgfc));
  TEST_ASSERT_EQUAL_HEX32((2u << 30) | (1u << 27),
                          ram(1, kStdFilterWord + 27u));
  TEST_ASSERT_EQUAL_HEX32((2u << 30) | (1u << 27),
                          ram(2, kStdFilterWord + 27u));
  /* FDCAN1 was never opened: its RAM keeps the sentinel. */
  TEST_ASSERT_EQUAL_HEX32(kSentinel, ram(0, 0u));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kRxgfc));
}

void test_the_shared_clock_stops_with_the_last_instance(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(0, config(1u, 500000u, 0u, false)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(1, config(2u, 500000u, 0u, false)));
  P.deinit(&s_ctx[0]);
  s_open[0] = false;
  TEST_ASSERT_TRUE((cell(kRccApb1enr1) & kFdcanEn) != 0u);
  P.deinit(&s_ctx[1]);
  s_open[1] = false;
  TEST_ASSERT_EQUAL_HEX32(0u, cell(kRccApb1enr1) & kFdcanEn);
  TEST_ASSERT_EQUAL_HEX32(INIT | CCE, reg(1, kCccr) & (INIT | CCE));
}

void test_a_failed_init_releases_the_instance_and_the_clock(void) {
  /* 4 Mbit/s has no data timing at 170 MHz; the failure comes after the
   * clock was enabled. */
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        open(0, config(1u, 500000u, 4000000u, true)));
  TEST_ASSERT_EQUAL_HEX32(0u, cell(kRccApb1enr1) & kFdcanEn);
  TEST_ASSERT_TRUE((reg(0, kCccr) & INIT) != 0u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));

  /* With another instance running, the clock stays on. */
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        open(1, config(2u, 500000u, 4000000u, true)));
  TEST_ASSERT_TRUE((cell(kRccApb1enr1) & kFdcanEn) != 0u);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & INIT);
}

void test_invalid_rates_are_refused_before_the_hardware_is_touched(void) {
  /* Arbitration above 1 Mbit/s, data slower than arbitration. */
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, open(0, config(1u, 2000000u, 0u, false)));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        open(0, config(1u, 1000000u, 500000u, true)));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, open(0, config(1u, 0u, 0u, false)));
  TEST_ASSERT_EQUAL_size_t(0u, s_events.size());
  TEST_ASSERT_EQUAL_HEX32(0u, cell(kRccApb1enr1));
}

void test_an_instance_opens_once_until_released(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, open_default(1));
  P.deinit(&s_ctx[0]);
  s_open[0] = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(1));
}

void test_instance_pins_get_their_alternate_function(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(0, config(1u, 500000u, 0u, false)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(1, config(2u, 500000u, 0u, false)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(2, config(3u, 500000u, 0u, false)));

  /* DS12288 Table 13: FDCAN1/2 on AF9, FDCAN3 on AF11. */
  TEST_ASSERT_EQUAL_UINT32(9u, gpio_afr(kGpioA, PA11));
  TEST_ASSERT_EQUAL_UINT32(9u, gpio_afr(kGpioA, PA12));
  TEST_ASSERT_EQUAL_UINT32(9u, gpio_afr(kGpioB, PB12 - 16u));
  TEST_ASSERT_EQUAL_UINT32(9u, gpio_afr(kGpioB, PB13 - 16u));
  TEST_ASSERT_EQUAL_UINT32(11u, gpio_afr(kGpioA, PA8));
  TEST_ASSERT_EQUAL_UINT32(11u, gpio_afr(kGpioB, PB4 - 16u));
}

void test_pins_of_another_instance_are_refused(void) {
  hal_can_config_t cfg = config(3u, 500000u, 0u, false);
  cfg.stm32g474_fdcan.rx_pin = PA11; /* FDCAN1 RX */
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, open(0, cfg));
  cfg = config(4u, 500000u, 0u, false);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, open(0, cfg));
}

/* ── Bit timing ───────────────────────────────────────────────────────── */

/* 170 MHz, 500k: prescaler 2, 170 tq, 1 + 135 + 34, SJW 34 (80.0 %).
 * 2M: no exact timing at prescaler 1..4; prescaler 5, 17 tq, 1 + 12 + 4,
 * SJW 4 (76.5 %); prescaler 5 > 2 keeps TDC off. */
void test_timing_at_170mhz_is_exact_and_leaves_tdc_off(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_HEX32((33u << 25) | (1u << 16) | (134u << 8) | 33u,
                          reg(0, kNbtp));
  TEST_ASSERT_EQUAL_HEX32((4u << 16) | (11u << 8) | (3u << 4) | 3u,
                          reg(0, kDbtp));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTdcr));
}

void test_classic_channel_below_the_data_phase_range_starts(void) {
  /* 100 kbit/s is under the data-phase limits; a classic channel must not
   * need them. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(0, config(1u, 100000u, 0u, false)));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & (FDOE | BRSE));
  TEST_ASSERT_EQUAL_INT(0, g.ignored_writes);
}

void test_data_rate_above_the_transceiver_limit_is_refused(void) {
  hal_can_config_t cfg = config(1u, 500000u, 8000000u, true);
  cfg.stm32g474_fdcan.transceiver_max_bitrate_hz = 5000000u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, open(0, cfg));
}

void test_manual_tdc_writes_offset_and_enable_bit(void) {
  hal_can_config_t cfg = config(1u, 1000000u, 5000000u, true);
  cfg.stm32g474_fdcan.tdc_mode = HAL_CAN_TDC_MANUAL;
  cfg.stm32g474_fdcan.tdc_offset = 26u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(0, cfg));
  TEST_ASSERT_TRUE((reg(0, kDbtp) & (1u << 23)) != 0u);
  TEST_ASSERT_EQUAL_HEX32(26u << 8, reg(0, kTdcr));
}

void test_solver_at_80mhz_gives_cia_timings_and_tdc_offsets(void) {
  jh_stm32g474_fdcan_timing_t nominal = {};
  jh_stm32g474_fdcan_timing_t data = {};
  /* 500k: 160 tq at prescaler 1, sample at 128/160 = 80 %, SJW = PS2 = 32. */
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_stm32g474_fdcan_compute_timing(80000000u, 500000u, false, 0u,
                                                0u, &nominal));
  TEST_ASSERT_EQUAL_HEX32((31u << 25) | (0u << 16) | (126u << 8) | 31u,
                          jh_stm32g474_fdcan_encode_nbtp(&nominal));
  /* 2M: 40 tq at the same prescaler, 1 + 29 + 10 (75 %), TDCO = 30. */
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_stm32g474_fdcan_compute_timing(80000000u, 2000000u, true, 0u,
                                                nominal.prescaler, &data));
  TEST_ASSERT_EQUAL_HEX32((1u << 23) | (28u << 8) | (9u << 4) | 9u,
                          jh_stm32g474_fdcan_encode_dbtp(&data, true));
  TEST_ASSERT_EQUAL_UINT8(30u, jh_stm32g474_fdcan_tdc_offset(&data));
  /* 4M: 20 tq, 1 + 14 + 5 (75 %), TDCO 15. 5M: 16 tq, 1 + 11 + 4, TDCO 12.
   * 8M: 10 tq, 8 / 10 = 80 % is the nearest, TDCO 8. */
  const struct {
    uint32_t hz;
    uint8_t tdco;
  } kRates[] = {{4000000u, 15u}, {5000000u, 12u}, {8000000u, 8u}};
  for (const auto &r : kRates) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_stm32g474_fdcan_compute_timing(
                                      80000000u, r.hz, true, 0u, 1u, &data));
    TEST_ASSERT_EQUAL_UINT16(1u, data.prescaler);
    TEST_ASSERT_EQUAL_UINT32(r.hz, data.actual_bitrate_hz);
    TEST_ASSERT_EQUAL_UINT8(r.tdco, jh_stm32g474_fdcan_tdc_offset(&data));
  }
}

struct TimingCase {
  uint32_t clock_hz;
  uint32_t bitrate_hz;
  bool data;
  uint16_t preferred;
  uint16_t prescaler;
  uint16_t segment1;
  uint16_t segment2;
  uint8_t tdco;
};

/* Worked by hand from the rules in stm32g474_fdcan_timing.h. */
void test_solver_reference_vectors_at_160_24_and_170mhz(void) {
  const TimingCase cases[] = {
      /* 160 MHz: 500k fits 320 tq at prescaler 1 (seg1 limit 256). */
      {160000000u, 500000u, false, 0u, 1u, 255u, 64u, 0u},
      /* Data 2M needs prescaler 2 (80 tq is over 49), TDCO 2 x 30. */
      {160000000u, 2000000u, true, 1u, 2u, 29u, 10u, 60u},
      {160000000u, 4000000u, true, 1u, 1u, 29u, 10u, 30u},
      {160000000u, 5000000u, true, 1u, 1u, 23u, 8u, 24u},
      {160000000u, 8000000u, true, 1u, 1u, 14u, 5u, 15u},
      /* 24 MHz (HSE as kernel clock): 48 tq, 38 / 48 = 79.2 %. */
      {24000000u, 500000u, false, 0u, 1u, 37u, 10u, 0u},
      {24000000u, 1000000u, false, 0u, 1u, 18u, 5u, 0u},
      {24000000u, 20000u, false, 0u, 4u, 239u, 60u, 0u},
      {24000000u, 2000000u, true, 1u, 1u, 8u, 3u, 9u},
      /* 4M has only 6 tq: 5 / 6 is the nearest sample point. */
      {24000000u, 4000000u, true, 1u, 1u, 4u, 1u, 5u},
      /* 170 MHz, 20k: exact rates at prescalers 25 and 34; only 34 x 250 tq
       * puts the sample point at 80 % within the segment limits. */
      {170000000u, 20000u, false, 0u, 34u, 199u, 50u, 0u},
  };
  for (const TimingCase &c : cases) {
    jh_stm32g474_fdcan_timing_t t = {};
    TEST_ASSERT_EQUAL_INT(
        HAL_OK, jh_stm32g474_fdcan_compute_timing(c.clock_hz, c.bitrate_hz,
                                                  c.data, 0u, c.preferred, &t));
    TEST_ASSERT_EQUAL_UINT32(c.bitrate_hz, t.actual_bitrate_hz);
    TEST_ASSERT_EQUAL_UINT16(c.prescaler, t.prescaler);
    TEST_ASSERT_EQUAL_UINT16(c.segment1, t.segment1);
    TEST_ASSERT_EQUAL_UINT16(c.segment2, t.segment2);
    TEST_ASSERT_EQUAL_UINT16(c.segment1 < c.segment2 ? c.segment1 : c.segment2,
                             t.sync_jump_width);
    TEST_ASSERT_EQUAL_UINT8(c.tdco, jh_stm32g474_fdcan_tdc_offset(&t));
  }
  /* 24 MHz cannot make 5M (4.8 tq) or 8M (3 tq). */
  jh_stm32g474_fdcan_timing_t t = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNSUPPORTED,
      jh_stm32g474_fdcan_compute_timing(24000000u, 5000000u, true, 0u, 1u, &t));
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNSUPPORTED,
      jh_stm32g474_fdcan_compute_timing(24000000u, 8000000u, true, 0u, 1u, &t));
}

void test_solver_rejects_rates_the_clock_cannot_make(void) {
  jh_stm32g474_fdcan_timing_t t = {};
  /* 170 MHz has no exact 4M or 8M data phase (best 1.19 % off). */
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_stm32g474_fdcan_compute_timing(170000000u, 4000000u,
                                                          true, 0u, 0u, &t));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_stm32g474_fdcan_compute_timing(170000000u, 8000000u,
                                                          true, 0u, 0u, &t));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_stm32g474_fdcan_compute_timing(
                            170000000u, 170000000u, true, 0u, 0u, &t));
}

void test_solver_prefers_the_lowest_prescaler_with_a_good_sample_point(void) {
  jh_stm32g474_fdcan_timing_t t = {};
  /* 2.5M at 170 MHz: 2 x 34 tq (76.5 %), not 17 x 4 tq. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_stm32g474_fdcan_compute_timing(
                                    170000000u, 2500000u, true, 0u, 0u, &t));
  TEST_ASSERT_EQUAL_UINT16(2u, t.prescaler);
  TEST_ASSERT_EQUAL_UINT16(25u, t.segment1);
  TEST_ASSERT_EQUAL_UINT16(8u, t.segment2);
  /* CiA: the same prescaler in both phases when it gives an exact rate.
   * 125k at 80 MHz needs prescaler 2 (320 tq); 2M then uses 2 x 20 tq, not
   * 1 x 40 tq. */
  jh_stm32g474_fdcan_timing_t nominal = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_stm32g474_fdcan_compute_timing(80000000u, 125000u, false, 0u,
                                                0u, &nominal));
  TEST_ASSERT_EQUAL_UINT16(2u, nominal.prescaler);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_stm32g474_fdcan_compute_timing(80000000u, 2000000u, true, 0u,
                                                nominal.prescaler, &t));
  TEST_ASSERT_EQUAL_UINT16(2u, t.prescaler);
  TEST_ASSERT_EQUAL_UINT16(14u, t.segment1);
  TEST_ASSERT_EQUAL_UINT16(5u, t.segment2);
  /* TDC stays off at 1 Mbit/s or less. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_stm32g474_fdcan_compute_timing(
                                    80000000u, 1000000u, true, 0u, 1u, &t));
  TEST_ASSERT_EQUAL_UINT8(0u, jh_stm32g474_fdcan_tdc_offset(&t));
}

/* ── Modes ────────────────────────────────────────────────────────────── */

void test_internal_loopback_sets_test_after_cccr_test(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, P.apply_mode(&s_ctx[0], HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_FD));
  TEST_ASSERT_EQUAL_INT(0, g.ignored_writes);
  TEST_ASSERT_EQUAL_HEX32(LBCK, reg(0, kTest));
  TEST_ASSERT_EQUAL_HEX32(TEST | MON, reg(0, kCccr) & (TEST | MON | INIT));
}

void test_external_loopback_drives_the_bus(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, P.apply_mode(&s_ctx[0], HAL_CAN_MODE_EXTERNAL_LOOPBACK));
  TEST_ASSERT_EQUAL_HEX32(LBCK, reg(0, kTest));
  TEST_ASSERT_EQUAL_HEX32(TEST, reg(0, kCccr) & (TEST | MON));
}

void test_leaving_loopback_clears_test(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.apply_mode(&s_ctx[0], HAL_CAN_MODE_LOOPBACK));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.apply_mode(&s_ctx[0], HAL_CAN_MODE_NORMAL));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTest));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr));
}

void test_mode_flags_map_to_dar_fd_and_mon(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.apply_mode(&s_ctx[0], HAL_CAN_MODE_ONE_SHOT));
  TEST_ASSERT_EQUAL_HEX32(DAR, reg(0, kCccr));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      P.apply_mode(&s_ctx[0], HAL_CAN_MODE_FD | HAL_CAN_MODE_LISTEN_ONLY));
  TEST_ASSERT_EQUAL_HEX32(FDOE | BRSE | MON, reg(0, kCccr));
  TEST_ASSERT_EQUAL_INT(0, g.ignored_writes);
}

void test_standby_is_released_before_the_node_joins_and_set_on_stop(void) {
  hal_can_config_t cfg = config(2u, 500000u, 0u, false);
  cfg.stm32g474_fdcan.has_standby = true;
  cfg.stm32g474_fdcan.standby_pin = PC7;
  cfg.stm32g474_fdcan.standby_high = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(1, cfg));

  /* Configured in standby, then released, waited for, and only then INIT
   * cleared. */
  const int release = last_event_index(Event::GPIO_WRITE, PC7, 0u);
  const int wake = last_event_index(Event::DELAY_US, 40u, 0u);
  const int join = last_cccr_without_init(1);
  TEST_ASSERT_TRUE(last_event_index(Event::GPIO_MODE, PC7,
                                    (uint32_t)HAL_GPIO_OUTPUT_HIGH) >= 0);
  TEST_ASSERT_TRUE(release >= 0);
  TEST_ASSERT_TRUE(release < wake);
  TEST_ASSERT_TRUE(wake < join);

  TEST_ASSERT_EQUAL_INT(HAL_OK, P.stop(&s_ctx[1]));
  TEST_ASSERT_TRUE(last_event_index(Event::GPIO_WRITE, PC7, 1u) > join);
  TEST_ASSERT_TRUE((reg(1, kCccr) & (INIT | CCE)) == (INIT | CCE));
}

/* ── Transmission ─────────────────────────────────────────────────────── */

void test_send_writes_the_element_at_the_put_index(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  reg(0, kTxfqs) = (2u << 16) | 3u; /* put index 2 */
  hal_can_frame_t f = {};
  f.id = 0x12345u;
  f.flags = HAL_CAN_FRAME_EXTENDED | HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS;
  f.len = 12u;
  f.dlc = 9u;
  for (uint8_t k = 0; k < 12u; ++k) {
    f.data[k] = (uint8_t)(0x10u + k);
  }

  TEST_ASSERT_EQUAL_INT(HAL_OK, P.send_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_size_t(1u, g.sent[0].size());
  const std::vector<uint32_t> &e = g.sent[0][0];
  TEST_ASSERT_EQUAL_HEX32((1u << 30) | 0x12345u, e[0]);
  TEST_ASSERT_EQUAL_HEX32((1u << 21) | (1u << 20) | (9u << 16), e[1]);
  TEST_ASSERT_EQUAL_HEX32(0x13121110u, e[2]);
  TEST_ASSERT_EQUAL_HEX32(0x1B1A1918u, e[4]);
  TEST_ASSERT_EQUAL_HEX32(
      0u, ram(0, kTxBufferWord + 0u * kElementWords)); /* idx 0 */
}

void test_send_reports_a_failed_one_shot_attempt(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  g.fail_tx = true;
  hal_can_frame_t f = {};
  f.id = 0x123u;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, P.send_frame(&s_ctx[0], &f));
}

void test_stuck_send_times_out_and_flushes_the_queue(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  g.stall_tx = true;
  const int entries = g.init_entries[0];
  const uint32_t before = s_now_us;
  hal_can_frame_t f = {};
  f.id = 0x123u;

  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, P.send_frame(&s_ctx[0], &f));
  /* 500 kbit/s: 20 x 160 bits = 6.4 ms bound. */
  TEST_ASSERT_TRUE(s_now_us - before >= 6400u);
  TEST_ASSERT_TRUE(s_now_us - before < 20000u);
  TEST_ASSERT_EQUAL_INT(entries + 1, g.init_entries[0]);
  TEST_ASSERT_EQUAL_INT(0, g.txbcr_writes);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTxbrp));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & INIT);
}

void test_bus_off_during_send_drops_the_frame_and_starts_recovery(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  g.bus_off_tx = true;
  hal_can_frame_t f = {};
  f.id = 0x321u;
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, P.send_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTxbrp)); /* not sent after recovery */
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & INIT);
}

/* A blocking send that meets bus-off must not start the recovery under
 * HAL_CAN_MODE_MANUAL_RECOVERY: the node waits for recover(). */
void test_bus_off_during_send_waits_for_manual_recovery(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      P.apply_mode(&s_ctx[0], HAL_CAN_MODE_FD | HAL_CAN_MODE_MANUAL_RECOVERY));
  g.bus_off_tx = true;
  hal_can_frame_t f = {};
  f.id = 0x321u;
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, P.send_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTxbrp)); /* dropped */
  TEST_ASSERT_TRUE((reg(0, kCccr) & INIT) != 0u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.recover(&s_ctx[0]));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & INIT);
}

void test_send_while_bus_off_queues_nothing(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  reg(0, kPsr) = 1u << 7;
  reg(0, kCccr) = reg(0, kCccr) | INIT;
  hal_can_frame_t f = {};
  f.id = 0x321u;
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, P.send_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_size_t(0u, g.sent[0].size());
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & INIT);
}

void test_send_with_full_fifo_is_refused(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  reg(0, kTxfqs) = 1u << 21;
  hal_can_frame_t f = {};
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, P.send_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_size_t(0u, g.sent[0].size());
}

/* ── Reception ────────────────────────────────────────────────────────── */

void test_receive_decodes_and_acknowledges_in_fifo_order(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  rx_arrive(0, 0x7E0u << 18, 8u << 16, {0x04030201u, 0x08070605u});
  rx_arrive(0, (1u << 31) | (1u << 30) | 0x1ABCDEu,
            (1u << 21) | (1u << 20) | (10u << 16),
            {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u});
  rx_arrive(0, (1u << 29) | (0x55u << 18), 2u << 16, {});

  hal_can_frame_t f = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.available(&s_ctx[0]));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.receive_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_HEX32(0x7E0u, f.id);
  TEST_ASSERT_EQUAL_HEX8(0u, f.flags);
  TEST_ASSERT_EQUAL_UINT8(8u, f.len);
  TEST_ASSERT_EQUAL_HEX8(0x08u, f.data[7]);

  TEST_ASSERT_EQUAL_INT(HAL_OK, P.receive_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_HEX32(0x1ABCDEu, f.id);
  TEST_ASSERT_EQUAL_HEX8(HAL_CAN_FRAME_EXTENDED | HAL_CAN_FRAME_FD |
                             HAL_CAN_FRAME_BRS | HAL_CAN_FRAME_ESI,
                         f.flags);
  TEST_ASSERT_EQUAL_UINT8(16u, f.len);
  TEST_ASSERT_EQUAL_HEX8(0x44u, f.data[15]);

  TEST_ASSERT_EQUAL_INT(HAL_OK, P.receive_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_HEX32(0x55u, f.id);
  TEST_ASSERT_EQUAL_HEX8(HAL_CAN_FRAME_RTR, f.flags);
  TEST_ASSERT_EQUAL_UINT8(2u, f.dlc);

  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, P.available(&s_ctx[0]));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, P.receive_frame(&s_ctx[0], &f));
}

void test_lost_frames_are_counted_and_the_flag_cleared(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  for (int k = 0; k < 4; ++k) {
    rx_arrive(0, 0x100u << 18, 0u, {});
  }
  hal_can_frame_t f = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.receive_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_UINT32(1u, s_ctx[0].rx_hw_lost);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kIr) & (1u << 2));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.receive_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.receive_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_UINT32(1u, s_ctx[0].rx_hw_lost);
}

/* ── Filters ──────────────────────────────────────────────────────────── */

void test_filters_change_while_running_and_replace_accept_all(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  const int entries = g.init_entries[0];
  const hal_can_filter_t std_filter = {0x120u, 0x7E0u, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&s_ctx[0], 0u, &std_filter));

  TEST_ASSERT_EQUAL_INT(entries, g.init_entries[0]); /* never left the bus */
  TEST_ASSERT_EQUAL_HEX32((2u << 30) | (1u << 27) | (0x120u << 16) | 0x7E0u,
                          ram(0, kStdFilterWord + 0u));
  TEST_ASSERT_EQUAL_HEX32(0u, ram(0, kStdFilterWord + 27u));
  TEST_ASSERT_EQUAL_HEX32(0u, ram(0, kExtFilterWord + 14u));
}

void test_reprogramming_a_slot_moves_it_between_the_lists(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  const hal_can_filter_t std_filter = {0x7E0u, 0x7FFu, 0u};
  const hal_can_filter_t ext_filter = {0x18DAF110u, 0x1FFFFFFFu,
                                       HAL_CAN_FILTER_EXTENDED};
  /* Slot 1 takes the lowest free standard element. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&s_ctx[0], 1u, &std_filter));
  TEST_ASSERT_EQUAL_HEX32((2u << 30) | (1u << 27) | (0x7E0u << 16) | 0x7FFu,
                          ram(0, kStdFilterWord + 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&s_ctx[0], 1u, &ext_filter));
  TEST_ASSERT_EQUAL_HEX32(0u, ram(0, kStdFilterWord + 0u));
  TEST_ASSERT_EQUAL_HEX32((1u << 29) | 0x18DAF110u, ram(0, kExtFilterWord));
  TEST_ASSERT_EQUAL_HEX32((2u << 30) | 0x1FFFFFFFu,
                          ram(0, kExtFilterWord + 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&s_ctx[0], 1u, &std_filter));
  TEST_ASSERT_EQUAL_HEX32(0u, ram(0, kExtFilterWord));
  TEST_ASSERT_TRUE(ram(0, kStdFilterWord + 0u) != 0u);
}

void test_filter_on_a_stopped_channel_keeps_it_stopped(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.stop(&s_ctx[0]));
  const hal_can_filter_t f = {0x123u, 0x7FFu, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&s_ctx[0], 0u, &f));
  TEST_ASSERT_TRUE((reg(0, kCccr) & INIT) != 0u);
}

/* ── State ────────────────────────────────────────────────────────────── */

void test_state_and_counters_follow_psr_and_ecr(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  hal_can_state_t st = HAL_CAN_STATE_STOPPED;
  reg(0, kPsr) = 1u << 6;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.get_state(&s_ctx[0], &st));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_ERROR_WARNING, st);
  reg(0, kPsr) = 1u << 5;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.get_state(&s_ctx[0], &st));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_ERROR_PASSIVE, st);
  reg(0, kEcr) = (0x55u << 8) | 0x99u;
  hal_can_error_counters_t c = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.get_error_counters(&s_ctx[0], &c));
  TEST_ASSERT_EQUAL_UINT8(0x99u, c.tx);
  TEST_ASSERT_EQUAL_UINT8(0x55u, c.rx);
}

void test_bus_off_is_reported_and_recovery_started(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  /* The controller enters INIT on its own when it goes bus-off. */
  reg(0, kPsr) = 1u << 7;
  reg(0, kCccr) = reg(0, kCccr) | INIT;
  hal_can_state_t st = HAL_CAN_STATE_ERROR_ACTIVE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.get_state(&s_ctx[0], &st));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_BUS_OFF, st);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & INIT);
}

/* ── Interrupt-driven operation ───────────────────────────────────────── */

extern "C" void FDCAN1_IT0_IRQHandler(void);
extern "C" void FDCAN1_IT1_IRQHandler(void);

namespace {

jh_can_queues_t s_q;
jh_can_rx_entry_t s_rx_store[4];
jh_can_tx_entry_t s_tx_store[8];
jh_can_event_t s_event_store[16];

void attach_queues(int slot) {
  memset(&s_q, 0, sizeof(s_q));
  jh_spsc_ring_init(&s_q.rx, s_rx_store, sizeof(jh_can_rx_entry_t), 4u);
  jh_spsc_ring_init(&s_q.tx, s_tx_store, sizeof(jh_can_tx_entry_t), 8u);
  jh_spsc_ring_init(&s_q.events, s_event_store, sizeof(jh_can_event_t), 16u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.attach(&s_ctx[slot], &s_q));
}

void queue_tx(uint32_t id, uint32_t tag) {
  jh_can_tx_entry_t entry = {};
  entry.frame.id = id;
  entry.frame.len = 1u;
  entry.frame.dlc = 1u;
  entry.tag = tag;
  TEST_ASSERT_TRUE(jh_spsc_ring_push(&s_q.tx, &entry));
}

std::vector<jh_can_event_t> take_events(void) {
  std::vector<jh_can_event_t> out;
  jh_can_event_t event;
  while (jh_spsc_ring_pop(&s_q.events, &event)) {
    out.push_back(event);
  }
  return out;
}

uint8_t nvic_priority(uint32_t irqn) {
  return *jh_stm32g474_host_reg8(kNvicIpr + irqn);
}

} // namespace

void test_attach_routes_the_interrupts_to_both_lines(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  /* RF0N/RF0L/RF1N/RF1L, TC, TCF, TFE, MRAF, EP, EW, BO; RX FIFO groups
   * on IT0. */
  TEST_ASSERT_EQUAL_HEX32((1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) |
                              IR_TC | IR_TCF | IR_TFE | IR_MRAF | (1u << 17) |
                              (1u << 18) | IR_BO | (kTim3 ? IR_TEFN : 0u),
                          reg(0, kIe));
  TEST_ASSERT_EQUAL_HEX32(0x7Cu, reg(0, kIls));
  TEST_ASSERT_EQUAL_HEX32(0x3u, reg(0, kIle));
  TEST_ASSERT_EQUAL_HEX32(0x7u, reg(0, kTxbtie));
  TEST_ASSERT_EQUAL_HEX32(0x7u, reg(0, kTxbcie));
  /* FDCAN1_IT0 = 21, FDCAN1_IT1 = 22 (RM0440 Table 97). */
  TEST_ASSERT_EQUAL_HEX8(0x80u, nvic_priority(21u));
  TEST_ASSERT_EQUAL_HEX8(0x80u, nvic_priority(22u));
  TEST_ASSERT_EQUAL_HEX32(1u << 22, cell(kNvicIser));
  TEST_ASSERT_EQUAL_INT(0, s_critical_depth);

  P.deinit(&s_ctx[0]);
  s_open[0] = false;
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kIe));
  TEST_ASSERT_EQUAL_HEX32(1u << 22, cell(kNvicIcer));
  FDCAN1_IT0_IRQHandler(); /* a late interrupt finds nothing to serve */
}

void test_second_instance_uses_its_own_interrupt_lines(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(1, config(2u, 500000u, 0u, false)));
  attach_queues(1);
  /* FDCAN2_IT0 = 86, FDCAN2_IT1 = 87: ISER2 bits 22 and 23. */
  TEST_ASSERT_EQUAL_HEX8(0x80u, nvic_priority(86u));
  TEST_ASSERT_EQUAL_HEX8(0x80u, nvic_priority(87u));
  TEST_ASSERT_EQUAL_HEX32(1u << 23, cell(kNvicIser + 8u));
}

void test_the_interrupt_queues_received_frames_with_their_filter(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  /* Slot 2 lands in element 0, an added filter (index 6) in element 1. */
  const hal_can_filter_t slot = {0x123u, 0x7FFu, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&s_ctx[0], 2u, &slot));
  const hal_can_filter_ex_t added = {HAL_CAN_FILTER_RANGE,
                                     HAL_CAN_FILTER_ACCEPT, 0u, 0x200u, 0x2FFu};
  uint8_t index = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.add_filter(&s_ctx[0], &added, &index));
  TEST_ASSERT_EQUAL_UINT8(6u, index);
  rx_arrive(0, 0x123u << 18, (0u << 24) | (2u << 16), {0x0201u}); /* FIDX 0 */
  rx_arrive(0, 0x250u << 18, (1u << 24) | (1u << 16), {0x03u});   /* FIDX 1 */
  rx_arrive(0, 0x125u << 18, (1u << 31) | (1u << 16), {0x04u});   /* ANMF */
  FDCAN1_IT0_IRQHandler();

  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kIr) & IR_RF0N);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kRxf0s) & 0xFu); /* all acknowledged */
  jh_can_rx_entry_t entry;
  TEST_ASSERT_TRUE(jh_spsc_ring_pop(&s_q.rx, &entry));
  TEST_ASSERT_EQUAL_HEX32(0x123u, entry.frame.id);
  TEST_ASSERT_EQUAL_UINT8(2u, entry.frame.len);
  TEST_ASSERT_EQUAL_HEX8(0x02u, entry.frame.data[1]);
  TEST_ASSERT_EQUAL_UINT8(2u, entry.info.filter_index);
  TEST_ASSERT_TRUE(jh_spsc_ring_pop(&s_q.rx, &entry));
  TEST_ASSERT_EQUAL_UINT8(6u, entry.info.filter_index);
  TEST_ASSERT_TRUE(jh_spsc_ring_pop(&s_q.rx, &entry));
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_FILTER_NONE, entry.info.filter_index);
  TEST_ASSERT_EQUAL_UINT32(3u, s_q.counters.rx_frames);
}

void test_a_full_receive_queue_and_a_full_fifo_are_counted(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  for (int k = 0; k < 4; ++k) { /* the 4th is lost by the 3-element FIFO */
    rx_arrive(0, 0x100u << 18, 0u, {});
  }
  FDCAN1_IT0_IRQHandler();
  for (int k = 0; k < 3; ++k) {
    rx_arrive(0, 0x101u << 18, 0u, {});
  }
  FDCAN1_IT0_IRQHandler(); /* the HAL queue (4) takes one of these three */
  TEST_ASSERT_EQUAL_UINT32(1u, s_q.counters.rx_hw_lost);
  TEST_ASSERT_EQUAL_UINT32(4u, s_q.counters.rx_frames);
  TEST_ASSERT_EQUAL_UINT32(2u, s_q.counters.rx_queue_overflow);
}

void test_queued_frames_end_with_events_in_order(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  for (uint32_t k = 0; k < 5u; ++k) {
    queue_tx(0x200u + k, 10u + k);
  }
  P.kick_tx(&s_ctx[0]);
  /* Three TX elements; the rest waits for their completion interrupt. */
  TEST_ASSERT_EQUAL_size_t(3u, g.sent[0].size());
  TEST_ASSERT_EQUAL_UINT32(2u, jh_spsc_ring_count(&s_q.tx));
  FDCAN1_IT1_IRQHandler();
  FDCAN1_IT1_IRQHandler();
  TEST_ASSERT_EQUAL_size_t(5u, g.sent[0].size());
  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(5u, events.size());
  for (uint32_t k = 0; k < 5u; ++k) {
    TEST_ASSERT_EQUAL_UINT8(JH_CAN_EVENT_TX, events[k].kind);
    TEST_ASSERT_EQUAL_UINT32(10u + k, events[k].tx.tag);
    TEST_ASSERT_EQUAL_INT(HAL_OK, events[k].tx.result);
    TEST_ASSERT_EQUAL_HEX32((0x200u + k) << 18, g.sent[0][k][0]);
  }
  TEST_ASSERT_EQUAL_UINT32(5u, s_q.counters.tx_frames);
  TEST_ASSERT_EQUAL_INT(0, s_critical_depth);
}

void test_a_failed_one_shot_attempt_ends_as_failed(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  /* One-shot mode adds the protocol-error interrupts that report a failed
   * attempt; leaving it removes them again. */
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kIe) & (IR_PEA | IR_PED));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, P.apply_mode(&s_ctx[0], HAL_CAN_MODE_FD | HAL_CAN_MODE_ONE_SHOT));
  TEST_ASSERT_EQUAL_HEX32(IR_PEA | IR_PED, reg(0, kIe) & (IR_PEA | IR_PED));
  g.fail_tx = true;
  queue_tx(0x300u, 7u);
  P.kick_tx(&s_ctx[0]);
  FDCAN1_IT1_IRQHandler();
  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(1u, events.size());
  TEST_ASSERT_EQUAL_UINT32(7u, events[0].tx.tag);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, events[0].tx.result);
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_FAILED, events[0].tx.reason);
  TEST_ASSERT_EQUAL_UINT32(1u, s_q.counters.tx_failed);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kIr) & IR_PEA); /* acknowledged */
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.apply_mode(&s_ctx[0], HAL_CAN_MODE_FD));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kIe) & (IR_PEA | IR_PED));
}

void test_bus_off_ends_every_pending_frame_and_starts_recovery(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  g.stall_tx = true;
  for (uint32_t k = 0; k < 5u; ++k) {
    queue_tx(0x400u + k, 20u + k);
  }
  P.kick_tx(&s_ctx[0]); /* three frames pending in the controller */
  reg(0, kPsr) = 1u << 7;
  reg(0, kCccr) = reg(0, kCccr) | INIT;
  reg(0, kIr) |= IR_BO;
  FDCAN1_IT1_IRQHandler();

  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(6u, events.size());
  for (uint32_t k = 0; k < 5u; ++k) {
    TEST_ASSERT_EQUAL_UINT32(20u + k, events[k].tx.tag);
    TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_BUS_OFF, events[k].tx.reason);
    TEST_ASSERT_EQUAL_INT(HAL_EBUS, events[k].tx.result);
  }
  TEST_ASSERT_EQUAL_UINT8(JH_CAN_EVENT_STATE, events[5].kind);
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_BUS_OFF, events[5].state);
  TEST_ASSERT_EQUAL_UINT32(1u, s_q.counters.bus_off_count);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTxbrp)); /* dropped, not sent later */
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & INIT); /* recovery running */
}

/* A transmission the TX handler could not read from the message RAM puts
 * the controller in restricted operation, where it sends nothing; the
 * interrupt counts the failure and ends that mode, and the waiting frames go
 * out (RM0440 FDCAN_IR.MRAF, FDCAN_CCCR.ASM). */
void test_a_message_ram_failure_ends_restricted_operation(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  g.tx_ram_failure = true;
  queue_tx(0x410u, 31u);
  queue_tx(0x411u, 32u);
  P.kick_tx(&s_ctx[0]);
  TEST_ASSERT_EQUAL_HEX32(ASM, reg(0, kCccr) & ASM);
  TEST_ASSERT_EQUAL_HEX32(3u, reg(0, kTxbrp)); /* both held */
  FDCAN1_IT1_IRQHandler();

  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & ASM);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTxbrp));
  TEST_ASSERT_EQUAL_UINT32(1u, s_q.counters.ram_access_failures);
  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(2u, events.size());
  for (uint32_t k = 0; k < 2u; ++k) {
    TEST_ASSERT_EQUAL_UINT32(31u + k, events[k].tx.tag);
    TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_DONE, events[k].tx.reason);
  }
}

/* Without interrupts a blocking send finds the failure itself and its frame
 * still goes out; the status counts it. */
void test_a_blocking_send_rides_out_a_message_ram_failure(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  g.tx_ram_failure = true;
  hal_can_frame_t f = {};
  f.id = 0x412u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.send_frame(&s_ctx[0], &f));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & ASM);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kIr) & IR_MRAF);
  hal_can_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.get_status(&s_ctx[0], &status));
  TEST_ASSERT_EQUAL_UINT32(1u, status.ram_access_failures);
}

void test_manual_recovery_keeps_the_node_off_until_recover(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      P.apply_mode(&s_ctx[0], HAL_CAN_MODE_FD | HAL_CAN_MODE_MANUAL_RECOVERY));
  reg(0, kPsr) = 1u << 7;
  reg(0, kCccr) = reg(0, kCccr) | INIT;
  reg(0, kIr) |= IR_BO;
  FDCAN1_IT1_IRQHandler();
  TEST_ASSERT_TRUE((reg(0, kCccr) & INIT) != 0u);
  hal_can_state_t state = HAL_CAN_STATE_ERROR_ACTIVE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.get_state(&s_ctx[0], &state));
  TEST_ASSERT_TRUE((reg(0, kCccr) & INIT) != 0u); /* no automatic recovery */
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.recover(&s_ctx[0]));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kCccr) & INIT);
}

void test_stop_ends_pending_and_waiting_frames_as_stopped(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  g.stall_tx = true;
  for (uint32_t k = 0; k < 4u; ++k) {
    queue_tx(0x500u + k, 30u + k);
  }
  P.kick_tx(&s_ctx[0]);
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.stop(&s_ctx[0]));
  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(4u, events.size());
  for (uint32_t k = 0; k < 4u; ++k) {
    TEST_ASSERT_EQUAL_UINT32(30u + k, events[k].tx.tag);
    TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_STOPPED, events[k].tx.reason);
    TEST_ASSERT_EQUAL_INT(HAL_ECANCELED, events[k].tx.result);
  }
  TEST_ASSERT_EQUAL_INT(0, s_critical_depth);
}

/* Frames were queued for the old mode: a mode change ends the waiting ones
 * as well, so an FD frame never reaches a channel switched to classic CAN. */
void test_a_mode_change_ends_waiting_frames_too(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  g.stall_tx = true;
  for (uint32_t k = 0; k < 5u; ++k) {
    jh_can_tx_entry_t entry = {};
    entry.frame.id = 0x600u + k;
    entry.frame.flags = HAL_CAN_FRAME_FD;
    entry.frame.len = 12u;
    entry.frame.dlc = hal_can_bytes_to_dlc(12u);
    entry.tag = 50u + k;
    TEST_ASSERT_TRUE(jh_spsc_ring_push(&s_q.tx, &entry));
  }
  P.kick_tx(&s_ctx[0]);
  const size_t sent_before = g.sent[0].size();
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.apply_mode(&s_ctx[0], HAL_CAN_MODE_NORMAL));
  TEST_ASSERT_EQUAL_size_t(sent_before, g.sent[0].size());
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTxbrp));
  TEST_ASSERT_EQUAL_UINT32(0u, jh_spsc_ring_count(&s_q.tx));
  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(5u, events.size());
  for (uint32_t k = 0; k < 5u; ++k) {
    TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_STOPPED, events[k].tx.reason);
  }
}

void test_a_blocking_send_beside_queued_frames_reports_once(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  queue_tx(0x600u, 40u);
  P.kick_tx(&s_ctx[0]); /* done in hardware, not yet reported */
  hal_can_frame_t f = {};
  f.id = 0x601u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.send_frame(&s_ctx[0], &f));
  FDCAN1_IT1_IRQHandler();
  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(1u, events.size()); /* the blocking send has none */
  TEST_ASSERT_EQUAL_UINT32(40u, events[0].tx.tag);
  TEST_ASSERT_EQUAL_size_t(2u, g.sent[0].size());
  TEST_ASSERT_EQUAL_INT(0, s_critical_depth);
}

void test_status_keeps_the_last_error_codes_across_reads(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  /* LEC 3 (ACK), DLEC 6 (CRC), TDCV 28; reading resets them to 7. */
  reg(0, kPsr) = (28u << 16) | (6u << 8) | 3u;
  hal_can_state_t state;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.get_state(&s_ctx[0], &state));
  reg(0, kPsr) = (28u << 16) | (7u << 8) | 7u;
  hal_can_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.get_status(&s_ctx[0], &status));
  TEST_ASSERT_EQUAL_UINT8(3u, status.last_error);
  TEST_ASSERT_EQUAL_UINT8(6u, status.last_data_error);
  TEST_ASSERT_EQUAL_UINT8(28u, status.tdc_value);
}

/* ── Filters ──────────────────────────────────────────────────────────── */

namespace {

hal_can_filter_ex_t filter_ex(uint8_t type, uint8_t action, bool ext,
                              uint32_t id1, uint32_t id2) {
  hal_can_filter_ex_t f = {};
  f.type = type;
  f.action = action;
  f.flags = ext ? (uint8_t)HAL_CAN_FILTER_EXTENDED : (uint8_t)0u;
  f.id1 = id1;
  f.id2 = id2;
  return f;
}

uint8_t add(const hal_can_filter_ex_t &f) {
  uint8_t index = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.add_filter(&s_ctx[0], &f, &index));
  return index;
}

} // namespace

void test_filter_kinds_and_actions_become_elements(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(&s_ctx[0], false, false, true));
  /* Standard: SFT 0 range, 1 dual, 2 classic; SFEC 1 FIFO0, 3 reject. */
  (void)add(filter_ex(HAL_CAN_FILTER_RANGE, HAL_CAN_FILTER_ACCEPT, false,
                      0x100u, 0x1FFu));
  (void)add(filter_ex(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_REJECT, false, 0x7DFu,
                      0x7E0u));
  (void)add(filter_ex(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT, false, 0x120u,
                      0x7F0u));
  TEST_ASSERT_EQUAL_HEX32((0u << 30) | (1u << 27) | (0x100u << 16) | 0x1FFu,
                          ram(0, kStdFilterWord + 0u));
  TEST_ASSERT_EQUAL_HEX32((1u << 30) | (3u << 27) | (0x7DFu << 16) | 0x7E0u,
                          ram(0, kStdFilterWord + 1u));
  TEST_ASSERT_EQUAL_HEX32((2u << 30) | (1u << 27) | (0x120u << 16) | 0x7F0u,
                          ram(0, kStdFilterWord + 2u));
  /* Extended: EFEC in F0, EFT in F1 (3 = range without XIDAM). */
  (void)add(filter_ex(HAL_CAN_FILTER_RANGE, HAL_CAN_FILTER_REJECT, true,
                      0x1000u, 0x1FFFu));
  (void)add(filter_ex(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT, true,
                      0x18DAF110u, 0x18DAF111u));
  TEST_ASSERT_EQUAL_HEX32((3u << 29) | 0x1000u, ram(0, kExtFilterWord));
  TEST_ASSERT_EQUAL_HEX32((3u << 30) | 0x1FFFu, ram(0, kExtFilterWord + 1u));
  TEST_ASSERT_EQUAL_HEX32((1u << 29) | 0x18DAF110u,
                          ram(0, kExtFilterWord + 2u));
  TEST_ASSERT_EQUAL_HEX32((1u << 30) | 0x18DAF111u,
                          ram(0, kExtFilterWord + 3u));
}

void test_an_extended_element_never_matches_half_written(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  g.ram_writes.clear();
  (void)add(filter_ex(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT, true, 0x1234u,
                      0x1FFFFFFFu));
  const uintptr_t f0 = kRam[0] + kExtFilterWord * 4u;
  TEST_ASSERT_EQUAL_size_t(3u, g.ram_writes.size());
  TEST_ASSERT_EQUAL_HEX32(f0, g.ram_writes[0].first);
  TEST_ASSERT_EQUAL_HEX32(0u, g.ram_writes[0].second); /* disabled first */
  TEST_ASSERT_EQUAL_HEX32(f0 + 4u, g.ram_writes[1].first);
  TEST_ASSERT_EQUAL_HEX32(f0, g.ram_writes[2].first);
}

void test_the_lists_fill_up_to_their_size(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  /* Accepting unmatched frames keeps the last position: 27 standard. */
  for (uint32_t k = 0; k < 27u; ++k) {
    TEST_ASSERT_EQUAL_UINT8(6u + k,
                            add(filter_ex(HAL_CAN_FILTER_DUAL,
                                          HAL_CAN_FILTER_ACCEPT, false, k, k)));
  }
  hal_can_filter_ex_t one_more =
      filter_ex(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT, false, 99u, 99u);
  uint8_t index = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, P.add_filter(&s_ctx[0], &one_more, &index));
  /* Rejecting unmatched frames frees it: the 28th fits, the 29th does not. */
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(&s_ctx[0], false, true, true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.add_filter(&s_ctx[0], &one_more, &index));
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, P.add_filter(&s_ctx[0], &one_more, &index));
  /* ... and accepting them again has no room any more. */
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM,
                        P.set_unmatched_policy(&s_ctx[0], true, true, true));
  /* Extended: 7 beside the accept-all element. */
  for (uint32_t k = 0; k < 7u; ++k) {
    (void)add(
        filter_ex(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT, true, k, k));
  }
  one_more.flags = HAL_CAN_FILTER_EXTENDED;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, P.add_filter(&s_ctx[0], &one_more, &index));
}

/* A slot moving to the other ID kind needs room there; without it the old
 * filter stays as it was. */
void test_a_slot_change_without_room_keeps_the_old_filter(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(&s_ctx[0], false, false, true));
  for (uint32_t k = 0; k < 8u; ++k) {
    (void)add(
        filter_ex(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT, true, k, k));
  }
  const hal_can_filter_t std_filter = {0x7E0u, 0x7FFu, 0u};
  const hal_can_filter_t ext_filter = {0x18DAF110u, 0x1FFFFFFFu,
                                       HAL_CAN_FILTER_EXTENDED};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&s_ctx[0], 0u, &std_filter));
  const uint32_t before = ram(0, kStdFilterWord + 0u);
  TEST_ASSERT_TRUE(before != 0u);
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, P.set_filter(&s_ctx[0], 0u, &ext_filter));
  TEST_ASSERT_EQUAL_HEX32(before, ram(0, kStdFilterWord + 0u));
}

void test_removed_filters_free_their_position(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  const uint8_t a = add(filter_ex(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT,
                                  false, 0x100u, 0x7FFu));
  const uint8_t b = add(filter_ex(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT,
                                  false, 0x101u, 0x7FFu));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.remove_filter(&s_ctx[0], a));
  TEST_ASSERT_EQUAL_HEX32(0u, ram(0, kStdFilterWord + 0u));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, P.remove_filter(&s_ctx[0], a));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, P.remove_filter(&s_ctx[0], 0xFEu));
  /* The next filter takes the lowest free position and index. */
  const uint8_t c = add(filter_ex(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT,
                                  false, 0x102u, 0x7FFu));
  TEST_ASSERT_EQUAL_UINT8(a, c);
  TEST_ASSERT_TRUE(b != c);
  TEST_ASSERT_EQUAL_HEX32((2u << 30) | (1u << 27) | (0x102u << 16) | 0x7FFu,
                          ram(0, kStdFilterWord + 0u));
  /* A classic slot can be removed too. */
  const hal_can_filter_t slot = {0x7E8u, 0x7FFu, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&s_ctx[0], 0u, &slot));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.remove_filter(&s_ctx[0], 0u));
}

void test_remote_frames_need_a_stopped_channel_to_change(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  const int entries = g.init_entries[0];
  /* Unmatched frames change while running, without leaving the bus. */
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(&s_ctx[0], false, true, true));
  TEST_ASSERT_EQUAL_HEX32(0u, ram(0, kStdFilterWord + 27u));
  TEST_ASSERT_EQUAL_HEX32(1u << 29, ram(0, kExtFilterWord + 14u));
  TEST_ASSERT_EQUAL_INT(entries, g.init_entries[0]);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        P.set_unmatched_policy(&s_ctx[0], false, true, false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.stop(&s_ctx[0]));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(&s_ctx[0], false, true, false));
  TEST_ASSERT_EQUAL_HEX32(0x3u, reg(0, kRxgfc) & 0x3u); /* RRFS, RRFE */
  TEST_ASSERT_EQUAL_INT(0, g.ignored_writes);
}

void test_a_chosen_policy_survives_the_first_classic_filter(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(&s_ctx[0], true, true, true));
  const hal_can_filter_t slot = {0x7E8u, 0x7FFu, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&s_ctx[0], 0u, &slot));
  TEST_ASSERT_TRUE(ram(0, kStdFilterWord + 27u) != 0u); /* still accepting */
}

/* ── Timestamps ───────────────────────────────────────────────────────── */

#ifdef HAL_CAN_STM32G474_TIMESTAMP_TIM3

void test_tim3_counts_microseconds_for_every_instance(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  TEST_ASSERT_EQUAL_UINT32(169u, cell(kTim3Psc)); /* 170 MHz / 170 */
  TEST_ASSERT_EQUAL_HEX32(0xFFFFu, cell(kTim3Arr));
  TEST_ASSERT_EQUAL_HEX32(1u, cell(kTim3Cr1) & 1u);
  TEST_ASSERT_EQUAL_HEX32(2u, reg(0, kTscc) & 3u); /* TSS: external */
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(1, config(2u, 500000u, 0u, false)));
  TEST_ASSERT_EQUAL_HEX32(2u, reg(1, kTscc) & 3u);
}

void test_received_frames_carry_their_start_of_frame_time(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  rx_arrive(0, 0x100u << 18, (2u << 16) | 0x1234u, {0x0101u});
  rx_arrive(0, 0x101u << 18, (2u << 16) | 0xFFF0u, {0x0101u});
  cell(kTim3Cnt) = 0x1300u;
  s_now_us = 5000000u;
  FDCAN1_IT0_IRQHandler();
  jh_can_rx_entry_t entry;
  TEST_ASSERT_TRUE(jh_spsc_ring_pop(&s_q.rx, &entry));
  TEST_ASSERT_EQUAL_UINT64(5000000u - 0xCCu, entry.info.timestamp_us);
  TEST_ASSERT_TRUE(jh_spsc_ring_pop(&s_q.rx, &entry));
  /* 0xFFF0 was captured before TIM3 wrapped: 0x1300 + 0x10 earlier. */
  TEST_ASSERT_EQUAL_UINT64(5000000u - 0x1310u, entry.info.timestamp_us);
}

void test_a_sent_frame_waits_for_its_tx_event(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  g.hold_tx_events = true;
  g.sof16 = 0x2000u;
  queue_tx(0x300u, 77u);
  P.kick_tx(&s_ctx[0]);
  /* The element asks for a TX event naming its own index. */
  TEST_ASSERT_EQUAL_HEX32((1u << 23), g.sent[0][0][1] & (1u << 23));
  FDCAN1_IT1_IRQHandler(); /* TC without the event yet */
  TEST_ASSERT_EQUAL_size_t(0u, take_events().size());
  release_tx_events(0);
  cell(kTim3Cnt) = 0x2010u;
  s_now_us = 7000000u;
  FDCAN1_IT1_IRQHandler();
  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(1u, events.size());
  TEST_ASSERT_EQUAL_UINT32(77u, events[0].tx.tag);
  TEST_ASSERT_EQUAL_UINT64(7000000u - 0x10u, events[0].tx.timestamp_us);
  TEST_ASSERT_EQUAL_HEX32(0u, reg(0, kTxefs) & 7u); /* acknowledged */
}

#else

void test_software_timestamps_are_the_interrupt_entry_time(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  rx_arrive(0, 0x100u << 18, 2u << 16, {0x0101u});
  queue_tx(0x200u, 5u);
  P.kick_tx(&s_ctx[0]);
  s_now_us = 1234567u;
  FDCAN1_IT0_IRQHandler();
  jh_can_rx_entry_t entry;
  TEST_ASSERT_TRUE(jh_spsc_ring_pop(&s_q.rx, &entry));
  TEST_ASSERT_EQUAL_UINT64(1234567u, entry.info.timestamp_us);
  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(1u, events.size());
  TEST_ASSERT_EQUAL_UINT64(1234567u, events[0].tx.timestamp_us);
  /* No TX events are stored without TIM3 timestamps. */
  TEST_ASSERT_EQUAL_HEX32(0u, g.sent[0][0][1] & (1u << 23));
}

void test_frames_that_did_not_go_out_have_no_time(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open_default(0));
  attach_queues(0);
  g.fail_tx = true;
  queue_tx(0x200u, 5u);
  P.kick_tx(&s_ctx[0]);
  s_now_us = 1234567u;
  FDCAN1_IT1_IRQHandler();
  const std::vector<jh_can_event_t> events = take_events();
  TEST_ASSERT_EQUAL_size_t(1u, events.size());
  TEST_ASSERT_EQUAL_UINT64(0u, events[0].tx.timestamp_us);
}

#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_programs_the_g4_layout_without_ignored_writes);
  RUN_TEST(test_each_instance_uses_its_own_registers_and_message_ram);
  RUN_TEST(test_the_shared_clock_stops_with_the_last_instance);
  RUN_TEST(test_a_failed_init_releases_the_instance_and_the_clock);
  RUN_TEST(test_invalid_rates_are_refused_before_the_hardware_is_touched);
  RUN_TEST(test_an_instance_opens_once_until_released);
  RUN_TEST(test_instance_pins_get_their_alternate_function);
  RUN_TEST(test_pins_of_another_instance_are_refused);
  RUN_TEST(test_timing_at_170mhz_is_exact_and_leaves_tdc_off);
  RUN_TEST(test_classic_channel_below_the_data_phase_range_starts);
  RUN_TEST(test_data_rate_above_the_transceiver_limit_is_refused);
  RUN_TEST(test_manual_tdc_writes_offset_and_enable_bit);
  RUN_TEST(test_solver_at_80mhz_gives_cia_timings_and_tdc_offsets);
  RUN_TEST(test_solver_reference_vectors_at_160_24_and_170mhz);
  RUN_TEST(test_solver_rejects_rates_the_clock_cannot_make);
  RUN_TEST(test_solver_prefers_the_lowest_prescaler_with_a_good_sample_point);
  RUN_TEST(test_internal_loopback_sets_test_after_cccr_test);
  RUN_TEST(test_external_loopback_drives_the_bus);
  RUN_TEST(test_leaving_loopback_clears_test);
  RUN_TEST(test_mode_flags_map_to_dar_fd_and_mon);
  RUN_TEST(test_standby_is_released_before_the_node_joins_and_set_on_stop);
  RUN_TEST(test_send_writes_the_element_at_the_put_index);
  RUN_TEST(test_send_reports_a_failed_one_shot_attempt);
  RUN_TEST(test_stuck_send_times_out_and_flushes_the_queue);
  RUN_TEST(test_bus_off_during_send_drops_the_frame_and_starts_recovery);
  RUN_TEST(test_bus_off_during_send_waits_for_manual_recovery);
  RUN_TEST(test_send_while_bus_off_queues_nothing);
  RUN_TEST(test_send_with_full_fifo_is_refused);
  RUN_TEST(test_receive_decodes_and_acknowledges_in_fifo_order);
  RUN_TEST(test_lost_frames_are_counted_and_the_flag_cleared);
  RUN_TEST(test_filters_change_while_running_and_replace_accept_all);
  RUN_TEST(test_reprogramming_a_slot_moves_it_between_the_lists);
  RUN_TEST(test_filter_on_a_stopped_channel_keeps_it_stopped);
  RUN_TEST(test_state_and_counters_follow_psr_and_ecr);
  RUN_TEST(test_bus_off_is_reported_and_recovery_started);
  RUN_TEST(test_attach_routes_the_interrupts_to_both_lines);
  RUN_TEST(test_second_instance_uses_its_own_interrupt_lines);
  RUN_TEST(test_the_interrupt_queues_received_frames_with_their_filter);
  RUN_TEST(test_a_full_receive_queue_and_a_full_fifo_are_counted);
  RUN_TEST(test_queued_frames_end_with_events_in_order);
  RUN_TEST(test_a_failed_one_shot_attempt_ends_as_failed);
  RUN_TEST(test_bus_off_ends_every_pending_frame_and_starts_recovery);
  RUN_TEST(test_a_message_ram_failure_ends_restricted_operation);
  RUN_TEST(test_a_blocking_send_rides_out_a_message_ram_failure);
  RUN_TEST(test_manual_recovery_keeps_the_node_off_until_recover);
  RUN_TEST(test_stop_ends_pending_and_waiting_frames_as_stopped);
  RUN_TEST(test_a_mode_change_ends_waiting_frames_too);
  RUN_TEST(test_a_blocking_send_beside_queued_frames_reports_once);
  RUN_TEST(test_status_keeps_the_last_error_codes_across_reads);
  RUN_TEST(test_filter_kinds_and_actions_become_elements);
  RUN_TEST(test_an_extended_element_never_matches_half_written);
  RUN_TEST(test_the_lists_fill_up_to_their_size);
  RUN_TEST(test_a_slot_change_without_room_keeps_the_old_filter);
  RUN_TEST(test_removed_filters_free_their_position);
  RUN_TEST(test_remote_frames_need_a_stopped_channel_to_change);
  RUN_TEST(test_a_chosen_policy_survives_the_first_classic_filter);
#ifdef HAL_CAN_STM32G474_TIMESTAMP_TIM3
  RUN_TEST(test_tim3_counts_microseconds_for_every_instance);
  RUN_TEST(test_received_frames_carry_their_start_of_frame_time);
  RUN_TEST(test_a_sent_frame_waits_for_its_tx_event);
#else
  RUN_TEST(test_software_timestamps_are_the_interrupt_entry_time);
  RUN_TEST(test_frames_that_did_not_go_out_have_no_time);
#endif
  return UNITY_END();
}
