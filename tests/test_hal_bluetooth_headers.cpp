#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/bluetooth/hal_bluetooth_hid_host.h"

static_assert(HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN == 6u,
              "Bluetooth Classic address size changed");
static_assert(HAL_BLUETOOTH_HID_REPORT_MAX_LEN == 32u,
              "Bluetooth HID report size changed");

int main() {
  hal_bluetooth_classic_t classic = nullptr;
  hal_bluetooth_hid_host_t hid = nullptr;
  return classic != nullptr || hid != nullptr;
}
