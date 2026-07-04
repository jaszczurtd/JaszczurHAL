#!/usr/bin/env bash
# Regenerate the SBOM and run optional local vulnerability scanners.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SBOM="${REPO_ROOT}/security/sbom.cdx.json"

info() { printf '[INFO] %s\n' "$*"; }
warn() { printf '[WARN] %s\n' "$*" >&2; }

find_tool() {
    if command -v "$1" >/dev/null 2>&1; then
        command -v "$1"
        return 0
    fi

    if [[ -x "${HOME}/.local/bin/$1" ]]; then
        printf '%s\n' "${HOME}/.local/bin/$1"
        return 0
    fi

    return 1
}

info "Generating CycloneDX SBOM"
"${REPO_ROOT}/scripts/generate_sbom.py" --output "${SBOM}"

ran_scanner=0

if scanner="$(find_tool osv-scanner)"; then
    ran_scanner=1
    info "Running osv-scanner against repository sources"
    "${scanner}" scan source --recursive "${REPO_ROOT}"
else
    warn "osv-scanner not found; skipping OSV vulnerability scan"
fi

if [[ "${JH_SECURITY_SCAN_SOURCE:-0}" == "1" ]]; then
    if scanner="$(find_tool cve-bin-tool)"; then
        ran_scanner=1
        info "Running cve-bin-tool against ${SBOM}"
        "${scanner}" \
            --disable-data-source OSV \
            --disable-version-check \
            --sbom cyclonedx \
            --sbom-file "${SBOM}"
    else
        warn "cve-bin-tool not found; skipping SBOM CVE scan"
    fi
fi

if [[ "${ran_scanner}" -eq 0 ]]; then
    warn "No vulnerability scanner was available. Install osv-scanner for source/vendored dependency checks."
fi
