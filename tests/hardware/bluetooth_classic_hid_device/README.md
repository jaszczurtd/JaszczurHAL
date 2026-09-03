# Bluetooth Classic HID device fixture

The complete procedure and recorded result are maintained in the
[central hardware-fixture reference](../../../doc/api/en/03_build_tests.md#bluetooth-classic-non-gamepad-hid-host-hardware-probe).

This private Pico W fixture advertises a standards-based Bluetooth Classic HID
mouse, accepts Just Works pairing, and emits alternating raw mouse reports. It
exists only to validate the public JaszczurHAL Classic manager and generic HID
Host on a second radio; it is not a public HID-device API.

Build it for `rp2040:picow`, flash it only to the designated peripheral test
board, and run the `hid-host` variant of example 29 on the host board. `INFO`
must report `controller=1` on the fixture. Use `SCAN` on the host and authorize
the host-side request with the serial `AUTHORIZE` command. Acceptance requires
`JHC85-HID-PASS`; the following `INFO` must report the descriptor, input and
saved-peer flags, while fixture `INFO` reports an HID connection and a non-zero
report count.

The fixture keeps link keys in RAM. Restarting either board clears its local
test state, so this procedure does not validate persistent bonding.
