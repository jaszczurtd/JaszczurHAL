# Native RP OTA Workflow

This document is the complete operational contract for native JaszczurHAL OTA:
supported targets, project and firmware configuration, build artifacts, first
installation, VS Code integration, network flow, host firewall rules, trial
confirmation, rollback, recovery, and security boundaries.

The general dispatcher-backed project model remains in
[Firmware Project Workflow](FwProjectWorkflow.md). The public API is documented
under [`hal_ota`](api/15_connectivity.md), and the reference implementation is
[`examples/25_ota`](../examples/25_ota/README.md).

## Support Matrix And Workflow

Native OTA is supported by the official Pico SDK targets `rp2040` and
`rp2350-arm`. The complete WiFi path has been validated on Pico W, Pico 2 W,
and an ordinary Pico with a PIM730/RM2 add-on, in both bare-metal and FreeRTOS
builds.

The native workflow has four distinct stages:

1. Build an application with `HAL_ENABLE_OTA`. CMake creates two firmware
   slots, the OTA control area, an unsigned `.ota` container, and a merged UF2
   containing the boot applier plus the application.
2. Install that merged UF2 once through BOOTSEL. A blank board cannot receive
   a network update.
3. Discover or address the running board over UDP. The host signs the
   container at upload time with the configured password.
4. Transfer the signed container into the staging slot. The board reboots the
   image as a trial, and application code confirms it only after product
   startup checks succeed.

The merged UF2 contains zero-filled pages for every touched non-final flash
sector. This follows the Pico SDK workaround for
[RP2040-E14](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf):
without that padding, a sparse boundary between the boot applier and
application can make BOOTSEL program an incomplete image.

## Project Manifest

Enable OTA in `.vscode/jaszczurhal.project.json`, publish the generated
artifact paths, and define the host-side discovery and authentication
settings:

```json
{
  "toolchain": "cmake",
  "target": "rp2040",
  "board": "picow",
  "buildDir": "${project}/.build",
  "cmakeBuildDir": "${buildDir}/cmake",
  "cmake": {
    "sourceDir": "${project}/../libraries/JaszczurHAL/cmake/jh_firmware_project",
    "cache": {
      "JH_PROJECT_DIR": "${project}",
      "JH_MODULE_NAME": "tracker",
      "JH_EXTRA_DEFINES": "HAL_ENABLE_OTA",
      "JH_OTA_GENERATION": 7,
      "JH_OTA_VERSION": "1.4.0"
    }
  },
  "artifacts": {
    "elf": "${buildDir}/firmware.elf",
    "uf2": "${buildDir}/firmware.uf2",
    "ota": "${buildDir}/firmware.ota",
    "compileCommands": "${buildDir}/compile_commands_patched.json"
  },
  "ota": {
    "hostname": "tracker-office",
    "port": 8266,
    "listenPort": 8266,
    "passwordEnv": "TRACKER_OTA_PASSWORD"
  }
}
```

`JH_EXTRA_DEFINES` is a semicolon-separated CMake list. Preserve other
project defines when adding OTA, for example
`"HAL_ENABLE_OTA;HAL_ENABLE_FREERTOS"`. `HAL_ENABLE_OTA` automatically enables
the required WiFi, UDP, TCP, crypto, and CRC modules.

The build metadata has the following contract:

| Setting | Meaning |
|---|---|
| `JH_OTA_GENERATION` | Unsigned 32-bit image generation stored in the container. Increment it according to the project's release policy. It is metadata, not an enforced anti-rollback counter. The default is `1`. |
| `JH_OTA_VERSION` | Human-readable version stored in the image. It must be shorter than 32 UTF-8 bytes. The default is `dev`. |
| `artifacts.ota` | Preferred exact path to the unsigned container. Without it, the dispatcher accepts only one unambiguous `.ota` below `buildDir`. |
| `artifacts.uf2` | Merged boot-applier and application image used for initial installation and USB recovery. |

The `ota` object controls the host tool:

