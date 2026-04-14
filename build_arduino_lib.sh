#!/usr/bin/env bash
# =============================================================================
# build_arduino_lib.sh
#
# Build JaszczurHAL as a linkable static library (.a) for Arduino RP2040.
#
# Prerequisites:
#   - Arduino RP2040 core installed (arduino-cli core install rp2040:rp2040)
#   - CMake >= 3.16
#
# Usage:
#   ./build_arduino_lib.sh [options]
#
# Options:
#   -r, --root PATH      Arduino rp2040 package root
#                         (default: ~/.arduino15/packages/rp2040)
#   -b, --board VARIANT   Board variant (default: rpipico)
#   -c, --chip CHIP       Target chip (default: rp2040)
#   -p, --project-config DIR  Path to dir with hal_project_config.h
#   -D KEY=VALUE          Extra compile definitions (repeatable)
#   -o, --output DIR      Output directory (default: ./build_arduino)
#   --clean               Remove build directory before building
#   -j, --jobs N          Parallel jobs (default: nproc)
#   -h, --help            Show this help
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
die()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# ── Defaults ─────────────────────────────────────────────────────────────────
ARDUINO_ROOT="${HOME}/.arduino15/packages/rp2040"
BOARD_VARIANT="rpipico"
CHIP="rp2040"
PROJECT_CONFIG_DIR=""
EXTRA_DEFS=()
OUTPUT_DIR="${SCRIPT_DIR}/build_arduino"
CLEAN=0
JOBS="$(nproc 2>/dev/null || echo 4)"

# ── Parse arguments ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        -r|--root)         ARDUINO_ROOT="$2";        shift 2 ;;
        -b|--board)        BOARD_VARIANT="$2";        shift 2 ;;
        -c|--chip)         CHIP="$2";                 shift 2 ;;
        -p|--project-config) PROJECT_CONFIG_DIR="$2"; shift 2 ;;
        -D)                EXTRA_DEFS+=("$2");        shift 2 ;;
        -o|--output)       OUTPUT_DIR="$2";           shift 2 ;;
        --clean)           CLEAN=1;                   shift   ;;
        -j|--jobs)         JOBS="$2";                 shift 2 ;;
        -h|--help)
            head -27 "$0" | tail -24
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

# ── Validate Arduino installation ────────────────────────────────────────────
[[ -d "${ARDUINO_ROOT}" ]] || die "Arduino root not found: ${ARDUINO_ROOT}\n  Install RP2040 core: arduino-cli core install rp2040:rp2040"

# Auto-detect core version
CORE_DIR=$(find "${ARDUINO_ROOT}/hardware/rp2040" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | sort -V | tail -1)
[[ -n "${CORE_DIR}" ]] || die "No Arduino RP2040 core version found in ${ARDUINO_ROOT}/hardware/rp2040/"
info "Arduino core: ${CORE_DIR}"

# Auto-detect toolchain version
TC_DIR=$(find "${ARDUINO_ROOT}/tools/pqt-gcc" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | sort -V | tail -1)
[[ -n "${TC_DIR}" ]] || die "No pqt-gcc toolchain found in ${ARDUINO_ROOT}/tools/pqt-gcc/"
info "Toolchain:    ${TC_DIR}"

# Verify compiler exists
COMPILER="${TC_DIR}/bin/arm-none-eabi-g++"
[[ -x "${COMPILER}" ]] || die "Compiler not found: ${COMPILER}"

# Verify variant exists
VARIANT_DIR="${CORE_DIR}/variants/${BOARD_VARIANT}"
[[ -d "${VARIANT_DIR}" ]] || die "Board variant not found: ${VARIANT_DIR}"
info "Board:        ${BOARD_VARIANT}"

# ── Build ────────────────────────────────────────────────────────────────────
TOOLCHAIN_FILE="${SCRIPT_DIR}/arduino_lib/toolchain_rp2040.cmake"
CMAKE_SOURCE="${SCRIPT_DIR}/arduino_lib"

if [[ ${CLEAN} -eq 1 ]] && [[ -d "${OUTPUT_DIR}" ]]; then
    info "Cleaning ${OUTPUT_DIR}"
    rm -rf "${OUTPUT_DIR}"
fi

mkdir -p "${OUTPUT_DIR}"

CMAKE_EXTRA_ARGS=()

if [[ -n "${PROJECT_CONFIG_DIR}" ]]; then
    CMAKE_EXTRA_ARGS+=("-DHAL_PROJECT_CONFIG_DIR=${PROJECT_CONFIG_DIR}")
fi

# Pass extra -D defines as a semicolon-separated list
if [[ ${#EXTRA_DEFS[@]} -gt 0 ]]; then
    joined=$(IFS=';'; echo "${EXTRA_DEFS[*]}")
    CMAKE_EXTRA_ARGS+=("-DEXTRA_HAL_DEFINES=${joined}")
fi

info "Configuring CMake..."
cmake -S "${CMAKE_SOURCE}" -B "${OUTPUT_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DARDUINO_ROOT="${ARDUINO_ROOT}" \
    -DARDUINO_CHIP="${CHIP}" \
    -DARDUINO_VARIANT="${BOARD_VARIANT}" \
    "${CMAKE_EXTRA_ARGS[@]+"${CMAKE_EXTRA_ARGS[@]}"}"

info "Building with ${JOBS} parallel jobs..."
cmake --build "${OUTPUT_DIR}" -j "${JOBS}"

# ── Result ───────────────────────────────────────────────────────────────────
LIB_FILE=$(find "${OUTPUT_DIR}" -name "libJaszczurHAL.a" -print -quit 2>/dev/null)
if [[ -n "${LIB_FILE}" ]]; then
    SIZE=$(stat --printf="%s" "${LIB_FILE}" 2>/dev/null || stat -f "%z" "${LIB_FILE}" 2>/dev/null || echo "?")
    ok "Library built: ${LIB_FILE}  (${SIZE} bytes)"
    echo ""
    info "Link with:  -L${OUTPUT_DIR} -lJaszczurHAL"
    info "Headers in: ${SCRIPT_DIR}/src/"
else
    die "Library not found after build"
fi
