# Bluetooth Classic non-gamepad HID Host hardware test

`tests/hardware/bluetooth_classic_hid_device` is a private, test-only BTstack
HID Device fixture. A Pico W advertises a standards-based Classic HID mouse
with a Generic Desktop descriptor and emits alternating relative-motion input
reports. This is not a public HID-device API. `device_tree.json` describes
the hardware of both assemblies: `picow` (Pico W, the default) and `pico2w`
(Pico 2 W). Neither needs external wiring. Build the Pico W fixture and the
public `HID_HOST` example for the Pico 2 W host:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hid_device --assembly picow
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --assembly pico2w --variant HID_HOST
```

Flash each image only to its designated board. `INFO` on the fixture must show
`controller=1`. On the host, use `SCAN`, approve the pending Just Works request
with `AUTHORIZE`, then use `INFO`. Acceptance requires
`JHC85-HID-PASS`, `descriptor=1`, `input=1`, and `saved=1`; the fixture must
show `hid=1` and a non-zero report count. Neither console prints Bluetooth
addresses or link keys.

Both sides keep link keys in RAM only, so this test does not cover pairing
that survives a reset.
