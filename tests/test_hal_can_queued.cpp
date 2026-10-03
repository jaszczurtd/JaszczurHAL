// The status-returning CAN API through the real facade and the mock
// controller: queued channels (hal_mock_can_set_queued, like the native
// FDCAN backend) and synchronous ones (the SPI controllers).

#include "hal/can/hal_can.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <string.h>
#include <vector>

namespace {

struct Seen {
  std::vector<hal_can_tx_event_t> tx;
  std::vector<uint32_t> rx_ids;
  std::vector<uint8_t> rx_filters;
  std::vector<hal_can_state_t> states;
  int notifies = 0;
  hal_can_t resend_on = nullptr; /* tx callback sends again on this handle */
} g;

hal_can_t s_can;

hal_can_config_t fd_config(void) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_MCP251XFD;
  cfg.mcp251xfd.cs_pin = 10u;
  cfg.mcp251xfd.arbitration_bitrate_hz = 500000u;
  cfg.mcp251xfd.data_bitrate_hz = 2000000u;
  cfg.mcp251xfd.oscillator_hz = 40000000u;
  cfg.mcp251xfd.enable_fd = true;
  return cfg;
}

hal_can_frame_t frame(uint32_t id, uint8_t len = 8u, uint8_t flags = 0u) {
  hal_can_frame_t f = {};
  f.id = id;
  f.flags = flags;
  f.len = len;
  f.dlc = hal_can_bytes_to_dlc(len);
  for (uint8_t i = 0; i < len; ++i) {
    f.data[i] = (uint8_t)(id + i);
  }
  return f;
}

void on_tx(hal_can_t h, const hal_can_tx_event_t *event, void *) {
  g.tx.push_back(*event);
  if (g.resend_on == h) {
    g.resend_on = nullptr;
    const hal_can_frame_t again = frame(0x7FFu);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame_ex(h, &again, 0u, NULL));
  }
}

void on_rx(hal_can_t, const hal_can_frame_t *f, const hal_can_rx_info_t *info,
           void *) {
  g.rx_ids.push_back(f->id);
  g.rx_filters.push_back(info->filter_index);
}

void on_state(hal_can_t, hal_can_state_t state,
              const hal_can_error_counters_t *, void *) {
  g.states.push_back(state);
}

void notify(void *user) { ++*static_cast<int *>(user); }

hal_can_t create_queued(void) {
  hal_mock_can_set_queued(true);
  const hal_can_config_t cfg = fd_config();
  hal_can_t h = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_create(&cfg, &h));
  hal_mock_can_set_queued(false);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_set_callbacks(h, on_rx, on_tx, on_state, NULL));
  return h;
}

hal_can_status_t status(hal_can_t h) {
  hal_can_status_t st = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_status(h, &st));
  return st;
}

/* Software matches of a valid frame against a valid filter. */
bool can_matches_ex(const hal_can_frame_t *frame,
                    const hal_can_filter_ex_t *filter) {
  bool matches = false;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_can_frame_matches_filter_ex(frame, filter, &matches));
  return matches;
}

bool can_matches(const hal_can_frame_t *frame, const hal_can_filter_t *filter) {
  bool matches = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_frame_matches_filter(frame, filter, &matches));
  return matches;
}

} // namespace

void setUp(void) {
  hal_mock_set_millis(0u);
  hal_mock_can_set_queued(false);
  g = Seen();
  s_can = NULL;
}

void tearDown(void) {
  if (s_can != NULL) {
    hal_can_destroy(s_can);
  }
}

/* ── Queued channels ──────────────────────────────────────────────────── */

void test_a_queued_channel_reports_its_capabilities(void) {
  s_can = create_queued();
  hal_can_caps_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_caps(s_can, &caps));
  TEST_ASSERT_EQUAL_HEX8(HAL_CAN_CAP_FD | HAL_CAN_CAP_TX_EVENTS |
                             HAL_CAN_CAP_RX_QUEUE | HAL_CAN_CAP_MANUAL_RECOVERY,
                         caps.features);
  TEST_ASSERT_TRUE((caps.modes & HAL_CAN_MODE_MANUAL_RECOVERY) != 0u);
}

