# Security Supply Chain

This document describes the lightweight SBOM and vulnerability-tracking process
used by JaszczurHAL.

## Scope

The tracked supply-chain surface includes:

- bundled third-party source copied into `src/`,
- pinned external checkouts used by build helpers, such as FreeRTOS-Kernel,
- adapted upstream code where local changes may affect security behavior.

The inventory does not replace per-product firmware analysis. Downstream
firmware should generate or retain its own SBOM because active `HAL_ENABLE_*`
flags decide which optional modules are actually compiled.

## Files

| File | Purpose |
|------|---------|
| `security/third_party.json` | Human-maintained source of truth for bundled and pinned components. |
| `security/third_party.schema.json` | JSON schema for reviewing the inventory shape. |
| `security/sbom.cdx.json` | Generated CycloneDX SBOM for the library repository. |
| `security/vulnerability_log.md` | Human-maintained vulnerability assessment and patch log. |
| `SECURITY.md` | Reporting, triage, severity and maintenance policy. |
| `scripts/generate_sbom.py` | Offline SBOM generator using only Python standard library. |
| `scripts/check_vulnerabilities.sh` | Optional scanner wrapper for local vulnerability checks. |

## Generate the SBOM

```bash
./scripts/generate_sbom.py
```

The generator reads `security/third_party.json` and writes
`security/sbom.cdx.json`. The generated SBOM is deterministic so normal
regeneration should produce small, reviewable diffs.

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
./scripts/check_sbom.sh
```

This regenerates the SBOM to a temporary file and compares it with the committed
`security/sbom.cdx.json`. CI uses this as a guard against changing
`security/third_party.json` without committing the regenerated SBOM.

## CI policy

GitHub Actions run a dedicated `security-scan` job on pull requests, pushes to
`main`, a weekly schedule and manual dispatch. The job:

- installs `osv-scanner` and `cve-bin-tool`,
- verifies that `security/sbom.cdx.json` is current,
- runs `osv-scanner` against the repository source tree and SBOM,
- runs `cve-bin-tool` against the CycloneDX SBOM.

The job is intentionally separate from build/test/static-analysis jobs. Security
scanner failures can be triaged independently from compiler or test failures,
and scheduled runs catch newly published CVEs even when the code has not
changed.

Policy for findings:

- Critical and high severity findings block release unless a
  `not_affected` decision is recorded.
- Medium severity findings require a triage entry before release.
- Low severity findings may be handled in planned maintenance, but should still
  be recorded.
- Opt-in module findings should state the affected `HAL_ENABLE_*` flags and
  supported targets.

## Updating a bundled component

1. Update the component source in its existing location.
2. Preserve upstream license files and attribution.
3. Update `security/third_party.json` with the new version, tag, commit, purl
   or upstream reference.
4. Run `./scripts/generate_sbom.py`.
5. Run focused tests for the affected module and the relevant build target.
6. If the update fixes or assesses a CVE, add an entry to
   `security/vulnerability_log.md` with CVSS, affected flags and decision.
7. Mention security-relevant updates in `doc/CHANGELOG.md`.

## Vulnerability assessment rules

Use the inventory as the starting point, then decide reachability:

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
