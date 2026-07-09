#!/usr/bin/env bash
# One-time local setup for JaszczurHAL on Debian/Ubuntu-like systems.
# Installs everything needed to build the library, run the host tests, run the
# CI quality gates, and build both the Arduino/RP2040 and STM32 targets locally.
# Safe to re-run.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Single source of truth for RP2040 Arduino core version ────────────────────
# shellcheck source=rp2040_core_version.conf
source "${SCRIPT_DIR}/rp2040_core_version.conf"

RP2040_INDEX="https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"

clean_build_artifacts() {
  local candidate
  local cleaned=0
  local build_dirs=(
    "${SCRIPT_DIR}/build"
  )

  for candidate in "${SCRIPT_DIR}"/build_*; do
    [ -d "${candidate}" ] && build_dirs+=("${candidate}")
  done

  for candidate in "${build_dirs[@]}"; do
    [ -d "${candidate}" ] || continue
    case "${candidate}" in
      "${SCRIPT_DIR}/build"|"${SCRIPT_DIR}/build_"*) ;;
      *) continue ;;
    esac

    rm -rf -- "${candidate}"
    cleaned=$((cleaned + 1))
  done

  if [ "${cleaned}" -eq 0 ]; then
    echo "No existing build artifact directories to remove."
  else
    echo "Removed ${cleaned} build artifact directories."
  fi
}

clean_build_artifacts

sudo apt-get update

# Core build + host-test toolchain (required: without these `cmake -B build`
# and `ctest` cannot run). build-essential provides gcc/g++/make; the host mock
# backend links pthreads, which comes with glibc. curl fetches arduino-cli and
# the security scanner below. Python runs repository helper scripts.
sudo apt-get install -y build-essential cmake git curl ca-certificates python3

# Client/runtime tooling used by generated jh-vscode projects:
# - openocd flashes/debugs STM32G474 targets,
# - python3-serial powers the persistent serial monitor,
# - psmisc provides fuser for safe serial monitor handoff before upload.
sudo apt-get install -y openocd python3-serial psmisc

# STM32 FreeRTOS dependency - fetched only as part of this explicit setup step.
"${SCRIPT_DIR}/scripts/ensure_freertos_kernel.sh" --force --repo-root "${SCRIPT_DIR}"

# Quality-gate tooling - memory safety (valgrind / `ctest -T memcheck`) and
# static analysis (clang-tidy + cppcheck; clang-tools provides run-clang-tidy).
# See README "Continuous integration and quality gates".
sudo apt-get install -y valgrind clang-tidy cppcheck clang-tools clang-format

# Security/SBOM tooling. `generate_sbom.py` only needs Python stdlib, but the
# vulnerability check wrapper uses osv-scanner for source/vendored dependency
# checks and can optionally use cve-bin-tool for SBOM-based CVE checks.
sudo apt-get install -y pipx

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

install_osv_scanner
install_cve_bin_tool

# ARM bare-metal toolchain - cross-compiles real STM32G474 firmware
# (scripts/build_stm32_lib.sh). The host-compiler STM32 build and the unit
# tests do not need it, but it is part of a complete JaszczurHAL setup.
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi

# Arduino/RP2040 toolchain - arduino-cli is not an apt package, so install it
# via the official script (into /usr/local/bin) and then add the RP2040 core.
if ! command -v arduino-cli >/dev/null 2>&1; then
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
    | sudo BINDIR=/usr/local/bin sh
fi
arduino-cli core update-index --additional-urls "$RP2040_INDEX"
arduino-cli core install "rp2040:rp2040@${RP2040_CORE_VERSION}" --additional-urls "$RP2040_INDEX"

# Git hooks for formatting and commit-message validation.
if [ -d "${SCRIPT_DIR}/.githooks" ]; then
  chmod +x "${SCRIPT_DIR}/.githooks/pre-commit" "${SCRIPT_DIR}/.githooks/commit-msg"
  git -C "${SCRIPT_DIR}" config core.hooksPath .githooks
fi

# ── Self-check: report anything still missing ────────────────────────────────
echo
echo "Verifying toolchain..."
missing=0
tool_exists() {
  command -v "$1" >/dev/null 2>&1 || [ -x "${HOME}/.local/bin/$1" ]
}

for tool in cmake g++ gcc make git python3 valgrind clang-tidy cppcheck \
            run-clang-tidy clang-format osv-scanner cve-bin-tool \
            arm-none-eabi-gcc arm-none-eabi-g++ arm-none-eabi-ar \
            arm-none-eabi-ranlib arm-none-eabi-objcopy arm-none-eabi-objdump \
            openocd fuser arduino-cli; do
  if tool_exists "$tool"; then
    printf '  ok       %s\n' "$tool"
  else
    printf '  MISSING  %s\n' "$tool"
    missing=1
  fi
done

if python3 -c 'import serial' >/dev/null 2>&1; then
  printf '  ok       %s\n' "python3:serial"
else
  printf '  MISSING  %s\n' "python3:serial"
  missing=1
fi

if [ "$missing" -ne 0 ]; then
  echo "Some tools are still missing (see above)."
  exit 1
fi
echo "Git hooks configured: $(git -C "${SCRIPT_DIR}" config --get core.hooksPath || echo "not configured")"
echo "All required tools present. JaszczurHAL is ready to build and test."