void test_queued_sends_report_their_outcome_with_their_tag(void) {
  s_can = create_queued();
  uint32_t tags[3] = {};
  for (uint32_t i = 0; i < 3u; ++i) {
    const hal_can_frame_t f = frame(0x100u + i);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_can_send_frame_ex(s_can, &f, 0u, &tags[i]));
  }
  hal_mock_can_fail_sends(s_can, 1u);
  const hal_can_frame_t failing = frame(0x200u);
  uint32_t failing_tag = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_can_send_frame_ex(s_can, &failing, 0u, &failing_tag));

  TEST_ASSERT_EQUAL_INT(4, hal_can_service(s_can, 0));
  TEST_ASSERT_EQUAL_size_t(4u, g.tx.size());
  for (uint32_t i = 0; i < 3u; ++i) {
    TEST_ASSERT_EQUAL_UINT32(tags[i], g.tx[i].tag);
    TEST_ASSERT_EQUAL_INT(HAL_OK, g.tx[i].result);
    TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_DONE, g.tx[i].reason);
  }
  TEST_ASSERT_EQUAL_UINT32(failing_tag, g.tx[3].tag);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, g.tx[3].result);
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_FAILED, g.tx[3].reason);
  TEST_ASSERT_TRUE(tags[0] != 0u && tags[1] == tags[0] + 1u);
  hal_can_frame_t sent;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_can_get_sent_frame(s_can, &sent));
  TEST_ASSERT_EQUAL_HEX32(0x100u, sent.id);
  const hal_can_status_t st = status(s_can);
  TEST_ASSERT_EQUAL_UINT32(3u, st.tx_frames);
  TEST_ASSERT_EQUAL_UINT32(1u, st.tx_failed);
}

void test_received_frames_overflowing_the_queue_are_counted_exactly(void) {
  s_can = create_queued();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_set_isr_notify(s_can, notify, &g.notifies));
  for (uint32_t i = 0; i < HAL_CAN_RX_QUEUE_LEN + 4u; ++i) {
    const hal_can_frame_t f = frame(0x300u + i);
    hal_mock_can_inject_frame(s_can, &f);
  }
  TEST_ASSERT_EQUAL_INT(HAL_CAN_RX_QUEUE_LEN + 4, g.notifies);
  TEST_ASSERT_EQUAL_INT(HAL_CAN_RX_QUEUE_LEN, hal_can_service(s_can, 0));
  TEST_ASSERT_EQUAL_size_t(HAL_CAN_RX_QUEUE_LEN, g.rx_ids.size());
  for (uint32_t i = 0; i < HAL_CAN_RX_QUEUE_LEN; ++i) {
    TEST_ASSERT_EQUAL_HEX32(0x300u + i, g.rx_ids[i]);
  }
  const hal_can_status_t st = status(s_can);
  TEST_ASSERT_EQUAL_UINT32(HAL_CAN_RX_QUEUE_LEN, st.rx_frames);
  TEST_ASSERT_EQUAL_UINT32(4u, st.rx_queue_overflow);
}

void test_received_frames_carry_their_filter_slot(void) {
  s_can = create_queued();
  const hal_can_filter_t f2 = {0x123u, 0x7FFu, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_set_filter(s_can, 2u, &f2));
  const hal_can_frame_t hit = frame(0x123u);
  const hal_can_frame_t miss = frame(0x124u);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_can_available(s_can));
  hal_mock_can_inject_frame(s_can, &miss);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_can_available(s_can)); /* filtered */
  hal_mock_can_inject_frame(s_can, &hit);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_available(s_can));
  hal_can_frame_t rx;
  hal_can_rx_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_receive_frame_ex(s_can, &rx, &info, 0u));
  TEST_ASSERT_EQUAL_HEX32(0x123u, rx.id);
  TEST_ASSERT_EQUAL_UINT8(2u, info.filter_index);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        hal_can_receive_frame_ex(s_can, &rx, &info, 0u));
}

void test_a_classic_read_consumes_and_counts_an_fd_frame(void) {
  s_can = create_queued();
  const hal_can_frame_t fd = frame(0x10u, 12u, HAL_CAN_FRAME_FD);
  const hal_can_frame_t classic = frame(0x11u, 3u);
  hal_mock_can_inject_frame(s_can, &fd);
  hal_mock_can_inject_frame(s_can, &classic);
  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[8];
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_can_receive(s_can, &id, &len, data));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_receive(s_can, &id, &len, data));
  TEST_ASSERT_EQUAL_HEX32(0x11u, id);
  TEST_ASSERT_EQUAL_UINT32(1u, status(s_can).rx_dropped_fd_on_classic_read);
}

