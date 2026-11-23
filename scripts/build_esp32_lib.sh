#!/usr/bin/env bash
# Build JaszczurHAL as a linkable static library with the pinned ESP-IDF.
# The build itself runs in `jh library build`; this launcher keeps the options,
# the default target and the provider check of the ESP-IDF runner.
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
Build JaszczurHAL as a linkable static library with the pinned ESP-IDF.

Usage:
  scripts/build_esp32_lib.sh [options]

Options:
  --target NAME            esp32s3 (default) or another ESP-IDF registry target
  --board NAME             JaszczurHAL board profile (target default if omitted)
  --assembly KEY           Assembly of the device_tree.json in -p DIR
  --variant ID             Variant declared by JH_PROJECT_VARIANTS (device tree)
  --all-features           Enable every feature supported by the target
  --library-only           Accepted for parity; ESP-IDF always links the probe
  --freertos               Accepted for parity; ESP-IDF always provides FreeRTOS
  -p, --project-config DIR Directory containing hal_project_config.h
  -D KEY=VALUE             Extra HAL compile definition (repeatable)
  --idf-dir PATH           Externally managed ESP-IDF checkout
  -o, --output DIR         Build directory below .build/
                           (default: .build/static/<target>/<board>, or
                           .build/static/<target>/<assembly>/<key> with a
                           device tree)
  --clean                  Remove the selected build directory first
  -j, --jobs N             Accepted for parity; ESP-IDF selects its own jobs
  -h, --help               Show this help

The archive is published as <output>/libJaszczurHAL.a next to the generated
board headers in <output>/include/generated/.
USAGE
}

TARGET="esp32s3"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        -p|--project-config) jh_launcher_option project-config "$2"; shift 2 ;;
        -D) JH_DEFINES+=("--define=$2"); shift 2 ;;
        --board|--assembly|--variant|--idf-dir)
            jh_launcher_option "${1#--}" "$2"; shift 2 ;;
        --all-features|--clean) jh_launcher_flag "${1#--}"; shift ;;
        --library-only|--freertos) shift ;;
        -o|--output) jh_launcher_option output "$2"; shift 2 ;;
        -j|--jobs) shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

jh_launch_library_build "${REPO_ROOT}" "${TARGET}" esp-idf \
    "Target '${TARGET}' is not built with ESP-IDF; use scripts/build_link_library.sh" \
    board assembly variant idf-dir project-config output all-features clean
