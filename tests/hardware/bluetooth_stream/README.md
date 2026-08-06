# JH BLE Stream v1 hardware gate

This fixture validates the authenticated application stream on Raspberry Pi
Pico W and STM32G474 Nucleo with PIM730/RM2. The firmware advertises as
`JH Stream HW`, requires the fixed test-only 256-bit secret, and echoes every
authenticated payload. `verify.py` acts as a Linux Central through BlueZ.

Build and upload each board separately:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2040 --board picow

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target stm32g474 --board nucleo-g474re-pim730
```

Run the host verifier with the address printed by the fixture:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX
```

The verifier explicitly selects LE discovery, so a stale BlueZ alias from an
older firmware does not affect address-based selection. It requires the
system Python packages for D-Bus and GLib plus the `cryptography` package.

The verifier reads the public metadata, requires a sufficient ATT MTU,
completes mutual authentication, exchanges an encrypted burst, and checks
wrong proof, forged tag, replay, forward counter gap, and authentication
backoff. A passing run ends with `JHBL5 HOST PASS`. The device log uses the
`JHBL5` prefix and records negotiated MTU, counters, authentication failures,
replay rejections, and bounded queue loss.

The embedded secret and its copy in `verify.py` are public test material. They
must never be reused by a product. TimerNTP needs a unique random per-device
secret delivered out of band and stored through its product provisioning flow.
