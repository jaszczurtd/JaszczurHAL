<a id="29---bluetooth-gamepad"></a>

# 29 - Bluetooth gamepad and HID input

This example reads buttons, axes, and D-pad directions from a Bluetooth
Classic gamepad. It exposes a common input format without requiring the
application to use BTstack types. Additional variants discover Classic
devices and services, receive raw HID reports, or scan BLE while a gamepad
is connected.

| Variant | Behavior |
|---|---|
| Base | Connects a gamepad, reads input changes, and stores the accepted device for reconnection after restart. |
| `classic-scan` | Discovers Classic devices and services through inquiry and SDP. |
| `hid-host` | Connects to a discovered HID service, copies its descriptor, and receives raw reports without interpreting them as gamepad input. |
| `ble` | Adds passive BLE scanning alongside the Classic gamepad on the shared CYW43 controller. |

## Build and run

Run from the repository root:

```bash
./scripts/examples_dispatcher.py build --target rp2040 \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target rp2350-arm \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target stm32g474 \
  --example 29_bluetooth_gamepad
```

The default boards are `picow` for RP2040, `pico2w` for RP2350 ARM, and
`nucleo-g474re-pim730` for STM32G474. RP2350 RISC-V is not supported because
CYW43 Bluetooth transport is not enabled for that target.

To build a specific variant:

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

The Bluedroid implementation for the original ESP32 has a separate
compile-and-link test project at `tests/fixtures/esp32_gamepad`. Its radio
operation has not yet been verified on hardware. The shared example build
script does not support ESP targets.

## Pair a gamepad

Complete hardware tests used an 8BitDo Zero 2, model 80EH, in Android D-input
mode with the `rp2350-arm:pico2w` host. Other controllers, modes, and boards
need separate tests.

For initial pairing, power on the gamepad with `B+Start`, then hold `Select`
until its pairing LED flashes. The example opens a time-limited discovery
window and automatically accepts a pending Just Works or legacy PIN `0000`
request. If the window expires before a device is selected, it opens another
window. **Automatic approval is an example setting, not a complete secure
pairing policy for a product.**

Once the device has been stored, power on the gamepad normally with `Start`.
Do not enter pairing mode again just to reconnect. The storage callbacks
passed to `hal_gamepad_open_ex()` retain the accepted device across restarts.
The gamepad compatibility interface provides one storage slot through the
indexed Bluetooth Classic device manager. KV key `0xd001` keeps the stored
gamepad compatible with the doomConsole hardware-regression image.

## Discover devices and services

The `classic-scan` variant starts with a ten-second inquiry. After inquiry
finishes, it processes pending SDP service queries one at a time. Each
discovered device receives a temporary index `n`; Bluetooth addresses are
not printed.

| Command | Behavior |
|---|---|
| `SCAN`, `STOP` | Start or stop the discovery window. |
| `SDP n` | Repeat service discovery for device `n`. |
| `PAIR n` | Start pairing with device `n`. |
| `AUTHORIZE`, `REJECT` | Accept or reject the pending pairing request. |
| `SAVE n`, `FORGET n` | Request that device `n` be saved or removed. |
| `INFO` | Print state, pairing details, queue counters, and the device count. |

This variant does not configure persistent storage: saved devices are kept
only until restart. `AUTHORIZE` is a manual test command. In a product,
connect approval to a trusted local user action and save a device only after
validating it for the intended profile. Discovery or a `SAVE` command alone
does not replace that validation.

## Receive raw HID reports

The `hid-host` variant selects a discovered HID service and exposes a copy
of its descriptor and raw reports. The operator decides how to handle a
pending pairing request with the `AUTHORIZE` or `REJECT` serial command.
`SCAN` and `INFO` are also available. Service discovery does not grant
pairing approval; the serial command grants it in this implementation.

The example requests that a device be saved only after authorization,
descriptor retrieval, and receipt of an Input report. It does not configure
persistent storage. Before using this design in a product, replace serial
approval with a trusted consent mechanism and define which descriptors and
reports the application can accept.

## Snapshot model

`hal_gamepad_snapshot_next()` returns successive input changes. Button bit 0
represents HID Button 1, bit 1 represents HID Button 2, and so on. Axes use
`HAL_GAMEPAD_AXIS_*` indexes and the range `-32767..32767`. The D-pad is a
mask of `HAL_GAMEPAD_DPAD_*` directions.

The queue has a fixed capacity. `HAL_EOVERFLOW` means intermediate states
were lost; continue reading to reach the newest retained state. Connection
and disconnection records describe all controls, including clearing them
after a lost connection. This prevents a disconnected gamepad from leaving
a button pressed in the application.

When FreeRTOS is enabled, initialization runs after the scheduler starts.
The example covers pairing, authorization, reconnection, input updates, and
queue-overflow handling.

## Run BLE and Classic together

The `ble` variant scans as a passive BLE Observer; it does not advertise a
BLE service. At startup it closes and reopens each profile while the other
continues using the shared CYW43 controller and BTstack host. Use `INFO`,
`BLE_START`, `BLE_STOP`, and `DISCONNECT` to test BLE scanning alongside
the gamepad and HID reconnection.

Diagnostics report stack use, maximum HCI/L2CAP/link-key/HID pool usage,
allocation failures, and HCI traffic. They also count occasions when a
processing pass reaches its event limit. RP configurations reserve 4 KiB
for the core-0 stack, based on measurements of this example's detailed
diagnostics.
