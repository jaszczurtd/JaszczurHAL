#!/usr/bin/env bash
# Regenerate the SBOM and run optional local vulnerability scanners.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SBOM="${REPO_ROOT}/security/sbom.cdx.json"

CVE_DB="${HOME}/.cache/cve-bin-tool/cve.db"
CVE_REFRESH_ATTEMPTS="${JH_CVE_REFRESH_ATTEMPTS:-3}"
CVE_REFRESH_DELAY_S="${JH_CVE_REFRESH_DELAY_S:-20}"
CVE_COMMON=(--disable-data-source OSV --disable-version-check)

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

# cve-bin-tool exits with 1 both for found CVEs and for a failed data download.
# Refreshing the database against an empty directory keeps the two apart: the
# SBOM scan then runs offline, so its failure can only mean findings.
refresh_cve_data() {
    local scanner="$1"
    local empty attempt
    empty="$(mktemp -d)"
    for ((attempt = 1; attempt <= CVE_REFRESH_ATTEMPTS; attempt++)); do
        if "${scanner}" "${CVE_COMMON[@]}" --update daily "${empty}"; then
            rmdir "${empty}"
            return 0
        fi
        warn "CVE data refresh failed (attempt ${attempt}/${CVE_REFRESH_ATTEMPTS})"
        if ((attempt < CVE_REFRESH_ATTEMPTS)); then
            sleep "$((attempt * CVE_REFRESH_DELAY_S))"
        fi
    done
    rmdir "${empty}"
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
        info "Refreshing the cve-bin-tool database"
        if ! refresh_cve_data "${scanner}"; then
            if [[ ! -s "${CVE_DB}" ]]; then
                printf '[ERROR] CVE data source unavailable and no cached database at %s\n' \
                    "${CVE_DB}" >&2
                exit 1
            fi
            stale="CVE data source unavailable; scanning with the cached database from $(date -u -r "${CVE_DB}" +%Y-%m-%d)"
            warn "${stale}"
            if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
                printf '::warning title=CVE database::%s\n' "${stale}"
            fi
        fi
        info "Running cve-bin-tool against ${SBOM}"
        "${scanner}" "${CVE_COMMON[@]}" \
            --update never \
            --sbom cyclonedx \
            --sbom-file "${SBOM}"
    else
        warn "cve-bin-tool not found; skipping SBOM CVE scan"
    fi
fi

if [[ "${ran_scanner}" -eq 0 ]]; then
    warn "No vulnerability scanner was available. Install osv-scanner for source/vendored dependency checks."
fi
