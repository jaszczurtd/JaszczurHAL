#!/usr/bin/env bash
# Build JaszczurHAL as a linkable static library for the STM32 family.
# The build itself runs in `jh library build`; this launcher keeps the options,
# the default target and the provider check of the STM32 runner.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=lib/build_artifacts.sh
source "${SCRIPT_DIR}/lib/build_artifacts.sh"

RED='\033[0;31m'
NC='\033[0m'

die()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

usage() {
    cat <<'USAGE'
Build JaszczurHAL as a linkable static library for the STM32 family.

Usage:
  scripts/build_stm32_lib.sh [options]

Options:
  --target NAME            stm32g474 (default) or another STM32 registry target
  --board NAME             JaszczurHAL board profile (target default if omitted)
  --assembly KEY           Assembly of the device_tree.json in -p DIR
  --variant ID             Variant declared by JH_PROJECT_VARIANTS (device tree)
  --freertos               Define HAL_ENABLE_FREERTOS and ensure FreeRTOS-Kernel
  --all-features           Enable every feature supported by the target
  --library-only           Accepted for parity; this runner only builds the archive
  --freertos-kernel PATH   FreeRTOS-Kernel checkout
  -p, --project-config DIR Directory containing hal_project_config.h
  -D KEY=VALUE             Extra HAL compile definition (repeatable)
  -t, --toolchain FILE     CMake toolchain file
                           (default: link_libraries/stm32_lib/toolchain_stm32g474.cmake)
  -o, --output DIR         Build directory below .build/
                           (default: .build/static/<target>/<board>, or
                           .build/static/<target>/<assembly>/<key> with a
                           device tree)
  --clean                  Remove the selected build directory first
  -j, --jobs N             Parallel jobs (default: nproc)
  -h, --help               Show this help
USAGE
}

TARGET="stm32g474"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        -p|--project-config) jh_launcher_option project-config "$2"; shift 2 ;;
        -D) JH_DEFINES+=("--define=$2"); shift 2 ;;
        --board|--assembly|--variant|--freertos-kernel)
            jh_launcher_option "${1#--}" "$2"; shift 2 ;;
        --library-only|--freertos|--all-features|--clean)
            jh_launcher_flag "${1#--}"; shift ;;
        -o|--output) jh_launcher_option output "$2"; shift 2 ;;
        -t|--toolchain) jh_launcher_option toolchain "$2"; shift 2 ;;
        -j|--jobs) jh_launcher_option jobs "$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

jh_launch_library_build "${REPO_ROOT}" "${TARGET}" jh-stm32-baremetal \
    "Target '${TARGET}' is not an STM32 bare-metal target; use scripts/build_link_library.sh" \
    board assembly variant freertos-kernel project-config output toolchain jobs \
    library-only freertos all-features clean
