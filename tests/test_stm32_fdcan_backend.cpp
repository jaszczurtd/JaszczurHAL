// The STM32G474 native FDCAN backend on a register table. The G4 register
// offsets and message RAM layout are spelled out here from RM0440 and ST's
// stm32g474xx.h instead of being taken from port/stm32g474_regs.h, so a wrong
// map there fails these tests. Registers that do not exist on the G4 (or are
// read-only) hold a sentinel that the backend must leave alone.

#include "hal/gpio/hal_gpio.h"
#include "hal/impl/stm32g474/hal_can_stm32g474_fdcan.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"
#include "jh_stm32g474_host_regs.h"
#include "utils/unity.h"

#include <stdint.h>
#include <string.h>

// hal_can_util.cpp also holds facade helpers these tests never call.
extern "C" {
hal_can_t hal_can_create(const hal_can_config_t *) { return nullptr; }
bool hal_can_receive(hal_can_t, uint32_t *, uint8_t *, uint8_t *) {
  return false;
}
void hal_gpio_set_mode(uint8_t, hal_gpio_mode_t) {}
void hal_gpio_attach_interrupt(uint8_t, void (*)(void), hal_gpio_irq_mode_t) {}
void hal_delay_ms(uint32_t) {}
void hal_derr_limited(const char *, const char *, ...) {}
}

namespace {

constexpr uintptr_t kFdcan1 = 0x40006400u;
constexpr uintptr_t kSramCan = 0x4000A400u; /* SRAMCAN_BASE */
constexpr uint32_t kMramWords = 0x350u / 4u;

constexpr uintptr_t kCccr = 0x018u;
constexpr uintptr_t kPsr = 0x044u;
constexpr uintptr_t kEcr = 0x040u;
constexpr uintptr_t kRxgfc = 0x080u;
constexpr uintptr_t kXidam = 0x084u;
constexpr uintptr_t kHpms = 0x088u;
constexpr uintptr_t kRxf0s = 0x090u;
constexpr uintptr_t kRxf0a = 0x094u;
constexpr uintptr_t kTxbc = 0x0C0u;
constexpr uintptr_t kTxfqs = 0x0C4u;
constexpr uintptr_t kTxbar = 0x0CCu;
constexpr uintptr_t kTxbcr = 0x0D0u;
constexpr uintptr_t kTxbto = 0x0D4u;

constexpr uint32_t kStdFilterWord = 0u;
constexpr uint32_t kExtFilterWord = 0x070u / 4u;
constexpr uint32_t kRxFifo0Word = 0x0B0u / 4u;
constexpr uint32_t kTxBufferWord = 0x278u / 4u;
constexpr uint32_t kElementWords = 18u;

constexpr uint32_t kSentinel = 0xA5A5A5A5u;

volatile uint32_t &reg(uintptr_t offset) {
  return *jh_stm32g474_host_reg32(kFdcan1 + offset);
}

volatile uint32_t &mram(uint32_t word) {
  return *jh_stm32g474_host_reg32(kSramCan + (uintptr_t)word * 4u);
}

// Offsets that are reserved or read-only on the G4 but writable M_CAN
// configuration registers on STM32H7: SIDFC/XIDFC/RXF0C/RXF1C/RXESC land
// here, so does the old TXBAR, which is TXBCR (cancel) on the G4.
constexpr uintptr_t kUntouched[] = {kHpms,  0x0A0u, 0x0A4u, 0x0A8u,
                                    0x0B0u, 0x0BCu, kTxbcr};

hal_can_stm32g474_fdcan_t s_can;

void init_can(bool fd) {
  hal_can_stm32g474_fdcan_config_t cfg = {};
  cfg.arbitration_bitrate_hz = 500000u;
  cfg.data_bitrate_hz = fd ? 2000000u : 0u;
  cfg.enable_fd = fd;
  TEST_ASSERT_TRUE(hal_can_stm32g474_fdcan_init(&s_can, &cfg));
}

void assert_untouched(void) {
  for (uintptr_t offset : kUntouched) {
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(kSentinel, reg(offset),
                                    "backend wrote a non-G4 register");
  }
}

} // namespace

