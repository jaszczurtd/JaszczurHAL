#!/usr/bin/env bash
# =============================================================================
# build_stm32_lib.sh
#
# Build JaszczurHAL as a linkable static library (.a) for STM32G474.
#
# Prerequisites:
#   - GNU Arm Embedded toolchain (arm-none-eabi-gcc / g++)
#   - CMake >= 3.16
#
# Usage:
#   ./scripts/build_stm32_lib.sh [options]
#
# Options:
#   -p, --project-config DIR   Path to dir with hal_project_config.h
#   -D KEY=VALUE               Extra compile definitions (repeatable)
#   --board NAME               Board profile (default: nucleo-g474re)
#   --freertos                 Define HAL_ENABLE_FREERTOS and ensure FreeRTOS-Kernel
#   --freertos-kernel PATH     Path to FreeRTOS-Kernel checkout
#   -o, --output DIR           Output directory below .build/
#                              (default: .build/static/stm32g474/<board>)
#   -t, --toolchain FILE       CMake toolchain file
#                              (default: ./stm32_lib/toolchain_stm32g474.cmake)
#   --clean                    Remove build directory before building
#   -j, --jobs N               Parallel jobs (default: nproc)
#   -h, --help                 Show this help
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=lib/build_artifacts.sh
source "${SCRIPT_DIR}/lib/build_artifacts.sh"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
die()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

PROJECT_CONFIG_DIR=""
BOARD="nucleo-g474re"
EXTRA_DEFS=()
FREERTOS=0
FREERTOS_KERNEL_DIR=""
OUTPUT_DIR=""
TOOLCHAIN_FILE="${REPO_ROOT}/stm32_lib/toolchain_stm32g474.cmake"
CLEAN=0
JOBS="$(nproc 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--project-config) PROJECT_CONFIG_DIR="$2"; shift 2 ;;
        -D) EXTRA_DEFS+=("$2"); shift 2 ;;
        --board) BOARD="$2"; shift 2 ;;
        --freertos) FREERTOS=1; shift ;;
        --freertos-kernel) FREERTOS_KERNEL_DIR="$2"; shift 2 ;;
        -o|--output) OUTPUT_DIR="$2"; shift 2 ;;
        -t|--toolchain) TOOLCHAIN_FILE="$2"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        -j|--jobs) JOBS="$2"; shift 2 ;;
        -h|--help)
            sed -n '3,25p' "$0"
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

for definition in "${EXTRA_DEFS[@]}"; do
    normalized="${definition#-D}"
    if [[ "${normalized}" == *'$<'* ]]; then
        die "[JH-CFG-VALUE] ${definition} is unsupported; generator expressions are not accepted"
    fi
    if [[ "${normalized}" == *HAL_ENABLE_* ]] &&
       [[ ! "${normalized}" =~ ^HAL_ENABLE_[A-Z0-9_]+(=1)?$ ]]; then
        die "[JH-CFG-VALUE] ${definition} is unsupported; use a standalone bare symbol or an explicit value of 1"
    fi
done

if ! OUTPUT_DIR="$(jh_resolve_build_output \
    "${REPO_ROOT}" "${OUTPUT_DIR}" "static/stm32g474/${BOARD}")"; then
    die "Build output must be inside ${REPO_ROOT}/.build"
fi

[[ -f "${TOOLCHAIN_FILE}" ]] || die "Toolchain file not found: ${TOOLCHAIN_FILE}"

if ! command -v arm-none-eabi-g++ >/dev/null 2>&1; then
    warn "arm-none-eabi-g++ not found in PATH."
    warn "If you use a custom prefix/path, pass it via CMake cache variables (e.g. -D ARM_GCC_PREFIX=...)."
fi

if [[ ${CLEAN} -eq 1 ]] && [[ -d "${OUTPUT_DIR}" ]]; then
    info "Cleaning ${OUTPUT_DIR}"
    rm -rf "${OUTPUT_DIR}"
fi

mkdir -p "${OUTPUT_DIR}"

CMAKE_EXTRA_ARGS=()

if [[ -n "${PROJECT_CONFIG_DIR}" ]]; then
    CMAKE_EXTRA_ARGS+=("-DHAL_PROJECT_CONFIG_DIR=${PROJECT_CONFIG_DIR}")
fi

has_hal_freertos=0
for def in "${EXTRA_DEFS[@]}"; do
    if [[ "${def}" == "HAL_ENABLE_FREERTOS" || "${def}" == HAL_ENABLE_FREERTOS=* ]]; then
        has_hal_freertos=1
        break
    fi
done

if [[ ${FREERTOS} -eq 1 && ${has_hal_freertos} -eq 0 ]]; then
    EXTRA_DEFS+=("HAL_ENABLE_FREERTOS")
    has_hal_freertos=1
fi

if [[ -n "${FREERTOS_KERNEL_DIR}" ]]; then
    CMAKE_EXTRA_ARGS+=("-DJH_FREERTOS_KERNEL_DIR=${FREERTOS_KERNEL_DIR}")
fi

if [[ ${has_hal_freertos} -eq 1 ]]; then
    ENSURE_ARGS=(--enable --repo-root "${REPO_ROOT}")
    if [[ -n "${FREERTOS_KERNEL_DIR}" ]]; then
        ENSURE_ARGS+=(--kernel-dir "${FREERTOS_KERNEL_DIR}")
    fi
    "${REPO_ROOT}/scripts/ensure_freertos_kernel.sh" "${ENSURE_ARGS[@]}"
fi

if [[ ${#EXTRA_DEFS[@]} -gt 0 ]]; then
    joined=$(IFS=';'; echo "${EXTRA_DEFS[*]}")
    CMAKE_EXTRA_ARGS+=("-DEXTRA_HAL_DEFINES=${joined}")
fi

info "Configuring CMake..."
cmake -S "${REPO_ROOT}/stm32_lib" -B "${OUTPUT_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DJH_TARGET=stm32g474 \
    -DJH_BOARD="${BOARD}" \
    "${CMAKE_EXTRA_ARGS[@]}"

info "Building with ${JOBS} parallel jobs..."
cmake --build "${OUTPUT_DIR}" -j "${JOBS}"

LIB_FILE=$(find "${OUTPUT_DIR}" -name "libJaszczurHAL.a" -print -quit 2>/dev/null)
if [[ -n "${LIB_FILE}" ]]; then
    GENERATED_SOURCE="${OUTPUT_DIR}/generated/boards/stm32g474/${BOARD}"
    GENERATED_INCLUDE="${OUTPUT_DIR}/include/generated"
    mkdir -p "${GENERATED_INCLUDE}"
    for generated_header in \
        jh_board_config.h jh_link_contract.h; do
        [[ -f "${GENERATED_SOURCE}/${generated_header}" ]] ||
            die "Generated board header not found: ${GENERATED_SOURCE}/${generated_header}"
        cp -f "${GENERATED_SOURCE}/${generated_header}" "${GENERATED_INCLUDE}/"
    done
    SIZE=$(stat --printf="%s" "${LIB_FILE}" 2>/dev/null || stat -f "%z" "${LIB_FILE}" 2>/dev/null || echo "?")
    ok "Library built: ${LIB_FILE}  (${SIZE} bytes)"
    echo ""
    info "Headers in: ${REPO_ROOT}/src/"
else
    die "Library not found after build"
fi
