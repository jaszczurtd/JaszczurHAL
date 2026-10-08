#!/usr/bin/env bash
# Regenerate the SBOM and run optional local vulnerability scanners.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SBOM="${REPO_ROOT}/security/sbom.cdx.json"
# Findings with a recorded decision (security/vulnerability_log.md).
OSV_CONFIG="${REPO_ROOT}/security/osv-scanner.toml"
CVE_TRIAGE="${REPO_ROOT}/security/cve-bin-tool-triage.toml"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

# shellcheck source=scanner_pins.sh
source "${SCRIPT_DIR}/scanner_pins.sh"

CVE_DB="${XDG_CACHE_HOME-${HOME}/.cache}"
CVE_DB="${CVE_DB:-.}/cve-bin-tool/cve.db"
CVE_REFRESH_ATTEMPTS="${JH_CVE_REFRESH_ATTEMPTS:-3}"
CVE_REFRESH_DELAY_S="${JH_CVE_REFRESH_DELAY_S:-20}"
CVE_COMMON=(--disable-data-source OSV --disable-version-check)

info() { printf '[INFO] %s\n' "$*"; }
warn() { printf '[WARN] %s\n' "$*" >&2; }

# A scanner release other than the pinned one, which CI runs, fails the scan:
# results differ between releases.
require_pinned() {
    local report
    if ! report="$(scanner_is_pinned "$1" "$2")"; then
        printf '[ERROR] %s; run scripts/install_host_tools.sh\n' "${report}" >&2
        exit 1
    fi
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

# osv-scanner exits with 1 for found vulnerabilities and with another code
# when it cannot finish, e.g. without network. Findings fail at once; other
# failures get retried and still fail the scan if they last.
run_osv_scanner() {
    local attempt status
    for ((attempt = 1; attempt <= CVE_REFRESH_ATTEMPTS; attempt++)); do
        status=0
        "$@" || status=$?
        if [[ "${status}" -le 1 ]]; then
            return "${status}"
        fi
        warn "osv-scanner could not finish (exit ${status}, attempt ${attempt}/${CVE_REFRESH_ATTEMPTS})"
        if ((attempt < CVE_REFRESH_ATTEMPTS)); then
            sleep "$((attempt * CVE_REFRESH_DELAY_S))"
        fi
    done
    return "${status}"
}

if scanner="$(scanner_path osv-scanner)"; then
    require_pinned osv-scanner "${scanner}"
    ran_scanner=1
    # The repository's own files: manifests and the SBOM, honouring
    # .gitignore. The vendored-directory heuristic stays off; it guesses
    # components from file hashes, cannot hash ESP-IDF and mismatches others.
    info "Running osv-scanner against repository sources"
    run_osv_scanner "${scanner}" scan source --config "${OSV_CONFIG}" \
        --experimental-disable-plugins filesystem/vendored \
        --recursive "${REPO_ROOT}"
    # The pinned components (git-ignored checkouts with their submodules),
    # identified by their commits; build trees stay out. osv-scanner matches
    # the excluded .build against paths relative to the working directory, so
    # the scan runs from the repository root.
    info "Running osv-scanner against the pinned component commits"
    (cd "${REPO_ROOT}" && run_osv_scanner "${scanner}" scan source --config "${OSV_CONFIG}" \
        --experimental-no-default-plugins --experimental-plugins vcs/gitrepo \
        --include-git-root --no-ignore --experimental-exclude .build \
        --recursive "${REPO_ROOT}")
    # A component pinned to a commit after a release: advisories end at the
    # releases they name, so osv-scanner reports nothing for that commit. The
    # release it descends from, its SBOM pedigree, is checked as well.
    release_input="${WORK_DIR}/releases.json"
    releases="$(python3 - "${SBOM}" "${release_input}" <<'RELEASES'
import json
import re
import sys

sbom = json.load(open(sys.argv[1], encoding="utf-8"))
packages = []
for component in sbom.get("components", []):
    for ancestor in component.get("pedigree", {}).get("ancestors", []):
        match = re.fullmatch(r"pkg:github/([^/@]+/[^/@]+)@([0-9a-f]{40})",
                             ancestor.get("purl", ""))
        if not match:
            sys.exit(f"[ERROR] {component['name']}: release without a GitHub commit purl")
        packages.append({"package": {"name": f"https://github.com/{match[1]}",
                                     "commit": match[2]}})
with open(sys.argv[2], "w", encoding="utf-8") as output:
    json.dump({"results": [{"source": {"path": sys.argv[1], "type": "lockfile"},
                            "packages": packages}]}, output)
print(len(packages))
RELEASES
)"
    if ((releases > 0)); then
        info "Running osv-scanner against the releases of components pinned after them"
        run_osv_scanner "${scanner}" scan source --config "${OSV_CONFIG}" \
            -L "osv-scanner:${release_input}"
    fi
else
    warn "osv-scanner not found at $(scanner_bin osv-scanner); skipping OSV vulnerability scan"
fi

if [[ "${JH_SECURITY_SCAN_SOURCE:-0}" == "1" ]]; then
    if scanner="$(scanner_path cve-bin-tool)"; then
        require_pinned cve-bin-tool "${scanner}"
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
        # Recorded decisions become an OpenVEX file (the parser needs a .json
        # name); the scan then drops the findings they cover.
        triage_vex="${WORK_DIR}/cve-bin-tool-triage.json"
        python3 "${SCRIPT_DIR}/cve_bin_tool_triage.py" \
            --triage "${CVE_TRIAGE}" --sbom "${SBOM}" --output "${triage_vex}"
        info "Running cve-bin-tool against ${SBOM}"
        "${scanner}" "${CVE_COMMON[@]}" \
            --update never \
            --sbom cyclonedx \
            --sbom-file "${SBOM}" \
            --vex-file "${triage_vex}" \
            --filter-triage
    else
        warn "cve-bin-tool not found at $(scanner_bin cve-bin-tool); skipping SBOM CVE scan"
    fi
fi

if [[ "${ran_scanner}" -eq 0 ]]; then
    warn "No vulnerability scanner was available. Install osv-scanner for source/vendored dependency checks."
fi
