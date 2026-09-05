#include "hal/bluetooth/hal_bluetooth_a2dp_sink.h"
#include "hal/bluetooth/hal_bluetooth_avrcp_target.h"
#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/bluetooth/hal_bluetooth_hid_host.h"

static_assert(HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN == 6u,
              "Bluetooth Classic address size changed");
static_assert(HAL_BLUETOOTH_HID_REPORT_MAX_LEN == 32u,
              "Bluetooth HID report size changed");
static_assert(HAL_BLUETOOTH_A2DP_PCM_MAX_FRAMES == 129u,
              "Bluetooth A2DP PCM block size changed");

int main() {
  hal_bluetooth_classic_t classic = nullptr;
  hal_bluetooth_hid_host_t hid = nullptr;
  hal_bluetooth_a2dp_sink_t sink = nullptr;
  hal_bluetooth_avrcp_target_t avrcp = nullptr;
  return classic != nullptr || hid != nullptr || sink != nullptr ||
         avrcp != nullptr;
}