/* Fill the model's bus so the next frames stay in the transmit queue, then
 * queue @p count frames with IDs from @p first.id up; tags go to @p tags. */
static void queue_behind_a_full_bus(const hal_can_frame_t &first,
                                    uint32_t count, uint32_t *tags) {
  for (int i = 0; i < MOCK_CAN_BUF_SIZE; ++i) {
    const hal_can_frame_t f = frame(0x400u);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame_ex(s_can, &f, 0u, NULL));
  }
  for (uint32_t i = 0; i < count; ++i) {
    hal_can_frame_t f = first;
    f.id += i;
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_can_send_frame_ex(s_can, &f, 0u, &tags[i]));
  }
  (void)hal_can_service(s_can, 0);
  g.tx.clear();
}

/* Both frames end once with HAL_CAN_TX_STOPPED. */
static void expect_two_stopped(const uint32_t *tags) {
  TEST_ASSERT_EQUAL_INT(2, hal_can_service(s_can, 0));
  for (uint32_t i = 0; i < 2u; ++i) {
    TEST_ASSERT_EQUAL_UINT32(tags[i], g.tx[i].tag);
    TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_STOPPED, g.tx[i].reason);
    TEST_ASSERT_EQUAL_INT(HAL_ECANCELED, g.tx[i].result);
  }
}

void test_stop_ends_frames_still_waiting_as_stopped(void) {
  s_can = create_queued();
  uint32_t waiting[2] = {};
  queue_behind_a_full_bus(frame(0x500u), 2u, waiting);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_stop(s_can));
  expect_two_stopped(waiting);
  const hal_can_frame_t f = frame(0x600u);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_can_send_frame_ex(s_can, &f, 0u, NULL));
}

/* Frames queued for the old mode end with it: an FD frame must not go out
 * on a channel switched to classic CAN once the bus has room again. */
void test_a_mode_change_ends_frames_still_waiting(void) {
  s_can = create_queued();
  uint32_t waiting[2] = {};
  queue_behind_a_full_bus(
      frame(0x500u, 12u, HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS), 2u, waiting);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_set_mode(s_can, HAL_CAN_MODE_NORMAL));
  expect_two_stopped(waiting);
  hal_mock_can_reset(s_can); /* room on the modelled bus again */
  const hal_can_frame_t classic = frame(0x600u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_send_frame_ex(s_can, &classic, 0u, NULL));
  hal_can_frame_t sent = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_can_get_sent_frame(s_can, &sent));
  TEST_ASSERT_EQUAL_HEX32(0x600u, sent.id);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_mock_can_get_sent_frame(s_can, &sent));
}

void test_a_full_transmit_queue_waits_then_times_out(void) {
  s_can = create_queued();
  for (int i = 0; i < MOCK_CAN_BUF_SIZE + HAL_CAN_TX_QUEUE_LEN; ++i) {
    const hal_can_frame_t f = frame(0x400u);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame_ex(s_can, &f, 0u, NULL));
  }
  const hal_can_frame_t f = frame(0x401u);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_can_send_frame_ex(s_can, &f, 0u, NULL));
  const uint32_t before = hal_millis();
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        hal_can_send_frame_ex(s_can, &f, 20u, NULL));
  TEST_ASSERT_TRUE(hal_millis() - before >= 20u);
}

void test_bus_off_ends_queued_frames_and_reports_the_state(void) {
  s_can = create_queued();
  for (int i = 0; i < MOCK_CAN_BUF_SIZE; ++i) {
    const hal_can_frame_t f = frame(0x400u);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame_ex(s_can, &f, 0u, NULL));
  }
  uint32_t waiting = 0u;
  const hal_can_frame_t f = frame(0x500u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame_ex(s_can, &f, 0u, &waiting));
  (void)hal_can_service(s_can, 0);
  g.tx.clear();

  hal_mock_can_set_state(s_can, HAL_CAN_STATE_BUS_OFF);
  TEST_ASSERT_EQUAL_INT(2, hal_can_service(s_can, 0));
  TEST_ASSERT_EQUAL_size_t(1u, g.tx.size());
  TEST_ASSERT_EQUAL_UINT32(waiting, g.tx[0].tag);
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_BUS_OFF, g.tx[0].reason);
  TEST_ASSERT_EQUAL_size_t(1u, g.states.size());
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_BUS_OFF, g.states[0]);
  TEST_ASSERT_EQUAL_UINT32(1u, status(s_can).bus_off_count);
}

