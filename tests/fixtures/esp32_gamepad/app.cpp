#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/bluetooth/hal_bluetooth_hid_host.h"
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

  hal_bluetooth_classic_t classic = nullptr;
  hal_bluetooth_hid_host_t hid = nullptr;
  hal_bluetooth_classic_info_t classic_info{};
  hal_bluetooth_classic_scan_result_t scan_result{};
  hal_bluetooth_classic_peer_t peer{};
  hal_bluetooth_hid_info_t hid_info{};
  hal_bluetooth_hid_report_t report{};
  uint8_t descriptor[HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN]{};
  size_t descriptor_length = 0u;
  size_t peer_count = 0u;
  (void)hal_bluetooth_classic_open(&classic);
  (void)hal_bluetooth_classic_poll(classic);
  (void)hal_bluetooth_classic_get_info(classic, &classic_info);
  (void)hal_bluetooth_classic_scan_start(classic, 1000u);
  (void)hal_bluetooth_classic_scan_result_next(classic, &scan_result);
  (void)hal_bluetooth_classic_sdp_query(classic, &scan_result.address);
  (void)hal_bluetooth_classic_pair(classic, &scan_result.address);
  (void)hal_bluetooth_classic_pairing_authorize(classic);
  (void)hal_bluetooth_classic_pairing_reject(classic);
  (void)hal_bluetooth_classic_peer_count(classic, &peer_count);
  (void)hal_bluetooth_classic_peer_get(classic, 0u, &peer);
  (void)hal_bluetooth_hid_host_open(classic, &hid);
  (void)hal_bluetooth_hid_host_get_info(hid, &hid_info);
  (void)hal_bluetooth_hid_host_connect(hid, &scan_result.address);
  (void)hal_bluetooth_hid_host_descriptor(hid, descriptor, sizeof(descriptor),
                                          &descriptor_length);
  (void)hal_bluetooth_hid_host_report_next(hid, &report);
  report.type = HAL_BLUETOOTH_HID_REPORT_OUTPUT;
  (void)hal_bluetooth_hid_host_report_send(hid, &report);
  (void)hal_bluetooth_hid_host_report_request(
      hid, HAL_BLUETOOTH_HID_REPORT_INPUT, 0u);
  (void)hal_bluetooth_hid_host_disconnect(hid);
  (void)hal_bluetooth_hid_host_close(hid);
  (void)hal_bluetooth_classic_peer_forget(classic, &peer.address);
  (void)hal_bluetooth_classic_scan_stop(classic);
  (void)hal_bluetooth_classic_close(classic);
}

} // namespace

void app_start(void) {
  JH_BOARD_CONTRACT_SYMBOL();
  link_probe();
}

void app_task0(void) { vTaskDelay(pdMS_TO_TICKS(1000u)); }
