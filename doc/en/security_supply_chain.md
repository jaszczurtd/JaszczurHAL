<a id="security-supply-chain"></a>

# Dependency and toolchain security

*Also available in [Polish](../pl/security_supply_chain.md).*

JaszczurHAL maintains an inventory of external components and tools, generates an SBOM from that inventory, and records vulnerability assessments. This guide explains how to reproduce and check those records and how to prepare dependency updates for a release.

## Scope

The inventory covers:

- bundled third-party source copied into `src/`, including the CYW43 host
  stack and the redistributed radio firmware under
  `src/hal/network/cyw43/vendor`,
- pinned external checkouts used by the component updater, including BearSSL,
  cJSON, LodePNG, TJpg_Decoder, FatFs, Unity, lwIP, littlefs, BTstack, the
  Semtech SX126x driver, FreeRTOS-Kernel, Pico SDK, and ESP-IDF,
- the exact binary and Python development tools selected by the pinned ESP-IDF
  tool registry for `esp32` and `esp32s3`,
- adapted upstream code where local changes may affect security behavior.

The repository inventory is not a substitute for product-specific analysis. Applications should maintain their own SBOM because the enabled `HAL_ENABLE_*` flags determine which optional modules are included in the firmware.

<a id="native-ota-security-boundary"></a>

## Native OTA protections and limitations

Native RP OTA authenticates the versioned image header with HMAC-SHA256 and verifies the payload SHA-256 and header CRC before activation. The symmetric HMAC key is derived from the same application password used for transport authentication. Anyone who knows that password can therefore create an accepted image. Products should use a unique, high-entropy secret supplied to the VS Code project tooling through `ota.passwordEnv`, not a password written into a tracked file.

The transport and image are not encrypted; firmware confidentiality is outside
this mechanism. Image generation is authenticated metadata, not an
anti-rollback counter: an older image signed with the current secret can be
replayed unless the application or product provisioning layer imposes a
stricter version policy. BOOTSEL access likewise remains a physical
recovery/provisioning boundary.

ESP32-S3 OTA transfers the raw application BIN selected from the validated
ESP-IDF artifact manifest. The host verifies the manifest size/SHA-256 record;
the device verifies the protocol MD5 and calls ESP-IDF image validation before
selecting the inactive OTA partition.

Both targets use AUTH2 when firmware has a configured non-empty password. It
computes HMAC-SHA256 with the lowercase ASCII MD5 of the password as key and binds the
command, callback TCP port, image size, image MD5, and independent 16-byte
device and client nonces. The device pins authentication to the invitation's
UDP address and source port and calls back to the same IPv4 address; the host
uses a connected UDP socket and requires the TCP peer to match its selected UDP
peer.

Both nonce generators use the platform secure-random provider.

The ASCII parser rejects ambiguous whitespace, embedded NULs, numeric aliases, invalid lengths, and extra fields. A non-empty host password rules out direct `OK`, legacy `AUTH`, and legacy `200` authentication. If mutex allocation fails, the service stays stopped on both platforms rather than starting the transport without its required lock.

AUTH2 is symmetric password authentication, not modern image signing or
encryption. Omitting the device password or setting it to an empty string skips
AUTH2. `ota.allowEmptyPassword=true` only lets the host proceed in that
explicitly unauthenticated development mode. Product authenticity,
confidentiality, and anti-rollback policy on ESP32-S3 require the appropriate
ESP-IDF Secure Boot V2, flash-encryption, eFuse, protected-key, and recovery
configuration. Normal uploads and tests do not enable irreversible eFuse
settings.