void test_manual_recovery_waits_for_hal_can_recover(void) {
  s_can = create_queued();
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_can_recover(s_can, 10u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_can_set_mode(s_can, HAL_CAN_MODE_FD | HAL_CAN_MODE_MANUAL_RECOVERY));
  hal_mock_can_set_state(s_can, HAL_CAN_STATE_BUS_OFF);
  hal_can_state_t state = HAL_CAN_STATE_STOPPED;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_state(s_can, &state));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_BUS_OFF, state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_recover(s_can, 10u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_state(s_can, &state));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_ERROR_ACTIVE, state);
  (void)hal_can_service(s_can, 0);
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_ERROR_ACTIVE, g.states.back());
}

void test_a_callback_may_send_on_its_own_handle(void) {
  s_can = create_queued();
  g.resend_on = s_can;
  const hal_can_frame_t f = frame(0x100u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame_ex(s_can, &f, 0u, NULL));
  TEST_ASSERT_EQUAL_INT(1, hal_can_service(s_can, 1));
  TEST_ASSERT_EQUAL_INT(1, hal_can_service(s_can, 0));
  TEST_ASSERT_EQUAL_size_t(2u, g.tx.size());
}

void test_service_stops_at_its_budget(void) {
  s_can = create_queued();
  for (uint32_t i = 0; i < 5u; ++i) {
    const hal_can_frame_t f = frame(0x700u + i);
    hal_mock_can_inject_frame(s_can, &f);
  }
  TEST_ASSERT_EQUAL_INT(2, hal_can_service(s_can, 2));
  TEST_ASSERT_EQUAL_INT(3, hal_can_service(s_can, 0));
  TEST_ASSERT_EQUAL_INT(0, hal_can_service(s_can, 0));
}

void test_a_receive_with_timeout_gives_up(void) {
  s_can = create_queued();
  hal_can_frame_t rx;
  const uint32_t before = hal_millis();
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        hal_can_receive_frame_ex(s_can, &rx, NULL, 15u));
  TEST_ASSERT_TRUE(hal_millis() - before >= 15u);
}

/* ── Synchronous channels ─────────────────────────────────────────────── */

void test_a_synchronous_channel_sends_at_once_and_still_reports(void) {
  const hal_can_config_t cfg = fd_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_create(&cfg, &s_can));
  hal_can_caps_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_caps(s_can, &caps));
  TEST_ASSERT_EQUAL_HEX8(HAL_CAN_CAP_FD, caps.features);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_can_set_isr_notify(s_can, notify, &g.notifies));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_can_set_callbacks(s_can, on_rx, on_tx, on_state, NULL));
  const hal_can_frame_t f = frame(0x123u);
  uint32_t tag = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame_ex(s_can, &f, 0u, &tag));
  hal_can_frame_t sent;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_can_get_sent_frame(s_can, &sent)); /* already out */

  const hal_can_frame_t in = frame(0x321u);
  hal_mock_can_inject_frame(s_can, &in);
  hal_mock_can_set_state(s_can, HAL_CAN_STATE_ERROR_PASSIVE);
  TEST_ASSERT_EQUAL_INT(3, hal_can_service(s_can, 0));
  TEST_ASSERT_EQUAL_UINT32(tag, g.tx[0].tag);
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_TX_DONE, g.tx[0].reason);
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_ERROR_PASSIVE, g.states[0]);
  TEST_ASSERT_EQUAL_HEX32(0x321u, g.rx_ids[0]);
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_FILTER_NONE, g.rx_filters[0]);
}