void setUp(void) {
  jh_stm32g474_host_regs_reset();
  memset(&s_can, 0, sizeof(s_can));
  for (uintptr_t offset : kUntouched) {
    reg(offset) = kSentinel;
  }
  // FDCAN2 message RAM right after FDCAN1, and where the H7 layout put it.
  mram(kMramWords) = kSentinel;
  *jh_stm32g474_host_reg32(0x4000AC00u) = kSentinel;
}

void tearDown(void) {}

void test_init_uses_only_g4_registers_and_own_message_ram(void) {
  for (uint32_t i = 0; i < kMramWords; ++i) {
    mram(i) = kSentinel;
  }
  init_can(true);

  assert_untouched();
  for (uint32_t i = 0; i < kMramWords; ++i) {
    TEST_ASSERT_EQUAL_HEX32(0u, mram(i));
  }
  TEST_ASSERT_EQUAL_HEX32(kSentinel, mram(kMramWords));
  TEST_ASSERT_EQUAL_HEX32(kSentinel, *jh_stm32g474_host_reg32(0x4000AC00u));
  TEST_ASSERT_EQUAL_HEX32(0x1FFFFFFFu, reg(kXidam));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(kRxgfc));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(kTxbc));              /* TX FIFO mode */
  TEST_ASSERT_EQUAL_HEX32(0u, reg(kCccr) & 0x1u);       /* left INIT */
  TEST_ASSERT_EQUAL_HEX32(0x300u, reg(kCccr) & 0x300u); /* FDOE | BRSE */
}

void test_send_writes_the_put_index_element_and_requests_it(void) {
  init_can(true);
  reg(kTxfqs) = 2u << 16; /* put index 2 */
  reg(kTxbto) = 1u << 2;  /* transmission occurred */

  hal_can_frame_t frame = {};
  frame.id = 0x1ABCDEFu;
  frame.flags = HAL_CAN_FRAME_EXTENDED | HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS;
  frame.dlc = 9u; /* 12 bytes */
  frame.len = 12u;
  for (uint8_t i = 0; i < frame.len; ++i) {
    frame.data[i] = (uint8_t)(0x10u + i);
  }
  TEST_ASSERT_TRUE(hal_can_stm32g474_fdcan_send_frame(&s_can, &frame));

  TEST_ASSERT_EQUAL_HEX32(1u << 2, reg(kTxbar));
  assert_untouched();
  const uint32_t elem = kTxBufferWord + 2u * kElementWords;
  TEST_ASSERT_EQUAL_HEX32((1u << 30) | 0x1ABCDEFu, mram(elem));
  TEST_ASSERT_EQUAL_HEX32((1u << 21) | (1u << 20) | (9u << 16),
                          mram(elem + 1u));
  TEST_ASSERT_EQUAL_HEX32(0x13121110u, mram(elem + 2u));
  TEST_ASSERT_EQUAL_HEX32(0x17161514u, mram(elem + 3u));
  TEST_ASSERT_EQUAL_HEX32(0x1B1A1918u, mram(elem + 4u));
}

void test_send_refuses_a_full_tx_fifo(void) {
  init_can(false);
  reg(kTxfqs) = 1u << 21; /* TFQF */
  hal_can_frame_t frame = {};
  frame.id = 0x123u;
  frame.dlc = 1u;
  frame.len = 1u;
  TEST_ASSERT_FALSE(hal_can_stm32g474_fdcan_send_frame(&s_can, &frame));
  TEST_ASSERT_EQUAL_HEX32(0u, reg(kTxbar));
}

