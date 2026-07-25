#!/usr/bin/env bash
# One-time local setup for JaszczurHAL on Debian/Ubuntu-like systems.
# Installs everything needed to build the library, run the host tests, run the
# CI quality gates, and build the native RP and STM32 targets locally.
# Safe to re-run.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

clean_build_artifacts() {
  if [ ! -d "${SCRIPT_DIR}/.build" ]; then
    echo "No existing build artifact directories to remove."
    return
  fi

  rm -rf -- "${SCRIPT_DIR}/.build"
  echo "Removed ${SCRIPT_DIR}/.build."
}

clean_build_artifacts

# ── Why this script needs sudo (shown before the first password prompt) ──────
cat <<'WHYSUDO'

This setup needs sudo (you'll be prompted for your password) to:
  - install system packages via apt: build tools, the arm-none-eabi toolchain,
    host test/QA tooling (valgrind, clang-tidy, cppcheck, ...), openocd, and
    libusb + pkg-config (picotool USB access),
  - install osv-scanner into /usr/local/bin,
  - write a udev rule under /etc/udev/rules.d so you can flash RP2040/RP2350
    boards over USB without sudo afterwards,
  - inspect the host firewall and, only after separate confirmation, allow the
    OTA TCP/8266 callback from the detected local IPv4 network persistently.

WHYSUDO

sudo apt-get update

# Core build + host-test toolchain (required: without `cmake -B .build/host`
# and `ctest` the gate cannot run). build-essential provides gcc/g++/make; the host mock
# backend links pthreads, which comes with glibc. curl fetches the security
# scanner below. Python runs repository helper scripts.
sudo apt-get install -y build-essential cmake git curl ca-certificates python3 iproute2

# Client/runtime tooling used by generated jh-vscode projects:
# - openocd flashes/debugs STM32G474 targets,
# - python3-serial powers the persistent serial monitor,
# - psmisc provides fuser for safe serial monitor handoff before upload,
# - libusb-1.0-0-dev + pkg-config let picotool talk to RP2040/RP2350 over USB.
sudo apt-get install -y openocd python3-serial psmisc libusb-1.0-0-dev pkg-config

# Synchronize all pinned source and toolchain components after their host build
# prerequisites are installed.
"${SCRIPT_DIR}/third_party/update_components.sh"

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

# udev rule for sudo-less USB flashing of Raspberry Pi RP2040/RP2350 boards.
# picotool and the native UF2 upload need write access to the USB device node;
# without this rule that requires sudo on every flash. Vendor-wide 2e8a covers
# both BOOTSEL (2e8a:0003) and the app-mode CDC/picotool interface. Idempotent
# and skipped cleanly where udev is absent (minimal containers / non-udev CI).
install_pico_udev_rule() {
  local rule_file="/etc/udev/rules.d/99-jaszczurhal-pico.rules"
  local rule='SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", MODE="0666", GROUP="plugdev", TAG+="uaccess"'

  if [ ! -d /etc/udev/rules.d ]; then
    echo "  udev not present; skipping RP2040/RP2350 USB flashing rule."
    return 0
  fi

  if [ -f "${rule_file}" ] && [ "$(cat "${rule_file}" 2>/dev/null)" = "${rule}" ]; then
    return 0
  fi

  printf '%s\n' "${rule}" | sudo tee "${rule_file}" >/dev/null
  if command -v udevadm >/dev/null 2>&1; then
    sudo udevadm control --reload-rules >/dev/null 2>&1 || true
    sudo udevadm trigger >/dev/null 2>&1 || true
  fi
  echo "  Installed ${rule_file} (sudo-less RP2040/RP2350 USB flashing; reattach device to apply)."
}

install_osv_scanner
install_cve_bin_tool
install_pico_udev_rule
python3 "${SCRIPT_DIR}/scripts/configure_ota_firewall.py"

# ARM bare-metal toolchain - cross-compiles real STM32G474 firmware
# (scripts/build_stm32_lib.sh). The host-compiler STM32 build and the unit
# tests do not need it, but it is part of a complete JaszczurHAL setup.
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi

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

for tool in cmake g++ gcc make git python3 ip valgrind clang-tidy cppcheck \
            run-clang-tidy clang-format osv-scanner cve-bin-tool \
            arm-none-eabi-gcc arm-none-eabi-g++ arm-none-eabi-ar \
            arm-none-eabi-ranlib arm-none-eabi-objcopy arm-none-eabi-objdump \
            openocd fuser; do
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