| Setting | Meaning |
|---|---|
| `hostname` | Device name used to filter discovery results. It must match `hal_ota_set_hostname()` in firmware when discovery is used. |
| `port` | Device UDP discovery, invitation, and authentication port. It must match `hal_ota_set_port()`. The default is `8266`. This is not the TCP data-transfer port. |
| `listenPort` | Host TCP callback port advertised to the device. The default is `8266`, matching the persistent rule prepared by `runmefirst.sh`. Set it explicitly to `0` only when an ephemeral callback port and a matching firewall policy are intentional. |
| `passwordEnv` | Name of the host environment variable containing the OTA password. This takes precedence over `ota.password`. |
| `password` | Inline development-only password. Do not use it in a tracked product manifest. |
| `allowEmptyPassword` | Allows an empty password when explicitly `true`. The default host behavior rejects empty passwords. Do not enable this for deployed devices. |
| `broadcast` | Discovery destination. The default is `255.255.255.255`; a directed broadcast such as `192.168.2.255`, or a device address for unicast discovery, is often more reliable on multi-interface hosts. |
| `host` | Fixed device IPv4 address or resolvable hostname. Upload bypasses broadcast discovery, while the discovery command sends a direct unicast query. It is suitable for automation and routed networks. `--host` on the command line overrides it for one invocation. |

Use a unique hostname per concurrently powered device. Device selection also
matches the active dispatcher target. When more than one matching device is
visible, use interactive selection or a fixed `ota.host`; automation must not
guess.

## Device-Side Secret And Configuration

The password exists on both sides of the workflow:

- firmware passes it to `hal_ota_set_password()`;
- the host reads the identical string from `ota.passwordEnv` and uses it for
  transport authentication and container signing.

Do not place a product password in `app.c`, the tracked manifest, VS Code
settings, or a tracked task. A simple development layout uses a tracked
template and an ignored local header:

```text
tracker/
  app.c
  hal_project_config.h
  ota_secrets.example.h
  ota_secrets.h
  .gitignore
```

```c
/* ota_secrets.example.h - tracked template */
#pragma once
#define APP_WIFI_SSID "replace-with-wifi-ssid"
#define APP_WIFI_PASSWORD "replace-with-wifi-password"
#define APP_OTA_PASSWORD "replace-with-a-unique-high-entropy-secret"
```

```gitignore
# .gitignore
/ota_secrets.h
```

Copy the template to `ota_secrets.h`, replace the values locally, and include
that file from the firmware. For provisioned products, the password may
instead come from product-specific secure storage before `hal_ota_begin()` is
called. JaszczurHAL does not provide protected key storage on RP2040/RP2350;
a secret compiled into firmware can be recovered by an attacker with
sufficient physical access.

Export the same password in the shell that runs the dispatcher:

```bash
export TRACKER_OTA_PASSWORD='the-same-value-used-by-firmware'
```

VS Code tasks inherit the environment of the VS Code process, not exports
made later in an unrelated integrated terminal. On Linux, close existing VS
Code processes and launch the project from the configured shell when changing
the variable:

```bash
export TRACKER_OTA_PASSWORD='the-same-value-used-by-firmware'
code .
```

The value of `passwordEnv` is the variable name only; do not write
`"${TRACKER_OTA_PASSWORD}"` in the manifest.

## Firmware Integration

Configure hostname, UDP port, password, and optional callbacks before starting
the OTA service. Start the service only after the network is usable, call
`hal_ota_handle()` frequently, and confirm a trial only after all
product-specific readiness checks pass.

The following skeleton shows the complete application-side control flow:

```c
#include <hal/hal_app.h>
#include <hal/hal_ota.h>
#include <hal/hal_status.h>
#include <hal/hal_system.h>
#include <hal/hal_wifi.h>

#include "ota_secrets.h"

static const char *OTA_HOSTNAME = "tracker-office";
static const uint16_t OTA_PORT = 8266u;

static bool ota_configured;
static bool ota_started;
static bool boot_confirmed;
static uint32_t last_wifi_attempt_ms;

static bool application_startup_checks_passed(void) {
  /* Replace with real checks for required sensors, storage, and services. */
  return true;
}

static void ota_error(hal_ota_error_t error, void *user) {
  (void)error;
  (void)user;
}

static void connect_wifi(void) {
  if (hal_wifi_is_connected()) {
    return;
  }
  const uint32_t now = hal_millis();
  if (now - last_wifi_attempt_ms < 5000u) {
    return;
  }
  last_wifi_attempt_ms = now;
  (void)hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  (void)hal_wifi_set_hostname(OTA_HOSTNAME);
  (void)hal_wifi_begin_station(APP_WIFI_SSID, APP_WIFI_PASSWORD, true);
}

void app_start(void) {
  ota_configured =
      hal_ota_set_hostname(OTA_HOSTNAME) &&
      hal_ota_set_port(OTA_PORT) &&
      hal_ota_set_password(APP_OTA_PASSWORD) &&
      hal_ota_on_error(ota_error, NULL);
}

void app_task0(void) {
  connect_wifi();

  if (ota_configured && hal_wifi_is_connected() && !ota_started) {
    ota_started = hal_ota_begin();
  }

  if (ota_started) {
    hal_ota_handle();
    if (!boot_confirmed && application_startup_checks_passed()) {
      boot_confirmed = hal_ota_confirm_boot_ex() == HAL_OK;
    }
  }

  hal_delay_ms(1u);
}
```

