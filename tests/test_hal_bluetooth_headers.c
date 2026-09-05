#include "hal/bluetooth/hal_bluetooth_a2dp_sink.h"
#include "hal/bluetooth/hal_bluetooth_avrcp_target.h"
#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/bluetooth/hal_bluetooth_hid_host.h"

_Static_assert(HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN == 6u,
               "Bluetooth Classic address size changed");
_Static_assert(HAL_BLUETOOTH_HID_REPORT_MAX_LEN == 32u,
               "Bluetooth HID report size changed");
_Static_assert(HAL_BLUETOOTH_A2DP_PCM_MAX_FRAMES == 129u,
               "Bluetooth A2DP PCM block size changed");

int main(void) {
  hal_bluetooth_classic_t classic = NULL;
  hal_bluetooth_hid_host_t hid = NULL;
  hal_bluetooth_a2dp_sink_t sink = NULL;
  hal_bluetooth_avrcp_target_t avrcp = NULL;
  return classic != NULL || hid != NULL || sink != NULL || avrcp != NULL;
}
