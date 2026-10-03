// The MCP251XFD provider and driver against a model of the MCP2518FD on the
// mock SPI bus (tests/fakes/mcp251xfd). Register values come from the
// MCP25XXFD Family Reference Manual (DS20005678E) and the MCP2518FD data
// sheet (DS20006027B); the model also records every data sheet rule the
// driver breaks, and tearDown() requires none.

#include "fakes/mcp251xfd/mcp251xfd_model.h"
#include "hal/can/mcp251xfd/hal_can_mcp251xfd.h"
#include "hal/core/hal_array.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <string.h>

namespace {

constexpr uint8_t kCs = 10u;
alignas(JHMCP251XFD) uint8_t s_ctx[sizeof(JHMCP251XFD)];
Mcp251xfdModel *s_chip;
bool s_open;
jh_can_caps_t s_caps;
const jh_can_provider_t &P = jh_can_mcp251xfd_provider;

hal_can_config_t config(bool fd = true) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_MCP251XFD;
  cfg.mcp251xfd.cs_pin = kCs;
  cfg.mcp251xfd.arbitration_bitrate_hz = 500000u;
  cfg.mcp251xfd.data_bitrate_hz = 2000000u;
  cfg.mcp251xfd.oscillator_hz = 40000000u;
  cfg.mcp251xfd.enable_fd = fd;
  return cfg;
}

hal_status_t open(const hal_can_config_t &cfg) {
  s_caps = jh_can_caps_t();
  hal_can_mode_t mode = 0u;
  const hal_status_t st = P.init(s_ctx, &cfg, &s_caps, &mode);
  s_open = st == HAL_OK;
  return st;
}

hal_can_frame_t frame(uint32_t id, uint8_t len, uint8_t flags = 0u) {
  hal_can_frame_t f = {};
  f.id = id;
  f.flags = flags;
  f.len = len;
  f.dlc = hal_can_bytes_to_dlc(len);
  for (uint8_t i = 0; i < len; ++i) {
    f.data[i] = (uint8_t)(id * 7u + i);
  }
  return f;
}

Mcp251xfdFrame bus_frame(uint32_t id, bool ext = false, uint8_t len = 2u) {
  Mcp251xfdFrame f;
  f.id = id;
  f.ext = ext;
  f.dlc = len;
  for (uint8_t i = 0; i < len; ++i) {
    f.data[i] = (uint8_t)(0xA0u + i);
  }
  return f;
}

void assert_same(const hal_can_frame_t &a, const hal_can_frame_t &b) {
  TEST_ASSERT_EQUAL_HEX32(a.id, b.id);
  TEST_ASSERT_EQUAL_HEX8(a.flags, b.flags);
  TEST_ASSERT_EQUAL_UINT8(a.dlc, b.dlc);
  TEST_ASSERT_EQUAL_UINT8(a.len, b.len);
  if ((a.flags & HAL_CAN_FRAME_RTR) == 0u && a.len > 0u) {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a.data, b.data, a.len);
  }
}

hal_status_t receive(hal_can_frame_t *f) { return P.receive_frame(s_ctx, f); }

hal_can_filter_ex_t mask_filter(uint32_t id, uint32_t mask, bool ext = false) {
  hal_can_filter_ex_t f = {};
  f.type = HAL_CAN_FILTER_MASK;
  f.action = HAL_CAN_FILTER_ACCEPT;
  f.flags = 0u;
  if (ext) {
    f.flags = HAL_CAN_FILTER_EXTENDED;
  }
  f.id1 = id;
  f.id2 = mask;
  return f;
}

} // namespace

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0u);
  hal_mock_set_micros(0u);
  hal_mock_set_micros_step(1u); /* waits in the driver end */
  s_chip = new Mcp251xfdModel(kCs);
  s_open = false;
}

void tearDown(void) {
  if (s_open) {
    P.deinit(s_ctx);
  }
  const std::vector<std::string> broken =
      s_chip != nullptr ? s_chip->violations() : std::vector<std::string>();
  delete s_chip;
  s_chip = nullptr;
  TEST_ASSERT_EQUAL_MESSAGE(0, (int)broken.size(),
                            broken.empty() ? "" : broken[0].c_str());
}

