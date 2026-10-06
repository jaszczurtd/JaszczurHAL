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
    run-clang-tidy clang-format osv-scanner cve-bin-tool
    arm-none-eabi-gcc arm-none-eabi-g++ arm-none-eabi-ar arm-none-eabi-ranlib
    arm-none-eabi-objcopy arm-none-eabi-objdump openocd gdb-multiarch fuser
)
PYTHON_MODULES=(serial yaml)

install_osv_scanner() {
    if command -v osv-scanner >/dev/null 2>&1; then
        return
    fi

    local arch
    case "$(uname -m)" in
        x86_64|amd64) arch="amd64" ;;
        aarch64|arm64) arch="arm64" ;;
        *)
            echo "Unsupported architecture for automatic osv-scanner install: $(uname -m)"
            echo "Install osv-scanner manually and re-run this script."
            return 1
            ;;
    esac

    local version="${OSV_SCANNER_VERSION:-latest}"
    local url
    if [ "${version}" = "latest" ]; then
        url="https://github.com/google/osv-scanner/releases/latest/download/osv-scanner_linux_${arch}"
    else
        url="https://github.com/google/osv-scanner/releases/download/${version}/osv-scanner_linux_${arch}"
    fi

    local tmp
    tmp="$(mktemp)"
    curl -fsSL "${url}" -o "${tmp}"
    chmod +x "${tmp}"
    sudo install -m 0755 "${tmp}" /usr/local/bin/osv-scanner
    rm -f "${tmp}"
}

install_cve_bin_tool() {
    if command -v cve-bin-tool >/dev/null 2>&1 || [ -x "${HOME}/.local/bin/cve-bin-tool" ]; then
        return
    fi

    python3 -m pipx install cve-bin-tool
}

check_tools() {
    local missing=0 tool module
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
    for module in "${PYTHON_MODULES[@]}"; do
        if python3 -c "import ${module}" >/dev/null 2>&1; then
            printf '  ok       %s\n' "python3:${module}"
        else
            printf '  MISSING  %s\n' "python3:${module}"
            missing=1
        fi
    done
    if [ "${missing}" -ne 0 ]; then
        echo "Some tools are missing; run ./runmefirst.sh (CI: scripts/install_host_tools.sh)."
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
