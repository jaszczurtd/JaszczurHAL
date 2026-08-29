# JH BLE Stream v1 hardware gate

The complete requirements, procedure, acceptance criteria, and recorded results
are maintained in the
[central hardware-fixture reference](../../../doc/api/en/03_build_tests.md#jh-ble-stream-v1-hardware-gate).

## BLE command-router smoke

The `commands` variants of `examples/26_ble_stream` exercise the separate
`hal_ble_commands` adapter while this fixture's base firmware continues to
exercise raw Stream payloads. Linux/BlueZ is the Central and each board remains
a Peripheral.

Build and upload the bare-metal and FreeRTOS images to two Pico W boards. When
both boards are already in BOOTSEL, select each volume explicitly:

```bash
vscode/entry/jh-vscode upload \
  --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands \
  --bootsel-volume /dev/<baremetal-partition>

vscode/entry/jh-vscode upload \
  --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands-freertos \
  --bootsel-volume /dev/<freertos-partition>
```

Read each BLE address from its USB CDC log and run the short verifier against
the two explicit addresses:

```bash
python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 --board picow --runtime baremetal

python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address YY:YY:YY:YY:YY:YY \
  --target rp2040 --board picow --runtime freertos
```

A pass requires exact reassembly of a 500-byte binary echo in both directions,
`BLE_STREAM` provenance, all four security flags, a nonzero peer and session,
`HAL_EPERM` for a source-restricted route, `HAL_ENOENT` for an unknown route,
the Peripheral event and request/response exchange, and a fresh authenticated
session after one reconnect. Swap the two images between the physical boards
and repeat when validating the complete two-board fixture.
