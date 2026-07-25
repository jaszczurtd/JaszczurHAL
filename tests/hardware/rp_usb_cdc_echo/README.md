# RP USB CDC hardware probe

This fixture validates the native RP TinyUSB owner on a physical Pico or
Pico 2, including the RP2350 ARM and RISC-V targets. The firmware echoes
arbitrary CDC bytes and toggles the board LED after each fully echoed USB
receive block.

Build and perform the first BOOTSEL upload:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload-uf2 \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico
```

For Pico W and Pico 2 W use `--board picow` and `--board pico2w`
respectively. When another board is already in BOOTSEL, target-neutral
`upload` snapshots the existing drives before the 1200-bps touch and writes
only to the newly appeared drive.

Validate data integrity, delayed host reads, throughput, and close/reopen:

```sh
python3 -m pip install pyserial
python3 tests/hardware/rp_usb_cdc_echo/verify_cdc_echo.py \
  --port /dev/serial/by-id/<device>
```

After the first flash, the target-neutral `upload` action must enter BOOTSEL
through the 1200-bps DTR touch and return with the same CDC identity:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Use an explicit stable by-id port when multiple compatible boards are
connected. The workflow intentionally refuses to guess between two verified
ports.

## Linux runtime suspend and resume

Close every process holding the CDC port. Set `USB_DEVICE_SYSFS` to the USB
device node, not its interface node (for example,
`/sys/bus/usb/devices/3-4.1.4`):

```sh
printf '0\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/autosuspend_delay_ms" >/dev/null
printf 'auto\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/control" >/dev/null
sleep 3
cat "$USB_DEVICE_SYSFS/power/runtime_status"

printf 'on\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/control" >/dev/null
sleep 1
cat "$USB_DEVICE_SYSFS/power/runtime_status"
```

The expected states are `suspended` and then `active`. Run
`verify_cdc_echo.py` again after resume, then restore the original
`autosuspend_delay_ms` and `control` values.
