#!/usr/bin/env bash
# Build JaszczurHAL and optional portable firmware with the official Pico SDK.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=lib/build_artifacts.sh
source "${SCRIPT_DIR}/lib/build_artifacts.sh"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()   { echo -e "${GREEN}[OK]${NC} $*"; }
die()  { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

usage() {
    cat <<'USAGE'
Build JaszczurHAL and optional portable firmware with the official Pico SDK.

Usage:
  scripts/build_rp_native_lib.sh [options]

Options:
  --platform NAME          Compatibility alias for --target
  --target NAME            rp2040, rp2350-arm, or rp2350-riscv
                           (default: rp2040)
  --board NAME             JaszczurHAL board profile (target default if omitted)
  --sdk-dir PATH           Pico SDK checkout
  --toolchain PATH         Toolchain root containing bin/
  --picotool-dir PATH      picotool source checkout directory
  --picotool-build-dir PATH
                           picotool build directory below .build/
  --example NAME           Build examples/NAME through the native app entry
  --example-source FILE    Select one source from the example (repeatable)
  --freertos               Enable the pinned native FreeRTOS SMP kernel
  --library-only           Build only the linkable libJaszczurHAL.a archive
  -p, --project-config DIR Directory containing hal_project_config.h
  -D KEY=VALUE             Extra HAL compile definition (repeatable)
  -o, --output DIR         Build directory below .build/
                           (default: .build/static/<target>/<board>)
  --clean                  Remove the selected build directory first
  -j, --jobs N             Parallel jobs (default: nproc)
  -h, --help               Show this help
USAGE
}

PLATFORM="rp2040"
TARGET=""
BOARD=""
SDK_DIR="${JH_PICO_SDK_DIR:-${REPO_ROOT}/third_party/pico-sdk}"
TOOLCHAIN_DIR=""
PICOTOOL_DIR="${REPO_ROOT}/third_party/picotool"
PICOTOOL_BUILD_DIR="${JH_PICOTOOL_BUILD_DIR:-${REPO_ROOT}/.build/tools/picotool}"
EXAMPLE=""
EXAMPLE_SOURCES=()
PROJECT_CONFIG_DIR=""
EXTRA_DEFS=()
FREERTOS=0
LIBRARY_ONLY=0
OUTPUT_DIR=""
CLEAN=0
JOBS="$(nproc 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --platform) PLATFORM="$2"; shift 2 ;;
        --target) TARGET="$2"; shift 2 ;;
        --board) BOARD="$2"; shift 2 ;;
        --sdk-dir) SDK_DIR="$2"; shift 2 ;;
        --toolchain) TOOLCHAIN_DIR="$2"; shift 2 ;;
        --picotool-dir) PICOTOOL_DIR="$2"; shift 2 ;;
        --picotool-build-dir) PICOTOOL_BUILD_DIR="$2"; shift 2 ;;
        --example) EXAMPLE="$2"; shift 2 ;;
        --example-source) EXAMPLE_SOURCES+=("$2"); shift 2 ;;
        --freertos) FREERTOS=1; shift ;;
        --library-only) LIBRARY_ONLY=1; shift ;;
        -p|--project-config) PROJECT_CONFIG_DIR="$2"; shift 2 ;;
        -D) EXTRA_DEFS+=("$2"); shift 2 ;;
        -o|--output) OUTPUT_DIR="$2"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        -j|--jobs) JOBS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1" ;;
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

if [[ -z "${TARGET}" ]]; then
    case "${PLATFORM}" in
        rp2040) TARGET="rp2040" ;;
        rp2350-arm-s) TARGET="rp2350-arm" ;;
        rp2350-riscv) TARGET="rp2350-riscv" ;;
        *) die "Unsupported platform '${PLATFORM}'" ;;
    esac
fi

