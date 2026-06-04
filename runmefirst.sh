#!/usr/bin/env bash
# One-time local setup for JaszczurHAL on Debian/Ubuntu-like systems.
# Installs everything needed to build the library, run the host tests, run the
# CI quality gates, and build both the Arduino/RP2040 and STM32 targets locally.
# Safe to re-run.
set -euo pipefail

RP2040_INDEX="https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"

sudo apt-get update

# Core build + host-test toolchain (required: without these `cmake -B build`
# and `ctest` cannot run). build-essential provides gcc/g++/make; the host mock
# backend links pthreads, which comes with glibc. curl fetches arduino-cli below.
sudo apt-get install -y build-essential cmake git curl

# Quality-gate tooling — memory safety (valgrind / `ctest -T memcheck`) and
# static analysis (clang-tidy + cppcheck; clang-tools provides run-clang-tidy).
# See README "Continuous integration and quality gates".
sudo apt-get install -y valgrind clang-tidy cppcheck clang-tools

# ARM bare-metal toolchain — cross-compiles real STM32G474 firmware
# (build_stm32_lib.sh). The host-compiler STM32 build and the unit tests do not
# need it, but it is part of a complete JaszczurHAL setup.
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi

# Arduino/RP2040 toolchain — arduino-cli is not an apt package, so install it
# via the official script (into /usr/local/bin) and then add the RP2040 core.
if ! command -v arduino-cli >/dev/null 2>&1; then
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
    | sudo BINDIR=/usr/local/bin sh
fi
arduino-cli core update-index --additional-urls "$RP2040_INDEX"
arduino-cli core install rp2040:rp2040 --additional-urls "$RP2040_INDEX"

# ── Self-check: report anything still missing ────────────────────────────────
echo
echo "Verifying toolchain..."
missing=0
for tool in cmake g++ gcc make git valgrind clang-tidy cppcheck run-clang-tidy \
            arm-none-eabi-gcc arduino-cli; do
  if command -v "$tool" >/dev/null 2>&1; then
    printf '  ok       %s\n' "$tool"
  else
    printf '  MISSING  %s\n' "$tool"
    missing=1
  fi
done

if [ "$missing" -ne 0 ]; then
  echo "Some tools are still missing (see above)."
  exit 1
fi
echo "All required tools present. JaszczurHAL is ready to build and test."
