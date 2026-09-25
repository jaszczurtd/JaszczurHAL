# RP flash transaction hardware test

`tests/hardware/rp_flash_transaction` validates the native RP flash
coordinator on a physical Pico or Pico 2. It runs RAM-resident operations from
both cores, checks that a busy RAM-to-RAM DMA channel is tolerated while a
channel reading the XIP window is refused, rejects XIP callbacks, checks
recursive-entry handling, mutates one flash sector, and verifies cleanup plus
recovery after an operation stops between erase and program.

A second command runs the load probe: a DMA ring and an ADC scan on ADC0-ADC2
(GPIO 26-28) keep running while the host writes junk into the CDC port, and
sixteen published `hal_kv` writes alternate between the cores. Guard words
behind the scan buffer and the block counter prove that the ring neither
stalls nor runs past its buffer while a core keeps interrupts masked for the
flash transaction. The status line reports the reset reason and the retained
fault record, so a crash in the previous run is named on the next query.

The probe owns the EEPROM/KV reservation at the end of the board flash and the
sector right below it. Do not run it on firmware that stores unrelated data
there. The board LED toggles once per KV write.

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

Run the verifier; it queries the status, runs the transaction probes, then
the load probe, and expects a clean fault record and running tasks after each
phase:

```sh
python3 tests/hardware/rp_flash_transaction/verify_flash_transaction.py \
  --port /dev/serial/by-id/<device>
```
