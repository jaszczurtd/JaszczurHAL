#!/usr/bin/env bash
# Build JaszczurHAL as a linkable static library with the pinned ESP-IDF.
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
BOARD=""
ASSEMBLY=""
VARIANT=""
ALL_FEATURES=0
PROJECT_CONFIG_DIR=""
EXTRA_DEFS=()
IDF_DIR=""
OUTPUT_DIR=""
CLEAN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        --board) BOARD="$2"; shift 2 ;;
        --assembly) ASSEMBLY="$2"; shift 2 ;;
        --variant) VARIANT="$2"; shift 2 ;;
        --all-features) ALL_FEATURES=1; shift ;;
        --library-only|--freertos) shift ;;
        -p|--project-config) PROJECT_CONFIG_DIR="$2"; shift 2 ;;
        -D) EXTRA_DEFS+=("$2"); shift 2 ;;
        --idf-dir) IDF_DIR="$2"; shift 2 ;;
        -o|--output) OUTPUT_DIR="$2"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        -j|--jobs) shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1" ;;
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
[[ "${TARGET_PROVIDER}" == "esp-idf" ]] ||
    die "Target '${TARGET}' is not built with ESP-IDF; use scripts/build_link_library.sh"

if [[ -n "${PROJECT_CONFIG_DIR}" && "${PROJECT_CONFIG_DIR}" != /* ]]; then
    PROJECT_CONFIG_DIR="${REPO_ROOT}/${PROJECT_CONFIG_DIR}"
fi
DEVICE_TREE=0
if jh_device_tree_library "${PROJECT_CONFIG_DIR}" "${BOARD}" "${ASSEMBLY}" "${VARIANT}"; then
    DEVICE_TREE=1
    OUTPUT_KEY="$(jh_device_tree_build_key "${PROJECT_CONFIG_DIR}" "${ASSEMBLY}" \
        "variant=${VARIANT}" "all=${ALL_FEATURES}" "${EXTRA_DEFS[@]}")"
elif [[ $? -ne 1 ]]; then
    exit 2
else
    : "${BOARD:=${TARGET_DEFAULT_BOARD}}"
    OUTPUT_KEY="${BOARD}"
fi
if ! OUTPUT_DIR="$(jh_resolve_build_output \
    "${REPO_ROOT}" "${OUTPUT_DIR}" "static/${TARGET}/${OUTPUT_KEY}")"; then
    die "Build output must be inside ${REPO_ROOT}/.build"
fi

RUNNER_ARGS=(
    build
    --project "${REPO_ROOT}/link_libraries/esp32_lib"
    --target "${TARGET}"
    --output "${OUTPUT_DIR}"
)
if [[ ${DEVICE_TREE} -eq 1 ]]; then
    RUNNER_ARGS+=(--assembly "${ASSEMBLY}")
    [[ -z "${VARIANT}" ]] || RUNNER_ARGS+=(--variant "${VARIANT}")
else
    RUNNER_ARGS+=(--board "${BOARD}")
fi
if [[ ${CLEAN} -eq 1 ]]; then
    RUNNER_ARGS+=(--clean)
fi
if [[ -n "${PROJECT_CONFIG_DIR}" ]]; then
    RUNNER_ARGS+=(--project-config "${PROJECT_CONFIG_DIR}")
fi
if [[ -n "${IDF_DIR}" ]]; then
    RUNNER_ARGS+=(--idf-dir "${IDF_DIR}")
fi
if [[ ${ALL_FEATURES} -eq 1 ]]; then
    RUNNER_ARGS+=(--all-features)
fi
for definition in "${EXTRA_DEFS[@]}"; do
    RUNNER_ARGS+=(--define "${definition#-D}")
done

info "Building ESP-IDF library (${TARGET}, $(jh_build_selection "${DEVICE_TREE}" "${BOARD}" "${ASSEMBLY}"))..."
python3 "${REPO_ROOT}/scripts/build_esp_idf.py" "${RUNNER_ARGS[@]}"

COMPONENT_ARCHIVE="${OUTPUT_DIR}/esp-idf/jaszczurhal/libjaszczurhal.a"
[[ -f "${COMPONENT_ARCHIVE}" ]] ||
    die "ESP-IDF component archive not found: ${COMPONENT_ARCHIVE}"
LIB_FILE="${OUTPUT_DIR}/libJaszczurHAL.a"
cp -f "${COMPONENT_ARCHIVE}" "${LIB_FILE}"

GENERATED_SOURCE="${OUTPUT_DIR}/generated/jaszczurhal"
GENERATED_INCLUDE="${OUTPUT_DIR}/include/generated"
mkdir -p "${GENERATED_INCLUDE}"
GENERATED_HEADERS=(jh_board_config.h jh_link_contract.h)
if [[ ${DEVICE_TREE} -eq 1 ]]; then
    GENERATED_HEADERS+=(jh_hardware.h)
else
    rm -f "${GENERATED_INCLUDE}/jh_hardware.h"
fi
for generated_header in "${GENERATED_HEADERS[@]}"; do
    [[ -f "${GENERATED_SOURCE}/${generated_header}" ]] ||
        die "Generated board header not found: ${GENERATED_SOURCE}/${generated_header}"
    cp -f "${GENERATED_SOURCE}/${generated_header}" "${GENERATED_INCLUDE}/"
    chmod 644 "${GENERATED_INCLUDE}/${generated_header}"
done
ok "ESP-IDF library built: ${LIB_FILE}"
ok "Generated headers: ${GENERATED_INCLUDE}"
