# JH BLE Stream v1 hardware gate

This fixture validates the public BLE lifecycle and authenticated application
stream on Raspberry Pi Pico W, Pico 2 W, RP2040 Pico with RM2/PIM730, and
STM32G474 Nucleo with PIM730/RM2. The firmware advertises as `JH Stream HW`,
requires a fixed test-only 256-bit secret, and echoes authenticated payloads.
`verify.py` acts as a Linux Central through BlueZ.

## Build matrix

Build and upload all eight target, board, and runtime combinations separately:

| Target | Board | Runtime |
|---|---|---|
| `rp2040` | `picow` | bare-metal, FreeRTOS |
| `rp2040` | `pico-rm2` | bare-metal, FreeRTOS |
| `rp2350-arm` | `pico2w` | bare-metal, FreeRTOS |
| `stm32g474` | `nucleo-g474re-pim730` | bare-metal, FreeRTOS |

The same eight tuples are declared as `example.hardwareMatrix` in the fixture
manifest and are checked by the repository artifact-layout test.

Bare-metal builds:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2040 --board picow
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2040 --board pico-rm2
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2350-arm --board pico2w
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target stm32g474 --board nucleo-g474re-pim730
```

Append `--variant freertos` to each build and upload command for the FreeRTOS
image. The fixture initializes BLE from the first `app_task0()` call, after the
FreeRTOS scheduler has started. Its task-0 stack is 1024 words because the
authenticated handshake and its cryptographic temporaries exceed the generic
fixture default. Use the same explicit target, board, and variant for upload.
A successful build is a software result; it does not count as a hardware pass.

RP2350 RISC-V is absent intentionally: the CYW43 Bluetooth transport is not
enabled for that target.

## Hardware verifier

Run the verifier after uploading each image, using the address printed by the
fixture. `--target`, `--board`, and `--runtime` are required and must describe
the uploaded image:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 \
  --board picow \
  --runtime baremetal
```

The default gate performs:

- public metadata, ATT MTU, initialization, advertising, connection, and
  authenticated stream smoke, including exact service ownership and GATT
  flags plus both write-request and write-command DATA paths;
- an encrypted request for full Stream and BLE deinitialization followed by
  initialization without an MCU reset, with stable-address and generation
  checks;
- 50 consecutive disconnect, reconnect, authentication, and echo cycles;
- an authenticated stream for at least 300 seconds at a target rate of at
  least 10 messages per second, with at least 90% of that target rate plus
  sequence, duplication, and integrity checks;
- RX queue saturation with 12 encrypted frames, verification of retained and
  dropped frames, explicit overflow accounting, and a post-saturation echo;
- wrong proof, forged tag, replay, forward counter gap, and authentication
  backoff checks.

The principal workload parameters are explicit and enforce acceptance
minimums:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 \
  --board picow \
  --runtime baremetal \
  --reconnects 50 \
  --stream-seconds 300 \
  --stream-rate 10 \
  --saturation-frames 12 \
  --saturation-hold 5