The unconditional `true` in `application_startup_checks_passed()` is only a
placeholder. A product should include every condition required to consider the
new image safe: configuration compatibility, storage mount, required hardware,
network services, and any application self-test. Confirming too early removes
rollback protection for later startup failures.

Additional API rules:

- `hal_ota_set_port()` rejects port zero and cannot change the port after the
  service has started.
- The hostname must be non-empty. The default hostname, when none is provided,
  is the HAL target name.
- The device API accepts an empty password, but the host rejects it unless
  `allowEmptyPassword` is explicit. Always set a non-empty product secret.
- `hal_ota_handle()` performs network service, processes discovery,
  authentication and transfer, and dispatches callbacks. Do not stop calling
  it while OTA is enabled.
- The device reboots automatically after accepting and validating a complete
  image.
- `hal_ota_get_boot_info_ex()` reports stable, trial, rollback and recovery
  state together with generation, version, attempts, and attempt limit.
- `HAL_RP_OTA_MAX_BOOT_ATTEMPTS` defaults to `3` and accepts values from 1 to
  255. Override it through `JH_EXTRA_DEFINES` or `hal_project_config.h` only
  when the product has a deliberate boot policy.

For a FreeRTOS application, run the service from one task and size that task
for CYW43 initialization and OTA processing. The hardware regression fixture
uses 2048 FreeRTOS stack words, or 8 KiB on RP:

```c
/* hal_project_config.h */
#pragma once

#if defined(HAL_ENABLE_FREERTOS) && !defined(HAL_FREERTOS_TASK0_STACK)
#define HAL_FREERTOS_TASK0_STACK 2048u
#endif
```

Measure the final product's high-water mark rather than assuming this value is
universally sufficient.

## Build Artifacts And First Installation

Inspect the resolved target, board, paths, and OTA settings before the first
build:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  config-dump --project "$PWD"
```

Build from the firmware project directory:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  build --project "$PWD"
```

An OTA-enabled native RP build produces:

| Artifact | Purpose |
|---|---|
| `firmware.elf` | Debug symbols and memory-map inspection. |
| `firmware.bin` | Raw application image linked into the program slot. |
| `firmware.ota` | Unsigned plaintext OTA container. It contains target, load offset, generation, version, payload SHA-256 and header CRC, but no password. |
| `firmware.uf2` | Merged boot applier and application for first installation or USB recovery. |
| `firmware.signed.ota` | Upload-time artifact created by `upload-ota`; it is still plaintext and is normally kept below `buildDir`. |

The v1 container begins with this 160-byte little-endian header:

| Byte range | Field |
|---|---|
| `0..7` | `JHOTA1\r\n` magic |
| `8..9` | Header version, currently `1` |
| `10..11` | Header size, currently `160` |
| `12..13` | Target ID: `1` RP2040, `2` RP2350 Arm, `3` RP2350 RISC-V |
| `14..15` | Reserved |
| `16..19` | Program-slot flash offset |
| `20..23` | Payload size |
| `24..27` | Monotonic image generation |
| `28..31` | Flags |
| `32..63` | Payload SHA-256 |
| `64..95` | UTF-8 version, NUL-padded; the encoded value must be shorter than 32 bytes |
| `96..127` | HMAC-SHA256 |
| `128..155` | Reserved |
| `156..159` | CRC32 of header bytes `0..155` |

Packaging leaves the HMAC field zeroed. Upload verifies the payload digest,
computes the lowercase ASCII hexadecimal MD5 of the UTF-8 password, and uses
those 32 ASCII bytes as the HMAC-SHA256 key for header bytes `0..95`. It writes
the digest at bytes `96..127` and recomputes the CRC32. This key derivation
preserves the OTA transport contract; it is not a password-hardening
construction. The device verifies target/layout bounds, header CRC, HMAC, and
payload digest before marking staging pending.

OTA reserves a 16 KiB boot region, equal program and staging slots, and four
4 KiB control sectors before any LittleFS/EEPROM tail. The available
application space is therefore smaller than in a non-OTA build. CMake computes
the layout for the selected flash size and fails if regions overlap or the
application does not fit.

