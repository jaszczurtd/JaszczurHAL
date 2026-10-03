/**
 * @file app.c
 * @brief CAN-FD HAT hardware test: the native FDCAN backend on a
 *        NUCLEO-G474RE with the Embedded Garage CAN-FD HAT.
 *
 * Runs once after reset, then reports every check every two seconds over the
 * ST-LINK virtual COM port for verify_stm32_fdcan_canhat.py. Everything except
 * the shared-bus section works without wiring; that section passes when CN5,
 * CN6 and CN7 form one terminated bus and is skipped when they do not. The
 * BUSY LED (PC2) shines while the checks run; afterwards the RDY LED
 * (HAL_LED_BUILTIN, PC1) blinks at 1 Hz when everything passed and at 5 Hz
 * otherwise.
 */
#include <hal/can/hal_can.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_array.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Fixture-only peeks at the clock setup and the FDCAN1 INIT bit. */
#include "hal/impl/stm32g474/port/stm32g474_regs.h"

#define PIN(port, n) ((uint8_t)((port) * 16u + (n)))
#define LED_BUSY PIN(2u, 2u) /* PC2 */

enum { CHECK_FAIL = 0, CHECK_PASS = 1, CHECK_SKIP = 2 };

typedef struct {
  char name[40];
  uint8_t result;
  char detail[112];
} check_t;

#define CHECKS_MAX 64u
static check_t s_checks[CHECKS_MAX];
static unsigned s_count;
static bool s_done;
static bool s_led;