case "${TARGET}" in
    rp2040)
        : "${BOARD:=pico}"
        PLATFORM="rp2040"
        ;;
    rp2350-arm)
        : "${BOARD:=pico2}"
        PLATFORM="rp2350-arm-s"
        ;;
    rp2350-riscv)
        : "${BOARD:=pico2}"
        PLATFORM="rp2350-riscv"
        if [[ -z "${TOOLCHAIN_DIR}" ]]; then
            TOOLCHAIN_DIR="${JH_RISCV_TOOLCHAIN_DIR:-${REPO_ROOT}/third_party/riscv-toolchain}"
        fi
        ;;
    *)
        die "Unsupported target '${TARGET}'"
        ;;
esac

[[ "${SDK_DIR}" == /* ]] || SDK_DIR="${REPO_ROOT}/${SDK_DIR}"
[[ "${PICOTOOL_DIR}" == /* ]] || PICOTOOL_DIR="${REPO_ROOT}/${PICOTOOL_DIR}"
[[ "${PICOTOOL_BUILD_DIR}" == /* ]] ||
    PICOTOOL_BUILD_DIR="${REPO_ROOT}/${PICOTOOL_BUILD_DIR}"
if [[ -n "${TOOLCHAIN_DIR}" && "${TOOLCHAIN_DIR}" != /* ]]; then
    TOOLCHAIN_DIR="${REPO_ROOT}/${TOOLCHAIN_DIR}"
fi
if [[ -n "${PROJECT_CONFIG_DIR}" && "${PROJECT_CONFIG_DIR}" != /* ]]; then
    PROJECT_CONFIG_DIR="${REPO_ROOT}/${PROJECT_CONFIG_DIR}"
fi

if ! OUTPUT_DIR="$(jh_resolve_build_output \
    "${REPO_ROOT}" "${OUTPUT_DIR}" "static/${TARGET}/${BOARD}")"; then
    die "Build output must be inside ${REPO_ROOT}/.build"
fi
if ! PICOTOOL_BUILD_DIR="$(jh_resolve_build_output \
    "${REPO_ROOT}" "${PICOTOOL_BUILD_DIR}" "tools/picotool")"; then
    die "picotool build output must be inside ${REPO_ROOT}/.build"
fi

APP_DIR=""
if [[ -n "${EXAMPLE}" ]]; then
    if [[ "${EXAMPLE}" == */* || "${EXAMPLE}" == "." || "${EXAMPLE}" == ".." ]]; then
        die "Example must be a directory name under examples/: ${EXAMPLE}"
    fi
    APP_DIR="${REPO_ROOT}/examples/${EXAMPLE}"
    [[ -d "${APP_DIR}" ]] || die "Example directory not found: ${APP_DIR}"
    if [[ -z "${PROJECT_CONFIG_DIR}" ]]; then
        PROJECT_CONFIG_DIR="${APP_DIR}"
    elif [[ "${PROJECT_CONFIG_DIR}" != "${APP_DIR}" ]]; then
        die "--example uses its own hal_project_config.h; remove -p or select ${APP_DIR}"
    fi