/* ── Setup ──────────────────────────────────────────────────────────────── */

/* FRM 3.4.8, table 3-5: 40 MHz, 500 kbit/s and 2 Mbit/s, both at 80 %. */
void test_init_writes_the_bit_timing_of_the_reference_manual_example(void) {
  hal_can_config_t cfg = config();
  cfg.mcp251xfd.arbitration_sample_point_permille = 800u;
  cfg.mcp251xfd.data_sample_point_permille = 800u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(cfg));
  TEST_ASSERT_EQUAL_HEX32(0x003E0F0Fu, s_chip->reg32(0x004u)); /* NBTCFG */
  TEST_ASSERT_EQUAL_HEX32(0x000E0303u, s_chip->reg32(0x008u)); /* DBTCFG */
  /* TDCMOD = 2 (auto), TDCO = DBRP x DTSEG1 = 15. */
  TEST_ASSERT_EQUAL_HEX32(0x00020F00u, s_chip->reg32(0x00Cu));
  TEST_ASSERT_EQUAL(Mcp251xfdModel::kMixed, s_chip->mode());
}

void test_the_default_data_sample_point_is_75_percent(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  /* 20 quanta: sync + 14 + 5, SJW 5. */
  TEST_ASSERT_EQUAL_HEX32(0x000D0404u, s_chip->reg32(0x008u));
  TEST_ASSERT_EQUAL_HEX32(0x00020E00u, s_chip->reg32(0x00Cu));
}

void test_a_4_mhz_crystal_runs_the_pll(void) {
  hal_can_config_t cfg = config();
  cfg.mcp251xfd.oscillator_hz = 4000000u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(cfg));
  TEST_ASSERT_EQUAL_HEX32(0x1u, s_chip->reg32(0xE00u) & 0x1u); /* PLLEN */
  TEST_ASSERT_EQUAL_HEX32(0x003E0F0Fu, s_chip->reg32(0x004u)); /* 40 MHz */
  TEST_ASSERT_EQUAL_UINT32(40000000u, s_caps.public_caps.core_clock_hz);
}

void test_a_bitrate_the_clock_cannot_make_fails_init(void) {
  hal_can_config_t cfg = config();
  cfg.mcp251xfd.oscillator_hz = 20000000u;
  cfg.mcp251xfd.data_bitrate_hz = 8000000u; /* 2.5 quanta per bit */
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, open(cfg));
}

void test_a_chip_left_running_goes_to_configuration_before_reset(void) {
  s_chip->force_mode(Mcp251xfdModel::kMixed);
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_EQUAL_UINT32(1u, s_chip->resets());
}

void test_without_a_chip_init_fails(void) {
  delete s_chip;
  s_chip = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, open(config()));
}

void test_init_leaves_the_io_pins_alone(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_EQUAL_HEX32(0x03000003u, s_chip->reg32(0xE04u)); /* POR */
}

void test_caps_list_the_modes_the_chip_has(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  const hal_can_mode_t expected =
      HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_EXTERNAL_LOOPBACK |
      HAL_CAN_MODE_LISTEN_ONLY | HAL_CAN_MODE_ONE_SHOT | HAL_CAN_MODE_SLEEP |
      HAL_CAN_MODE_FD;
  TEST_ASSERT_EQUAL_HEX32(expected, s_caps.modes);
  TEST_ASSERT_EQUAL_HEX8(HAL_CAN_CAP_FD, s_caps.public_caps.features);
  TEST_ASSERT_EQUAL_UINT32(40000000u, s_caps.public_caps.core_clock_hz);
}

/* ── Modes ──────────────────────────────────────────────────────────────── */

