# STM32G474 storage and scan hardware test

`tests/hardware/stm32_storage_scan` validates the key-value store over the
flash EEPROM reservation and the ADC scan ring on a NUCLEO-G474RE. An odd boot
runs the checks: KV init, boot counter and name blob, five scan blocks, ten
runs with interrupts masked for longer than a block (the late interrupt must
publish the half the DMA is not filling), four KV writes to flash while
the scan runs (the ring must go on), then it stores the verdict in KV and
resets through the watchdog. The even boot confirms the counter, the name and
the verdict survived, keeps scanning and prints `JHSTM32REPORT` every two
seconds. The LED (PA5) toggles per scan block during the checks and blinks at
1 Hz while reporting. PA0 and PA1 may stay open; only the ring geometry is
judged, not the voltages.

`device_tree.json` describes the one assembly, `nucleo`: the scan inputs
`scanA` on PA0 (CN7 pin 28 / A0) and `scanB` on PA1 (CN7 pin 30 / A1), and LD2
(PA5) as the built-in LED.

The fixture owns the EEPROM/KV reservation at the end of the flash. The boot
counter keeps counting across uploads, which is further persistence evidence.

Build and upload through the ST-LINK (OpenOCD):

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/stm32_storage_scan \
  --assembly nucleo
vscode/entry/jh-vscode upload \
  --project tests/hardware/stm32_storage_scan \
  --assembly nucleo
```

To check a larger EEPROM/KV reservation, add `--variant EEPROM_16K` to both
commands; the variant sets `HAL_STM32_FLASH_EEPROM_SIZE=16384u`.

Run the verifier right after the upload (the board restarts and runs both
phases on its own; press the reset button to repeat). The ST-LINK may still
hold reports of the image that ran before the upload, so the verifier waits
for half a second of silence before it takes a report:

```sh
python3 tests/hardware/stm32_storage_scan/verify_stm32_storage_scan.py \
  --port /dev/serial/by-id/<st-link virtual com port>
```

The verifier passes when the report of an even boot carries every verdict bit,
10/10 masked runs, 4/4 KV writes, `persist=1`, `wdg=1`, at least four keys, a
key capacity of at least 32 and a running scan.
