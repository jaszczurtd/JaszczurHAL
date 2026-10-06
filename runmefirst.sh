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
  - install (or update) system packages via apt: build tools, the arm-none-eabi toolchain,
    host test/QA tooling (valgrind, clang-tidy, ...), openocd, and
    libusb + pkg-config (picotool USB access),
  - install (or update) osv-scanner into /usr/local/bin,
  - write a udev rule under /etc/udev/rules.d so you can access RP2040/RP2350
    USB and serial devices without sudo afterwards,
  - inspect the host firewall and, only after separate confirmation, allow the
    OTA TCP/8266 callback and UDP/8266 discovery replies from the detected
    local IPv4 network persistently.

WHYSUDO

# Host packages, security scanners and the pinned components; Linux CI
# installs through the same script. It ends by checking every tool.
"${SCRIPT_DIR}/scripts/install_host_tools.sh"

# udev rules for sudo-less USB flashing of Raspberry Pi RP2040/RP2350 boards.
# picotool and native UF2 upload need the USB device node, while the automatic
# 1200-bps BOOTSEL touch needs the app-mode ttyACM node. Vendor-wide 2e8a covers
# BOOTSEL (2e8a:0003) and JaszczurHAL's CDC/picotool interfaces. Idempotent and
# skipped cleanly where udev is absent (minimal containers / non-udev CI).
install_pico_udev_rule() {
  local rule_file="/etc/udev/rules.d/99-jaszczurhal-pico.rules"
  local usb_rule='SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", MODE="0666", GROUP="plugdev", TAG+="uaccess"'
  local serial_rule='SUBSYSTEM=="tty", KERNEL=="ttyACM*", ATTRS{idVendor}=="2e8a", MODE="0666", GROUP="plugdev", TAG+="uaccess"'
  local rules="${usb_rule}"$'\n'"${serial_rule}"

  if [ ! -d /etc/udev/rules.d ]; then
    echo "  udev not present; skipping RP2040/RP2350 USB flashing rule."
    return 0
  fi

  if [ -f "${rule_file}" ] && [ "$(cat "${rule_file}" 2>/dev/null)" = "${rules}" ]; then
    return 0
  fi

  printf '%s\n' "${rules}" | sudo tee "${rule_file}" >/dev/null
  if command -v udevadm >/dev/null 2>&1; then
    sudo udevadm control --reload-rules >/dev/null 2>&1 || true
    sudo udevadm trigger >/dev/null 2>&1 || true
  fi
  echo "  Installed ${rule_file} (sudo-less RP2040/RP2350 USB and ttyACM access)."
}

install_pico_udev_rule
python3 "${SCRIPT_DIR}/scripts/configure_ota_firewall.py"

# Git hooks for formatting and commit-message validation.
if [ -d "${SCRIPT_DIR}/.githooks" ]; then
  chmod +x "${SCRIPT_DIR}/.githooks/pre-commit" "${SCRIPT_DIR}/.githooks/commit-msg"
  git -C "${SCRIPT_DIR}" config core.hooksPath .githooks
fi

echo "Git hooks configured: $(git -C "${SCRIPT_DIR}" config --get core.hooksPath || echo "not configured")"
echo "All required tools present. JaszczurHAL is ready to build and test."
