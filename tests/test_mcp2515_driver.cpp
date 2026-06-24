#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"
#define private public
#include "hal/impl/shared/drivers/mcp2515/mcp2515_driver.h"
#undef private

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

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0);
  hal_mock_set_micros(0);
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
  return UNITY_END();
}
