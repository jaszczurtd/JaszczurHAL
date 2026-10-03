#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <string.h>
#define private public
#include "hal/can/mcp2515/mcp2515_driver.h"
#undef private
#include "hal/can/mcp2515/hal_can_mcp2515.h"

/* The provider operations the facade calls; their context is a JHMCP2515. */
static const jh_can_provider_t &P = jh_can_mcp2515_provider;

/* MCP2515 datasheet (register map: TXBnSIDH/TXBnSIDL/TXBnEID8/TXBnEID0):
 * - Standard 11-bit ID uses SIDH[10:3] and SIDL[2:0] -> bits [2:0] shifted to
 * [7:5]
 * - Extended 29-bit ID uses SIDH/SIDL[1:0] + EXIDE + EID8 + EID0
 * - TXBnDLC bit6 is RTR, bits[3:0] are DLC
 *
 * Datasheet anchors used by these tests:
 * - Register 3-4 TXBnSIDL (EXIDE, SID[2:0], EID[17:16])
 * - Register 3-7 TXBnDLC (RTR + DLC[3:0])
 * - RXBnSIDL and RXBnCTRL (IDE/SRR and RXRTR flag semantics)
 */

static void assert_spi_tx_equals(const uint8_t *expected, size_t expected_len) {
  uint8_t tx[128] = {};
  size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
  TEST_ASSERT_EQUAL_size_t(expected_len, tx_len);
  for (size_t i = 0; i < expected_len; ++i) {
    TEST_ASSERT_EQUAL_UINT8(expected[i], tx[i]);
  }
}

static void assert_spi_tx_contains(const uint8_t *expected,
                                   size_t expected_len) {
  uint8_t tx[512] = {};
  const size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
  bool found = false;
  for (size_t offset = 0u; offset + expected_len <= tx_len; ++offset) {
    size_t matched = 0u;
    while (matched < expected_len &&
           tx[offset + matched] == expected[matched]) {
      ++matched;
    }
    if (matched == expected_len) {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(found);
}

static void push_set_filter_rx_script(void) {
  /* Successful CONFIG/NORMAL transitions for init_Mask and init_Filt. The
   * latter also performs two RXBnCTRL bit-modify transactions. */
  uint8_t rx_script[76] = {};
  rx_script[13] = MODE_CONFIG;
  rx_script[22] = MODE_CONFIG;
  rx_script[47] = MODE_CONFIG;
  rx_script[64] = MODE_CONFIG;
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));
}

static INT8U send_with_final_txctrl(JHMCP2515 *can, uint8_t txctrl,
                                    uint8_t canctrl = MODE_ONESHOT) {
  /* Empty standard frame: initial TXB0CTRL read, buffer writes, TXREQ bit
   * modify, final TXB0CTRL read, then CANCTRL when a failure flag is present.
   * Every register read returns its value on the third SPI byte. */
  uint8_t rx_script[24] = {};
  rx_script[2] = 0u;
  rx_script[20] = txctrl;
  rx_script[23] = canctrl;
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));
  return can->sendMsgBuf(0x123u, 0u, nullptr);
}

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0);
  hal_mock_set_micros(0);
  hal_mock_set_micros_step(0u);
}

void tearDown(void) {}

void test_set_gpo_uses_hal_spi_and_configures_cs_pin(void) {
  JHMCP2515 can(10u, 0u);

  TEST_ASSERT_EQUAL_UINT8(0u, can.setGPO(1u));
  TEST_ASSERT_TRUE(hal_mock_spi_is_initialized());
  TEST_ASSERT_EQUAL_UINT8(0u, hal_mock_spi_get_bus());
  TEST_ASSERT_EQUAL(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(10u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10u));

  uint8_t tx[16] = {};
  size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
  TEST_ASSERT_EQUAL_size_t(4u, tx_len);
  TEST_ASSERT_EQUAL_UINT8(MCP_BITMOD, tx[0]);
  TEST_ASSERT_EQUAL_UINT8(MCP_BFPCTRL, tx[1]);
  TEST_ASSERT_EQUAL_UINT8(MCP_BxBFS_MASK, tx[2]);
  TEST_ASSERT_EQUAL_UINT8(0x10u, tx[3]);
}