```

`--reconnects` cannot be lower than 50, `--stream-seconds` cannot be lower than
300, and `--stream-rate` cannot be lower than 10. A gate also fails if the
observed authenticated-message rate is below 90% of `--stream-rate`. Increase
the stream duration for an overnight soak. Use runtime `baremetal` for the
base image and `freertos` for the `freertos` manifest variant. The verifier
explicitly selects LE discovery, so a stale BlueZ alias does not affect
address-based selection. It requires the system Python packages for D-Bus and
GLib plus the `cryptography` package.

## Recorded hardware results - 2026-08-25

| Target and runtime | Reconnects | Sustained authenticated stream | Result |
|---|---:|---:|---|
| RP2040 Pico W, bare-metal | 50/50 | 2773 messages / 300.1 s (9.24 Hz) | PASS |
| RP2040 Pico W, FreeRTOS | - | - | rerun pending after the 1024-word fixture stack change |
| RP2350 ARM Pico 2 W, bare-metal | 50/50 | 3000 messages / 300.0 s (10.00 Hz) | PASS |
| RP2350 ARM Pico 2 W, FreeRTOS | 50/50 | 3000 messages / 300.0 s (10.00 Hz) | PASS |
| RP2040 Pico + RM2/PIM730, both runtimes | - | - | physical gate pending |
| STM32G474 + RM2/PIM730, both runtimes | - | - | extended physical gate pending |

After the per-boot oracle was added, a complete Pico 2 W bare-metal rerun
again passed 50/50 reconnects and 3000 messages in 300.0 s (10.00 Hz), plus
saturation and all security cases. Its watchdog step changed reset reason
`3 -> 4` and boot identifier
`cb3ef2a0b00439b4 -> bc75beed8bfd5cf1` while retaining address
`2C:CF:67:BB:40:2E`.

The recorded passing runs completed the public lifecycle restart, retained the
local address across the reset test, changed reset reason from `3` to watchdog
reason `4`, and authenticated a fresh session. The current oracle accepts any
pre-reset reason, but requires watchdog reason `4` afterwards and a nonzero
random per-boot identifier to change. Consequently, an old watchdog reason
cannot make a mere BLE restart look like an MCU reset. Each run also
retained four of 12 saturation frames while reporting eight drops and one
overflow acknowledgement, and passed forged-tag, replay, counter-gap,
wrong-proof, backoff, and fresh-session recovery checks. The watchdog command
provides an unattended MCU-reset interruption test; it does not remove VBUS
physically.

The earlier Pico W FreeRTOS image used a 512-word task stack and reset during
the first authenticated handshake. The fixture now reserves 1024 words, which
passed on Pico 2 W FreeRTOS, but the Pico W board became unavailable before the
corrected image could be rerun. Do not infer either pass or failure for that
tuple until a new physical run completes.

The current host oracle is Linux/BlueZ. Native Windows execution is deferred,
as is downstream consumer/lights-timer integration; neither is a requirement
for the results recorded above.

## Fixture command and identity contract

Identity, restart, saturation, and stats controls are fixture-only commands
carried inside mutually authenticated and encrypted Stream DATA payloads. Each
command is one complete DATA frame written with a BlueZ `WriteValue` request
to the existing RX characteristic
`b7ce0002-3c13-4fe2-801f-d71bdab1369b`. Its response is one encrypted DATA
notification from the existing TX characteristic
`b7ce0003-3c13-4fe2-801f-d71bdab1369b`. Commands are not split across GATT
writes. They add no characteristic and do not change the JH BLE Stream v1 wire
protocol.

The verifier additionally sends an ordinary authenticated echo through BlueZ
`type=command` to exercise the RX `write-without-response` path; fixture
control commands use `type=request` so their request completion is observable.

| Authenticated command payload | Fixture response or effect |
|---|---|
| `JHBL5/IDENTITY` | `J5I1|<target>|<board>|<runtime>` |
| `JHBL5/RESTART` | `JHBL5/RESTARTING`, then full Stream and BLE restart |
| `JHBL5/BOOT` | `J5B1` followed by one reset-reason byte and a little-endian random 64-bit boot identifier |
| `JHBL5/POWER-LOSS` (RP only) | `JHBL5/POWER-LOSS-ARMED`, then a watchdog reset without host or user intervention |
| `JHBL5/SATURATE` + little-endian hold time | `JHBL5/SATURATE-READY`, then bounded RX pause |
| `JHBL5/STATS` | compact `J5S1` binary recovery oracle |

The identity response is compiled directly from `HAL_TARGET_NAME`,
`HAL_BOARD_PROFILE_NAME`, and `HAL_ENABLE_FREERTOS`; runtime is exactly
`baremetal` or `freertos`. The verifier compares it with all three required
CLI values before accepting workload results. For example, the Pico W
bare-metal response is `J5I1|rp2040|picow|baremetal`.

A passing physical run ends with `JHBL5 HOST PASS`. The device log uses the
`JHBL5` prefix and records negotiated MTU, counters, authentication failures,
replay rejections, lifecycle failures, restarts, and bounded queue loss.

The final security phase intentionally enters the authentication backoff
window, sends rejected HELLO probes at least once per second throughout the
configured 30-second window, and proves a fresh authenticated recovery only
after its deadline before printing `JHBL5 HOST PASS`. A passing run therefore
leaves the fixture ready for another gate without a reload.

The embedded secret and its copy in `verify.py` are public test material. They
must never be reused by a product. A product needs a unique random per-device
secret delivered out of band and stored through its provisioning flow.
