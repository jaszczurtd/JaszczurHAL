# RP multicore USB hardware probe

This fixture starts one CDC producer on each RP core. Both producers write
4096 independently numbered and checksummed records through `hal_usb` while
the host verifies record boundaries, integrity, completeness, producer
affinity, and the final HAL status. A malformed line detects byte-level
interleaving between concurrent writes.

Build and upload the bare-metal RP2040 variant:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_usb_multicore \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_usb_multicore \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_usb_multicore/verify_usb_multicore.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

For Pico 2, select `rp2350-arm` or `rp2350-riscv`, use the `pico2` build board,
and pass `--board pico-2` to the verifier. Add `--variant freertos` to build
and upload commands and use `--runtime freertos` for the FreeRTOS SMP run.

The verifier's default `--records 4096` must match
`JH_USB_MULTICORE_RECORDS` in the firmware build.