void test_write_id_standard_11bit_uses_expected_register_encoding(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();

  can.mcp2515_write_id((INT8U)(MCP_TXB0CTRL + 1u), 0u, 0x7DFu);

  const uint8_t expected[] = {
      MCP_WRITE, (uint8_t)(MCP_TXB0CTRL + 1u), 0xFBu, /* SIDH = 0x7DF >> 3 */
      0xE0u, /* SIDL = (0x7DF & 0x7) << 5 */
      0x00u, /* EID8 = 0 for standard frame */
      0x00u, /* EID0 = 0 for standard frame */
  };
  assert_spi_tx_equals(expected, sizeof(expected));
}

void test_write_id_extended_29bit_uses_expected_register_encoding(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();

  can.mcp2515_write_id((INT8U)(MCP_TXB0CTRL + 1u), 1u, 0x1ABCDE3u);

  const uint8_t expected[] = {
      MCP_WRITE, (uint8_t)(MCP_TXB0CTRL + 1u), 0x0Du, /* SIDH */
      0x4Bu, /* SIDL (includes EXIDE=1) */
      0xCDu, /* EID8 */
      0xE3u, /* EID0 */
  };
  assert_spi_tx_equals(expected, sizeof(expected));
}

void test_read_id_extended_29bit_decodes_from_raw_register_bytes(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();

  /* First two bytes are consumed by READ opcode/address transfers. */
  const uint8_t rx_script[] = {
      0x00u, 0x00u, 0x0Du, 0x4Bu, 0xCDu, 0xE3u,
  };
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));

  INT8U ext = 0u;
  INT32U id = 0u;
  can.mcp2515_read_id((INT8U)(MCP_TXB0CTRL + 1u), &ext, &id);

  TEST_ASSERT_EQUAL_UINT8(1u, ext);
  TEST_ASSERT_EQUAL_HEX32(0x1ABCDE3u, id);
}

void test_read_id_standard_11bit_decodes_from_raw_register_bytes(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();

  /* READ opcode/address consume first two scripted bytes. */
  const uint8_t rx_script[] = {
      0x00u, 0x00u, 0xFBu, 0xE0u, 0x00u, 0x00u,
  };
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));

  INT8U ext = 1u;
  INT32U id = 0u;
  can.mcp2515_read_id((INT8U)(MCP_TXB0CTRL + 1u), &ext, &id);

  TEST_ASSERT_EQUAL_UINT8(0u, ext);
  TEST_ASSERT_EQUAL_HEX32(0x7DFu, id);
}

void test_read_can_message_decodes_id_rtr_dlc_and_payload(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();

  /* MCP2515 datasheet framing: ID bytes in SIDH/SIDL/EID8/EID0, RTR in RXBnCTRL
   * bit3, DLC in RXBnDLC[3:0], data in RXBnDm. */
  const uint8_t rx_script[] = {
      0x00u, 0x00u, 0xFBu, 0xE0u, 0x00u, 0x00u, /* read_id: standard 0x7DF */
      0x00u, 0x00u, 0x08u,                      /* ctrl: RTR set */
      0x00u, 0x00u, 0xE0u,                      /* sidl: IDE=0, SRR=0 */
      0x00u, 0x00u, 0xF3u,                      /* dlc: upper bits set, len=3 */
      0x00u, 0x00u, 0xAAu, 0xBBu, 0xCCu,        /* payload bytes */
  };
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));

  can.mcp2515_read_canMsg(MCP_RXBUF_0);

  TEST_ASSERT_EQUAL_UINT8(0u, can.m_nExtFlg);
  TEST_ASSERT_EQUAL_HEX32(0x7DFu, can.m_nID);
  TEST_ASSERT_EQUAL_UINT8(1u, can.m_nRtr);
  TEST_ASSERT_EQUAL_UINT8(3u, can.m_nDlc);
  TEST_ASSERT_EQUAL_UINT8(0xAAu, can.m_nDta[0]);
  TEST_ASSERT_EQUAL_UINT8(0xBBu, can.m_nDta[1]);
  TEST_ASSERT_EQUAL_UINT8(0xCCu, can.m_nDta[2]);
}