void test_receive_reads_the_get_index_and_acknowledges_it(void) {
  init_can(false);
  TEST_ASSERT_FALSE(hal_can_stm32g474_fdcan_available(&s_can));

  const uint32_t elem = kRxFifo0Word + 1u * kElementWords;
  mram(elem) = 0x123u << 18;
  mram(elem + 1u) = 6u << 16;
  mram(elem + 2u) = 0x44332211u;
  mram(elem + 3u) = 0x00006655u;
  reg(kRxf0s) = (1u << 8) | 1u; /* get index 1, fill level 1 */
  TEST_ASSERT_TRUE(hal_can_stm32g474_fdcan_available(&s_can));

  hal_can_frame_t frame = {};
  TEST_ASSERT_TRUE(hal_can_stm32g474_fdcan_receive_frame(&s_can, &frame));
  TEST_ASSERT_EQUAL_HEX32(0x123u, frame.id);
  TEST_ASSERT_EQUAL_UINT8(6u, frame.len);
  const uint8_t expected[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame.data, sizeof(expected));
  TEST_ASSERT_EQUAL_HEX32(1u, reg(kRxf0a));
  assert_untouched();
}

void test_standard_filter_keeps_id_in_sfid1_and_mask_in_sfid2(void) {
  init_can(false);
  const hal_can_filter_t filter = {0x123u, 0x7F0u, 0u};
  TEST_ASSERT_TRUE(hal_can_stm32g474_fdcan_set_filter(&s_can, 0u, &filter));

  TEST_ASSERT_EQUAL_HEX32((2u << 30) | (1u << 27) | (0x123u << 16) | 0x7F0u,
                          mram(kStdFilterWord));
  TEST_ASSERT_EQUAL_HEX32((1u << 16) | (3u << 4) | (3u << 2), reg(kRxgfc));
  TEST_ASSERT_EQUAL_HEX32(0x1FFFFFFFu, reg(kXidam));
  assert_untouched();
}

void test_extended_filter_sets_its_list_size(void) {
  init_can(false);
  const hal_can_filter_t filter = {0x1234567u, 0x1FFFFF00u,
                                   HAL_CAN_FILTER_EXTENDED};
  TEST_ASSERT_TRUE(hal_can_stm32g474_fdcan_set_filter(&s_can, 1u, &filter));

  const uint32_t elem = kExtFilterWord + 2u;
  TEST_ASSERT_EQUAL_HEX32((1u << 29) | 0x1234567u, mram(elem));
  TEST_ASSERT_EQUAL_HEX32((2u << 30) | 0x1FFFFF00u, mram(elem + 1u));
  TEST_ASSERT_EQUAL_HEX32((2u << 24) | (3u << 4) | (3u << 2), reg(kRxgfc));
  assert_untouched();
}

void test_state_and_error_counters_come_from_psr_and_ecr(void) {
  init_can(false);
  reg(kPsr) = 1u << 7;
  reg(kEcr) = (0x55u << 8) | 0x81u;
  hal_can_state_t state = HAL_CAN_STATE_STOPPED;
  TEST_ASSERT_TRUE(hal_can_stm32g474_fdcan_get_state(&s_can, true, &state));
  TEST_ASSERT_EQUAL_INT(HAL_CAN_STATE_BUS_OFF, state);
  hal_can_error_counters_t counters = {};
  TEST_ASSERT_TRUE(
      hal_can_stm32g474_fdcan_get_error_counters(&s_can, &counters));
  TEST_ASSERT_EQUAL_UINT8(0x81u, counters.tx);
  TEST_ASSERT_EQUAL_UINT8(0x55u, counters.rx);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_uses_only_g4_registers_and_own_message_ram);
  RUN_TEST(test_send_writes_the_put_index_element_and_requests_it);
  RUN_TEST(test_send_refuses_a_full_tx_fifo);
  RUN_TEST(test_receive_reads_the_get_index_and_acknowledges_it);
  RUN_TEST(test_standard_filter_keeps_id_in_sfid1_and_mask_in_sfid2);
  RUN_TEST(test_extended_filter_sets_its_list_size);
  RUN_TEST(test_state_and_error_counters_come_from_psr_and_ecr);
  return UNITY_END();
}
