# Native RP OTA hardware probe

This fixture validates OTA discovery and authentication, acknowledged
chunk-by-chunk transfer, trial boot, explicit confirmation, a second
unconfirmed trial, automatic rollback, and network/USB recovery after each
reboot. It supports Pico W/RP2040 and Pico 2 W/RP2350 ARM in bare-metal and
FreeRTOS builds, plus an ordinary Pico/RP2040 connected to a PIM730/RM2
wireless breakout.

Copy the local secret template and replace all values. The resulting header is
ignored by Git:

```sh
cp tests/hardware/rp_ota/ota_test_secrets.example.h \
  tests/hardware/rp_ota/ota_test_secrets.h
```

The verifier reads the OTA password from this ignored header. An environment
variable can override it when needed:

```sh
export JH_OTA_TEST_PASSWORD='the-value-from-ota_test_secrets.h'
```

Build, upload, and verify the bare-metal RP2040 variant:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_ota \
  --target rp2040 --board picow
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_ota \
  --target rp2040 --board picow \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board picow --runtime baremetal
```

Use `--target rp2350-arm --board pico2w` for Pico 2 W. Add
`--variant freertos` to both build and upload, then pass
`--runtime freertos` to the verifier for the FreeRTOS variant.
The fixture gives its application task an 8 KiB stack in FreeRTOS builds
because CYW43 initialization and OTA handling exceed the general-purpose
2 KiB default.

For Pico+PIM730, connect the breakout to an ordinary Pico as follows:

| PIM730 | Pico signal | Physical pin |
|---|---|---|
| `WL_ON` | GP2 | 4 |
| `CS` | GP3 | 5 |
| `DAT` | GP4 | 6 |
| `CLK` | GP5 | 7 |
| `GND` | GND | 8 |
| `3V3` | 3V3(OUT) | 36 |

`DAT` is the combined bidirectional data/host-wake signal. Leave `BL_ON`,
`GPIO0..2`, and `N/C` disconnected. Build and verify this profile explicitly:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_ota \
  --target rp2040 --board pico-rm2
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_ota \
  --target rp2040 --board pico-rm2 \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico-rm2 --runtime baremetal \
  --artifact-dir .build/hardware/rp_ota/cmake/rp2040/pico-rm2
```

Add `--status-only` to validate the board identity, network readiness, and
automatic gSPI clock telemetry without generating or transferring OTA images.
This diagnostic mode does not require the OTA password or build artifacts.

Use `--variant freertos`, `--runtime freertos`, and the artifact directory
`.build/hardware/rp_ota/cmake/variants/freertos/rp2040/pico-rm2` for the
FreeRTOS run.

When several target/runtime builds exist at once, point the verifier at the
matching CMake output instead of the last published artifact directory:

```sh
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --target rp2350-arm --board pico2w --runtime freertos \
  --artifact-dir \
    .build/hardware/rp_ota/cmake/variants/freertos/rp2350-arm/pico2w
```

Pico W and Pico 2 W may remain connected together. Their OTA hostnames are
`jh-ota-rp2040` and `jh-ota-rp2350-arm`, so discovery never guesses between
them. Pico W and Pico+PIM730 share the RP2040 hostname; when both are powered,
pass the selected device's IP to `--broadcast`. The CDC status response
includes and the verifier checks the board profile, current IPv4 address,
target, runtime and OTA boot state, plus the live `clk_sys`, requested and
effective gSPI rates, 16.8 divider and selected PIO timing program. The
verifier creates signed A/B containers below `.build/hardware/rp_ota` and
leaves the device in stable image A after proving rollback from image B.

`hal_ota_begin()` also publishes the configured hostname through mDNS. While
the probe is connected, verify the responder independently with, for example,
`getent hosts jh-ota-rp2040.local` (or the RP2350 hostname). The WiFi hostname
is sent in DHCP option 12 and changing it with an active lease triggers a DHCP
renewal.

The OTA upload callback is a TCP connection from the board to host TCP/8266.
Run `./runmefirst.sh` and approve its persistent, LAN-scoped OTA rule before
the hardware test. Verify the rule without changing it with:

```sh
python3 scripts/configure_ota_firewall.py --check
```

On native Windows, first verify that the selected trusted LAN has a `Private`
connection profile, then inspect the exact rule plan without changing the
host:

```powershell
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --dry-run `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
```

Apply the same scope from an already elevated PowerShell after removing
`--dry-run`. The helper requests confirmation, creates an idempotent Windows
Defender Firewall rule limited to the `Private` profile, interface, source
subnet, and TCP/8266, and never changes the network profile itself. Windows
hardware verification uses the managed Python executable and accepts a COM
port such as `--port COM3`.

If the test network differs from the network selected during initial setup,
re-run the helper with explicit `--interface` and `--network`. If
limited-broadcast discovery is blocked but the device IP is known, use a
unicast discovery destination:

```sh
python3 tests/hardware/rp_ota/verify_ota.py \
  <other-options> --broadcast <device-ip>
```

When a factory UF2 replaces the active program with a different target/runtime
build, or a board reused by another flash fixture reports an invalid initial
OTA state, enter BOOTSEL and erase only the four OTA control sectors before
loading the new UF2. The old metadata contains a digest of the previous active
program and must not be paired with the replacement image. The absolute ranges
are `0x101fc000..0x10200000` for Pico W or Pico+PIM730 and
`0x103fc000..0x10400000` for Pico 2 W:

```sh
.build/tools/picotool/picotool erase -r <range-start> <range-end> \
  --bus <usb-bus> --address <usb-address>
```

This automated probe does not simulate loss of power during an in-progress
flash swap. Power-cut validation requires a controlled power switch and is a
separate destructive/recovery run.
