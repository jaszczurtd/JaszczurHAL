// A build with HAL_CAN_TX_QUEUE_LEN 0: a controller that can queue
// (hal_mock_can_set_queued, like the native FDCAN backend) stays synchronous
// instead of refusing every queued send.

#include "hal/can/hal_can.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <vector>

static_assert(HAL_CAN_TX_QUEUE_LEN == 0, "built with a zero-length TX queue");

namespace {

std::vector<hal_can_tx_event_t> g_tx;
hal_can_t s_can;

void on_tx(hal_can_t, const hal_can_tx_event_t *event, void *) {
  g_tx.push_back(*event);
}

} // namespace

void setUp(void) {
  g_tx.clear();
  s_can = NULL;
}

void tearDown(void) {
  if (s_can != NULL) {
    hal_can_destroy(s_can);
  }
}

void test_a_queueing_controller_without_a_transmit_queue_sends_at_once(void) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_MCP251XFD;
  cfg.mcp251xfd.cs_pin = 10u;
  cfg.mcp251xfd.arbitration_bitrate_hz = 500000u;
  cfg.mcp251xfd.data_bitrate_hz = 2000000u;
  cfg.mcp251xfd.oscillator_hz = 40000000u;
  cfg.mcp251xfd.enable_fd = true;
  hal_mock_can_set_queued(true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_create(&cfg, &s_can));
  hal_mock_can_set_queued(false);

  hal_can_caps_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_caps(s_can, &caps));
  TEST_ASSERT_EQUAL_HEX8(
      0u, caps.features & (HAL_CAN_CAP_TX_EVENTS | HAL_CAN_CAP_RX_QUEUE));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_set_callbacks(s_can, NULL, on_tx, NULL, NULL));

  hal_can_frame_t f = {};
  f.id = 0x123u;
  f.len = 8u;
  f.dlc = 8u;
  uint32_t tag = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame_ex(s_can, &f, 0u, &tag));
  hal_can_frame_t sent = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_can_get_sent_frame(s_can, &sent));
  TEST_ASSERT_EQUAL_HEX32(0x123u, sent.id);
  TEST_ASSERT_EQUAL_INT(1, hal_can_service(s_can, 0));
  TEST_ASSERT_EQUAL_UINT32(tag, g_tx[0].tag);
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_DONE, g_tx[0].reason);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_queueing_controller_without_a_transmit_queue_sends_at_once);
  return UNITY_END();
}
