/**
 * @file app.c
 * @brief CAN FD on every channel of the board with the STM32G474 built-in
 *        FDCAN controllers.
 *
 * Opens the CAN channels the board profile describes (three on the CAN-FD HAT
 * of the nucleo-g474re-canhat profile) and sends one CAN FD frame per channel
 * every second through the send queue. Received frames and failed sends
 * arrive through callbacks run by hal_can_service(). By default each channel
 * runs in internal loopback and receives its own frames, which needs no
 * wiring; with EXAMPLE_SHARED_BUS set to 1 the channels share a wired bus and
 * each one receives the frames of the others. The built-in LED toggles after
 * every second in which each channel received a frame and stays off
 * otherwise.
 */

#include <hal/can/hal_can.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#if !HAL_TARGET_IS_STM32G474
#error "21_stm32g474_fdcan_native is only supported on HAL_TARGET_STM32G474"
#endif
#if HAL_BOARD_CAN_CHANNEL_COUNT < 1
#error "21_stm32g474_fdcan_native needs a board profile with CAN channels"
#endif

/* 0: internal loopback, no wiring. 1: the channels share a terminated bus
 * (on the CAN-FD HAT: CN5-CN7 joined, termination jumpers on two of them). */
#ifndef EXAMPLE_SHARED_BUS
#define EXAMPLE_SHARED_BUS 0
#endif

#define EXAMPLE_BASE_ID 0x120u
#define EXAMPLE_PERIOD_MS 1000u

static hal_can_t s_can[HAL_BOARD_CAN_CHANNEL_COUNT];
static uint32_t s_received[HAL_BOARD_CAN_CHANNEL_COUNT];
static uint32_t s_counter = 0u;
static uint32_t s_last_send_ms = 0u;
static bool s_led = false;

static void on_rx(hal_can_t h, const hal_can_frame_t *frame,
                  const hal_can_rx_info_t *info, void *user) {
  (void)h;
  const uint32_t channel = (uint32_t)(uintptr_t)user;
  s_received[channel]++;
  deb("CAN%lu RX id=0x%03lX len=%u%s seq=%u t=%lu us",
      (unsigned long)(channel + 1u), (unsigned long)frame->id,
      (unsigned)frame->len,
      (frame->flags & HAL_CAN_FRAME_BRS) != 0u ? " BRS" : "",
      (unsigned)frame->data[0], (unsigned long)info->timestamp_us);
}

static void on_tx(hal_can_t h, const hal_can_tx_event_t *event, void *user) {
  (void)h;
  if (event->reason != HAL_CAN_TX_DONE) {
    derr("CAN%lu TX tag=%lu not sent, reason=%u",
         (unsigned long)((uint32_t)(uintptr_t)user + 1u),
         (unsigned long)event->tag, (unsigned)event->reason);
  }
}

static hal_status_t open_channel(uint8_t channel) {
  hal_can_config_t cfg;
  hal_status_t st = hal_can_board_config(channel, &cfg);
  if (st == HAL_OK) {
    st = hal_can_create(&cfg, &s_can[channel]);
  }
  if (st == HAL_OK) {
    st = hal_can_set_mode(
        s_can[channel],
        HAL_CAN_MODE_FD |
            (EXAMPLE_SHARED_BUS ? HAL_CAN_MODE_NORMAL : HAL_CAN_MODE_LOOPBACK));
  }
  if (st == HAL_OK) {
    st = hal_can_set_callbacks(s_can[channel], on_rx, on_tx, NULL,
                               (void *)(uintptr_t)channel);
  }
  if (st != HAL_OK && s_can[channel] != NULL) {
    hal_can_destroy(s_can[channel]);
    s_can[channel] = NULL;
  }
  return st;
}

void app_start(void) {
  hal_debug_init_default();
#if defined(HAL_LED_BUILTIN)
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT_LOW);
#endif
  deb("");
  deb("=== JaszczurHAL STM32G474 native FDCAN example ===");
  deb("%s, %d channel(s)", EXAMPLE_SHARED_BUS ? "shared bus" : "loopback",
      (int)HAL_BOARD_CAN_CHANNEL_COUNT);
  for (uint8_t c = 0; c < HAL_BOARD_CAN_CHANNEL_COUNT; c++) {
    const hal_status_t st = open_channel(c);
    if (st == HAL_OK) {
      deb("CAN%u ready", (unsigned)(c + 1u));
    } else {
      derr("CAN%u setup FAILED: %s", (unsigned)(c + 1u),
           hal_status_to_string(st));
    }
  }
  s_last_send_ms = hal_millis();
}

void app_task0(void) {
  for (uint8_t c = 0; c < HAL_BOARD_CAN_CHANNEL_COUNT; c++) {
    if (s_can[c] != NULL) {
      (void)hal_can_service(s_can[c], 0);
    }
  }
  if (!hal_millis_interval_elapsed(hal_millis(), &s_last_send_ms,
                                   EXAMPLE_PERIOD_MS)) {
    return;
  }

  /* Judge the second that just ended, then start the next one. */
  bool all_received = true;
  for (uint8_t c = 0; c < HAL_BOARD_CAN_CHANNEL_COUNT; c++) {
    all_received = all_received && s_can[c] != NULL && s_received[c] > 0u;
    s_received[c] = 0u;
  }
  s_led = all_received && !s_led;
#if defined(HAL_LED_BUILTIN)
  hal_gpio_write(HAL_LED_BUILTIN, s_led);
#endif

  for (uint8_t c = 0; c < HAL_BOARD_CAN_CHANNEL_COUNT; c++) {
    if (s_can[c] == NULL) {
      continue;
    }
    hal_can_frame_t frame = {};
    frame.id = EXAMPLE_BASE_ID + c;
    frame.len = 12u;
    frame.dlc = hal_can_bytes_to_dlc(frame.len);
    frame.flags = HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS;
    frame.data[0] = (uint8_t)s_counter;
    frame.data[1] = c;
    const hal_status_t st = hal_can_send_frame_ex(s_can[c], &frame, 0u, NULL);
    if (st != HAL_OK) {
      derr("CAN%u TX not queued: %s", (unsigned)(c + 1u),
           hal_status_to_string(st));
    }
  }
  s_counter++;
}
