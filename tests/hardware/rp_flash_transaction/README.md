# RP flash transaction hardware probe

This fixture validates the native RP flash coordinator on a physical Pico or
Pico 2. It runs RAM-resident operations from both cores, verifies rejection of
active DMA and XIP callbacks, checks recursive-entry handling, mutates the last
flash sector, and verifies cleanup plus recovery after an operation stops
between erase and program.

The probe intentionally owns the last sector of the board flash. Do not run it
on firmware that stores unrelated data there.

Build and upload the bare-metal variant through the regular workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_flash_transaction \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_flash_transaction \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

For the FreeRTOS SMP variant, add the following temporary cache entry to the
manifest and run the same build/upload commands:

```json
"JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS=1"
```

Remove the cache entry before rebuilding the bare-metal variant.

Run the verifier:

```sh
python3 tests/hardware/rp_flash_transaction/verify_flash_transaction.py \
  --port /dev/serial/by-id/<device>
```
