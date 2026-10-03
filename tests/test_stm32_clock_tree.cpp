// The STM32G474 clock tree bring-up on a modelled RCC. Built twice: with the
// default HSI16 170 MHz tree and with HAL_STM32G474_CLOCK_HSE_160MHZ. The
// frequencies are worked out from the written PLLCFGR, so a macro that does
// not match the programmed tree fails here. Register offsets and bits are
// written out from RM0440 instead of taken from stm32g474_regs.h.

#include "hal/impl/stm32g474/port/stm32g474_clock_tree.h"
#include "jh_stm32g474_host_regs.h"
#include "utils/unity.h"

#include <stdint.h>

namespace {

constexpr uintptr_t kRccCr = 0x40021000u;
constexpr uintptr_t kRccCfgr = 0x40021008u;
constexpr uintptr_t kRccPllcfgr = 0x4002100Cu;
constexpr uintptr_t kRccCcipr = 0x40021088u;
constexpr uintptr_t kFlashAcr = 0x40022000u;
constexpr uintptr_t kPwrCr5 = 0x40007080u;
constexpr uintptr_t kDwtCyccnt = 0xE0001004u;

constexpr uint32_t HSION = 1u << 8, HSIRDY = 1u << 10, HSEON = 1u << 16,
                   HSERDY = 1u << 17, PLLON = 1u << 24, PLLRDY = 1u << 25;
constexpr uint32_t kCyclesPerRead = 997u;

struct Model {
  bool hse_dead = false;
  bool hse_requested = false;
  int violations = 0; /* writes RM0440 forbids at that moment */
  uint32_t cycle_reads = 0u;
} g;

volatile uint32_t &cell(uintptr_t address) {
  return *jh_stm32g474_host_reg32(address);
}

uint32_t read_hook(uintptr_t address, uint32_t value) {
  if (address == kDwtCyccnt) {
    g.cycle_reads++;
    return g.cycle_reads * kCyclesPerRead;
  }
  return value;
}

void write_cr(uint32_t value, uint32_t *c) {
  uint32_t v = value & ~(HSIRDY | HSERDY | PLLRDY);
  v |= *c & (HSIRDY | HSERDY | PLLRDY);
  if (v & HSION) {
    v |= HSIRDY;
  }
  if (v & HSEON) {
    g.hse_requested = true;
    if (!g.hse_dead) {
      v |= HSERDY;
    }
  } else {
    v &= ~HSERDY;
  }
  if (v & PLLON) {
    const uint32_t src = cell(kRccPllcfgr) & 3u;
    const bool ready = src == 2u ? (v & HSIRDY) != 0u : (v & HSERDY) != 0u;
    if (ready) {
      v |= PLLRDY;
    } else {
      g.violations++; /* PLL started on an oscillator that is not running */
    }
  } else {
    v &= ~PLLRDY;
  }
  *c = v;
}

void write_cfgr(uint32_t value, uint32_t *c) {
  const uint32_t sw = value & 3u;
  if (sw == 3u && (*c & 3u) != 3u) {
    /* Switching to the PLL: boost mode, four wait states, PLL locked and
     * HCLK halved for the step above 80 MHz. */
    if ((cell(kFlashAcr) & 0xFu) < 4u || (cell(kPwrCr5) & (1u << 8)) != 0u ||
        (cell(kRccCr) & PLLRDY) == 0u || ((value >> 4) & 0xFu) != 0x8u) {
      g.violations++;
    }
  }
  *c = (value & ~(3u << 2)) | (sw << 2); /* SWS follows SW */
}

void write_hook(uintptr_t address, uint32_t value, uint32_t *c) {
  if (address == kRccCr) {
    write_cr(value, c);
  } else if (address == kRccCfgr) {
    write_cfgr(value, c);
  } else if (address == kRccPllcfgr) {
    if ((cell(kRccCr) & (PLLON | PLLRDY)) != 0u) {
      g.violations++;
    }
    *c = value;
  } else {
    *c = value;
  }
}

struct Pll {
  uint32_t input_hz;
  uint32_t vco_hz;
  uint32_t r_hz;
  uint32_t q_hz;
  bool q_enabled;
};

Pll decode_pll(uint32_t v) {
  Pll p = {};
  const uint32_t src = v & 3u;
  const uint32_t osc = src == 3u ? 24000000u : (src == 2u ? 16000000u : 0u);
  const uint32_t m = ((v >> 4) & 0xFu) + 1u;
  const uint32_t n = (v >> 8) & 0x7Fu;
  p.input_hz = osc / m;
  p.vco_hz = p.input_hz * n;
  p.r_hz = (v & (1u << 24)) ? p.vco_hz / (2u * (((v >> 25) & 3u) + 1u)) : 0u;
  p.q_enabled = (v & (1u << 20)) != 0u;
  p.q_hz = p.q_enabled ? p.vco_hz / (2u * (((v >> 21) & 3u) + 1u)) : 0u;
  return p;
}

void check_running_tree(void) {
  const Pll p = decode_pll(cell(kRccPllcfgr));
  /* RM0440 PLL limits: input 2.66..16 MHz, VCO 96..344 MHz. */
  TEST_ASSERT_TRUE(p.input_hz >= 2660000u && p.input_hz <= 16000000u);
  TEST_ASSERT_TRUE(p.vco_hz >= 96000000u && p.vco_hz <= 344000000u);
  TEST_ASSERT_EQUAL_UINT32(JH_G474_CORE_CLOCK_HZ, p.r_hz);
  TEST_ASSERT_EQUAL_UINT32(0u, g.violations);
  /* PLL is SYSCLK, AHB and both APB buses undivided. */
  TEST_ASSERT_EQUAL_HEX32(0xFu, cell(kRccCfgr) & 0xFu);
  TEST_ASSERT_EQUAL_HEX32(0u, cell(kRccCfgr) & ((0xFu << 4) | (0x3Fu << 8)));
  TEST_ASSERT_EQUAL_UINT32(JH_G474_HCLK_HZ, p.r_hz);
  TEST_ASSERT_EQUAL_UINT32(JH_G474_PCLK1_HZ, p.r_hz);
  TEST_ASSERT_EQUAL_UINT32(JH_G474_PCLK2_HZ, p.r_hz);
  TEST_ASSERT_EQUAL_UINT32(4u, cell(kFlashAcr) & 0xFu);
  /* I2C1/I2C2 on HSI16 (10 each). */
  TEST_ASSERT_EQUAL_HEX32((2u << 12) | (2u << 14),
                          cell(kRccCcipr) & (0xFu << 12));
  const uint32_t fdcansel = (cell(kRccCcipr) >> 24) & 3u;
  if (fdcansel == 1u) {
    TEST_ASSERT_TRUE(p.q_enabled);
    TEST_ASSERT_EQUAL_UINT32(JH_G474_FDCAN_CLOCK_HZ, p.q_hz);
  } else {
    TEST_ASSERT_EQUAL_UINT32(2u, fdcansel);
    TEST_ASSERT_EQUAL_UINT32(JH_G474_FDCAN_CLOCK_HZ, JH_G474_PCLK1_HZ);
  }
}

/* STOP leaves the core on HSI16 with the PLL and HSE off. */
void enter_stop(void) {
  cell(kRccCfgr) = cell(kRccCfgr) & ~0xFu;
  cell(kRccCr) = cell(kRccCr) & ~(PLLON | PLLRDY | HSEON | HSERDY);
}

} // namespace