For a blank board:

1. Hold BOOTSEL while connecting or resetting the board.
2. Ensure only the intended RP BOOTSEL drive is visible.
3. Run:

   ```bash
   ../libraries/JaszczurHAL/vscode/entry/jh-vscode \
     upload-uf2 --project "$PWD"
   ```

The normal `upload` action also writes UF2 over USB, but it can use the
firmware's CDC 1200-bps reset only after firmware has been installed once.
`upload` is not an OTA command. Manual BOOTSEL remains the recovery path when
the application, WiFi, or OTA service cannot start.

## Discovery And OTA Upload

Wait until firmware has joined WiFi and `hal_ota_begin()` has succeeded, then
list devices:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  ota-discover --project "$PWD"
```

Machine-readable discovery is available with `--json`. A discovery response
contains the device hostname, address, target, UDP port, slot size, active
generation, and boot mode.

Upload interactively:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  upload-ota --project "$PWD" --interactive
```

Or bypass discovery for a known address:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  upload-ota --project "$PWD" --host 192.168.2.200
```

`upload-ota` always builds first, locates the unsigned `.ota` artifact, reads
the password, creates `firmware.signed.ota`, authenticates the invitation,
transfers the image, waits for device acceptance, and reports that the device
is rebooting. It automatically selects a device only when exactly one
discovery result matches the active target and configured hostname.

For multiple target/board builds, select the intended profile first or pass
the override explicitly:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  upload-ota --project "$PWD" \
  --target rp2350-arm --board pico2w --host 192.168.2.200
```

`--variant` applies only when the manifest declares that variant. For example,
a declared `freertos` variant is selected with `--variant freertos`.

## VS Code Tasks And Keyboard Shortcuts

Generated projects contain these maintained tasks:

| Task | Purpose |
|---|---|
| `Project: Build` | Build all selected-target artifacts, including `.ota` and merged UF2. |
| `Project: Upload (UF2 / BOOTSEL)` | First installation or USB recovery. |
| `Project: Discover OTA devices` | Run OTA discovery and print matching devices. |
| `Project: Upload (OTA)` | Build, select a device interactively, sign, and upload over the network. |
| `Project: Config Dump` | Inspect the resolved manifest and local target/board selection. |

Migrated projects should copy the current task definitions from
[`vscode/examples/tasks.json`](../vscode/examples/tasks.json). The tasks call
`${config:jaszczurhal.vscodeEntry}`, so `.vscode/settings.json` must point that
setting at the project's JaszczurHAL checkout.

The corresponding reference shortcuts are:

| Shortcut | Task |
|---|---|
| `Ctrl+Shift+1` | `Project: Build` |
| `Ctrl+Shift+4` | `Project: Upload (UF2 / BOOTSEL)` |
| `Ctrl+Shift+8` | `Project: Upload (OTA)` |
| `Ctrl+Shift+9` | `Project: Config Dump` |
| `Ctrl+Shift+Alt+3` | `Project: Discover OTA devices` |

Project `.vscode/keybindings.reference.json` files are documentation only.
VS Code does not load repository-local keybindings. Add the entries to the
real user keybindings file through **Preferences: Open Keyboard Shortcuts
(JSON)**:

```json
[
  {
    "key": "ctrl+shift+8",
    "command": "workbench.action.tasks.runTask",
    "args": "Project: Upload (OTA)"
  },
  {
    "key": "ctrl+shift+alt+3",
    "command": "workbench.action.tasks.runTask",
    "args": "Project: Discover OTA devices"
  }
]
```

On Linux the user file is normally
`~/.config/Code/User/keybindings.json`. Reload the window after changing task
bindings. Do not confuse the repository-root shortcuts, which operate on the
JaszczurHAL library itself, with firmware-project shortcuts.

## Network Flow And Host Firewall

The OTA data connection is intentionally reversed:

1. The host sends discovery, invitation, and authentication packets over UDP
   to the device's configured OTA port, normally `8266`.
2. The host opens a TCP listener. `ota.listenPort` selects its port; the
   default is `8266`, while an explicit value `0` asks the operating system for
   an ephemeral port.
3. The invitation tells the device that TCP port.
4. The device initiates a new TCP connection back to the host and receives the
   image in acknowledged chunks.

With `"listenPort": 8266`, the SYN from the device targets host TCP port 8266,
so a firewall rule for that exact callback port is sufficient. With
`"listenPort": 0`, Linux normally selects a port from
`/proc/sys/net/ipv4/ip_local_port_range`, and the firewall policy must cover
that selected ephemeral port. The access point or routed network must also
permit device-to-host traffic; disable wireless client isolation for the OTA
network.