void test_read_can_message_standard_remote_uses_srr_when_ide_zero(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();

  /* RXBnSIDL[4]=SRR is valid for standard frames (IDE=0). */
  const uint8_t rx_script[] = {
      0x00u, 0x00u, 0xFBu, 0xF0u,
      0x00u, 0x00u,               /* read_id: standard 0x7DF, SRR=1 */
      0x00u, 0x00u, 0x00u,        /* ctrl: RXRTR=0 */
      0x00u, 0x00u, 0xF0u,        /* sidl: IDE=0, SRR=1 */
      0x00u, 0x00u, 0x02u,        /* dlc = 2 */
      0x00u, 0x00u, 0xDEu, 0xADu, /* payload */
  };
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));

  can.mcp2515_read_canMsg(MCP_RXBUF_0);

  TEST_ASSERT_EQUAL_UINT8(0u, can.m_nExtFlg);
  TEST_ASSERT_EQUAL_HEX32(0x7DFu, can.m_nID);
  TEST_ASSERT_EQUAL_UINT8(1u, can.m_nRtr);
  TEST_ASSERT_EQUAL_UINT8(2u, can.m_nDlc);
  TEST_ASSERT_EQUAL_UINT8(0xDEu, can.m_nDta[0]);
  TEST_ASSERT_EQUAL_UINT8(0xADu, can.m_nDta[1]);
}

void test_read_can_message_does_not_treat_srr_as_rtr_for_extended_frame(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();

  /* IDE=1 (extended) with bit4 high must not force RTR when RXRTR=0. */
  const uint8_t rx_script[] = {
      0x00u, 0x00u, 0x0Du, 0x5Bu, 0xCDu, 0xE3u, /* read_id: extended */
      0x00u, 0x00u, 0x00u,                      /* ctrl: RXRTR=0 */
      0x00u, 0x00u, 0x5Bu,                      /* sidl: IDE=1, bit4=1 */
      0x00u, 0x00u, 0x01u,                      /* dlc = 1 */
      0x00u, 0x00u, 0x99u,                      /* payload */
  };
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));

  can.mcp2515_read_canMsg(MCP_RXBUF_0);

  TEST_ASSERT_EQUAL_UINT8(1u, can.m_nExtFlg);
  TEST_ASSERT_EQUAL_HEX32(0x1ABCDE3u, can.m_nID);
  TEST_ASSERT_EQUAL_UINT8(0u, can.m_nRtr);
  TEST_ASSERT_EQUAL_UINT8(1u, can.m_nDlc);
  TEST_ASSERT_EQUAL_UINT8(0x99u, can.m_nDta[0]);
}

void test_write_can_message_sets_rtr_bit_and_keeps_dlc_low_nibble(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();

  INT8U payload[MAX_CHAR_IN_MESSAGE] = {0x11u, 0x22u, 0x33u, 0x44u,
                                        0x55u, 0x66u, 0x77u, 0x88u};
  TEST_ASSERT_EQUAL_UINT8(MCP2515_OK, can.setMsg(0x123u, 1u, 0u, 3u, payload));

  can.mcp2515_write_canMsg((INT8U)(MCP_TXB0CTRL + 1u));

  const uint8_t expected[] = {
      MCP_WRITE,
      (uint8_t)(MCP_TXB0CTRL + 6u),
      0x11u,
      0x22u,
      0x33u,
      MCP_WRITE,
      (uint8_t)(MCP_TXB0CTRL + 5u),
      (uint8_t)(0x03u | MCP_RTR_MASK),
      MCP_WRITE,
      (uint8_t)(MCP_TXB0CTRL + 1u),
      0x24u,
      0x60u,
      0x00u,
      0x00u,
  };
  assert_spi_tx_equals(expected, sizeof(expected));
}