fi
if [[ ${#EXAMPLE_SOURCES[@]} -gt 0 ]]; then
    [[ -n "${APP_DIR}" ]] || die "--example-source requires --example"
    for source in "${EXAMPLE_SOURCES[@]}"; do
        if [[ -z "${source}" || "${source}" == */* || "${source}" == "." ||
              "${source}" == ".." || ! -f "${APP_DIR}/${source}" ]]; then
            die "Example source must be a file directly under ${APP_DIR}: ${source}"
        fi
    done
fi
if [[ ${LIBRARY_ONLY} -eq 1 && -n "${APP_DIR}" ]]; then
    die "--library-only cannot be combined with --example"
fi

case "${OUTPUT_DIR}" in
    /|"${REPO_ROOT}"|"$(dirname "${REPO_ROOT}")")
        die "Refusing unsafe build output directory: ${OUTPUT_DIR}"
        ;;
esac

"${REPO_ROOT}/scripts/ensure_pico_sdk.sh" \
    --enable --repo-root "${REPO_ROOT}" --sdk-dir "${SDK_DIR}"
"${REPO_ROOT}/scripts/ensure_picotool.sh" \
    --enable --repo-root "${REPO_ROOT}" \
    --picotool-dir "${PICOTOOL_DIR}" \
    --build-dir "${PICOTOOL_BUILD_DIR}" --sdk-dir "${SDK_DIR}"

if [[ ${FREERTOS} -eq 1 ]]; then
    "${REPO_ROOT}/scripts/ensure_freertos_kernel.sh" \
        --enable --repo-root "${REPO_ROOT}"
    EXTRA_DEFS+=("HAL_ENABLE_FREERTOS=1")
fi

if [[ "${PLATFORM}" == "rp2350-riscv" ]]; then
    "${REPO_ROOT}/scripts/ensure_riscv_toolchain.sh" \
        --enable --repo-root "${REPO_ROOT}" --dir "${TOOLCHAIN_DIR}"
fi

if [[ ${CLEAN} -eq 1 && -d "${OUTPUT_DIR}" ]]; then
    info "Cleaning ${OUTPUT_DIR}"
    rm -rf -- "${OUTPUT_DIR}"
fi

CMAKE_ARGS=(
    "-DPICO_SDK_PATH=${SDK_DIR}"
    "-DJH_TARGET=${TARGET}"
    "-DJH_BOARD=${BOARD}"
    "-DCMAKE_BUILD_TYPE=Release"
)
if [[ ${LIBRARY_ONLY} -eq 1 ]]; then
    CMAKE_ARGS+=(
        "-DJH_RP_NATIVE_BUILD_ARTIFACT_PROBE=OFF"
        "-DJH_RP_NATIVE_BUILD_CORE1_PROBE=OFF"
    )
fi
PICOTOOL_EXECUTABLE="${PICOTOOL_BUILD_DIR}/picotool"
[[ -x "${PICOTOOL_EXECUTABLE}" ]] ||
    die "picotool executable not found: ${PICOTOOL_EXECUTABLE}"
CMAKE_ARGS+=("-DJH_PICOTOOL_EXECUTABLE=${PICOTOOL_EXECUTABLE}")
if [[ -n "${TOOLCHAIN_DIR}" ]]; then
    CMAKE_ARGS+=("-DPICO_TOOLCHAIN_PATH=${TOOLCHAIN_DIR}")
fi
if [[ -n "${PROJECT_CONFIG_DIR}" ]]; then
    CMAKE_ARGS+=("-DHAL_PROJECT_CONFIG_DIR=${PROJECT_CONFIG_DIR}")
fi
if [[ -n "${APP_DIR}" ]]; then
    CMAKE_ARGS+=("-DJH_RP_NATIVE_APP_DIR=${APP_DIR}")
fi
if [[ ${#EXAMPLE_SOURCES[@]} -gt 0 ]]; then
    joined_sources="$(IFS=';'; echo "${EXAMPLE_SOURCES[*]}")"
    CMAKE_ARGS+=("-DJH_RP_NATIVE_APP_SOURCES=${joined_sources}")
fi
if [[ ${#EXTRA_DEFS[@]} -gt 0 ]]; then
    joined="$(IFS=';'; echo "${EXTRA_DEFS[*]}")"
    CMAKE_ARGS+=("-DEXTRA_HAL_DEFINES=${joined}")
fi

info "Configuring native Pico SDK build (${TARGET}, board ${BOARD})..."
cmake -S "${REPO_ROOT}/rp_native_lib" -B "${OUTPUT_DIR}" "${CMAKE_ARGS[@]}"

info "Building with ${JOBS} parallel jobs..."
if [[ ${LIBRARY_ONLY} -eq 1 ]]; then
    cmake --build "${OUTPUT_DIR}" --target JaszczurHAL --parallel "${JOBS}"
else
    cmake --build "${OUTPUT_DIR}" --parallel "${JOBS}"
fi

LIB_FILE="${OUTPUT_DIR}/libJaszczurHAL.a"
GENERATED_SOURCE="${OUTPUT_DIR}/generated/boards/${TARGET}/${BOARD}"
GENERATED_INCLUDE="${OUTPUT_DIR}/include/generated"
mkdir -p "${GENERATED_INCLUDE}"
for generated_header in \
    jh_board_config.h jh_link_contract.h; do
    [[ -f "${GENERATED_SOURCE}/${generated_header}" ]] ||
        die "Generated board header not found: ${GENERATED_SOURCE}/${generated_header}"
    cp -f "${GENERATED_SOURCE}/${generated_header}" "${GENERATED_INCLUDE}/"
done
[[ -f "${LIB_FILE}" ]] || die "Static library not found: ${LIB_FILE}"
ok "Native Pico SDK library built: ${LIB_FILE}"
if [[ ${LIBRARY_ONLY} -eq 0 ]]; then
    PROBE_BASE="${OUTPUT_DIR}/jh_rp_native_artifact_probe"
    CORE1_PROBE_BASE="${OUTPUT_DIR}/jh_rp_native_core1_probe"
    for artifact_base in "${PROBE_BASE}" "${CORE1_PROBE_BASE}"; do
        for extension in elf bin uf2; do
            [[ -f "${artifact_base}.${extension}" ]] ||
                die "Native ${extension^^} artifact not found: ${artifact_base}.${extension}"
        done
    done
    if [[ -n "${APP_DIR}" ]]; then
        FIRMWARE_BASE="${OUTPUT_DIR}/jh_rp_native_firmware"
        for extension in elf bin uf2; do
            [[ -f "${FIRMWARE_BASE}.${extension}" ]] ||
                die "Native example ${extension^^} not found: ${FIRMWARE_BASE}.${extension}"
        done
    fi

    if [[ "${PLATFORM}" == "rp2350-riscv" ]]; then
        NM_TOOL="${TOOLCHAIN_DIR}/bin/riscv32-unknown-elf-nm"
    else
        NM_TOOL="$(command -v arm-none-eabi-nm || true)"
    fi
    [[ -x "${NM_TOOL}" ]] || die "Target nm tool not found: ${NM_TOOL:-unknown}"

    verify_symbol() {
        local elf_file="$1"
        local symbol="$2"
        "${NM_TOOL}" --defined-only "${elf_file}" |
            awk -v expected="${symbol}" '$NF == expected { found = 1 } END { exit !found }' ||
            die "Required symbol '${symbol}' not found in ${elf_file}"
    }

    ENTRY_SYMBOLS=(main app_start app_task0 app_task1)
    if [[ ${FREERTOS} -eq 1 ]]; then
        ENTRY_SYMBOLS+=(vTaskStartScheduler xTaskCreateAffinitySet)
    else
        ENTRY_SYMBOLS+=(multicore_launch_core1 multicore_lockout_victim_init)
    fi
    for symbol in "${ENTRY_SYMBOLS[@]}"; do
        verify_symbol "${CORE1_PROBE_BASE}.elf" "${symbol}"
    done
    if [[ -n "${APP_DIR}" ]]; then
        for symbol in main app_start app_task0; do
            verify_symbol "${FIRMWARE_BASE}.elf" "${symbol}"
        done
    fi

    ok "Artifacts: ${PROBE_BASE}.{elf,bin,uf2}"
    ok "Core-1 entry probe: ${CORE1_PROBE_BASE}.{elf,bin,uf2}"
    if [[ -n "${APP_DIR}" ]]; then
        ok "Native example ${EXAMPLE}: ${FIRMWARE_BASE}.{elf,bin,uf2}"
    fi
fi
