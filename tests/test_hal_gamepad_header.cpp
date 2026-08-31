#include "hal/bluetooth/hal_gamepad.h"

static_assert(HAL_GAMEPAD_BUTTON_COUNT == 32u, "gamepad button count changed");
static_assert(HAL_GAMEPAD_AXIS_COUNT == 9u, "gamepad axis count changed");

int main() {
  hal_gamepad_t gamepad = nullptr;
  hal_gamepad_snapshot_t snapshot{};
  hal_gamepad_info_t info{};
  return gamepad != nullptr || snapshot.connected || info.known_device;
}