void test_set_msg_clamps_dlc_to_8_and_write_does_not_overflow_payload(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();

  INT8U payload[10] = {0x10u, 0x11u, 0x12u, 0x13u, 0x14u,
                       0x15u, 0x16u, 0x17u, 0x18u, 0x19u};
  TEST_ASSERT_EQUAL_UINT8(MCP2515_OK,
                          can.setMsg(0x321u, 0u, 0u, 0x0Fu, payload));
  TEST_ASSERT_EQUAL_UINT8(8u, can.m_nDlc);

  can.mcp2515_write_canMsg((INT8U)(MCP_TXB0CTRL + 1u));

  const uint8_t expected[] = {
      MCP_WRITE,
      (uint8_t)(MCP_TXB0CTRL + 6u),
      0x10u,
      0x11u,
      0x12u,
      0x13u,
      0x14u,
      0x15u,
      0x16u,
      0x17u,
      MCP_WRITE,
      (uint8_t)(MCP_TXB0CTRL + 5u),
      0x08u,
      MCP_WRITE,
      (uint8_t)(MCP_TXB0CTRL + 1u),
      0x64u,
      0x20u,
      0x00u,
      0x00u,
  };
  assert_spi_tx_equals(expected, sizeof(expected));
}

void test_backend_set_filter_enables_filters_on_both_receive_buffers(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();
  push_set_filter_rx_script();
  const hal_can_filter_t filter = {0x127u, HAL_CAN_STD_ID_MASK, 0u};

  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(&can, 0u, &filter));

  const uint8_t enable_rxb0_filters[] = {MCP_BITMOD, MCP_RXB0CTRL,
                                         MCP_RXB_RX_MASK, MCP_RXB_RX_STDEXT};
  const uint8_t enable_rxb1_filters[] = {MCP_BITMOD, MCP_RXB1CTRL,
                                         MCP_RXB_RX_MASK, MCP_RXB_RX_STDEXT};
  assert_spi_tx_contains(enable_rxb0_filters, sizeof(enable_rxb0_filters));
  assert_spi_tx_contains(enable_rxb1_filters, sizeof(enable_rxb1_filters));
}

void test_one_shot_configuration_reports_readback_failure(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();
  const uint8_t rx_script[7] = {};
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));

  TEST_ASSERT_EQUAL_UINT8(CAN_FAIL, can.enOneShotTX());
}

void test_set_mode_caches_confirmed_hardware_mode(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();
  uint8_t rx_script[14] = {};
  rx_script[13] = MCP_LOOPBACK;
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));

  TEST_ASSERT_EQUAL_UINT8(CAN_OK, can.setMode(MCP_LOOPBACK));
  TEST_ASSERT_EQUAL_UINT8(MCP_LOOPBACK, can.mcpMode);
}

void test_send_reports_success_when_txreq_clears_without_error_flags(void) {
  JHMCP2515 can(10u, 0u);

  TEST_ASSERT_EQUAL_UINT8(CAN_OK, send_with_final_txctrl(&can, 0u));
}

void test_send_reports_failure_when_one_shot_sets_abtf(void) {
  JHMCP2515 can(10u, 0u);

  TEST_ASSERT_EQUAL_UINT8(CAN_FAILTX,
                          send_with_final_txctrl(&can, MCP_TXB_ABTF_M));
}

void test_send_reports_failure_when_one_shot_sets_mloa(void) {
  JHMCP2515 can(10u, 0u);

  TEST_ASSERT_EQUAL_UINT8(CAN_FAILTX,
                          send_with_final_txctrl(&can, MCP_TXB_MLOA_M));
}

void test_send_reports_failure_when_one_shot_sets_txerr(void) {
  JHMCP2515 can(10u, 0u);

  TEST_ASSERT_EQUAL_UINT8(CAN_FAILTX,
                          send_with_final_txctrl(&can, MCP_TXB_TXERR_M));
}