void setUp(void) {
  jh_stm32g474_host_regs_reset();
  jh_stm32g474_host_regs_set_hooks(read_hook, write_hook);
  g = Model();
  cell(kRccCr) = HSION | HSIRDY; /* reset: running on HSI16 */
  cell(kFlashAcr) = 0u;
  cell(kPwrCr5) = 1u << 8; /* reset: Range 1 normal mode */
}

void tearDown(void) {}

#if defined(HAL_STM32G474_CLOCK_HSE_160MHZ)

void test_hse_tree_runs_160mhz_with_fdcan_at_80mhz(void) {
  TEST_ASSERT_EQUAL(JH_STM32G474_PLL_FROM_HSE, jh_stm32g474_clock_tree_init());
  check_running_tree();
  const uint32_t pll = cell(kRccPllcfgr);
  TEST_ASSERT_EQUAL_UINT32(3u, pll & 3u); /* HSE */
  TEST_ASSERT_EQUAL_UINT32(160000000u, JH_G474_CORE_CLOCK_HZ);
  TEST_ASSERT_EQUAL_UINT32(80000000u, decode_pll(pll).q_hz);
  TEST_ASSERT_EQUAL_UINT32(1u, (cell(kRccCcipr) >> 24) & 3u); /* PLLQ */
  TEST_ASSERT_EQUAL_UINT32(40000000u, JH_G474_ADC_CLOCK_HZ);
}