void test_every_mode_change_passes_through_configuration(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, P.apply_mode(s_ctx, HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_FD));
  TEST_ASSERT_EQUAL(Mcp251xfdModel::kIntLoopback, s_chip->mode());
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.apply_mode(s_ctx, HAL_CAN_MODE_LISTEN_ONLY));
  TEST_ASSERT_EQUAL(Mcp251xfdModel::kListenOnly, s_chip->mode());
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      P.apply_mode(s_ctx, HAL_CAN_MODE_EXTERNAL_LOOPBACK | HAL_CAN_MODE_FD));
  TEST_ASSERT_EQUAL(Mcp251xfdModel::kExtLoopback, s_chip->mode());
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.apply_mode(s_ctx, HAL_CAN_MODE_NORMAL));
  TEST_ASSERT_EQUAL(Mcp251xfdModel::kCan20, s_chip->mode());
  TEST_ASSERT_EQUAL_HEX32(0u, s_chip->reg32(0x00Cu) & 0x30000u); /* no TDC */
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.stop(s_ctx));
  TEST_ASSERT_EQUAL(Mcp251xfdModel::kConfig, s_chip->mode());
}

void test_listen_only_sends_nothing(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.apply_mode(s_ctx, HAL_CAN_MODE_LISTEN_ONLY));
  const hal_can_frame_t f = frame(0x123u, 8u);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, P.send_frame(s_ctx, &f));
  TEST_ASSERT_EQUAL_size_t(0u, s_chip->sent.size());
}

/* ── Frames ─────────────────────────────────────────────────────────────── */

/* More frames than the receive FIFO holds, so both FIFOs wrap; odd lengths
 * check that RAM is written in whole words. */
void test_every_frame_kind_comes_back_in_loopback(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, P.apply_mode(s_ctx, HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_FD));
  const struct {
    uint32_t id;
    uint8_t len;
    uint8_t flags;
  } kinds[] = {
      {0x123u, 8u, 0u},
      {0x1ABCDEF0u, 8u, HAL_CAN_FRAME_EXTENDED},
      {0x7FFu, 0u, 0u},
      {0x456u, 3u, 0u},
      {0x457u, 0u, HAL_CAN_FRAME_RTR},
      {0x1F0u, 64u, HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS},
      {0x12345u, 12u, HAL_CAN_FRAME_FD | HAL_CAN_FRAME_EXTENDED},
      {0x0F1u, 5u, HAL_CAN_FRAME_FD},
  };
  for (uint32_t round = 0; round < 40u; ++round) {
    const auto &k = kinds[round % COUNTOF(kinds)];
    hal_can_frame_t f = frame(k.id, k.len, k.flags);
    f.data[0] = (uint8_t)round;
    TEST_ASSERT_EQUAL_INT(HAL_OK, P.send_frame(s_ctx, &f));
    TEST_ASSERT_EQUAL_INT(HAL_OK, P.available(s_ctx));
    hal_can_frame_t rx = {};
    TEST_ASSERT_EQUAL_INT(HAL_OK, receive(&rx));
    assert_same(f, rx);
  }
  hal_can_frame_t rx = {};
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, receive(&rx));
}

void test_frames_waiting_in_the_receive_fifo_keep_their_order(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  for (uint32_t i = 0; i < 30u; ++i) {
    s_chip->deliver(bus_frame(0x100u + i));
  }
  TEST_ASSERT_EQUAL_UINT32(6u, s_chip->rx_overflows()); /* 24 objects */
  for (uint32_t i = 0; i < 24u; ++i) {
    hal_can_frame_t rx = {};
    TEST_ASSERT_EQUAL_INT(HAL_OK, receive(&rx));
    TEST_ASSERT_EQUAL_HEX32(0x100u + i, rx.id);
    TEST_ASSERT_EQUAL_HEX8(0xA1u, rx.data[1]);
  }
}

/* A classic DLC above 8 still means 8 bytes; the frame is delivered with
 * DLC 8 rather than dropped by validation after it left the FIFO. */
void test_a_classic_frame_with_dlc_above_8_arrives_as_8_bytes(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  Mcp251xfdFrame f = bus_frame(0x155u, false, 8u);
  f.dlc = 12u;
  TEST_ASSERT_TRUE(s_chip->deliver(f));
  hal_can_frame_t rx = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, receive(&rx));
  TEST_ASSERT_EQUAL_HEX32(0x155u, rx.id);
  TEST_ASSERT_EQUAL_UINT8(8u, rx.dlc);
  TEST_ASSERT_EQUAL_UINT8(8u, rx.len);
  TEST_ASSERT_EQUAL_HEX8(0xA7u, rx.data[7]);
}