void test_send_accepts_successful_normal_mode_retry_with_latched_errors(void) {
  JHMCP2515 can(10u, 0u);

  TEST_ASSERT_EQUAL_UINT8(
      CAN_OK, send_with_final_txctrl(&can, MCP_TXB_MLOA_M | MCP_TXB_TXERR_M,
                                     MCP_NORMAL));
}

void test_backend_send_propagates_one_shot_tx_failure(void) {
  JHMCP2515 can(10u, 0u);

  uint8_t rx_script[24] = {};
  rx_script[2] = 0u;
  rx_script[20] = MCP_TXB_ABTF_M | MCP_TXB_MLOA_M | MCP_TXB_TXERR_M;
  rx_script[23] = MODE_ONESHOT;
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));

  TEST_ASSERT_EQUAL_INT(HAL_EIO, P.legacy_send(&can, 0x123u, 0u, nullptr));
}

/* Datasheet 3.3 "Aborting transmission": clearing TXBnCTRL.TXREQ requests the
 * abort of a pending frame. Without it a frame reported as failed after the
 * send timeout would still be transmitted later. */
void test_send_timeout_clears_txreq_of_the_stuck_buffer(void) {
  JHMCP2515 can(10u, 0u);
  uint8_t rx_script[200];
  memset(rx_script, MCP_TXB_TXREQ_M, sizeof(rx_script));
  memset(rx_script, 0, 18); /* free TXB0, then the frame writes */
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));
  hal_mock_set_micros_step(100u);

  TEST_ASSERT_EQUAL_UINT8(CAN_SENDMSGTIMEOUT,
                          can.sendMsgBuf(0x123u, 0u, nullptr));

  const uint8_t clear_txreq[] = {MCP_BITMOD, MCP_TXB0CTRL, MCP_TXB_TXREQ_M,
                                 0x00u};
  assert_spi_tx_contains(clear_txreq, sizeof(clear_txreq));
}

/* A send that was preempted longer than the timeout but finds TXREQ already
 * clear went out; reporting a failure would make the caller send it twice. */
void test_send_after_long_preemption_reports_success_when_txreq_cleared(void) {
  JHMCP2515 can(10u, 0u);
  uint8_t rx_script[24] = {};
  rx_script[23] = MODE_ONESHOT;
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));
  hal_mock_set_micros_step(3000u); /* every clock read jumps past 2.5 ms */

  TEST_ASSERT_EQUAL_UINT8(CAN_OK, can.sendMsgBuf(0x123u, 0u, nullptr));
}

/* Register 10-1 CANCTRL.ABAT: while set, every pending and later transmission
 * is aborted; it has to be cleared once the buffers dropped TXREQ. */
void test_abort_tx_releases_abat_after_buffers_drop_txreq(void) {
  JHMCP2515 can(10u, 0u);
  const uint8_t rx_script[17] = {}; /* every TXBnCTRL read: TXREQ clear */
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));

  TEST_ASSERT_EQUAL_UINT8(CAN_OK, can.abortTX());

  const uint8_t expected[] = {
      MCP_BITMOD, MCP_CANCTRL,  ABORT_TX, ABORT_TX, /* request abort */
      MCP_READ,   MCP_TXB0CTRL, 0x00u,              /* TXREQ clear */
      MCP_READ,   MCP_TXB1CTRL, 0x00u,              //
      MCP_READ,   MCP_TXB2CTRL, 0x00u,              //
      MCP_BITMOD, MCP_CANCTRL,  ABORT_TX, 0x00u};   /* release ABAT */
  assert_spi_tx_equals(expected, sizeof(expected));
}

