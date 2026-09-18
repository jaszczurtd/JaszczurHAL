#!/usr/bin/env bash
# Build JaszczurHAL as a linkable static library for the STM32 family.
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

usage() {
    cat <<'USAGE'
Build JaszczurHAL as a linkable static library for the STM32 family.

Usage:
  scripts/build_stm32_lib.sh [options]

Options:
  --target NAME            stm32g474 (default) or another STM32 registry target
  --board NAME             JaszczurHAL board profile (target default if omitted)
  --freertos               Define HAL_ENABLE_FREERTOS and ensure FreeRTOS-Kernel
  --all-features           Enable every feature supported by the target
  --library-only           Accepted for parity; this runner only builds the archive
  --freertos-kernel PATH   FreeRTOS-Kernel checkout
  -p, --project-config DIR Directory containing hal_project_config.h
  -D KEY=VALUE             Extra HAL compile definition (repeatable)
  -t, --toolchain FILE     CMake toolchain file
                           (default: link_libraries/stm32_lib/toolchain_stm32g474.cmake)
  -o, --output DIR         Build directory below .build/
                           (default: .build/static/<target>/<board>)
  --clean                  Remove the selected build directory first
  -j, --jobs N             Parallel jobs (default: nproc)
  -h, --help               Show this help
USAGE
}

TARGET="stm32g474"
PROJECT_CONFIG_DIR=""
BOARD=""
EXTRA_DEFS=()
FREERTOS=0
ALL_FEATURES=0
FREERTOS_KERNEL_DIR=""
OUTPUT_DIR=""
TOOLCHAIN_FILE="${REPO_ROOT}/link_libraries/stm32_lib/toolchain_stm32g474.cmake"
CLEAN=0
JOBS="$(nproc 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        -p|--project-config) PROJECT_CONFIG_DIR="$2"; shift 2 ;;
        -D) EXTRA_DEFS+=("$2"); shift 2 ;;
        --board) BOARD="$2"; shift 2 ;;
        --library-only) shift ;;
        --freertos) FREERTOS=1; shift ;;
        --all-features) ALL_FEATURES=1; FREERTOS=1; shift ;;
        --freertos-kernel) FREERTOS_KERNEL_DIR="$2"; shift 2 ;;
        -o|--output) OUTPUT_DIR="$2"; shift 2 ;;
        -t|--toolchain) TOOLCHAIN_FILE="$2"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        -j|--jobs) JOBS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

jh_validate_hal_defines "${EXTRA_DEFS[@]}" || exit 1

TARGET_PROVIDER=""
TARGET_DEFAULT_BOARD=""
facts="$(jh_target_facts "${REPO_ROOT}" "${TARGET}")" ||
    die "Unknown target '${TARGET}'"
while IFS='=' read -r key value; do
    case "${key}" in
        provider) TARGET_PROVIDER="${value}" ;;
        defaultBoard) TARGET_DEFAULT_BOARD="${value}" ;;
    esac
done <<< "${facts}"
[[ "${TARGET_PROVIDER}" == "jh-stm32-baremetal" ]] ||
    die "Target '${TARGET}' is not an STM32 bare-metal target; use scripts/build_link_library.sh"
: "${BOARD:=${TARGET_DEFAULT_BOARD}}"

if ! OUTPUT_DIR="$(jh_resolve_build_output \
    "${REPO_ROOT}" "${OUTPUT_DIR}" "static/${TARGET}/${BOARD}")"; then
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

if [[ ${ALL_FEATURES} -eq 1 ]]; then
    CMAKE_EXTRA_ARGS+=("-DJH_ENABLE_ALL_FEATURES=ON")
fi

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
cmake -S "${REPO_ROOT}/link_libraries/stm32_lib" -B "${OUTPUT_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DJH_TARGET="${TARGET}" \
    -DJH_BOARD="${BOARD}" \
    "${CMAKE_EXTRA_ARGS[@]}"

info "Building with ${JOBS} parallel jobs..."
cmake --build "${OUTPUT_DIR}" --target JaszczurHAL -j "${JOBS}"

LIB_FILE=$(find "${OUTPUT_DIR}" -name "libJaszczurHAL.a" -print -quit 2>/dev/null)
if [[ -n "${LIB_FILE}" ]]; then
    GENERATED_SOURCE="${OUTPUT_DIR}/generated/boards/${TARGET}/${BOARD}"
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