void test_classic_and_queued_sends_add_up_in_the_status(void) {
  const hal_can_config_t cfg = fd_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_create(&cfg, &s_can));
  const hal_can_frame_t f = frame(0x10u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame(s_can, &f));
  const uint8_t data[2] = {1u, 2u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send(s_can, 0x11u, 2u, data));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame_ex(s_can, &f, 0u, NULL));
  const hal_can_status_t st = status(s_can);
  TEST_ASSERT_EQUAL_UINT32(3u, st.tx_frames);
  TEST_ASSERT_EQUAL_UINT32(0u, st.tx_failed);
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_ERROR_ACTIVE, st.state);
}

/* ── Filters ──────────────────────────────────────────────────────────── */

namespace {

hal_can_filter_ex_t fx(uint8_t type, uint8_t action, uint32_t id1, uint32_t id2,
                       uint8_t flags = 0u) {
  hal_can_filter_ex_t f = {};
  f.type = type;
  f.action = action;
  f.flags = flags;
  f.id1 = id1;
  f.id2 = id2;
  return f;
}

/* Inject ids and return those that got through, with their filter index. */
std::vector<std::pair<uint32_t, uint8_t>>
pass(hal_can_t h, std::vector<hal_can_frame_t> in) {
  for (const hal_can_frame_t &f : in) {
    hal_mock_can_inject_frame(h, &f);
  }
  std::vector<std::pair<uint32_t, uint8_t>> out;
  hal_can_frame_t rx;
  hal_can_rx_info_t info;
  while (hal_can_receive_frame_ex(h, &rx, &info, 0u) == HAL_OK) {
    out.emplace_back(rx.id, info.filter_index);
  }
  return out;
}

} // namespace

void test_the_first_matching_filter_decides(void) {
  s_can = create_queued();
  uint8_t reject = 0u, accept = 0u;
  /* An exception added first keeps 0x150 out of the accepted range. */
  hal_can_filter_ex_t f =
      fx(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_REJECT, 0x150u, 0x150u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_add_filter(s_can, &f, &reject));
  f = fx(HAL_CAN_FILTER_RANGE, HAL_CAN_FILTER_ACCEPT, 0x100u, 0x1FFu);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_add_filter(s_can, &f, &accept));
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_FILTER_FIRST_ADDED, reject);
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_FILTER_FIRST_ADDED + 1u, accept);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_set_unmatched_policy(s_can, false, true, true));
  const auto got = pass(s_can, {frame(0x150u), frame(0x151u), frame(0x250u),
                                frame(0x150u, 8u, HAL_CAN_FRAME_EXTENDED)});
  TEST_ASSERT_EQUAL_size_t(2u, got.size());
  TEST_ASSERT_EQUAL_HEX32(0x151u, got[0].first);
  TEST_ASSERT_EQUAL_UINT8(accept, got[0].second);
  TEST_ASSERT_EQUAL_HEX32(0x150u, got[1].first); /* extended, unmatched */
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_FILTER_NONE, got[1].second);
}

void test_removing_a_filter_lets_its_frames_follow_the_policy(void) {
  s_can = create_queued();
  uint8_t index = 0u;
  const hal_can_filter_ex_t f =
      fx(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_REJECT, 0x300u, 0x700u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_add_filter(s_can, &f, &index));
  TEST_ASSERT_EQUAL_size_t(0u, pass(s_can, {frame(0x3AAu)}).size());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_remove_filter(s_can, index));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_can_remove_filter(s_can, index));
  TEST_ASSERT_EQUAL_size_t(1u, pass(s_can, {frame(0x3AAu)}).size());
}

void test_remote_frames_can_be_refused(void) {
  s_can = create_queued();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_set_unmatched_policy(s_can, true, true, false));
  hal_can_frame_t rtr = frame(0x123u, 0u, HAL_CAN_FRAME_RTR);
  TEST_ASSERT_EQUAL_size_t(1u, pass(s_can, {rtr, frame(0x124u)}).size());
}

void test_filters_need_a_backend_that_has_them(void) {
  const hal_can_config_t cfg = fd_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_create(&cfg, &s_can));
  uint8_t index = 0u;
  const hal_can_filter_ex_t f =
      fx(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT, 0x1u, 0x7FFu);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_can_add_filter(s_can, &f, &index));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_can_remove_filter(s_can, 6u));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_can_set_unmatched_policy(s_can, true, true, true));
}