void test_abort_tx_releases_abat_even_when_a_buffer_stays_pending(void) {
  JHMCP2515 can(10u, 0u);
  uint8_t rx_script[400];
  memset(rx_script, MCP_TXB_TXREQ_M, sizeof(rx_script));
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));
  hal_mock_set_micros_step(100u);

  TEST_ASSERT_EQUAL_UINT8(CAN_FAIL, can.abortTX());

  uint8_t tx[1024] = {};
  const size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
  const uint8_t release[] = {MCP_BITMOD, MCP_CANCTRL, ABORT_TX, 0x00u};
  TEST_ASSERT_TRUE(tx_len >= sizeof(release));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(release, &tx[tx_len - sizeof(release)],
                                sizeof(release));
}

/* ── Statuses of the provider operations ──────────────────────────────── */

/* A controller that answers every byte with 0xFF (no chip on the bus, MISO
 * pulled up) while time passes, so mode requests run into their 200 ms. */
static void silent_edge(void *) {}
static uint8_t silent_exchange(void *, uint8_t) {
  hal_mock_set_millis(hal_millis() + 1u);
  return 0xFFu;
}
static const hal_mock_spi_device_t kSilentChip = {silent_edge, silent_exchange,
                                                  silent_edge};

static hal_can_config_t fiesta_config(void) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_MCP2515;
  cfg.mcp2515.cs_pin = 10u;
  cfg.mcp2515.bitrate_hz = 500000u;
  cfg.mcp2515.oscillator_hz = 8000000u;
  cfg.mcp2515.one_shot_tx = true;
  return cfg;
}

/* Context storage like the facade's; a failed init leaves it destroyed. */
alignas(JHMCP2515) static unsigned char s_ctx[sizeof(JHMCP2515)];

static hal_status_t provider_init(const hal_can_config_t &cfg) {
  jh_can_caps_t caps = {};
  hal_can_mode_t mode = HAL_CAN_MODE_NORMAL;
  const hal_status_t st = P.init(s_ctx, &cfg, &caps, &mode);
  if (st == HAL_OK) {
    P.deinit(s_ctx);
  }
  return st;
}

/* Register file of an MCP2515 that answers READ, WRITE and BIT MODIFY,
 * resets into configuration mode and reports in CANSTAT the operating mode
 * CANCTRL requests. With stuck_osm the one-shot bit (CANCTRL.OSM) never
 * reads back set. */
struct RegisterChip {
  uint8_t regs[128];
  uint8_t instruction;
  uint8_t address;
  uint8_t mask;
  uint8_t count;
  bool stuck_osm;
};

static void chip_write(RegisterChip *chip, uint8_t addr, uint8_t value) {
  addr &= 0x7Fu;
  chip->regs[addr] = value;
  if (addr == MCP_CANCTRL) {
    if (chip->stuck_osm) {
      chip->regs[MCP_CANCTRL] &= (uint8_t)~MODE_ONESHOT;
    }
    chip->regs[MCP_CANSTAT] = (uint8_t)(value & MODE_MASK);
  }
}

static void chip_select(void *user) {
  RegisterChip *chip = static_cast<RegisterChip *>(user);
  chip->count = 0u;
}

static uint8_t chip_exchange(void *user, uint8_t mosi) {
  RegisterChip *chip = static_cast<RegisterChip *>(user);
  const uint8_t index = chip->count++;
  if (index == 0u) {
    chip->instruction = mosi;
    if (mosi == MCP_RESET) {
      memset(chip->regs, 0, sizeof(chip->regs));
      chip->regs[MCP_CANCTRL] = MODE_CONFIG;
      chip->regs[MCP_CANSTAT] = MODE_CONFIG;
    }
    return 0u;
  }
  if (index == 1u) {
    chip->address = mosi;
    return 0u;
  }
  switch (chip->instruction) {
  case MCP_READ:
    return chip->regs[chip->address++ & 0x7Fu];
  case MCP_WRITE:
    chip_write(chip, chip->address++, mosi);
    return 0u;
  case MCP_BITMOD:
    if (index == 2u) {
      chip->mask = mosi;
    } else if (index == 3u) {
      const uint8_t old = chip->regs[chip->address & 0x7Fu];
      chip_write(chip, chip->address,
                 (uint8_t)((old & ~chip->mask) | (mosi & chip->mask)));
    }
    return 0u;
  default:
    return 0u;
  }
}

