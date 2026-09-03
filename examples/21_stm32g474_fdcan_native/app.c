/**
 * @file app.c
 * @brief STM32G474 native FDCAN1 CAN FD example.
 */

#include <hal/can/hal_can.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#if !HAL_TARGET_IS_STM32G474
#error "21_stm32g474_fdcan_native is only supported on HAL_TARGET_STM32G474"
#endif

#define EXAMPLE_FDCAN_RX_PIN 11u
#define EXAMPLE_FDCAN_TX_PIN 12u
#define EXAMPLE_CAN_ID 0x123u

static hal_can_t s_can = NULL;
static uint32_t s_counter = 0u;

static void log_rx_frame(const hal_can_frame_t *frame) {
  deb("RX id=0x%lX len=%u flags=0x%02X", (unsigned long)frame->id,
      (unsigned)frame->len, (unsigned)frame->flags);
}

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL STM32G474 native FDCAN example ===");
  deb("Initialising FDCAN1 on PA11/PA12...");

  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_STM32G474_FDCAN;
  cfg.stm32g474_fdcan.rx_pin = EXAMPLE_FDCAN_RX_PIN;
  cfg.stm32g474_fdcan.tx_pin = EXAMPLE_FDCAN_TX_PIN;
  cfg.stm32g474_fdcan.arbitration_bitrate_hz = 500000u;
  cfg.stm32g474_fdcan.data_bitrate_hz = 2000000u;
  cfg.stm32g474_fdcan.enable_fd = true;
  cfg.stm32g474_fdcan.one_shot_tx = true;

  s_can = hal_can_create(&cfg);
  if (!s_can) {
    derr("FDCAN init FAILED");
    return;
  }

  if (!hal_can_set_mode(s_can, HAL_CAN_MODE_FD | HAL_CAN_MODE_ONE_SHOT)) {
    derr("FDCAN mode setup FAILED");
    hal_can_destroy(s_can);
    s_can = NULL;
    return;
  }

  hal_can_filter_t filter = {};
  filter.id = EXAMPLE_CAN_ID;
  filter.mask = HAL_CAN_STD_ID_MASK;
  filter.flags = 0u;
  if (!hal_can_set_filter(s_can, 0u, &filter)) {
    derr("FDCAN filter setup FAILED");
    hal_can_destroy(s_can);
    s_can = NULL;
    return;
  }

  deb("FDCAN init OK");
}

void app_task0(void) {
  if (!s_can) {
    hal_delay_ms(1000u);
    return;
  }

  hal_can_frame_t frame = {};
  frame.id = EXAMPLE_CAN_ID;
  frame.len = 12u;
  frame.dlc = hal_can_bytes_to_dlc(frame.len);
  frame.flags = HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS;
  frame.data[0] = (uint8_t)(s_counter & 0xFFu);
  frame.data[1] = (uint8_t)((s_counter >> 8) & 0xFFu);
  frame.data[2] = (uint8_t)((s_counter >> 16) & 0xFFu);
  frame.data[3] = (uint8_t)((s_counter >> 24) & 0xFFu);
  frame.data[4] = 0x4Au;
  frame.data[5] = 0x48u;
  frame.data[6] = 0x46u;
  frame.data[7] = 0x44u;
  frame.data[8] = 0x43u;
  frame.data[9] = 0x41u;
  frame.data[10] = 0x4Eu;
  frame.data[11] = 0x00u;

  if (hal_can_send_frame(s_can, &frame)) {
    deb("TX FD id=0x%lX seq=%lu OK", (unsigned long)frame.id,
        (unsigned long)s_counter);
  } else {
    derr("TX FD id=0x%lX seq=%lu FAIL", (unsigned long)frame.id,
         (unsigned long)s_counter);
  }

  while (hal_can_available(s_can)) {
    hal_can_frame_t rx = {};
    if (!hal_can_receive_frame(s_can, &rx)) {
      break;
    }
    log_rx_frame(&rx);
  }

  s_counter++;
  hal_delay_ms(1000u);
}
