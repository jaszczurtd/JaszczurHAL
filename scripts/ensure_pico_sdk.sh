#!/usr/bin/env bash
# Fetch or verify the pinned upstream Pico SDK dependency for the native
# RP2040 / RP2350 backend. Fetch/verify primitives are shared with
# ensure_freertos_kernel.sh via scripts/lib/pinned_repo.sh.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_FILE="${REPO_ROOT}/pico_sdk_version.conf"

# shellcheck source=lib/pinned_repo.sh
source "${SCRIPT_DIR}/lib/pinned_repo.sh"

usage() {
    cat <<'USAGE'
Fetch or verify the pinned upstream Pico SDK for the native RP2040/RP2350 build.

Usage:
  scripts/ensure_pico_sdk.sh [options]

Options:
  --enable, --native        Ensure the SDK now
  --force                   Ensure even when the native target is not detected
  --verify-only             Verify only; do not fetch or change a checkout
  --no-submodules           Skip the SDK submodules listed in the conf
  --with-submodules "A B"   Override the SDK submodule list to init
  --repo-root PATH          Repository root (default: script parent)
  --sdk-dir PATH            Pico SDK checkout path
  -h, --help                Show this help

The helper is a no-op unless one of these is true:
  - --enable / --native / --force / --verify-only is passed
  - JH_ENABLE_PICO_SDK is set in the environment

JH_PICO_SDK_DIR (or --sdk-dir) overrides the configured default and is verified
without touching third_party/pico-sdk. On success PICO_SDK_PATH points at the
checkout for the native target build.
USAGE
}

ENABLE=0
FORCE=0
VERIFY_ONLY=0
SKIP_SUBMODULES=0
SDK_DIR_ARG=""
SUBMODULES_ARG=""
SUBMODULES_OVERRIDDEN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --enable|--native) ENABLE=1; shift ;;
        --force) FORCE=1; ENABLE=1; shift ;;
        --verify-only) VERIFY_ONLY=1; ENABLE=1; shift ;;
        --no-submodules) SKIP_SUBMODULES=1; shift ;;
        --with-submodules) SUBMODULES_ARG="$2"; SUBMODULES_OVERRIDDEN=1; shift 2 ;;
        --repo-root)
            REPO_ROOT="$(cd "$2" && pwd)"
            CONFIG_FILE="${REPO_ROOT}/pico_sdk_version.conf"
            shift 2
            ;;
        --sdk-dir) SDK_DIR_ARG="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

[[ -f "${CONFIG_FILE}" ]] || die "Pico SDK config not found: ${CONFIG_FILE}"
# shellcheck source=../pico_sdk_version.conf
source "${CONFIG_FILE}"

if [[ ${ENABLE} -eq 0 && -n "${JH_ENABLE_PICO_SDK:-}" ]]; then
    ENABLE=1
fi
if [[ ${ENABLE} -eq 0 ]]; then
    info "Native Pico SDK target not requested; skipping Pico SDK ensure."
    exit 0
fi

[[ -n "${PICO_SDK_REPO:-}" ]] || die "PICO_SDK_REPO missing in ${CONFIG_FILE}"
[[ -n "${PICO_SDK_REF:-}" ]] || die "PICO_SDK_REF missing in ${CONFIG_FILE}"
[[ -n "${PICO_SDK_DIR:-}" ]] || die "PICO_SDK_DIR missing in ${CONFIG_FILE}"

if [[ ${SUBMODULES_OVERRIDDEN} -eq 0 ]]; then
    SUBMODULES_ARG="${PICO_SDK_SUBMODULES:-}"
fi
[[ ${SKIP_SUBMODULES} -eq 1 ]] && SUBMODULES_ARG=""

USER_PROVIDED_DIR=0
if [[ -n "${SDK_DIR_ARG}" ]]; then
    SDK_DIR="${SDK_DIR_ARG}"; USER_PROVIDED_DIR=1
elif [[ -n "${JH_PICO_SDK_DIR:-}" ]]; then
    SDK_DIR="${JH_PICO_SDK_DIR}"; USER_PROVIDED_DIR=1
else
    SDK_DIR="${PICO_SDK_DIR}"
fi
[[ "${SDK_DIR}" == /* ]] || SDK_DIR="${REPO_ROOT}/${SDK_DIR}"

# Top-level paths that prove a usable RP2040 + RP2350 SDK checkout.
REQUIRED_PATHS=(
    "pico_sdk_init.cmake"
    "pico_sdk_version.cmake"
    "external/pico_sdk_import.cmake"
    "src/rp2_common/hardware_flash/include/hardware/flash.h"
    "src/rp2_common/pico_multicore/include/pico/multicore.h"
    "src/rp2040"
    "src/rp2350"
)

sdk_version() {
    local dir="$1" f="${1}/pico_sdk_version.cmake" maj min rev
    maj="$(sed -n 's/^[[:space:]]*set(PICO_SDK_VERSION_MAJOR[[:space:]]\+\([0-9]\+\).*/\1/p' "${f}" | head -1)"
    min="$(sed -n 's/^[[:space:]]*set(PICO_SDK_VERSION_MINOR[[:space:]]\+\([0-9]\+\).*/\1/p' "${f}" | head -1)"
    rev="$(sed -n 's/^[[:space:]]*set(PICO_SDK_VERSION_REVISION[[:space:]]\+\([0-9]\+\).*/\1/p' "${f}" | head -1)"
    [[ -n "${maj}" && -n "${min}" && -n "${rev}" ]] && echo "${maj}.${min}.${rev}"
}

verify_version() {
    local dir="$1"
    [[ -n "${PICO_SDK_VERSION:-}" ]] || return 0
    local found
    found="$(sdk_version "${dir}")"
    if [[ "${found}" != "${PICO_SDK_VERSION}" ]]; then
        die "Pico SDK version mismatch in ${dir}: expected ${PICO_SDK_VERSION}, found ${found:-unknown}."
    fi
}

if [[ ! -d "${SDK_DIR}" ]]; then
    [[ ${USER_PROVIDED_DIR} -eq 0 ]] || die "Pico SDK dir not found: ${SDK_DIR}"
    [[ ${VERIFY_ONLY} -eq 0 ]] || die "Pico SDK checkout missing at ${SDK_DIR} (verify-only; not fetching)."
    info "Fetching Pico SDK ${PICO_SDK_VERSION} (${PICO_SDK_REF}) into ${SDK_DIR}"
    jh_dep_clone_pinned "${PICO_SDK_REPO}" "${PICO_SDK_REF}" "${SDK_DIR}"
fi

if [[ ${VERIFY_ONLY} -eq 0 ]]; then
    jh_dep_verify_ref "${SDK_DIR}" "${PICO_SDK_REF}"
    jh_dep_init_submodules "${SDK_DIR}" "${SUBMODULES_ARG}"
fi

jh_dep_verify_paths "${SDK_DIR}" "${REQUIRED_PATHS[@]}"
verify_version "${SDK_DIR}"
jh_dep_verify_ref "${SDK_DIR}" "${PICO_SDK_REF}"

ok "Pico SDK ready: ${SDK_DIR} (v${PICO_SDK_VERSION}, ${PICO_SDK_REF})"
info "Export for the native build:  export PICO_SDK_PATH=${SDK_DIR}"