`runmefirst.sh` detects the RFC1918 network attached to the default IPv4
interface and checks for a persistent TCP/8266 callback rule. On Windows, run
the same focused Python helper from the managed environment. When the rule is
missing, it displays the exact interface, source subnet, port, persistence
backend, and any package or elevation boundary before asking for confirmation.
Declining leaves the firewall unchanged and does not prevent the remaining
setup.
A host with an empty `INPUT` chain and an `ACCEPT` policy already permits the
callback, so setup reports success without installing persistence tooling.

After confirmation, setup uses the active firewall manager:

- active UFW receives a persistent interface- and subnet-scoped rule;
- active firewalld receives matching runtime and permanent rich rules;
- an `iptables-nft`/`iptables` host receives an early `INPUT` rule, persisted
  with `netfilter-persistent`, whose boot service is enabled when systemd is
  available;
- `iptables-persistent` is installed through `apt` only when the iptables path
  needs persistence and no supported persistence tool is present.
- Windows Defender Firewall receives a named inbound rule limited to the
  `Private` profile, selected interface alias, RFC1918 source subnet, TCP, and
  callback port. The helper does not change `Public` networks to `Private`.

Setup never enables an inactive UFW or firewalld policy. The iptables
persistence path writes the complete active IPv4 ruleset to
`/etc/iptables/rules.v4` without saving IPv6 policy, which is stated in the
confirmation prompt. Re-run the focused helper after changing the LAN,
interface, or callback port:

```bash
python3 scripts/configure_ota_firewall.py
python3 scripts/configure_ota_firewall.py --check
python3 scripts/configure_ota_firewall.py --dry-run
python3 scripts/configure_ota_firewall.py \
  --interface enp7s0 --network 192.168.2.0/24
```

Native Windows uses the managed interpreter and the interface alias shown by
`Get-NetConnectionProfile`:

```powershell
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --dry-run `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --check `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
```

If the inspected connection is `Public`, choose a trusted LAN and change its
profile deliberately through Windows settings before provisioning the rule.
Apply from an already elevated PowerShell only after reviewing `--dry-run`.
The script reports the elevation boundary and does not invoke UAC itself.

The helper accepts only RFC1918 IPv4 source networks. `--yes` permits
non-interactive provisioning after the interface and network have been
selected explicitly. Setup refuses to expose the port while another process is
already listening on it. The uploader creates the TCP listener only for the
duration of a transfer; the persistent firewall rule does not start a
background service.

Determine the route, callback interface, and active firewall implementation:

```bash
jh_ota_device_ip=192.168.2.200
ip route get "$jh_ota_device_ip"
sudo nft list ruleset
sudo iptables-save
```

Use the `dev` value printed by `ip route get` as the ingress interface. The
host address in the route output is the destination address for the callback.
For a fixed listener, verify that the upload process is listening and capture
the handshake:

```bash
ss -ltn 'sport = :8266'
sudo tcpdump -ni enp7s0 \
  'host 192.168.2.200 and (udp port 8266 or tcp port 8266)'
```

If the capture shows repeated device SYN packets without a SYN-ACK, the
callback reached the host and the host firewall or listener is the remaining
boundary. If no SYN appears after successful UDP authentication, check the
advertised callback address, route, and AP isolation.

On an nftables host managed through the `iptables-nft` compatibility layer, the
helper installs an equivalent persistent rule. For manual diagnosis only, a
temporary source-host-scoped rule can be inserted after confirming the active
`INPUT` chain in `nft list ruleset` or `iptables-save`:

```bash
sudo /usr/sbin/iptables-nft -I INPUT 1 \
  -i enp7s0 \
  -s 192.168.2.200/32 \
  -d 192.168.2.180/32 \
  -p tcp --dport 8266 \
  -m conntrack --ctstate NEW \
  -m comment --comment 'JaszczurHAL OTA callback' \
  -j ACCEPT
```

Replace the interface and both addresses with values from the route. Inspect
the counter with:

```bash
sudo /usr/sbin/iptables-nft \
  -L INPUT -n -v --line-numbers
```

Delete a temporary rule with the same complete match and `-D` in place of
`-I ... 1`. Direct compatibility-chain changes may disappear after a firewall
reload or reboot. Use the helper for the normal persistent setup.

When `listenPort` is zero, inspect the selected listener with `ss -ltnp` and
read the host range with:

```bash
cat /proc/sys/net/ipv4/ip_local_port_range
```

A normal stateful firewall permits the UDP reply to the host's outgoing
discovery or invitation packet. A restrictive egress policy must additionally
allow host-to-device UDP on the configured OTA port and its reply traffic.
No `sudo` is required for `jh-vscode` itself.

If broadcast discovery is blocked but the address is known, use `ota.host`,
`--host`, or a unicast `ota.broadcast` value. This avoids broadcast only; it
does not remove the TCP callback firewall requirement.

## Trial Confirmation, Rollback, And Recovery

After a successful transfer, the boot applier swaps staging into the program
slot and starts it in `HAL_OTA_BOOT_TRIAL`. Each unconfirmed boot increments
the attempt counter. Once the stored limit is reached, the boot applier swaps
the previous image back and starts it as stable.

Call `hal_ota_confirm_boot_ex()` only after the new image has passed its
startup criteria. Calling it while already stable is harmless. Use
`hal_ota_get_boot_info_ex()` in diagnostics so logs can distinguish a normal
stable boot from trial, rollback, or recovery.

Keep a USB recovery path:

- BOOTSEL plus the merged `firmware.uf2` can reinstall the boot applier and
  application when network startup is broken.
- Changing flash size, OTA/storage layout, or target can leave incompatible
  state sectors behind. Reprovision or erase the relevant device only after
  preserving any required LittleFS/EEPROM data.
- Target-specific control-sector erase commands in
  [`tests/hardware/rp_ota/README.md`](../tests/hardware/rp_ota/README.md)
  apply to that fixture's exact layout and are not universal product erase
  ranges.
- Physical BOOTSEL access remains outside the OTA trust boundary.

## Security Boundary

The signed container authenticates its versioned header with HMAC-SHA256 and
verifies payload SHA-256 plus header CRC before activation. The same password
authenticates the UDP invitation flow. This provides authentication and
integrity, not confidentiality:

1. The device replies to an invitation with `AUTH <device-nonce>`.
2. The host generates a 16-byte random client nonce and formats it as 32
   lowercase hexadecimal characters.
3. Both sides compute lowercase `MD5(password UTF-8)` and then lowercase
   `MD5(password-md5:device-nonce:client-nonce)`.
4. The host sends `200 <client-nonce> <digest>` and proceeds only after the
   device replies `OK`.

This challenge-response sequence is retained for protocol compatibility. The
image HMAC remains the independent integrity check performed before staging is
accepted.

- firmware and both `.ota` artifacts are plaintext;
- anyone who knows the password can create an accepted image;
- image generation is not an anti-rollback counter, so an older correctly
  signed image can be replayed unless the product adds its own policy;
- use a unique high-entropy password per product or device group;
- perform OTA only on a trusted network segment or add a product-specific
  encrypted transport/VPN boundary.

See [Security Supply Chain](security_supply_chain.md#native-ota-security-boundary)
for the maintained security statement.

## Troubleshooting Checklist

If discovery or upload fails, check these in order:

1. `config-dump` reports the intended native RP target, WiFi board, hostname,
   UDP port, password environment variable name, and artifact paths.
2. The firmware was built with `HAL_ENABLE_OTA`; `firmware.ota` and the merged
   `firmware.uf2` exist below the resolved `buildDir`.
   For RP2040, every touched sector before the final UF2 page must contain all
   sixteen 256-byte pages; `test_rp_ota_artifacts` enforces this contract.
3. Firmware has joined WiFi, `hal_ota_begin()` returned true, and
   `hal_ota_handle()` continues to run.
4. Firmware and host use the same port, hostname, and exact password bytes.
   Restart VS Code from the configured environment if `passwordEnv` is
   reported missing.
5. The active target matches the discovered device. Use `--interactive` for
   several matching devices and `--host` for a known address.
6. Broadcast reaches the correct interface. Prefer a directed broadcast or
   direct host on multi-interface and routed hosts.
7. The AP permits device-to-host traffic and the host firewall counter
   increases when the board starts its TCP callback. Re-run
   `scripts/configure_ota_firewall.py` if the interface or LAN changed.
8. A repeated authentication failure usually means a password mismatch. A TCP
   accept timeout after successful UDP authentication usually means the
   callback firewall rule or route is wrong.
9. Immediate rollback means the application did not confirm the trial before
   the attempt limit, or its readiness checks never passed.
10. A board moved between incompatible target/runtime/layout images may need
    controlled USB reprovisioning or OTA metadata cleanup.