Operational secret placement, firewall scope, first installation, rollback,
and recovery are documented in
[Native OTA Workflow](OTAWorkflow.md#shared-auth2-transport-authentication).

## Files

| File | Purpose |
|------|---------|
| `security/third_party.json` | Human-maintained source of truth for bundled and pinned components. |
| `security/third_party.schema.json` | JSON schema for reviewing the inventory shape. |
| `security/sbom.cdx.json` | Generated CycloneDX SBOM for the library repository. |
| `security/esp_idf_tools.json` | Reviewed snapshot of the exact ESP-IDF target-tool versions, licenses, upstreams, framework commit, and `tools.json` digest. |
| `security/vulnerability_log.md` | Human-maintained vulnerability assessment and patch log. |
| `third_party/cyw43_driver_version.conf` | Pinned revision of the vendored CYW43 import, including the separate Pico SDK revision behind its Bluetooth shared-bus files. |
| `src/hal/network/cyw43/vendor/SHA256SUMS` | SHA-256 manifest of every vendored CYW43 file, enforced by `test_cyw43_dependency_boundary`. |
| `SECURITY.md` | Reporting, triage, severity and maintenance policy. |
| `scripts/generate_sbom.py` | Offline SBOM generator using only Python standard library. |
| `scripts/sync_generated.py` | Shared refresh and read-only verification runner for all tracked generated artifacts, including the SBOM. |
| `scripts/check_release_metadata.py` | Release gate for VERSION, SBOM, tag name and mainline ancestry. |
| `scripts/check_vulnerabilities.sh` | Optional scanner wrapper for local vulnerability checks. |

## Generate the SBOM

```bash
python3 scripts/sync_generated.py --write
```

The synchronization script invokes the generator, which reads `security/third_party.json` and `security/esp_idf_tools.json` and writes `security/sbom.cdx.json`. Generation is deterministic: unchanged inputs should produce the same output.

<a id="esp-idf-tool-provenance"></a>

## ESP-IDF tool versions and provenance

`third_party/esp_idf_version.conf` pins ESP-IDF v6.0.2 to one commit and selects
`esp32` and `esp32s3`. `security/esp_idf_tools.json` records the matching
official tool set:
Xtensa GDB/GCC, the companion RISC-V GCC bundle, ESP32 ULP tools, Espressif
OpenOCD, ROM ELF data, and eleven first-party Python tools declared directly by
ESP-IDF's core requirements. Those Python entries cover esptool, component
management, monitor, core-dump, Kconfig, NVS partition generation, size,
diagnostic, panic-decoder, Clang Python-binding, and FreeRTOS GDB tooling.
General-purpose transitive Python packages remain owned by the upstream
environment requirements rather than being duplicated in this snapshot.

The component manager verifies every listed Python distribution after the
upstream ESP-IDF installer runs. If a mutable upstream constraint selects a
different compatible release, setup reapplies the reviewed exact versions
under that constraint before a build may proceed. CI keys the ESP-IDF cache by
both the framework pin and this snapshot, so a reviewed tool change creates a
new environment instead of reusing stale packages.

`scripts/generate_sbom.py` expands the snapshot directly into development-scope
CycloneDX components, including each tool's own reviewed SPDX license. The tool
entries are not copied into `security/third_party.json`, which remains the
source for the framework and the repository's other third-party components.

Every production ESP-IDF build also emits
`generated/jaszczurhal/jh_esp_idf_toolchain.json` and embeds it in
`jh_esp_idf_artifacts.json`. This per-build record captures the actual compiler,
CMake, Ninja, IDF Python, and esptool versions plus the framework `tools.json`
SHA-256, while omitting absolute host paths. The artifact manifest separately
records the exact ESP-IDF commit, final `sdkconfig` digest, partition-table
profile/offset/hash, and every flashed image hash. The reviewed snapshot states
what the pin selects; the build manifest proves what produced one firmware
build.

## Check vulnerabilities

```bash
./scripts/check_vulnerabilities.sh
```

The check script regenerates the SBOM first. If `osv-scanner` is available, it
scans the repository source tree, including vendored C/C++ dependencies that the
scanner can identify. If `cve-bin-tool` is available, it can also scan the
generated CycloneDX SBOM with:

```bash
JH_SECURITY_SCAN_SOURCE=1 ./scripts/check_vulnerabilities.sh
```

The script intentionally does not install tools. CI or local workstations should
provide the scanner versions they trust. For a local Debian/Ubuntu-style setup,
`./runmefirst.sh` installs the default scanner tooling used by this repository:
`osv-scanner` for source/vendored dependency checks and `cve-bin-tool` for an
optional SBOM-based CVE check.

## Verify SBOM freshness

```bash
python3 scripts/sync_generated.py --check
```

This read-only check covers every tracked generated file. It creates a temporary SBOM and compares it with `security/sbom.cdx.json`. The compatibility command `./scripts/check_sbom.sh` uses the same check but limits its scope to the SBOM.

## CI policy

GitHub Actions run a required `test` job and a dependent `security-scan` job on
pull requests, pushes to `main`, a weekly schedule and manual dispatch. The
test job verifies all generated artifacts, including the SBOM. The security
job:

- installs `osv-scanner` and `cve-bin-tool`,
- runs `osv-scanner` against the repository source tree,
- runs `cve-bin-tool` against the CycloneDX SBOM.

The CVE database is cached for a day. When the NVD mirror is unreachable, the
scan uses the most recent cached database and reports a warning.

Vulnerability scanning runs separately from compilation, tests, and static analysis so scanner failures can be investigated independently. Scheduled scans also detect newly published CVEs when the repository code has not changed.

Handle findings as follows:

- Critical and high severity findings block release unless a
  `not_affected` decision is recorded.
- Medium severity findings require a triage entry before release.
- Low severity findings may be handled in planned maintenance, but should still
  be recorded.
- Opt-in module findings should state the affected `HAL_ENABLE_*` flags and
  supported targets.

<a id="release-gate"></a>

## Pre-release checks

Before creating a release tag, verify that `VERSION` and the SBOM project
version agree:

```bash
python3 scripts/check_release_metadata.py
```

Create the release tag only after its commit has been merged into `main`. Tag-triggered CI checks the tag name and verifies that the tagged commit is an ancestor of `origin/main`; it rejects a tag from a divergent release branch. Host CI also runs the complete test suite with ASan/UBSan, the native tests with ThreadSanitizer, and short fuzzing runs for the HTTP, WebSocket, and multipart upload parsers.

## Updating a component

1. For a managed external component, update its tracked
   `third_party/*_version.conf` pin and run
   `./third_party/update_components.sh`. For bundled code, update the source in
   its existing location.
2. Preserve upstream license files and attribution.
3. Update `security/third_party.json` with the new version, tag, commit, purl
   or upstream reference.
4. For ESP-IDF, refresh `security/esp_idf_tools.json` from the pinned
   `tools.json` and managed Python environment, then review every tool license.
5. Run `python3 scripts/sync_generated.py --write` and review the generated
   artifact diff.
6. Run focused tests for the affected module and the relevant build target.
7. If the update fixes or assesses a CVE, add an entry to
   `security/vulnerability_log.md` with CVSS, affected flags and decision.

<a id="vulnerability-assessment-rules"></a>

## Assessing vulnerability impact

Start with the component inventory, then check whether the vulnerable code is present and reachable in a supported configuration:

- `not_affected`: the vulnerable code is not present, not compiled, or not
  reachable in the supported HAL integration.
- `affected`: the code is present and reachable in at least one supported HAL
  configuration.
- `fixed`: the repository contains a patch or updated component version.
- `mitigated`: documented configuration or runtime constraints reduce practical
  impact but do not remove the vulnerable code.
- `under_investigation`: more analysis is needed.

For embedded products, record which `HAL_ENABLE_*` flags and targets make the
issue reachable. A vulnerability in `HAL_ENABLE_MQTT` or `HAL_ENABLE_WIREGUARD`
does not automatically affect firmware that does not compile those modules.