void test_invalid_filters_are_refused(void) {
  s_can = create_queued();
  uint8_t index = 0u;
  const hal_can_filter_ex_t bad[] = {
      fx(HAL_CAN_FILTER_RANGE, HAL_CAN_FILTER_ACCEPT, 0x200u, 0x100u),
      fx(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT, 0x800u, 0x7FFu),
      fx(3u, HAL_CAN_FILTER_ACCEPT, 0x1u, 0x1u),
      fx(HAL_CAN_FILTER_MASK, 2u, 0x1u, 0x1u),
      fx(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT, 0x20000000u, 0u,
         HAL_CAN_FILTER_EXTENDED),
      fx(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT, 0x1u, 0x1u, 0x80u),
  };
  for (const hal_can_filter_ex_t &f : bad) {
    TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_add_filter(s_can, &f, &index));
  }
  const hal_can_filter_ex_t ok =
      fx(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT, 0x1u, 0x7FFu);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_add_filter(s_can, &ok, NULL));
}

void test_matching_follows_each_filter_kind(void) {
  const hal_can_frame_t a = frame(0x123u);
  const hal_can_filter_ex_t mask =
      fx(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT, 0x120u, 0x7F0u);
  const hal_can_filter_ex_t range =
      fx(HAL_CAN_FILTER_RANGE, HAL_CAN_FILTER_REJECT, 0x123u, 0x124u);
  const hal_can_filter_ex_t dual =
      fx(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT, 0x122u, 0x124u);
  const hal_can_filter_ex_t ext =
      fx(HAL_CAN_FILTER_MASK, HAL_CAN_FILTER_ACCEPT, 0x123u, 0x1FFFFFFFu,
         HAL_CAN_FILTER_EXTENDED);
  TEST_ASSERT_TRUE(can_matches_ex(&a, &mask));
  TEST_ASSERT_TRUE(can_matches_ex(&a, &range));
  TEST_ASSERT_FALSE(can_matches_ex(&a, &dual));
  TEST_ASSERT_FALSE(can_matches_ex(&a, &ext));
  /* Both ends of a range and both IDs of a pair. */
  const hal_can_frame_t b = frame(0x124u);
  TEST_ASSERT_TRUE(can_matches_ex(&b, &range));
  TEST_ASSERT_TRUE(can_matches_ex(&b, &dual));
  const hal_can_frame_t c = frame(0x125u);
  TEST_ASSERT_FALSE(can_matches_ex(&c, &range));
  /* The classic matcher is the accepting mask filter. */
  const hal_can_filter_t classic = {0x120u, 0x7F0u, 0u};
  TEST_ASSERT_TRUE(can_matches(&a, &classic));
  /* An invalid frame or filter is an error, not a "no match". */
  bool matches = true;
  hal_can_frame_t bad = a;
  bad.dlc = 9u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_can_frame_matches_filter_ex(&bad, &mask, &matches));
  const hal_can_filter_ex_t bad_range =
      fx(HAL_CAN_FILTER_RANGE, HAL_CAN_FILTER_ACCEPT, 0x124u, 0x123u);
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_can_frame_matches_filter_ex(&a, &bad_range, &matches));
  TEST_ASSERT_TRUE(matches); /* untouched */
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_can_frame_matches_filter_ex(&a, &mask, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_can_frame_matches_filter(&a, NULL, &matches));
}

/* ── Creation and arguments ───────────────────────────────────────────── */

void test_create_reports_why_it_failed(void) {
  hal_can_t h = (hal_can_t)1;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_create(NULL, NULL));
  hal_mock_can_fail_creates(1u);
  const hal_can_config_t cfg = fd_config();
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_can_create(&cfg, &h));
  TEST_ASSERT_NULL(h);
  hal_can_t all[MOCK_CAN_MAX_INST];
  for (int i = 0; i < MOCK_CAN_MAX_INST; ++i) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_create(&cfg, &all[i]));
  }
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_can_create(&cfg, &h));
  for (int i = 0; i < MOCK_CAN_MAX_INST; ++i) {
    hal_can_destroy(all[i]);
  }
}

