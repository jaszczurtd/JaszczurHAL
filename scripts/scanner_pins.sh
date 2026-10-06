# Sourced by scripts/install_host_tools.sh and scripts/check_vulnerabilities.sh:
# the scanner releases pinned in third_party/osv_scanner_version.conf and
# third_party/cve_bin_tool_version.conf.

PINS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../third_party/osv_scanner_version.conf
source "${PINS_ROOT}/third_party/osv_scanner_version.conf"
# shellcheck source=../third_party/cve_bin_tool_version.conf
source "${PINS_ROOT}/third_party/cve_bin_tool_version.conf"

# Where scripts/install_host_tools.sh installs each scanner: osv-scanner into
# /usr/local/bin (JH_OSV_SCANNER_DIR overrides it), cve-bin-tool through pipx
# into ~/.local/bin. The tool check and the scan run exactly these files, so
# another copy earlier in PATH changes nothing.
scanner_bin() {
    case "$1" in
        osv-scanner) printf '%s\n' "${JH_OSV_SCANNER_DIR:-/usr/local/bin}/osv-scanner" ;;
        cve-bin-tool) printf '%s\n' "${HOME}/.local/bin/cve-bin-tool" ;;
    esac
}

# Prints the installed scanner $1; fails when it is not installed.
scanner_path() {
    local path
    path="$(scanner_bin "$1")"
    [[ -x "${path}" ]] && printf '%s\n' "${path}"
}

# Prints the version scanner $1 at path $2 reports, or nothing; never fails,
# so callers under set -e report the mismatch themselves.
scanner_version() {
    case "$1" in
        osv-scanner)
            { "$2" --version 2>/dev/null || true; } | sed -n 's/^osv-scanner version: //p'
            ;;
        cve-bin-tool)
            { "$2" --version 2>/dev/null || true; } | sed -n 's/^\([0-9][0-9.]*\)$/\1/p' | tail -n 1
            ;;
    esac
}

scanner_pin() {
    case "$1" in
        osv-scanner) printf '%s\n' "${OSV_SCANNER_VERSION}" ;;
        cve-bin-tool) printf '%s\n' "${CVE_BIN_TOOL_VERSION}" ;;
    esac
}

# Fails, printing "<scanner> <version> at <path>, pinned <version>", when
# scanner $1 at path $2 is not its pinned release.
scanner_is_pinned() {
    local version pin
    version="$(scanner_version "$1" "$2")"
    pin="$(scanner_pin "$1")"
    if [[ "${version}" != "${pin}" ]]; then
        printf '%s %s at %s, pinned %s\n' "$1" "${version:-unknown}" "$2" "${pin}"
        return 1
    fi
}
