# Security Policy

JaszczurHAL is an embedded HAL and utility library. Security handling focuses on
the library code, bundled third-party sources, and the documented build helpers
that can affect downstream firmware.

## Supported versions

Security fixes are normally made on `main` first and then released in the next
tagged version. Downstream products should track the latest released version or
vendor a specific commit together with the generated SBOM.

Release 1.9.0 is affected by the HTTP/WebSocket memory-safety and network
lifecycle issues corrected in 1.9.1. Deployments should use 1.9.1 or later.

## Reporting a vulnerability

Please report suspected vulnerabilities privately before opening a public issue.

- Preferred channel: GitHub Security Advisory for this repository, if available.
- Fallback: contact the maintainer listed in `README.md`.

Please include:

- affected module, backend, feature flag, or bundled component,
- reachable configuration, for example `HAL_ENABLE_WIFI` + `HAL_ENABLE_MQTT`,
- reproduction steps or a minimal proof of concept,
- expected impact and whether network, local, or physical access is required.

## Triage and severity

Security issues are classified with CVSS v3.1 or v4.0 where practical. The
classification should consider embedded reachability: a bug in an opt-in module
that is not compiled into firmware may be recorded as not affected for that
firmware profile.

Suggested handling targets:

| Severity | Target response | Target fix or mitigation |
|----------|-----------------|--------------------------|
| Critical | 3 business days | As soon as practical |
| High | 5 business days | Next patch release when feasible |
| Medium | 10 business days | Planned release or documented mitigation |
| Low | Best effort | Planned maintenance |

## Third-party and SBOM maintenance

Bundled or pinned third-party components are tracked in
`security/third_party.json`. The CycloneDX SBOM is generated from that inventory:

```bash
./scripts/generate_sbom.py
```

Optional vulnerability checks regenerate the SBOM and then run local scanners
when the scanner tools are installed:

```bash
./scripts/check_vulnerabilities.sh
```

Known vulnerability assessments and patch decisions should be recorded in
`security/vulnerability_log.md`, especially when a CVE exists but the affected
code path is not compiled, not reachable, or locally patched.