void test_invalid_handles_and_arguments_are_refused(void) {
  hal_can_frame_t f = frame(0x1u);
  hal_can_status_t st;
  hal_can_caps_t caps;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_send_frame_ex(NULL, &f, 0u, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_can_receive_frame_ex(NULL, &f, NULL, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_get_status(NULL, &st));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_get_caps(NULL, &caps));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_service(NULL, 0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_recover(NULL, 0u));
  s_can = create_queued();
  f.dlc = 9u; /* 12 bytes, but len says 8 */
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_send_frame_ex(s_can, &f, 0u, NULL));
}

/* FD needs the channel's current mode, not only its capabilities. */
void test_an_fd_frame_needs_fd_in_the_current_mode(void) {
  const hal_can_frame_t fd =
      frame(0x123u, 12u, HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS);
  for (int queued = 0; queued < 2; ++queued) {
    hal_mock_can_set_queued(queued != 0);
    const hal_can_config_t cfg = fd_config();
    hal_can_t h = NULL;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_create(&cfg, &h));
    hal_mock_can_set_queued(false);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_set_mode(h, HAL_CAN_MODE_NORMAL));
    TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                          hal_can_send_frame_ex(h, &fd, 0u, NULL));
    TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_can_send_frame(h, &fd));
    hal_can_frame_t sent;
    TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_mock_can_get_sent_frame(h, &sent));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_set_mode(h, HAL_CAN_MODE_FD));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send_frame(h, &fd));
    hal_can_destroy(h);
  }
}

/* A slot moving to a full list of the other ID kind keeps its old filter. */
void test_a_slot_change_without_room_keeps_the_old_filter(void) {
  s_can = create_queued();
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_can_set_unmatched_policy(s_can, false, false, true));
  uint8_t index = 0u;
  for (uint32_t k = 0; k < 8u; ++k) {
    const hal_can_filter_ex_t f =
        fx(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT, 0x1000u + k, 0x1000u + k,
           HAL_CAN_FILTER_EXTENDED);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_add_filter(s_can, &f, &index));
  }
  const hal_can_filter_t std_filter = {0x321u, 0x7FFu, 0u};
  const hal_can_filter_t ext_filter = {0x18DAF110u, 0x1FFFFFFFu,
                                       HAL_CAN_FILTER_EXTENDED};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_set_filter(s_can, 0u, &std_filter));
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_can_set_filter(s_can, 0u, &ext_filter));
  const auto got = pass(s_can, {frame(0x321u)});
  TEST_ASSERT_EQUAL_size_t(1u, got.size());
  TEST_ASSERT_EQUAL_UINT8(0u, got[0].second);
}

/* The status adds what the interrupt counted to what the controller counted
 * without it, on queued and synchronous channels alike. */
void test_message_ram_failures_reach_the_status(void) {
  s_can = create_queued();
  hal_mock_can_report_ram_access_failure(s_can);
  hal_mock_can_report_ram_access_failure(s_can);
  TEST_ASSERT_EQUAL_UINT32(2u, status(s_can).ram_access_failures);
  hal_can_destroy(s_can);
  const hal_can_config_t cfg = fd_config();
  s_can = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_create(&cfg, &s_can));
  TEST_ASSERT_EQUAL_UINT32(0u, status(s_can).ram_access_failures);
  hal_mock_can_report_ram_access_failure(s_can);
  TEST_ASSERT_EQUAL_UINT32(1u, status(s_can).ram_access_failures);
}