void test_a_classic_channel_sends_classic_frames_and_refuses_fd(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config(false)));
  TEST_ASSERT_EQUAL(Mcp251xfdModel::kCan20, s_chip->mode());
  const hal_can_frame_t f = frame(0x321u, 8u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.send_frame(s_ctx, &f));
  TEST_ASSERT_EQUAL_size_t(1u, s_chip->sent.size());
  TEST_ASSERT_EQUAL_HEX32(0x321u, s_chip->sent[0].id);
  TEST_ASSERT_FALSE(s_chip->sent[0].fdf);
  Mcp251xfdFrame fd = bus_frame(0x200u);
  fd.fdf = true;
  TEST_ASSERT_FALSE(s_chip->deliver(fd));
}

/* ── Sending ────────────────────────────────────────────────────────────── */

void test_a_failed_one_shot_frame_never_goes_out_later(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, P.apply_mode(s_ctx, HAL_CAN_MODE_FD | HAL_CAN_MODE_ONE_SHOT));
  s_chip->bus_acks = false;
  const hal_can_frame_t lost = frame(0x111u, 8u);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, P.send_frame(s_ctx, &lost));
  TEST_ASSERT_EQUAL_UINT8(8u, s_chip->tec());
  s_chip->bus_acks = true;
  const hal_can_frame_t next = frame(0x222u, 8u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.send_frame(s_ctx, &next));
  TEST_ASSERT_EQUAL_size_t(1u, s_chip->sent.size());
  TEST_ASSERT_EQUAL_HEX32(0x222u, s_chip->sent[0].id);
}

void test_an_unacknowledged_frame_is_aborted_after_the_timeout(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  s_chip->bus_acks = false;
  hal_mock_set_micros_step(100u);
  const hal_can_frame_t lost = frame(0x111u, 8u);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, P.send_frame(s_ctx, &lost));
  hal_can_state_t state = HAL_CAN_STATE_STOPPED;
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.get_state(s_ctx, &state));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_ERROR_PASSIVE, state);
  s_chip->bus_acks = true;
  const hal_can_frame_t next = frame(0x222u, 8u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.send_frame(s_ctx, &next));
  TEST_ASSERT_EQUAL_size_t(1u, s_chip->sent.size());
  TEST_ASSERT_EQUAL_HEX32(0x222u, s_chip->sent[0].id);
}

/* ── Filters ────────────────────────────────────────────────────────────── */

void test_without_filters_everything_arrives(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x123u)));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x1234567u, true)));
}

void test_the_first_classic_filter_rejects_the_rest(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  const hal_can_filter_t f = {0x100u, 0x700u, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(s_ctx, 0u, &f));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x123u)));
  TEST_ASSERT_FALSE(s_chip->deliver(bus_frame(0x223u)));
  TEST_ASSERT_FALSE(s_chip->deliver(bus_frame(0x123u, true)));
}

/* Each filter has its own FLTCON byte (register 3-32): programming slot 4
 * must leave slot 1 enabled and pointing at the receive FIFO. */
void test_classic_slots_do_not_disturb_each_other(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  const hal_can_filter_t a = {0x101u, 0x7FFu, 0u};
  const hal_can_filter_t b = {0x18FEF100u, 0x1FFFFFFFu,
                              HAL_CAN_FILTER_EXTENDED};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(s_ctx, 1u, &a));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(s_ctx, 4u, &b));
  TEST_ASSERT_EQUAL_HEX32(0x00008100u, s_chip->reg32(0x1D0u));
  TEST_ASSERT_EQUAL_HEX32(0x00000081u, s_chip->reg32(0x1D4u));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x101u)));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x18FEF100u, true)));
  TEST_ASSERT_FALSE(s_chip->deliver(bus_frame(0x18FEF101u, true)));
}

void test_unmatched_frames_follow_the_policy(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(s_ctx, false, true, true));
  TEST_ASSERT_FALSE(s_chip->deliver(bus_frame(0x123u)));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x123u, true)));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(s_ctx, true, false, true));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x124u)));
  TEST_ASSERT_FALSE(s_chip->deliver(bus_frame(0x124u, true)));
  /* After a policy, a classic filter no longer changes it. */
  const hal_can_filter_t f = {0x300u, 0x7FFu, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.set_filter(s_ctx, 0u, &f));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x125u)));
}