static void check(const char *name, uint8_t result, const char *fmt, ...) {
  check_t *c = &s_checks[s_count < CHECKS_MAX ? s_count++ : CHECKS_MAX - 1u];
  snprintf(c->name, sizeof(c->name), "%s", name);
  c->result = result;
  va_list args;
  va_start(args, fmt);
  vsnprintf(c->detail, sizeof(c->detail), fmt, args);
  va_end(args);
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

static hal_can_t open_channel(uint8_t channel, uint32_t arb, uint32_t data,
                              bool fd, hal_can_mode_t mode) {
  hal_can_config_t cfg;
  hal_can_t h = NULL;
  if (hal_can_board_config(channel, &cfg) != HAL_OK) {
    return NULL;
  }
  cfg.stm32g474_fdcan.arbitration_bitrate_hz = arb;
  cfg.stm32g474_fdcan.data_bitrate_hz = data;
  cfg.stm32g474_fdcan.enable_fd = fd;
  if (hal_can_create(&cfg, &h) != HAL_OK) {
    return NULL;
  }
  if (hal_can_set_mode(h, mode | (fd ? HAL_CAN_MODE_FD : 0u)) != HAL_OK) {
    hal_can_destroy(h);
    return NULL;
  }
  return h;
}

static void fill(hal_can_frame_t *f, uint32_t id, uint8_t flags, uint8_t len) {
  memset(f, 0, sizeof(*f));
  f->id = id;
  f->flags = flags;
  f->len = len;
  f->dlc = hal_can_bytes_to_dlc(len);
  if ((flags & HAL_CAN_FRAME_RTR) == 0u) {
    for (uint8_t i = 0; i < len; i++) {
      f->data[i] = (uint8_t)(id + i);
    }
  }
}

static bool same(const hal_can_frame_t *a, const hal_can_frame_t *b) {
  if (a->id != b->id || a->flags != b->flags || a->len != b->len) {
    return false;
  }
  return (a->flags & HAL_CAN_FRAME_RTR) != 0u ||
         memcmp(a->data, b->data, a->len) == 0;
}

/* Send and wait for the frame on @p rx_h; the filter index it came with, -1
 * when it did not arrive, -2 when the send failed. */
static int exchange(hal_can_t tx_h, hal_can_t rx_h, const hal_can_frame_t *f,
                    uint32_t wait_ms) {
  if (hal_can_send_frame(tx_h, f) != HAL_OK) {
    return -2;
  }
  hal_can_frame_t rx;
  hal_can_rx_info_t info;
  if (hal_can_receive_frame_ex(rx_h, &rx, &info, wait_ms) != HAL_OK) {
    return -1;
  }
  return same(f, &rx) ? (int)info.filter_index : -3;
}

static void drain(hal_can_t h, uint32_t ms) {
  const uint32_t start = hal_millis();
  while (!hal_millis_deadline_expired(start, ms)) {
    (void)hal_can_service(h, 0);
  }
}

/* ── Board and clock ────────────────────────────────────────────────────── */

static void check_board(void) {
  const uint32_t pll = RCC_PLLCFGR;
  const bool hse = (pll & RCC_PLLCFGR_PLLSRC_MASK) == RCC_PLLCFGR_PLLSRC_HSE;
  hal_can_caps_t caps = {0};
  hal_can_t h = open_channel(0u, 500000u, 2000000u, true, HAL_CAN_MODE_NORMAL);
  if (h) {
    (void)hal_can_get_caps(h, &caps);
    hal_can_destroy(h);
  }
  check("board",
        (HAL_BOARD_CAN_CHANNEL_COUNT == 3 && HAL_CAN_MAX_INSTANCES >= 3)
            ? CHECK_PASS
            : CHECK_FAIL,
        "channels=%d max_instances=%d", (int)HAL_BOARD_CAN_CHANNEL_COUNT,
        (int)HAL_CAN_MAX_INSTANCES);
  check("clock_tree",
        (hse &&
         (RCC_CCIPR & RCC_CCIPR_FDCANSEL_MASK) == RCC_CCIPR_FDCANSEL_PLLQ &&
         caps.core_clock_hz == 80000000u)
            ? CHECK_PASS
            : CHECK_FAIL,
        "pllcfgr=%08lx src=%s fdcan_hz=%lu", (unsigned long)pll,
        hse ? "HSE" : "HSI16", (unsigned long)caps.core_clock_hz);
  check("ucpd_dead_battery_off",
        (PWR_CR3 & PWR_CR3_UCPD1_DBDIS) != 0u ? CHECK_PASS : CHECK_FAIL,
        "pwr_cr3=%08lx", (unsigned long)PWR_CR3);
}

/* ── Loopback at several rates ──────────────────────────────────────────── */

static void loopback_rate(uint8_t channel, const char *tag, uint32_t arb,
                          uint32_t data, bool fd, hal_can_mode_t loop) {
  char name[28];
  snprintf(name, sizeof(name), "ch%u_%s", (unsigned)channel, tag);
  hal_can_t h = open_channel(channel, arb, data, fd, loop);
  if (h == NULL) {
    check(name, CHECK_FAIL, "create failed");
    return;
  }
  hal_can_frame_t f;
  unsigned ok = 0, total = 0;
  const struct {
    uint32_t id;
    uint8_t flags;
    uint8_t len;
  } frames[] = {
      {0x123u, 0u, 8u},
      {0x1ABCDEF0u, HAL_CAN_FRAME_EXTENDED, 8u},
      {0x7FFu, 0u, 0u},
      {0x456u, HAL_CAN_FRAME_RTR, 4u},
      {0x1F0u, HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS, 64u},
      {0x12345u, HAL_CAN_FRAME_FD | HAL_CAN_FRAME_EXTENDED, 12u},
  };
  for (size_t i = 0; i < COUNTOF(frames); i++) {
    if ((frames[i].flags & HAL_CAN_FRAME_FD) != 0u && !fd) {
      continue;
    }
    fill(&f, frames[i].id, frames[i].flags, frames[i].len);
    total++;
    ok += exchange(h, h, &f, 20u) >= 0 ? 1u : 0u;
  }
  hal_can_status_t st = {0};
  (void)hal_can_get_status(h, &st);
  check(name, (ok == total && st.tec == 0u) ? CHECK_PASS : CHECK_FAIL,
        "frames=%u/%u tec=%u rec=%u tdcv=%u", ok, total, st.tec, st.rec,
        st.tdc_value);
  hal_can_destroy(h);
}

static void check_loopback(void) {
  static const uint32_t kClassic[] = {125000u, 500000u, 1000000u};
  char tag[20];
  for (size_t i = 0; i < 3u; i++) {
    snprintf(tag, sizeof(tag), "int_%luk",
             (unsigned long)(kClassic[i] / 1000u));
    loopback_rate(0u, tag, kClassic[i], 0u, false, HAL_CAN_MODE_LOOPBACK);
    snprintf(tag, sizeof(tag), "ext_%luk",
             (unsigned long)(kClassic[i] / 1000u));
    loopback_rate(0u, tag, kClassic[i], 0u, false,
                  HAL_CAN_MODE_EXTERNAL_LOOPBACK);
  }
  loopback_rate(0u, "int_fd500k_2M", 500000u, 2000000u, true,
                HAL_CAN_MODE_LOOPBACK);
  loopback_rate(0u, "int_fd500k_4M", 500000u, 4000000u, true,
                HAL_CAN_MODE_LOOPBACK);
  loopback_rate(0u, "int_fd1M_5M", 1000000u, 5000000u, true,
                HAL_CAN_MODE_LOOPBACK);
  loopback_rate(0u, "ext_fd1M_5M", 1000000u, 5000000u, true,
                HAL_CAN_MODE_EXTERNAL_LOOPBACK);
  loopback_rate(1u, "int_fd500k_2M", 500000u, 2000000u, true,
                HAL_CAN_MODE_LOOPBACK);
  loopback_rate(2u, "int_fd500k_2M", 500000u, 2000000u, true,
                HAL_CAN_MODE_LOOPBACK);
}

/* ── Three channels at once ─────────────────────────────────────────────── */

static void check_three_channels(void) {
  hal_can_t h[3];
  for (uint8_t c = 0; c < 3u; c++) {
    h[c] = open_channel(c, 500000u, 2000000u, true, HAL_CAN_MODE_LOOPBACK);
  }
  unsigned own = 0, foreign = 0;
  if (h[0] && h[1] && h[2]) {
    hal_can_frame_t f, rx;
    for (uint8_t c = 0; c < 3u; c++) {
      fill(&f, 0x100u * (c + 1u), HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS, 64u);
      (void)hal_can_send_frame(h[c], &f);
    }
    for (uint8_t c = 0; c < 3u; c++) {
      while (hal_can_receive_frame_ex(h[c], &rx, NULL, 10u) == HAL_OK) {
        if (rx.id == 0x100u * (c + 1u)) {
          own++;
        } else {
          foreign++;
        }
      }
    }
  }
  check("three_channels",
        (own == 3u && foreign == 0u) ? CHECK_PASS : CHECK_FAIL,
        "opened=%d%d%d own=%u foreign=%u", h[0] != NULL, h[1] != NULL,
        h[2] != NULL, own, foreign);
  for (uint8_t c = 0; c < 3u; c++) {
    if (h[c]) {
      hal_can_destroy(h[c]);
    }
  }
  hal_can_config_t cfg;
  check("no_fourth_channel",
        hal_can_board_config(3u, &cfg) == HAL_ENOENT ? CHECK_PASS : CHECK_FAIL,
        "channel=3 refused");
}

/* ── Queues, events, filters, timestamps ────────────────────────────────── */

static uint32_t s_tx_done, s_tx_failed, s_tx_bus_off, s_tx_stopped, s_rx_cb;
static uint64_t s_tx_ts[20];

static void on_tx(hal_can_t h, const hal_can_tx_event_t *ev, void *user) {
  (void)h;
  (void)user;
  switch (ev->reason) {
  case HAL_CAN_TX_DONE:
    if (s_tx_done < 20u) {
      s_tx_ts[s_tx_done] = ev->timestamp_us;
    }
    s_tx_done++;
    break;
  case HAL_CAN_TX_FAILED:
    s_tx_failed++;
    break;
  case HAL_CAN_TX_BUS_OFF:
    s_tx_bus_off++;
    break;
  default:
    s_tx_stopped++;
    break;
  }
}

static void on_rx(hal_can_t h, const hal_can_frame_t *f,
                  const hal_can_rx_info_t *info, void *user) {
  (void)h;
  (void)f;
  (void)info;
  (void)user;
  s_rx_cb++;
}

static void reset_counts(void) {
  s_tx_done = s_tx_failed = s_tx_bus_off = s_tx_stopped = s_rx_cb = 0u;
}

static void check_queues(hal_can_t h) {
  (void)hal_can_set_mode(h, HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_FD);
  (void)hal_can_set_callbacks(h, NULL, on_tx, NULL, NULL);
  reset_counts();
  hal_can_status_t before = {0}, after = {0};
  (void)hal_can_get_status(h, &before);
  hal_can_frame_t f;
  unsigned queued = 0;
  for (uint32_t i = 0; i < 40u; i++) {
    fill(&f, 0x100u + i, 0u, 8u);
    queued += hal_can_send_frame_ex(h, &f, 50u, NULL) == HAL_OK ? 1u : 0u;
    (void)hal_can_service(h, 0);
  }
  drain(h, 20u);
  (void)hal_can_get_status(h, &after);
  const uint32_t rx = after.rx_frames - before.rx_frames;
  const uint32_t overflow = after.rx_queue_overflow - before.rx_queue_overflow;
  check("queued_burst_40",
        (queued == 40u && s_tx_done == 40u && rx == HAL_CAN_RX_QUEUE_LEN &&
         overflow == 40u - HAL_CAN_RX_QUEUE_LEN &&
         after.rx_hw_lost == before.rx_hw_lost)
            ? CHECK_PASS
            : CHECK_FAIL,
        "queued=%u done=%lu rx=%lu overflow=%lu", queued,
        (unsigned long)s_tx_done, (unsigned long)rx, (unsigned long)overflow);
  (void)hal_can_set_callbacks(h, on_rx, on_tx, NULL, NULL);
  drain(h, 10u);
  check("rx_callbacks",
        s_rx_cb == HAL_CAN_RX_QUEUE_LEN ? CHECK_PASS : CHECK_FAIL,
        "callbacks=%lu", (unsigned long)s_rx_cb);
}

static void check_timestamps(hal_can_t h) {
  (void)hal_can_set_mode(h, HAL_CAN_MODE_LOOPBACK);
  (void)hal_can_set_callbacks(h, NULL, on_tx, NULL, NULL);
  drain(h, 5u);
  reset_counts();
  hal_can_frame_t f;
  fill(&f, 0x555u, 0u, 8u);
  memset(f.data, 0x55, 8u); /* no stuff bits in the data field */
  for (uint32_t i = 0; i < 20u; i++) {
    (void)hal_can_send_frame_ex(h, &f, 50u, NULL);
  }
  drain(h, 30u);
  uint64_t min_gap = UINT64_MAX, max_gap = 0u;
  for (uint32_t i = 1; i < 20u && i < s_tx_done; i++) {
    const uint64_t gap = s_tx_ts[i] - s_tx_ts[i - 1u];
    min_gap = gap < min_gap ? gap : min_gap;
    max_gap = gap > max_gap ? gap : max_gap;
  }
  /* 112 bits at 500 kbit/s; 2 us of slack for the software timestamps. */
  check("tx_timestamps_500k",
        (s_tx_done == 20u && min_gap >= 222u && max_gap <= 226u) ? CHECK_PASS
                                                                 : CHECK_FAIL,
        "frames=%lu gap_min=%lu gap_max=%lu", (unsigned long)s_tx_done,
        (unsigned long)min_gap, (unsigned long)max_gap);
  hal_can_frame_t rx;
  while (hal_can_receive_frame_ex(h, &rx, NULL, 0u) == HAL_OK) {
  }
}

static hal_can_filter_ex_t filter(uint8_t type, uint8_t action, bool ext,
                                  uint32_t id1, uint32_t id2) {
  hal_can_filter_ex_t f;
  memset(&f, 0, sizeof(f));
  f.type = type;
  f.action = action;
  f.flags = ext ? HAL_CAN_FILTER_EXTENDED : 0u;
  f.id1 = id1;
  f.id2 = id2;
  return f;
}

static void check_filters(hal_can_t h) {
  (void)hal_can_set_mode(h, HAL_CAN_MODE_LOOPBACK);
  (void)hal_can_set_callbacks(h, NULL, NULL, NULL, NULL);
  (void)hal_can_set_unmatched_policy(h, false, false, true);
  uint8_t idx[36];
  unsigned std_ok = 0, ext_ok = 0, n = 0;
  for (uint32_t k = 0; k < 29u; k++) {
    hal_can_filter_ex_t f = filter(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT,
                                   false, 0x600u + k, 0x600u + k);
    if (hal_can_add_filter(h, &f, &idx[n]) == HAL_OK) {
      std_ok++;
      n++;
    }
  }
  for (uint32_t k = 0; k < 9u; k++) {
    hal_can_filter_ex_t f = filter(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT,
                                   true, 0x10000u + k, 0x10000u + k);
    if (hal_can_add_filter(h, &f, &idx[n]) == HAL_OK) {
      ext_ok++;
      n++;
    }
  }
  hal_can_frame_t f;
  fill(&f, 0x600u + 27u, 0u, 2u);
  const int last_std = exchange(h, h, &f, 5u);
  fill(&f, 0x600u + 28u, 0u, 2u);
  const int beyond = exchange(h, h, &f, 5u);
  check("filter_capacity",
        (std_ok == 28u && ext_ok == 8u && n == 36u && last_std == idx[27] &&
         beyond == -1)
            ? CHECK_PASS
            : CHECK_FAIL,
        "std=%u ext=%u last_std=%d beyond=%d", std_ok, ext_ok, last_std,
        beyond);
  for (unsigned i = 0; i < n; i++) {
    (void)hal_can_remove_filter(h, idx[i]);
  }

  uint8_t reject = 0u, accept = 0u;
  hal_can_filter_ex_t fx =
      filter(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_REJECT, false, 0x150u, 0x150u);
  (void)hal_can_add_filter(h, &fx, &reject);
  fx = filter(HAL_CAN_FILTER_RANGE, HAL_CAN_FILTER_ACCEPT, false, 0x100u,
              0x1FFu);
  (void)hal_can_add_filter(h, &fx, &accept);
  fill(&f, 0x150u, 0u, 2u);
  const int a = exchange(h, h, &f, 5u);
  fill(&f, 0x151u, 0u, 2u);
  const int b = exchange(h, h, &f, 5u);
  check("filter_precedence", (a == -1 && b == accept) ? CHECK_PASS : CHECK_FAIL,
        "0x150=%d 0x151=%d accept_index=%u", a, b, accept);

  /* 50 rounds of adding and removing a filter while frames flow. */
  unsigned wrong = 0, init_seen = 0;
  bool present = false;
  uint8_t toggled = 0u;
  for (unsigned round = 0; round < 50u; round++) {
    if (present) {
      (void)hal_can_remove_filter(h, toggled);
    } else {
      fx = filter(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT, false, 0x302u,
                  0x302u);
      (void)hal_can_add_filter(h, &fx, &toggled);
    }
    present = !present;
    init_seen +=
        (JH_REG32(FDCAN1_BASE + FDCAN_CCCR) & FDCAN_CCCR_INIT) != 0u ? 1u : 0u;
    for (uint32_t id = 0x301u; id <= 0x302u; id++) {
      fill(&f, id, 0u, 2u);
      const bool got = exchange(h, h, &f, 5u) >= 0;
      const bool expect = id == 0x302u && present;
      wrong += got != expect ? 1u : 0u;
    }
  }
  if (present) {
    (void)hal_can_remove_filter(h, toggled);
  }
  (void)hal_can_remove_filter(h, reject);
  (void)hal_can_remove_filter(h, accept);
  check("filter_changes_running",
        (wrong == 0u && init_seen == 0u) ? CHECK_PASS : CHECK_FAIL,
        "rounds=50 wrong=%u init_seen=%u", wrong, init_seen);

  const hal_status_t running =
      hal_can_set_unmatched_policy(h, true, true, false);
  (void)hal_can_stop(h);
  const hal_status_t stopped =
      hal_can_set_unmatched_policy(h, true, true, false);
  (void)hal_can_start(h);
  fill(&f, 0x123u, HAL_CAN_FRAME_RTR, 0u);
  const int rtr = exchange(h, h, &f, 5u);
  fill(&f, 0x124u, 0u, 2u);
  const int data = exchange(h, h, &f, 5u);
  check("remote_frames_refused",
        (running == HAL_EBUSY && stopped == HAL_OK && rtr == -1 &&
         data == HAL_CAN_FILTER_NONE)
            ? CHECK_PASS
            : CHECK_FAIL,
        "running=%d stopped=%d rtr=%d data=%d", (int)running, (int)stopped, rtr,
        data);
  (void)hal_can_stop(h);
  (void)hal_can_set_unmatched_policy(h, true, true, true);
  (void)hal_can_start(h);
}

/* ── Bus errors without a partner ───────────────────────────────────────── */

static void check_errors(hal_can_t h) {
  (void)hal_can_set_callbacks(h, NULL, on_tx, NULL, NULL);
  hal_can_frame_t f;
  hal_can_status_t before = {0}, st = {0};

  (void)hal_can_set_mode(h, HAL_CAN_MODE_FD);
  drain(h, 10u);
  reset_counts();
  (void)hal_can_get_status(h, &before);
  for (uint32_t i = 0; i < 5u; i++) {
    fill(&f, 0x321u, 0u, 8u);
    (void)hal_can_send_frame_ex(h, &f, 0u, NULL);
  }
  drain(h, 30u);
  (void)hal_can_get_status(h, &st);
  check("no_partner_retransmit",
        (s_tx_bus_off == 5u && st.bus_off_count == before.bus_off_count + 1u)
            ? CHECK_PASS
            : CHECK_FAIL,
        "bus_off_events=%lu bus_off_count=%lu lec=%u",
        (unsigned long)s_tx_bus_off, (unsigned long)st.bus_off_count,
        st.last_error);

  (void)hal_can_set_mode(h, HAL_CAN_MODE_FD | HAL_CAN_MODE_ONE_SHOT);
  drain(h, 10u);
  reset_counts();
  for (uint32_t i = 0; i < 5u; i++) {
    fill(&f, 0x322u, 0u, 8u);
    (void)hal_can_send_frame_ex(h, &f, 0u, NULL);
  }
  drain(h, 30u);
  check("no_partner_one_shot",
        (s_tx_failed == 5u && s_tx_done == 0u) ? CHECK_PASS : CHECK_FAIL,
        "failed_events=%lu", (unsigned long)s_tx_failed);

  (void)hal_can_set_mode(h, HAL_CAN_MODE_FD | HAL_CAN_MODE_MANUAL_RECOVERY);
  drain(h, 10u);
  reset_counts();
  fill(&f, 0x323u, 0u, 8u);
  (void)hal_can_send_frame_ex(h, &f, 0u, NULL);
  drain(h, 30u);
  hal_delay_ms(20u);
  hal_can_state_t held = HAL_CAN_STATE_STOPPED, after = HAL_CAN_STATE_STOPPED;
  (void)hal_can_get_state(h, &held);
  const hal_status_t recovered = hal_can_recover(h, 100u);
  (void)hal_can_get_state(h, &after);
  check("manual_recovery",
        (held == HAL_CAN_STATE_BUS_OFF && recovered == HAL_OK &&
         after != HAL_CAN_STATE_BUS_OFF)
            ? CHECK_PASS
            : CHECK_FAIL,
        "held=%d recover=%d after=%d", (int)held, (int)recovered, (int)after);

  (void)hal_can_set_mode(h, HAL_CAN_MODE_FD);
  drain(h, 10u);
  reset_counts();
  for (uint32_t i = 0; i < 6u; i++) {
    fill(&f, 0x330u + i, 0u, 8u);
    (void)hal_can_send_frame_ex(h, &f, 0u, NULL);
  }
  (void)hal_can_stop(h);
  drain(h, 10u);
  check("stop_ends_queued_frames",
        s_tx_stopped + s_tx_bus_off == 6u ? CHECK_PASS : CHECK_FAIL,
        "stopped=%lu bus_off=%lu", (unsigned long)s_tx_stopped,
        (unsigned long)s_tx_bus_off);
  (void)hal_can_start(h);
}

/* ── Shared bus (CN5-CN7 wired together) ────────────────────────────────── */

static void check_shared_bus(void) {
  hal_can_t h[3];
  for (uint8_t c = 0; c < 3u; c++) {
    h[c] = open_channel(c, 500000u, 2000000u, true, HAL_CAN_MODE_NORMAL);
  }
  if (!h[0] || !h[1] || !h[2]) {
    check("shared_bus", CHECK_FAIL, "create failed");
  } else {
    hal_can_frame_t f;
    unsigned got = 0, sent = 0;
    for (uint8_t from = 0; from < 3u; from++) {
      for (uint8_t kind = 0; kind < 2u; kind++) {
        fill(&f, 0x700u + from * 2u + kind,
             kind ? (HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS) : 0u,
             kind ? 64u : 8u);
        if (hal_can_send_frame(h[from], &f) != HAL_OK) {
          continue;
        }
        sent++;
        for (uint8_t to = 0; to < 3u; to++) {
          hal_can_frame_t rx;
          if (to != from &&
              hal_can_receive_frame_ex(h[to], &rx, NULL, 10u) == HAL_OK &&
              same(&f, &rx)) {
            got++;
          }
        }
      }
    }
    if (sent == 0u) {
      check("shared_bus", CHECK_SKIP,
            "no acknowledgement: CN5-CN7 not wired as one bus");
    } else {
      check("shared_bus", (sent == 6u && got == 12u) ? CHECK_PASS : CHECK_FAIL,
            "sent=%u received=%u/12", sent, got);
    }
  }
  for (uint8_t c = 0; c < 3u; c++) {
    if (h[c]) {
      hal_can_destroy(h[c]);
    }
  }
}

/* ── Mode changes, FD in the current mode, filter swaps ─────────────────── */

static void check_mode_rules(hal_can_t h) {
  (void)hal_can_set_callbacks(h, NULL, on_tx, NULL, NULL);
  hal_can_frame_t f;

  /* A blocking send that meets bus-off says so and leaves recovery to
   * hal_can_recover(). */
  (void)hal_can_set_mode(h, HAL_CAN_MODE_FD | HAL_CAN_MODE_MANUAL_RECOVERY);
  drain(h, 10u);
  fill(&f, 0x340u, 0u, 8u);
  const hal_status_t sent = hal_can_send_frame(h, &f);
  hal_delay_ms(20u);
  hal_can_state_t held = HAL_CAN_STATE_STOPPED, after = HAL_CAN_STATE_STOPPED;
  (void)hal_can_get_state(h, &held);
  const hal_status_t recovered = hal_can_recover(h, 100u);
  (void)hal_can_get_state(h, &after);
  check("manual_recovery_blocking_send",
        (sent == HAL_EBUS && held == HAL_CAN_STATE_BUS_OFF &&
         recovered == HAL_OK && after != HAL_CAN_STATE_BUS_OFF)
            ? CHECK_PASS
            : CHECK_FAIL,
        "sent=%d held=%d recover=%d after=%d", (int)sent, (int)held,
        (int)recovered, (int)after);

  /* Listen-only keeps frames waiting; switching to classic CAN ends all of
   * them instead of sending FD frames in the new mode. */
  (void)hal_can_set_mode(h, HAL_CAN_MODE_FD | HAL_CAN_MODE_LISTEN_ONLY);
  drain(h, 10u);
  reset_counts();
  hal_can_status_t before = {0}, st = {0};
  (void)hal_can_get_status(h, &before);
  for (uint32_t i = 0; i < 6u; i++) {
    fill(&f, 0x350u + i, HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS, 12u);
    (void)hal_can_send_frame_ex(h, &f, 0u, NULL);
  }
  (void)hal_can_set_mode(h, HAL_CAN_MODE_NORMAL);
  drain(h, 20u);
  (void)hal_can_get_status(h, &st);
  check("mode_change_ends_waiting_frames",
        (s_tx_stopped == 6u && st.tx_frames == before.tx_frames) ? CHECK_PASS
                                                                 : CHECK_FAIL,
        "stopped=%lu bus_off=%lu sent=%lu", (unsigned long)s_tx_stopped,
        (unsigned long)s_tx_bus_off,
        (unsigned long)(st.tx_frames - before.tx_frames));

  /* The channel can do FD, but its mode now has no FD. */
  fill(&f, 0x360u, HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS, 12u);
  const hal_status_t queued = hal_can_send_frame_ex(h, &f, 0u, NULL);
  const hal_status_t blocking = hal_can_send_frame(h, &f);
  check("fd_frame_refused_in_classic_mode",
        (queued == HAL_EUNSUPPORTED && blocking == HAL_EUNSUPPORTED)
            ? CHECK_PASS
            : CHECK_FAIL,
        "queued=%d blocking=%d", (int)queued, (int)blocking);

  /* A slot moving to the full extended list keeps its standard filter. */
  (void)hal_can_set_mode(h, HAL_CAN_MODE_LOOPBACK);
  (void)hal_can_set_callbacks(h, NULL, NULL, NULL, NULL);
  drain(h, 5u);
  (void)hal_can_set_unmatched_policy(h, false, false, true);
  uint8_t index = 0u;
  for (uint32_t k = 0; k < 8u; k++) {
    const hal_can_filter_ex_t fx =
        filter(HAL_CAN_FILTER_DUAL, HAL_CAN_FILTER_ACCEPT, true, 0x2000u + k,
               0x2000u + k);
    (void)hal_can_add_filter(h, &fx, &index);
  }
  const hal_can_filter_t std_filter = {0x370u, 0x7FFu, 0u};
  const hal_can_filter_t ext_filter = {0x18DAF110u, 0x1FFFFFFFu,
                                       HAL_CAN_FILTER_EXTENDED};
  const hal_status_t first = hal_can_set_filter(h, 0u, &std_filter);
  const hal_status_t swap = hal_can_set_filter(h, 0u, &ext_filter);
  fill(&f, 0x370u, 0u, 2u);
  const int got = exchange(h, h, &f, 5u);
  check("filter_swap_without_room",
        (first == HAL_OK && swap == HAL_ENOMEM && got == 0) ? CHECK_PASS
                                                            : CHECK_FAIL,
        "first=%d swap=%d frame_filter=%d", (int)first, (int)swap, got);
}

static void run_checks(void) {
  check_board();
  check_loopback();
  check_three_channels();
  hal_can_t h = open_channel(0u, 500000u, 2000000u, true, HAL_CAN_MODE_NORMAL);
  if (h == NULL) {
    check("channel0", CHECK_FAIL, "create failed");
  } else {
    check_queues(h);
    check_timestamps(h);
    check_filters(h);
    check_errors(h);
    check_mode_rules(h);
    hal_can_destroy(h);
  }
  check_shared_bus();
}

void app_start(void) {
  hal_debug_init_default();
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT_LOW);
  hal_gpio_set_mode(LED_BUSY, HAL_GPIO_OUTPUT_LOW);
}