void test_classic_calls_report_their_status(void) {
  const hal_can_config_t cfg = fd_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_create_with_retry(&cfg, HAL_CAN_NO_INT_PIN,
                                                  NULL, 0, NULL, &s_can));
  hal_can_mode_t mode = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_mode(s_can, &mode));
  TEST_ASSERT_TRUE((mode & HAL_CAN_MODE_FD) != 0u);
  hal_mock_can_set_error_counters(s_can, 5u, 7u);
  hal_can_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_error_counters(s_can, &counters));
  TEST_ASSERT_EQUAL_UINT8(5u, counters.tx);
  TEST_ASSERT_EQUAL_UINT8(7u, counters.rx);
  hal_can_state_t state = HAL_CAN_STATE_BUS_OFF;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_stop(s_can));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_state(s_can, &state));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_STOPPED, state);
  const uint8_t payload[2] = {1u, 2u};
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_can_send(s_can, 0x10u, 2u, payload));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_start(s_can));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_send(s_can, 0x10u, 2u, payload));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_send(s_can, 0x10u, 2u, NULL));
  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[HAL_CAN_MAX_DATA_LEN] = {};
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_can_receive(s_can, &id, &len, data));
  hal_mock_can_inject(s_can, 0x22u, 2u, payload);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_receive(s_can, &id, &len, data));
  TEST_ASSERT_EQUAL_HEX32(0x22u, id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_set_std_filters(s_can, 0x7E0u, 0x7DFu));
  const hal_can_filter_t f = {0x100u, 0x700u, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_can_set_filter(s_can, HAL_CAN_MAX_FILTERS, &f));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_set_filter(s_can, 2u, &f));
  /* NULL outputs and handles. */
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_get_mode(s_can, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_get_state(NULL, &state));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_get_error_counters(s_can, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_receive(s_can, NULL, &len, data));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_set_std_filters(NULL, 0u, 0u));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_can_create_with_retry(&cfg, HAL_CAN_NO_INT_PIN, NULL, 0, NULL, NULL));
}

/* Each failed attempt is retried; the error of the last one comes back. */
void test_create_with_retry_reports_the_last_error(void) {
  hal_mock_can_fail_creates(3u);
  const hal_can_config_t cfg = fd_config();
  hal_can_t h = (hal_can_t)1;
  TEST_ASSERT_EQUAL_INT(
      HAL_EIO,
      hal_can_create_with_retry(&cfg, HAL_CAN_NO_INT_PIN, NULL, 2, NULL, &h));
  TEST_ASSERT_NULL(h);
  hal_mock_can_fail_creates(1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_can_create_with_retry(&cfg, HAL_CAN_NO_INT_PIN,
                                                  NULL, 1, NULL, &s_can));
  /* Not every failure is an I/O error. */
  hal_can_config_t unknown = cfg;
  unknown.backend = (hal_can_backend_t)200;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_can_create_with_retry(&unknown, HAL_CAN_NO_INT_PIN,
                                                  NULL, 1, NULL, &h));
  TEST_ASSERT_NULL(h);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_queued_channel_reports_its_capabilities);
  RUN_TEST(test_queued_sends_report_their_outcome_with_their_tag);
  RUN_TEST(test_received_frames_overflowing_the_queue_are_counted_exactly);
  RUN_TEST(test_received_frames_carry_their_filter_slot);
  RUN_TEST(test_a_classic_read_consumes_and_counts_an_fd_frame);
  RUN_TEST(test_stop_ends_frames_still_waiting_as_stopped);
  RUN_TEST(test_a_mode_change_ends_frames_still_waiting);
  RUN_TEST(test_a_full_transmit_queue_waits_then_times_out);
  RUN_TEST(test_bus_off_ends_queued_frames_and_reports_the_state);
  RUN_TEST(test_manual_recovery_waits_for_hal_can_recover);
  RUN_TEST(test_a_callback_may_send_on_its_own_handle);
  RUN_TEST(test_service_stops_at_its_budget);
  RUN_TEST(test_a_receive_with_timeout_gives_up);
  RUN_TEST(test_a_synchronous_channel_sends_at_once_and_still_reports);
  RUN_TEST(test_classic_and_queued_sends_add_up_in_the_status);
  RUN_TEST(test_the_first_matching_filter_decides);
  RUN_TEST(test_removing_a_filter_lets_its_frames_follow_the_policy);
  RUN_TEST(test_remote_frames_can_be_refused);
  RUN_TEST(test_filters_need_a_backend_that_has_them);
  RUN_TEST(test_invalid_filters_are_refused);
  RUN_TEST(test_matching_follows_each_filter_kind);
  RUN_TEST(test_create_reports_why_it_failed);
  RUN_TEST(test_invalid_handles_and_arguments_are_refused);
  RUN_TEST(test_an_fd_frame_needs_fd_in_the_current_mode);
  RUN_TEST(test_a_slot_change_without_room_keeps_the_old_filter);
  RUN_TEST(test_message_ram_failures_reach_the_status);
  RUN_TEST(test_classic_calls_report_their_status);
  RUN_TEST(test_create_with_retry_reports_the_last_error);
  return UNITY_END();
}