void test_refused_remote_frames_are_skipped(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(s_ctx, true, true, false));
  Mcp251xfdFrame rtr = bus_frame(0x10u, false, 0u);
  rtr.rtr = true;
  TEST_ASSERT_TRUE(s_chip->deliver(rtr));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x11u)));
  hal_can_frame_t rx = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, receive(&rx));
  TEST_ASSERT_EQUAL_HEX32(0x11u, rx.id);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, receive(&rx));
}

void test_added_filters_take_the_free_positions(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        P.set_unmatched_policy(s_ctx, false, false, true));
  uint8_t index = 0u;
  for (uint8_t f = HAL_CAN_FILTER_FIRST_ADDED; f < 31u; ++f) {
    const hal_can_filter_ex_t ex = mask_filter(0x400u + f, 0x7FFu);
    TEST_ASSERT_EQUAL_INT(HAL_OK, P.add_filter(s_ctx, &ex, &index));
    TEST_ASSERT_EQUAL_UINT8(f, index);
  }
  const hal_can_filter_ex_t one_more = mask_filter(0x500u, 0x7FFu);
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, P.add_filter(s_ctx, &one_more, &index));
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x400u + 30u)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.remove_filter(s_ctx, 30u));
  TEST_ASSERT_FALSE(s_chip->deliver(bus_frame(0x400u + 30u)));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, P.remove_filter(s_ctx, 30u));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, P.remove_filter(s_ctx, 31u));
  /* The freed position is the next one used. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, P.add_filter(s_ctx, &one_more, &index));
  TEST_ASSERT_EQUAL_UINT8(30u, index);
  TEST_ASSERT_TRUE(s_chip->deliver(bus_frame(0x500u)));
}

void test_filters_the_chip_does_not_have_are_refused(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, open(config()));
  uint8_t index = 0u;
  hal_can_filter_ex_t range = mask_filter(0x100u, 0x1FFu);
  range.type = HAL_CAN_FILTER_RANGE;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, P.add_filter(s_ctx, &range, &index));
  hal_can_filter_ex_t reject = mask_filter(0x100u, 0x7FFu);
  reject.action = HAL_CAN_FILTER_REJECT;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, P.add_filter(s_ctx, &reject, &index));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_writes_the_bit_timing_of_the_reference_manual_example);
  RUN_TEST(test_the_default_data_sample_point_is_75_percent);
  RUN_TEST(test_a_4_mhz_crystal_runs_the_pll);
  RUN_TEST(test_a_bitrate_the_clock_cannot_make_fails_init);
  RUN_TEST(test_a_chip_left_running_goes_to_configuration_before_reset);
  RUN_TEST(test_without_a_chip_init_fails);
  RUN_TEST(test_init_leaves_the_io_pins_alone);
  RUN_TEST(test_caps_list_the_modes_the_chip_has);
  RUN_TEST(test_every_mode_change_passes_through_configuration);
  RUN_TEST(test_listen_only_sends_nothing);
  RUN_TEST(test_every_frame_kind_comes_back_in_loopback);
  RUN_TEST(test_frames_waiting_in_the_receive_fifo_keep_their_order);
  RUN_TEST(test_a_classic_frame_with_dlc_above_8_arrives_as_8_bytes);
  RUN_TEST(test_a_classic_channel_sends_classic_frames_and_refuses_fd);
  RUN_TEST(test_a_failed_one_shot_frame_never_goes_out_later);
  RUN_TEST(test_an_unacknowledged_frame_is_aborted_after_the_timeout);
  RUN_TEST(test_without_filters_everything_arrives);
  RUN_TEST(test_the_first_classic_filter_rejects_the_rest);
  RUN_TEST(test_classic_slots_do_not_disturb_each_other);
  RUN_TEST(test_unmatched_frames_follow_the_policy);
  RUN_TEST(test_refused_remote_frames_are_skipped);
  RUN_TEST(test_added_filters_take_the_free_positions);
  RUN_TEST(test_filters_the_chip_does_not_have_are_refused);
  return UNITY_END();
}
