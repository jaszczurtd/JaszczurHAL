# 29 - Bluetooth gamepad

Bluetooth Classic examples for all three public layers. The base image uses
the stack-independent normalized gamepad adapter. The `classic-scan` variant
builds only the manager and prints copied inquiry/SDP results. The `hid-host`
variant connects to any discovered HID service and exposes its copied report
descriptor and raw reports without gamepad assumptions. The `ble` variant adds
BLE to the gamepad image to exercise the shared controller runtime.

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

Build an isolated public layer or the combined BLE + Classic variant with:

```bash
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2040 --board picow --variant classic-scan
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2040 --board picow --variant hid-host
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2040 --board picow --variant ble
```

The `classic-scan` serial console assigns each observed address a volatile
index and never prints the address itself. It performs one inquiry at startup
and serializes pending SDP queries after inquiry finishes. The available
commands are:

- `SCAN` and `STOP` control a ten-second inquiry window;
- `SDP n` repeats service discovery for observed peer `n`;
- `PAIR n`, followed by `AUTHORIZE` or `REJECT`, applies an explicit
  local pairing decision;
- `SAVE n` publishes the authenticated peer after application-specific
  validation, while `FORGET n` removes it;
- `INFO` prints bounded-queue, pairing, state, and peer-count diagnostics.

This example opens the manager without a persistent provider, so saved peers
remain valid only until restart. A production application must replace the
serial `AUTHORIZE` command with a trusted local gesture and call `SAVE`
only after its profile has validated the peer.

On first boot, start the hardware-validated 8BitDo Zero 2 model 80EH in Android
D-input mode with `B+Start`, then hold `Select` until its pairing LED flashes.
The example opens a bounded discovery window and authorizes a pending Just
Works or legacy PIN `0000` request. If a window expires without selecting a
device, the example opens a new one. After a bond is stored, use the normal
`Start` power-on path for reconnect; do not put the controller back into
pairing mode. Passing a bond provider to `hal_gamepad_open_ex()` preserves the
accepted address across restarts; the compatibility provider is a one-slot
adapter over the indexed Classic manager. Only this controller, mode, and the
`rp2350-arm:pico2w` host have passed the complete gamepad hardware gate; other
combinations require separate validation.

The generic HID variant deliberately rejects pairing until
`localPairingConsent()` is connected to a trusted local gesture. After local
authorization, a copied descriptor, and a flowing Input report, it asks the
Classic manager to save the peer. This keeps the example safe by default while
showing the complete policy boundary.

## Snapshot model

`hal_gamepad_snapshot_next()` returns input changes without exposing BTstack
types. Button bit 0 represents HID Button 1, bit 1 represents HID Button 2,
and so on. Present axes use the `HAL_GAMEPAD_AXIS_*` indexes and are normalized
to `-32767..32767`. The D-pad is a mask of `HAL_GAMEPAD_DPAD_*` directions.

The queue is bounded. `HAL_EOVERFLOW` acknowledges lost intermediate states;
the caller continues draining to receive the newest retained state. Connection
and disconnection snapshots set or clear all controls, so applications cannot
retain a pressed button after a lost link.

The base example demonstrates initialization after scheduler start, pairing,
authorization, reconnect, state diagnostics, overflow handling and snapshot
draining. The `ble` variant adds a passive BLE Observer to the Classic gamepad
profile. At startup it releases and reacquires each profile while the other
one keeps the shared CYW43/BTstack host alive. Its `INFO`, `BLE_START`,
`BLE_STOP`, and `DISCONNECT` commands exercise concurrent scanning and HID
reconnection. Periodic diagnostics report stack use, HCI/L2CAP/link-key/HID
pool high-water and allocation failures, plus HCI transport traffic and drain
budget hits. RP builds reserve a measured 4 KiB core-0 stack for this verbose
diagnostic path. The variant uses KV key `0xd001` so its gamepad bond remains
compatible with the doomConsole hardware regression image. It does not
advertise a BLE service.

The original ESP32 Classic/HID implementation is covered by an ESP-IDF
compile/link fixture; ESP targets are not yet supported by the native example
dispatcher.
