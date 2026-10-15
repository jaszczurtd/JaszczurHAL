# JH BLE Stream v1 hardware validation

`tests/hardware/bluetooth_stream` validates the public BLE lifecycle and
authenticated application stream on Raspberry Pi Pico W, Pico 2 W, RP2040
Pico with RM2/PIM730, and STM32G474 Nucleo with PIM730/RM2. The firmware
advertises as `JH Stream HW`, requires a fixed test-only 256-bit secret, and
echoes authenticated payloads. `verify.py` acts as a Linux Central through
BlueZ.

## Build matrix

Build and upload all eight assembly and runtime combinations separately:

| Assembly | Target | Hardware | Runtime |
|---|---|---|---|
| `picow` | `rp2040` | Pico W | bare-metal, FreeRTOS |
| `pico-rm2` | `rp2040` | Pico + PIM730/RM2 | bare-metal, FreeRTOS |
| `pico2w` | `rp2350-arm` | Pico 2 W | bare-metal, FreeRTOS |
| `nucleo-pim730` | `stm32g474` | NUCLEO-G474RE + PIM730/RM2 | bare-metal, FreeRTOS |

This table is the fixture's hardware matrix. `device_tree.json` describes the
hardware of every assembly, `nucleo-pim730` being the default;
`hal_project_config.h` declares the `FREERTOS` variant, and
`tests/test_hardware_fixture_files.py`, run explicitly, checks that all eight
assembly/runtime combinations are listed; it does not compile the firmware. `pico-rm2` and `nucleo-pim730` are a plain Pico and a plain
NUCLEO-G474RE with an external PIM730/RM2 module; the table below repeats the
module wiring. These names select assemblies, not board profiles;
use `--assembly` even when an assembly shares an old profile name.

| PIM730/RM2 signal | Pico (`pico-rm2`) | NUCLEO-G474RE (`nucleo-pim730`) |
|---|---|---|
| CS | GPIO3 | PB12, CN10 pin 16 |
| DAT | GPIO4 | PB15, CN10 pin 26 |
| WL_ON | GPIO2 | PB14, CN10 pin 28 |
| CLK | GPIO5 | PB13, CN10 pin 30 |

Bare-metal builds:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream --assembly picow
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream --assembly pico-rm2
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream --assembly pico2w
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream --assembly nucleo-pim730
```

Append `--variant FREERTOS` to each build and upload command for the FreeRTOS
image. The fixture initializes BLE from the first `app_task0()` call, after the
FreeRTOS scheduler has started. Its task-0 stack is 1024 words because the
authenticated handshake and its cryptographic temporaries exceed the generic
fixture default. Use the same explicit assembly and variant for upload.
A successful build is a software result; it does not count as a hardware pass.

## STM32G474 PIM730 plus ILI9341 load variants

The optional `DISPLAY` and `DISPLAY_FREERTOS` variants keep the same BLE Stream
protocol and host verifier while continuously updating an ILI9341 connected to
the NUCLEO-G474RE Arduino SPI header. They exercise SPI1 in parallel with the
dedicated PIM730 gSPI transport on PB12-PB15. These variants are additional
coexistence/load evidence and do not replace either of the two base STM32 gate
images listed in the build matrix. Only the `nucleo-pim730` assembly has the
display; `hal_project_config.h` stops them with `#error` in the others.

