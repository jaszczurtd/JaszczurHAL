# RP FreeRTOS SMP hardware probe

This fixture validates the pinned native FreeRTOS kernel on a physical Pico or
Pico 2. It verifies scheduler startup, application task affinity on both cores,
cross-core HAL mutex operation, FreeRTOS heap reporting, and native USB CDC
traffic with delayed host reads.

Build and upload through the regular native VS Code workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_freertos_smp \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_freertos_smp \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Run the host verifier:

```sh
python3 tests/hardware/rp_freertos_smp/verify_freertos_smp.py \
  --port /dev/serial/by-id/<device>
```

Use `rp2350-arm` or `rp2350-riscv` with board `pico2` for Pico 2. When the
device has no running CDC firmware yet, use `upload-uf2` while it is in BOOTSEL.
