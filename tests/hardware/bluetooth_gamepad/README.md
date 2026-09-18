# Bluetooth Classic HID gamepad probe

`tests/hardware/bluetooth_gamepad` owns the private pre-API Classic HID Host
probe. The sanitized
`tests/fixtures/bluetooth_gamepad/zero2_android_dinput.json` test data keeps the
137-byte report descriptor, PnP identity, SDP metadata, all twelve input
states, the undeclared trailing input byte, and a repeated raw report. It omits
Bluetooth addresses, link keys, host identity, and USB serial numbers.

The firmware initializes the shared HCI/L2CAP runtime, a volatile one-entry
link-key database, the SDP client, one HID Host connection, and one event
handler. Inquiry starts only after the `DISCOVER` serial command and closes
after 120 seconds or after one matching device is accepted. A candidate must
have the peripheral device class, the captured name, a Classic HID service,
and the captured PnP identity. The private selector must not be combined with
public BLE or the earlier Stage 1 probe.

The private parser consumes the HID report descriptor instead of Zero 2 byte
offsets. It accepts Generic Desktop Game Pad and Joystick application
collections and normalizes up to 32 buttons, nine desktop axes, and one hat
switch into fixed-memory snapshots. The C6 limits are 256 descriptor bytes,
32 bytes per input report, and 16 queued snapshots. Unknown usages are ignored;
malformed or oversized descriptors, truncated or oversized reports, unknown
report IDs, duplicate mapped usages, and queue overflow are reported through
bounded diagnostics. Repeated reports that do not change the state do not add
another snapshot.

`test_bluetooth_gamepad_parser` uses the sanitized data independently of the
hardware fixture. It covers the captured reports, descriptor-driven layouts,
idempotent input, reconnect state clearing, malformed/truncated input, unknown report IDs
and usages, duplicate usages, queue overflow, and the absence of dynamic
allocation during parser operation.

Build the required Pico 2 W image:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-hid
```

Upload it and run the hardware verifier on the resulting CDC port:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-hid \
  --port /dev/ttyACM0

python3 tests/hardware/bluetooth_gamepad/verify_zero2.py \
  --port /dev/ttyACM0
```

When prompted, start the Zero 2 in Android D-input mode with `B+Start`, then
hold `Select` until the pairing LED flashes. The verifier authorizes the
pairing method reported by the controller, checks the captured descriptor and
all controls, runs the disconnect/reconnect and power-cycle cases, and keeps a
continuous connected interval for 30 minutes. During the first reconnect it
asks for a held control so the disconnect path can release active input.

The verifier writes `zero2_pico2w_c6_result.json`; the earlier C5 result remains
the baseline from before parser integration. The C6 report contains
target/library versions, durations, transport counters, parser diagnostics,
and pool high-water marks. It must not contain Bluetooth addresses, link-key
material, host identity, a serial port name, or a USB serial number. The
ELF/map and a symbol listing must also show `ENABLE_CLASSIC` HID Host, SDP
client, HID parser, and the memory link-key database while excluding ATT,
GATT, SM, RFCOMM, SDP server, HID Device, and audio profiles.
