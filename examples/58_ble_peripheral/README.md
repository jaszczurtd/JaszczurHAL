# BLE Peripheral Example

This experimental example starts connectable legacy advertising as
`JH BLE Peripheral`, reports the controller address, and logs connection,
disconnection, and ATT MTU events. The static GATT database contains only the
mandatory GAP and GATT services; application characteristics arrive in a later
stage.

Supported build profiles are Raspberry Pi Pico W and ST NUCLEO-G474RE with the
PIM730/RM2 radio module:

```bash
vscode/entry/jh-vscode build \
  --project examples/58_ble_peripheral \
  --target rp2040 --board picow

vscode/entry/jh-vscode build \
  --project examples/58_ble_peripheral \
  --target stm32g474 --board nucleo-g474re-pim730
```

Call `hal_ble_poll()` frequently. Event callbacks run from that call, outside
the shared WiFi/Bluetooth radio lock. The complete lifecycle, handle, queue,
and maturity contract is documented in the
[`hal_ble` API](../../doc/api/20_bluetooth.md).
