#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/bluetooth/hal_gamepad.h"
#include "hal/core/hal_app.h"
#include "hal/core/hal_target.h"
#include "jh_board_config.h"
#include "jh_link_contract.h"

#if !HAL_TARGET_IS_ESP32
#error "The ESP32 gamepad fixture requires the exact esp32 target"
#endif

#if !HAL_BOARD_IS_ESP32_DEVKITC_V4
#error "The ESP32 gamepad fixture requires esp32-devkitc-v4"
#endif

namespace {

volatile bool s_run_link_probe;

void link_probe(void) {
  if (!s_run_link_probe) {
    return;
  }
  hal_gamepad_t gamepad = nullptr;
  hal_gamepad_info_t info{};
  hal_gamepad_snapshot_t snapshot{};
  (void)hal_gamepad_open(&gamepad);
  (void)hal_gamepad_poll(gamepad);
  (void)hal_gamepad_get_info(gamepad, &info);
  (void)hal_gamepad_snapshot(gamepad, &snapshot);
  (void)hal_gamepad_snapshot_next(gamepad, &snapshot);
  (void)hal_gamepad_pairing_open(gamepad);
  (void)hal_gamepad_pairing_authorize(gamepad);
  (void)hal_gamepad_reconnect(gamepad);
  (void)hal_gamepad_disconnect(gamepad);
  (void)hal_gamepad_close(gamepad);
}

} // namespace

void app_start(void) {
  JH_BOARD_CONTRACT_SYMBOL();
  link_probe();
}

void app_task0(void) { vTaskDelay(pdMS_TO_TICKS(1000u)); }