static void chip_deselect(void *) {}

static const hal_mock_spi_device_t kRegisterChip = {chip_select, chip_exchange,
                                                    chip_deselect};

void test_init_reaches_normal_mode_with_one_shot_on_a_working_chip(void) {
  static RegisterChip chip = {};
  hal_mock_spi_reset();
  hal_mock_spi_attach_device(0u, 10u, &kRegisterChip, &chip);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider_init(fiesta_config()));
  TEST_ASSERT_EQUAL_HEX8(MCP_NORMAL, chip.regs[MCP_CANSTAT] & MODE_MASK);
  TEST_ASSERT_EQUAL_HEX8(MODE_ONESHOT, chip.regs[MCP_CANCTRL] & MODE_ONESHOT);
}

void test_init_fails_when_one_shot_does_not_read_back(void) {
  static RegisterChip chip = {};
  chip.stuck_osm = true;
  hal_mock_spi_reset();
  hal_mock_spi_attach_device(0u, 10u, &kRegisterChip, &chip);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider_init(fiesta_config()));
}

void test_init_reports_a_bitrate_without_timing_before_touching_spi(void) {
  hal_can_config_t cfg = fiesta_config();
  cfg.mcp2515.bitrate_hz = 300000u;
  hal_mock_spi_reset();
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, provider_init(cfg));
  cfg.mcp2515.bitrate_hz = 500000u;
  cfg.mcp2515.oscillator_hz = 12000000u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, provider_init(cfg));
  uint8_t tx[4] = {};
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
}

void test_init_of_a_controller_that_does_not_answer_is_an_io_error(void) {
  hal_mock_spi_reset();
  hal_mock_spi_attach_device(0u, 10u, &kSilentChip, nullptr);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider_init(fiesta_config()));
}

void test_a_mode_the_controller_never_reaches_is_a_timeout(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();
  hal_mock_spi_attach_device(0u, 10u, &kSilentChip, nullptr);
  /* The one-shot bit reads back set (0xFF); CANSTAT never shows NORMAL. */
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        P.apply_mode(&can, HAL_CAN_MODE_ONE_SHOT));
  const hal_can_filter_t filter = {0x127u, HAL_CAN_STD_ID_MASK, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, P.set_filter(&can, 0u, &filter));
  /* The first failed mode request ends the call: one 200 ms wait, not one
   * per mask and filter. */
  const uint32_t started = hal_millis();
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, P.set_std_filters(&can, 0x7E0u, 0x7DFu));
  TEST_ASSERT_TRUE(hal_millis() - started < 400u);
  hal_mock_set_micros_step(100u); /* the abort wait before the mode change */
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, P.stop(&can));
}

void test_a_one_shot_bit_that_does_not_read_back_is_an_io_error(void) {
  JHMCP2515 can(10u, 0u);
  hal_mock_spi_reset();
  hal_mock_spi_attach_device(0u, 10u, &kSilentChip, nullptr);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, P.apply_mode(&can, HAL_CAN_MODE_NORMAL));
}

void test_send_with_every_transmit_buffer_taken_is_busy(void) {
  JHMCP2515 can(10u, 0u);
  uint8_t rx_script[600];
  memset(rx_script, MCP_TXB_TXREQ_M, sizeof(rx_script));
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));
  hal_mock_set_micros_step(100u);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, P.legacy_send(&can, 0x123u, 0u, nullptr));
}

void test_send_of_a_frame_that_never_leaves_is_a_timeout(void) {
  JHMCP2515 can(10u, 0u);
  uint8_t rx_script[200];
  memset(rx_script, MCP_TXB_TXREQ_M, sizeof(rx_script));
  memset(rx_script, 0, 18); /* free TXB0, then the frame writes */
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));
  hal_mock_set_micros_step(100u);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, P.legacy_send(&can, 0x123u, 0u, nullptr));
  const hal_can_frame_t frame = {0x123u, 0u, 0u, 0u, {}};
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx_script, sizeof(rx_script));
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, P.send_frame(&can, &frame));
}

