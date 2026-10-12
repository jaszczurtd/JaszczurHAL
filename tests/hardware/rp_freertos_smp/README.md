# RP FreeRTOS SMP hardware test

`tests/hardware/rp_freertos_smp` validates the pinned native FreeRTOS kernel on
a physical Pico or Pico 2. It verifies scheduler startup, application task
affinity on both cores, cross-core HAL mutex operation, FreeRTOS heap reporting,
and native USB CDC traffic with delayed host reads.

Build and upload through the regular native VS Code workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_freertos_smp \
  --assembly pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_freertos_smp \
  --assembly pico \
  --port /dev/serial/by-id/<device>
```

Run the host verifier:

```sh
python3 tests/hardware/rp_freertos_smp/verify_freertos_smp.py \
  --port /dev/serial/by-id/<device>
```

`device_tree.json` describes the hardware of every assembly: `pico` for Pico,
`pico2-arm` and `pico2-riscv` for Pico 2. The test needs only the board. For
Pico 2 use `--assembly pico2-arm` or `--assembly pico2-riscv`. When the device
has no running CDC firmware yet, use `upload-uf2` while it is in BOOTSEL.
