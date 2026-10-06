#!/usr/bin/env bash
# Install the host tools the JaszczurHAL quality gate and generated projects
# need, then the pinned components. runmefirst.sh and every Linux CI job call
# this script, so a local gate and CI run with the same tool set; the gate's
# tools stage runs it with --check. Debian/Ubuntu only; installing uses sudo.
#
# Usage:
#   scripts/install_host_tools.sh          # install, sync components, check
#   scripts/install_host_tools.sh --check  # only report missing tools
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

PACKAGES=(
    # Build and host tests. Python runs the repository scripts, PyYAML lets
    # the tests read the CI workflow, Java runs PMD CPD, curl fetches
    # osv-scanner.
    build-essential cmake ninja-build git curl ca-certificates python3
    python3-yaml iproute2 default-jre-headless
    # runalltests.sh --commit copies the pinned components into its checkout.
    rsync
    # Generated projects: debugging, the serial monitor, its port handoff
    # (fuser) and picotool USB access.
    openocd gdb-multiarch python3-serial psmisc libusb-1.0-0-dev pkg-config
    # Memory safety, sanitizers and static analysis; pipx installs cve-bin-tool.
    valgrind clang clang-tidy clang-tools clang-format pipx
    # Arm cross toolchain for STM32 and RP.
    gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi
    libstdc++-arm-none-eabi-dev
)

# Clang itself may be installed with a version suffix only;
# scripts/run_sanitizer_fuzz.sh --check-tools resolves and checks it.
COMMANDS=(
    cmake ninja g++ gcc make git python3 ip java rsync valgrind clang-tidy
    run-clang-tidy clang-format
    arm-none-eabi-gcc arm-none-eabi-g++ arm-none-eabi-ar arm-none-eabi-ranlib
    arm-none-eabi-objcopy arm-none-eabi-objdump openocd gdb-multiarch fuser
)
PYTHON_MODULES=(serial yaml)

# shellcheck source=scanner_pins.sh
source "${SCRIPT_DIR}/scanner_pins.sh"

# True when scanner $1 is installed at its pinned release.
scanner_current() {
    local path
    path="$(scanner_path "$1")" && scanner_is_pinned "$1" "${path}" >/dev/null
}

# Downloads URL $1 to file $3; fails unless the file has SHA-256 $2.
fetch_pinned() {
    curl -fsSL "$1" -o "$3" || return 1
    if ! printf '%s  %s\n' "$2" "$3" | sha256sum --check --status; then
        echo "Download does not match its pinned SHA-256: $1" >&2
        return 1
    fi
}

install_osv_scanner() {
    if scanner_current osv-scanner; then
        return
    fi

    local url sha256
    case "$(uname -m)" in
        x86_64|amd64)
            url="${OSV_SCANNER_URL_AMD64}"
            sha256="${OSV_SCANNER_SHA256_AMD64}"
            ;;
        aarch64|arm64)
            url="${OSV_SCANNER_URL_ARM64}"
            sha256="${OSV_SCANNER_SHA256_ARM64}"
            ;;
        *)
            echo "Unsupported architecture for automatic osv-scanner install: $(uname -m)"
            echo "Install osv-scanner ${OSV_SCANNER_VERSION} manually and re-run this script."
            return 1
            ;;
    esac

    local tmp
    tmp="$(mktemp)"
    if ! fetch_pinned "${url}" "${sha256}" "${tmp}"; then
        rm -f "${tmp}"
        return 1
    fi
    sudo install -m 0755 "${tmp}" "$(scanner_bin osv-scanner)"
    rm -f "${tmp}"
}

install_cve_bin_tool() {
    if scanner_current cve-bin-tool; then
        return
    fi

    # pip reads the version from the wheel's file name, so the download keeps it.
    local dir wheel
    dir="$(mktemp -d)"
    wheel="${dir}/${CVE_BIN_TOOL_URL##*/}"
    if ! fetch_pinned "${CVE_BIN_TOOL_URL}" "${CVE_BIN_TOOL_SHA256}" "${wheel}"; then
        rm -rf "${dir}"
        return 1
    fi
    # Into the directory the scan runs it from, whatever PIPX_BIN_DIR says
    # (GitHub runners point it at /opt/pipx_bin).
    PIPX_BIN_DIR="$(dirname "$(scanner_bin cve-bin-tool)")" \
        python3 -m pipx install --force "${wheel}"
    rm -rf "${dir}"
}

check_tools() {
    local missing=0 tool module path report
    if "${SCRIPT_DIR}/run_sanitizer_fuzz.sh" --check-tools >/dev/null 2>&1; then
        printf '  ok       %s\n' "clang (sanitizer toolchain)"
    else
        printf '  MISSING  %s\n' "clang (sanitizer toolchain)"
        missing=1
    fi
    for tool in "${COMMANDS[@]}"; do
        if command -v "${tool}" >/dev/null 2>&1 || [ -x "${HOME}/.local/bin/${tool}" ]; then
            printf '  ok       %s\n' "${tool}"
        else
            printf '  MISSING  %s\n' "${tool}"
            missing=1
        fi
    done
    # The scanners scripts/check_vulnerabilities.sh runs: the pinned releases
    # where this script installs them, whatever else PATH holds.
    for tool in osv-scanner cve-bin-tool; do
        if ! path="$(scanner_path "${tool}")"; then
            printf '  MISSING  %s at %s\n' "${tool}" "$(scanner_bin "${tool}")"
            missing=1
        elif ! report="$(scanner_is_pinned "${tool}" "${path}")"; then
            printf '  VERSION  %s\n' "${report}"
            missing=1
        else
            printf '  ok       %s %s\n' "${tool}" "$(scanner_pin "${tool}")"
        fi
    done
    for module in "${PYTHON_MODULES[@]}"; do
        if python3 -c "import ${module}" >/dev/null 2>&1; then
            printf '  ok       %s\n' "python3:${module}"
        else
            printf '  MISSING  %s\n' "python3:${module}"
            missing=1
        fi
    done
    if [ "${missing}" -ne 0 ]; then
        echo "Some tools are missing or not at their pinned version; run ./runmefirst.sh (CI: scripts/install_host_tools.sh)."
        return 1
    fi
    echo "All required tools present."
}

case "${1:-}" in
    --check)
        check_tools
        exit
        ;;
    "") ;;
    *)
        echo "Unknown option: $1" >&2
        exit 1
        ;;
esac

sudo apt-get update
sudo apt-get install -y "${PACKAGES[@]}"
install_osv_scanner
install_cve_bin_tool
"${REPO_ROOT}/third_party/update_components.sh"
check_tools
