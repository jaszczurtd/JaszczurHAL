#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
JH_VSCODE="${REPO_ROOT}/vscode/entry/jh-vscode"
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() {
    cat <<'USAGE'
Build native RP parity hardware fixtures for every supported target/runtime.

Usage:
  scripts/build_rp_native_parity_fixtures.sh [--jobs N]

Builds rp_usb_multicore and rp_sdlogger for RP2040, RP2350 ARM and RP2350
RISC-V in bare-metal and FreeRTOS variants. It also builds the private
Bluetooth Classic HID Device and HCI trace fixtures for RP2040 Pico W and
RP2350 ARM Pico 2 W. Artifacts stay below .build/hardware/.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ ! "${JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: --jobs must be a positive integer" >&2
    exit 2
fi

selections=(
    "rp2040 pico"
    "rp2350-arm pico2"
    "rp2350-riscv pico2"
)
fixtures=(
    "rp_usb_multicore"
    "rp_sdlogger"
)

export CMAKE_BUILD_PARALLEL_LEVEL="${JOBS}"

for fixture in "${fixtures[@]}"; do
    project="${REPO_ROOT}/tests/hardware/${fixture}"
    "${JH_VSCODE}" clean --project "${project}"
    for selection in "${selections[@]}"; do
        read -r target board <<<"${selection}"
        "${JH_VSCODE}" build --project "${project}" \
            --target "${target}" --board "${board}"
        "${JH_VSCODE}" build --project "${project}" \
            --target "${target}" --board "${board}" --variant freertos
    done
done

bluetooth_selections=(
    "rp2040 picow"
    "rp2350-arm pico2w"
)
bluetooth_fixtures=(
    "bluetooth_classic_hid_device"
    "bluetooth_classic_hci_trace"
)

for fixture in "${bluetooth_fixtures[@]}"; do
    project="${REPO_ROOT}/tests/hardware/${fixture}"
    "${JH_VSCODE}" clean --project "${project}"
    for selection in "${bluetooth_selections[@]}"; do
        read -r target board <<<"${selection}"
        "${JH_VSCODE}" build --project "${project}" \
            --target "${target}" --board "${board}"
    done
done