void test_receive_without_a_frame_says_try_again(void) {
  JHMCP2515 can(10u, 0u);
  const uint8_t empty[8] = {}; /* READ STATUS: no RXnIF */
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, empty, sizeof(empty));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, P.available(&can));
  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[HAL_CAN_MAX_DATA_LEN] = {};
  hal_mock_spi_push_rx(0u, empty, sizeof(empty));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, P.legacy_receive(&can, &id, &len, data));
  hal_can_frame_t frame = {};
  hal_mock_spi_push_rx(0u, empty, sizeof(empty));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, P.receive_frame(&can, &frame));
  const uint8_t waiting[2] = {0x00u, MCP_STAT_RX0IF};
  hal_mock_spi_push_rx(0u, waiting, sizeof(waiting));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.available(&can));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_set_gpo_uses_hal_spi_and_configures_cs_pin);
  RUN_TEST(test_write_id_standard_11bit_uses_expected_register_encoding);
  RUN_TEST(test_write_id_extended_29bit_uses_expected_register_encoding);
  RUN_TEST(test_read_id_extended_29bit_decodes_from_raw_register_bytes);
  RUN_TEST(test_read_id_standard_11bit_decodes_from_raw_register_bytes);
  RUN_TEST(test_read_can_message_decodes_id_rtr_dlc_and_payload);
  RUN_TEST(test_read_can_message_standard_remote_uses_srr_when_ide_zero);
  RUN_TEST(test_read_can_message_does_not_treat_srr_as_rtr_for_extended_frame);
  RUN_TEST(test_write_can_message_sets_rtr_bit_and_keeps_dlc_low_nibble);
  RUN_TEST(test_set_msg_clamps_dlc_to_8_and_write_does_not_overflow_payload);
  RUN_TEST(test_backend_set_filter_enables_filters_on_both_receive_buffers);
  RUN_TEST(test_one_shot_configuration_reports_readback_failure);
  RUN_TEST(test_set_mode_caches_confirmed_hardware_mode);
  RUN_TEST(test_send_reports_success_when_txreq_clears_without_error_flags);
  RUN_TEST(test_send_reports_failure_when_one_shot_sets_abtf);
  RUN_TEST(test_send_reports_failure_when_one_shot_sets_mloa);
  RUN_TEST(test_send_reports_failure_when_one_shot_sets_txerr);
  RUN_TEST(test_send_accepts_successful_normal_mode_retry_with_latched_errors);
  RUN_TEST(test_backend_send_propagates_one_shot_tx_failure);
  RUN_TEST(test_send_timeout_clears_txreq_of_the_stuck_buffer);
  RUN_TEST(test_send_after_long_preemption_reports_success_when_txreq_cleared);
  RUN_TEST(test_abort_tx_releases_abat_after_buffers_drop_txreq);
  RUN_TEST(test_abort_tx_releases_abat_even_when_a_buffer_stays_pending);
  RUN_TEST(test_init_reaches_normal_mode_with_one_shot_on_a_working_chip);
  RUN_TEST(test_init_fails_when_one_shot_does_not_read_back);
  RUN_TEST(test_init_reports_a_bitrate_without_timing_before_touching_spi);
  RUN_TEST(test_init_of_a_controller_that_does_not_answer_is_an_io_error);
  RUN_TEST(test_a_mode_the_controller_never_reaches_is_a_timeout);
  RUN_TEST(test_a_one_shot_bit_that_does_not_read_back_is_an_io_error);
  RUN_TEST(test_send_with_every_transmit_buffer_taken_is_busy);
  RUN_TEST(test_send_of_a_frame_that_never_leaves_is_a_timeout);
  RUN_TEST(test_receive_without_a_frame_says_try_again);
  return UNITY_END();
}
