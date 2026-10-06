#pragma once

/* Targets this example builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)                                                     \
  X(HAL_TARGET_STM32G474)

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(CLASSIC_SCAN, "Classic Bluetooth device and service discovery example",    \
    EXAMPLE_BLUETOOTH_CLASSIC_SCAN = 1)                                        \
  X(HID_HOST, "Generic Classic HID descriptor and report example",             \
    EXAMPLE_BLUETOOTH_HID_HOST = 1)                                            \
  X(BLE, "Classic gamepad example with a BLE observer", HAL_ENABLE_BLE = 1,    \
    HAL_ENABLE_KV = 1, HAL_GAMEPAD_EXAMPLE_ENABLE_BLE = 1)

/* The gamepad application, or the Classic tool a variant selects instead;
 * each enables only the Bluetooth modules it uses. */
#if defined(EXAMPLE_BLUETOOTH_CLASSIC_SCAN)
#define HAL_ENABLE_BLUETOOTH_CLASSIC
#elif defined(EXAMPLE_BLUETOOTH_HID_HOST)
#define HAL_ENABLE_BLUETOOTH_HID_HOST
#else
#define HAL_ENABLE_BLUETOOTH_GAMEPAD
#endif

#if defined(HAL_TARGET_RP2040) || defined(HAL_TARGET_RP2350_ARM)
/* Formatting pool and transport diagnostics uses about 3 KiB of stack.
 * Reserve 4 KiB on core 0, matching the margin used by the A2DP example.
 */
#define HAL_RP_CORE0_STACK_SIZE 4096
#define HAL_RP_CORE1_STACK_SIZE 2048
#endif