void test_dead_hse_falls_back_to_hsi_with_the_same_frequencies(void) {
  g.hse_dead = true;
  TEST_ASSERT_EQUAL(JH_STM32G474_PLL_FROM_HSI16,
                    jh_stm32g474_clock_tree_init());
  check_running_tree();
  TEST_ASSERT_EQUAL_UINT32(2u, cell(kRccPllcfgr) & 3u); /* HSI16 */
  TEST_ASSERT_EQUAL_UINT32(80000000u, decode_pll(cell(kRccPllcfgr)).q_hz);
  TEST_ASSERT_TRUE(g.hse_requested);
  TEST_ASSERT_EQUAL_HEX32(0u, cell(kRccCr) & HSEON);
  /* Waited the full 100 ms of HSI16 cycles, and not much longer. */
  const uint32_t waited = g.cycle_reads * kCyclesPerRead;
  TEST_ASSERT_TRUE(waited >= 1600000u);
  TEST_ASSERT_TRUE(waited < 1600000u + 2u * kCyclesPerRead);
}

void test_stop_exit_rebuilds_the_hse_tree(void) {
  TEST_ASSERT_EQUAL(JH_STM32G474_PLL_FROM_HSE, jh_stm32g474_clock_tree_init());
  enter_stop();
  TEST_ASSERT_EQUAL(JH_STM32G474_PLL_FROM_HSE, jh_stm32g474_clock_tree_init());
  check_running_tree();
}

#else

void test_default_tree_runs_170mhz_from_hsi16(void) {
  TEST_ASSERT_EQUAL(JH_STM32G474_PLL_FROM_HSI16,
                    jh_stm32g474_clock_tree_init());
  check_running_tree();
  TEST_ASSERT_EQUAL_UINT32(2u, cell(kRccPllcfgr) & 3u);
  TEST_ASSERT_EQUAL_UINT32(170000000u, JH_G474_CORE_CLOCK_HZ);
  TEST_ASSERT_FALSE(g.hse_requested);
  TEST_ASSERT_EQUAL_UINT32(2u, (cell(kRccCcipr) >> 24) & 3u); /* PCLK1 */
  TEST_ASSERT_EQUAL_UINT32(42500000u, JH_G474_ADC_CLOCK_HZ);
}

void test_hsi_left_off_by_a_bootloader_is_started_first(void) {
  /* SYSCLK on HSE, HSI16 off, PLL off. */
  cell(kRccCr) = HSEON | HSERDY;
  cell(kRccCfgr) = 2u | (2u << 2);
  TEST_ASSERT_EQUAL(JH_STM32G474_PLL_FROM_HSI16,
                    jh_stm32g474_clock_tree_init());
  check_running_tree();
}

void test_stop_exit_rebuilds_the_default_tree(void) {
  (void)jh_stm32g474_clock_tree_init();
  enter_stop();
  TEST_ASSERT_EQUAL(JH_STM32G474_PLL_FROM_HSI16,
                    jh_stm32g474_clock_tree_init());
  check_running_tree();
}

#endif

int main(void) {
  UNITY_BEGIN();
#if defined(HAL_STM32G474_CLOCK_HSE_160MHZ)
  RUN_TEST(test_hse_tree_runs_160mhz_with_fdcan_at_80mhz);
  RUN_TEST(test_dead_hse_falls_back_to_hsi_with_the_same_frequencies);
  RUN_TEST(test_stop_exit_rebuilds_the_hse_tree);
#else
  RUN_TEST(test_default_tree_runs_170mhz_from_hsi16);
  RUN_TEST(test_hsi_left_off_by_a_bootloader_is_started_first);
  RUN_TEST(test_stop_exit_rebuilds_the_default_tree);
#endif
  return UNITY_END();
}