Build the separate artifacts with:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --assembly nucleo-pim730 --variant DISPLAY

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --assembly nucleo-pim730 --variant DISPLAY_FREERTOS
```

The ILI9341 uses the wiring of `examples/07_display_media`. In
`device_tree.json` it is the `display` node on the `spi` bus of the
`nucleo-pim730` assembly; the table repeats it:

| ILI9341 | STM32G474 | Morpho | Arduino |
|---|---|---|---|
| `SCK` | `PB3` | CN10 pin 31 | CN9 pin 4 (`D3`) |
| `MISO` | `PA6` | CN10 pin 13 | CN5 pin 5 (`D12`) |
| `MOSI` | `PA7` | CN10 pin 15 | CN5 pin 4 (`D11`) |
| `CS` | `PB6` | CN10 pin 17 | CN5 pin 3 (`D10`) |
| `DC` | `PC7` | CN10 pin 19 | CN5 pin 2 (`D9`) |
| `RESET` | `PA9` | CN10 pin 21 | CN5 pin 1 (`D8`) |

The SPI clock uses PB3 rather than D13 (PA5), because PA5 drives the LD2 LED.

The screen reports the controller address, BLE/Stream state, MTU, RX/TX,
drop/overflow/security counters, lifecycle restarts, status and uptime. Static
and unchanged fields are redrawn only when their value changes; RX/TX and
status/uptime continue to update once per second. This keeps persistent SPI1
load without repeatedly transferring identical full text rows. A display
initialization or update failure stops normal fixture progress and is reported
as a lifecycle failure. The LCD is write-only, so visual inspection remains
the physical oracle for panel output.

RP2350 RISC-V is absent intentionally: the CYW43 Bluetooth transport is not
enabled for that target.

## Hardware verifier

Run the verifier after uploading each image, using the address printed by the
fixture. `--assembly` and `--runtime` are required and must describe the
uploaded image. The verifier takes the assembly names and their targets from
`device_tree.json`:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX \
  --assembly picow \
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
  --assembly picow \
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
base image and `freertos` for the `FREERTOS` variant. The verifier
explicitly selects LE discovery, so a stale BlueZ alias does not affect
address-based selection. It requires the system Python packages for D-Bus and
GLib plus the `cryptography` package.

## Verification scope

After the watchdog step the verifier requires reset reason `4` and a new,
nonzero random per-boot identifier; the reason before the reset may be
anything. A restart of BLE alone therefore cannot pass as an MCU reset. The
watchdog command resets the MCU but does not cut VBUS, so it does not replace
a physical power-loss test.

Not yet run on hardware: RP2040 Pico with RM2/PIM730 in both runtimes, the
base STM32G474 + RM2/PIM730 images in both runtimes, and the STM32G474
`DISPLAY` variants with the SPI clock on PB3. The host side runs only on Linux
with BlueZ; native Windows is not covered.

## Fixture command and identity rules

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
`type=command` to exercise the RX `write-without-response` path; fixture control
commands use `type=request` so their request completion is observable.

| Authenticated command payload | Fixture response or effect |
|---|---|
| `JHBL5/IDENTITY` | `J5I1\|<target>\|<assembly>\|<runtime>` |
| `JHBL5/RESTART` | `JHBL5/RESTARTING`, then full Stream and BLE restart |
| `JHBL5/BOOT` | `J5B1` followed by one reset-reason byte and a little-endian random 64-bit boot identifier |
| `JHBL5/POWER-LOSS` (RP and STM32G474) | `JHBL5/POWER-LOSS-ARMED`, then a watchdog reset without host or user intervention |
| `JHBL5/SATURATE` + little-endian hold time | `JHBL5/SATURATE-READY`, then bounded RX pause |
| `JHBL5/STATS` | compact `J5S1` binary recovery oracle |

The identity response is compiled directly from `HAL_TARGET_NAME`,
`JH_HW_ASSEMBLY_NAME`, and `HAL_ENABLE_FREERTOS`; runtime is exactly
`baremetal` or `freertos`. The assembly name stands in for the board because
`pico-rm2` and `nucleo-pim730` have no board profile of their own. The
verifier compares the response with the selected assembly, its target and
`--runtime` before accepting workload results. For example, the Pico W
bare-metal response is `J5I1|rp2040|picow|baremetal`, and the Nucleo with
PIM730 answers `J5I1|stm32g474|nucleo-pim730|baremetal`.

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

## BLE command-router smoke

The `COMMANDS` variants of `examples/26_ble_stream` exercise the separate
`hal_ble_commands` adapter while this fixture's base firmware continues to
exercise raw Stream payloads. Linux/BlueZ is the Central and each board remains
a Peripheral.

Build and upload the bare-metal and FreeRTOS images to two Pico W boards. When
both boards are already in BOOTSEL, select each volume explicitly:

```bash
vscode/entry/jh-vscode upload \
  --project examples/26_ble_stream \
  --assembly picow --variant COMMANDS \
  --bootsel-volume /dev/<baremetal-partition>

vscode/entry/jh-vscode upload \
  --project examples/26_ble_stream \
  --assembly picow --variant COMMANDS_FREERTOS \
  --bootsel-volume /dev/<freertos-partition>
```

Read each BLE address from its USB CDC log and run the short verifier against
the two explicit addresses. `--assembly` names an assembly of
`examples/26_ble_stream/device_tree.json`; the verifier expects the target and
board of that assembly in the example's ready event:

```bash
python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address XX:XX:XX:XX:XX:XX \
  --assembly picow --runtime baremetal

python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address YY:YY:YY:YY:YY:YY \
  --assembly picow --runtime freertos
```

A pass requires exact reassembly of a 500-byte binary echo in both directions,
`BLE_STREAM` provenance, all four security flags, a nonzero peer and session,
`HAL_EPERM` for a source-restricted route, `HAL_ENOENT` for an unknown route,
the Peripheral event and request/response exchange, and a fresh authenticated
session after one reconnect. Swap the two images between the physical boards
and repeat when validating the complete two-board fixture.
