#include "hal/bluetooth/hal_gamepad.h"

_Static_assert(HAL_GAMEPAD_BUTTON_COUNT == 32u, "gamepad button count changed");
_Static_assert(HAL_GAMEPAD_AXIS_COUNT == 9u, "gamepad axis count changed");

int main(void) {
  hal_gamepad_t gamepad = NULL;
  hal_gamepad_snapshot_t snapshot = {0};
  hal_gamepad_info_t info = {0};
  return gamepad != NULL || snapshot.connected || info.known_device;
}
