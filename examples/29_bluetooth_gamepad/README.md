# 29 - Bluetooth gamepad

Bluetooth Classic HID gamepad example using the public, stack-independent
snapshot API. The base image runs only the Classic profile. The `ble` variant
also initializes and polls BLE to compile and exercise both profiles on the
shared controller runtime.

## Build and run

```bash
./scripts/examples_dispatcher.py build --target rp2040 \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target rp2350-arm \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target stm32g474 \
  --example 29_bluetooth_gamepad
```

The default boards are RP2040 `picow`, RP2350 ARM `pico2w`, and STM32G474
`nucleo-g474re-pim730`. RP2350 RISC-V is unsupported because its CYW43
Bluetooth transport is not enabled. The original ESP32 Bluedroid backend has
its own compile/link fixture in `tests/fixtures/esp32_gamepad`; it has not yet
passed a radio hardware gate.

Build only the combined BLE + Classic variant with:

```bash
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2040 --board picow --variant ble
```

On first boot, put the gamepad into pairing mode. The example opens a bounded
discovery window and authorizes a pending Just Works or legacy PIN `0000`
request. The accepted address can be reconnected during the same firmware
runtime. The application must be prepared to reopen pairing after a restart;
persistent HAL device selection is not part of this release.

## Snapshot model

`hal_gamepad_snapshot_next()` returns input changes without exposing BTstack
types. Button bit 0 represents HID Button 1, bit 1 represents HID Button 2,
and so on. Present axes use the `HAL_GAMEPAD_AXIS_*` indexes and are normalized
to `-32767..32767`. The D-pad is a mask of `HAL_GAMEPAD_DPAD_*` directions.

The queue is bounded. `HAL_EOVERFLOW` acknowledges lost intermediate states;
the caller continues draining to receive the newest retained state. Connection
and disconnection snapshots set or clear all controls, so applications cannot
retain a pressed button after a lost link.

The example demonstrates initialization after scheduler start, pairing,
authorization, reconnect, state diagnostics, overflow handling and snapshot
draining. The `ble` variant deliberately does not advertise a BLE service; it
only proves that both public profiles acquire, poll and release the same
CYW43/BTstack host. It is not available on either ESP target: ESP32-S3 exposes
base BLE without Classic HID, while the original ESP32 exposes the Classic
gamepad without the public BLE API.