void app_task0(void) {
  static uint32_t last_report, last_blink;
  if (!s_done) {
    hal_gpio_write(LED_BUSY, true);
    run_checks();
    hal_gpio_write(LED_BUSY, false);
    s_done = true;
  }
  unsigned pass = 0, fail = 0, skip = 0;
  for (unsigned i = 0; i < s_count; i++) {
    pass += s_checks[i].result == CHECK_PASS ? 1u : 0u;
    fail += s_checks[i].result == CHECK_FAIL ? 1u : 0u;
    skip += s_checks[i].result == CHECK_SKIP ? 1u : 0u;
  }
  const uint32_t now = hal_millis();
  if (hal_millis_interval_elapsed(now, &last_blink, fail == 0u ? 500u : 100u)) {
    s_led = !s_led;
    hal_gpio_write(HAL_LED_BUILTIN, s_led);
  }
  if (!hal_millis_interval_elapsed(now, &last_report, 2000u)) {
    return;
  }
  deb("JHCANHAT begin count=%u pass=%u fail=%u skip=%u", s_count, pass, fail,
      skip);
  for (unsigned i = 0; i < s_count; i++) {
    static const char *const kResult[] = {"FAIL", "PASS", "SKIP"};
    deb("JHCANHAT %s %s %s", s_checks[i].name, kResult[s_checks[i].result],
        s_checks[i].detail);
  }
  deb("JHCANHAT end");
}
