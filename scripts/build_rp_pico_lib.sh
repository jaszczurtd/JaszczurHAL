#!/usr/bin/env bash
# Build JaszczurHAL and optional portable firmware with the official Pico SDK.
# The build itself runs in `jh library build`; this launcher keeps the options,
# the default target and the provider check of the Pico SDK runner.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=lib/build_artifacts.sh
source "${SCRIPT_DIR}/lib/build_artifacts.sh"

RED='\033[0;31m'
NC='\033[0m'

die()  { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

usage() {
    cat <<'USAGE'
Build JaszczurHAL and optional portable firmware with the official Pico SDK.

Usage:
  scripts/build_rp_pico_lib.sh [options]

Options:
  --target NAME            rp2040, rp2350-arm, or rp2350-riscv
                           (default: rp2040)
  --board NAME             JaszczurHAL board profile (target default if omitted)
  --assembly KEY           Assembly of the device_tree.json in -p DIR
  --variant ID             Variant declared by JH_PROJECT_VARIANTS (device tree)
  --sdk-dir PATH           Pico SDK checkout
  --toolchain PATH         Toolchain root containing bin/
  --picotool-dir PATH      picotool source checkout directory
  --picotool-build-dir PATH
                           picotool build directory below .build/
  --example NAME           Build examples/NAME through the native app entry
  --freertos               Enable the pinned native FreeRTOS SMP kernel
  --all-features           Enable every feature supported by the target
  --library-only           Build only the linkable libJaszczurHAL.a archive
  -p, --project-config DIR Directory containing hal_project_config.h
  -D KEY=VALUE             Extra HAL compile definition (repeatable)
  -o, --output DIR         Build directory below .build/
                           (default: .build/static/<target>/<board>, or
                           .build/static/<target>/<assembly>/<key> with a
                           device tree)
  --clean                  Remove the selected build directory first
  -j, --jobs N             Parallel jobs (default: nproc)
  -h, --help               Show this help
USAGE
}

TARGET="rp2040"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        --board|--assembly|--variant|--sdk-dir|--toolchain|--picotool-dir|\
        --picotool-build-dir|--example)
            jh_launcher_option "${1#--}" "$2"; shift 2 ;;
        --freertos|--all-features|--library-only|--clean)
            jh_launcher_flag "${1#--}"; shift ;;
        -p|--project-config) jh_launcher_option project-config "$2"; shift 2 ;;
        -D) JH_DEFINES+=("--define=$2"); shift 2 ;;
        -o|--output) jh_launcher_option output "$2"; shift 2 ;;
        -j|--jobs) jh_launcher_option jobs "$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

jh_launch_library_build "${REPO_ROOT}" "${TARGET}" pico-sdk \
    "Target '${TARGET}' is not built with the Pico SDK; use scripts/build_link_library.sh" \
    board assembly variant sdk-dir toolchain picotool-dir picotool-build-dir \
    example project-config output jobs freertos all-features library-only clean
