# STM32G474 storage and scan hardware test

`tests/hardware/stm32_storage_scan` validates the key-value store over the
flash EEPROM reservation and the ADC scan ring on a NUCLEO-G474RE. An odd boot
runs the checks: KV init, boot counter and name blob, five scan blocks, ten
runs with interrupts masked for longer than a block (the late interrupt must
publish the half the DMA is not filling), four KV publications to flash while
the scan runs (the ring must go on), then it stores the verdict in KV and
resets through the watchdog. The even boot confirms the counter, the name and
the verdict survived, keeps scanning and prints `JHSTM32REPORT` every two
seconds. The LED (PA5) toggles per scan block during the checks and blinks at
1 Hz while reporting. PA0 and PA1 may stay open; only the ring geometry is
judged, not the voltages.

The fixture owns the EEPROM/KV reservation at the end of the flash. The boot
counter keeps counting across uploads, which is further persistence evidence.

Build and upload through the ST-LINK (OpenOCD):

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/stm32_storage_scan \
  --target stm32g474 --board nucleo-g474re
vscode/entry/jh-vscode upload \
  --project tests/hardware/stm32_storage_scan \
  --target stm32g474 --board nucleo-g474re
```

Run the verifier right after the upload (the board restarts and runs both
phases on its own; press the reset button to repeat):

```sh
python3 tests/hardware/stm32_storage_scan/verify_stm32_storage_scan.py \
  --port /dev/serial/by-id/<st-link virtual com port>
```

The verifier passes when the report of an even boot carries every verdict bit,
10/10 masked runs, 4/4 KV writes, `persist=1`, `wdg=1`, at least four keys, a
key capacity of at least 32 and a running scan.
