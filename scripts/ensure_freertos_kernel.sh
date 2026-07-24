#!/usr/bin/env bash
# Fetch or verify the pinned FreeRTOS-Kernel dependency for STM32G474 builds.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_FILE="${REPO_ROOT}/freertos_core_version.conf"

# shellcheck source=lib/pinned_repo.sh
source "${SCRIPT_DIR}/lib/pinned_repo.sh"

usage() {
    sed -n '2,80p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'USAGE'

Usage:
  scripts/ensure_freertos_kernel.sh [options]

Options:
  --enable, --freertos      Ensure the kernel now
  --force                   Ensure even when HAL_ENABLE_FREERTOS is not detected
  --verify-only             Verify only; do not fetch or change a checkout
  --repo-root PATH          Repository root (default: script parent)
  --kernel-dir PATH         FreeRTOS-Kernel checkout path
  -h, --help                Show this help

The helper is a no-op unless one of these is true:
  - --enable / --freertos / --force is passed
  - EXTRA_HAL_DEFINES contains HAL_ENABLE_FREERTOS
  - HAL_ENABLE_FREERTOS is set in the environment

JH_FREERTOS_KERNEL_DIR overrides the configured default and is verified without
touching third_party/FreeRTOS-Kernel.
USAGE
}

ENABLE=0
FORCE=0
VERIFY_ONLY=0
KERNEL_DIR_ARG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --enable|--freertos)
            ENABLE=1
            shift
            ;;
        --force)
            FORCE=1
            ENABLE=1
            shift
            ;;
        --verify-only)
            VERIFY_ONLY=1
            ENABLE=1
            shift
            ;;
        --repo-root)
            REPO_ROOT="$(cd "$2" && pwd)"
            CONFIG_FILE="${REPO_ROOT}/freertos_core_version.conf"
            shift 2
            ;;
        --kernel-dir)
            KERNEL_DIR_ARG="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

[[ -f "${CONFIG_FILE}" ]] || die "FreeRTOS config not found: ${CONFIG_FILE}"
# shellcheck source=../freertos_core_version.conf
source "${CONFIG_FILE}"

defs_contain_hal_freertos() {
    local defs="${1:-}"
    [[ -n "${defs}" ]] || return 1

    defs="${defs//;/ }"
    defs="${defs//,/ }"
    for def in ${defs}; do
        if [[ "${def}" == "HAL_ENABLE_FREERTOS" || "${def}" == HAL_ENABLE_FREERTOS=* ]]; then
            return 0
        fi
    done
    return 1
}

if [[ ${ENABLE} -eq 0 ]]; then
    if defs_contain_hal_freertos "${EXTRA_HAL_DEFINES:-}" || [[ -n "${HAL_ENABLE_FREERTOS:-}" ]]; then
        ENABLE=1
    fi
fi

if [[ ${ENABLE} -eq 0 ]]; then
    info "HAL_ENABLE_FREERTOS not requested; skipping FreeRTOS-Kernel ensure."
    exit 0
fi

[[ -n "${FREERTOS_KERNEL_REPO:-}" ]] || die "FREERTOS_KERNEL_REPO missing in ${CONFIG_FILE}"
[[ -n "${FREERTOS_KERNEL_REF:-}" ]] || die "FREERTOS_KERNEL_REF missing in ${CONFIG_FILE}"
[[ -n "${FREERTOS_KERNEL_DIR:-}" ]] || die "FREERTOS_KERNEL_DIR missing in ${CONFIG_FILE}"

USER_PROVIDED_DIR=0
if [[ -n "${KERNEL_DIR_ARG}" ]]; then
    KERNEL_DIR="${KERNEL_DIR_ARG}"
    USER_PROVIDED_DIR=1
elif [[ -n "${JH_FREERTOS_KERNEL_DIR:-}" ]]; then
    KERNEL_DIR="${JH_FREERTOS_KERNEL_DIR}"
    USER_PROVIDED_DIR=1
else
    KERNEL_DIR="${FREERTOS_KERNEL_DIR}"
fi

if [[ "${KERNEL_DIR}" != /* ]]; then
    KERNEL_DIR="${REPO_ROOT}/${KERNEL_DIR}"
fi

REQUIRED_PATHS=(
    "include/FreeRTOS.h"
    "include/task.h"
    "include/semphr.h"
    "portable/GCC/ARM_CM4F/port.c"
    "portable/GCC/ARM_CM4F/portmacro.h"
    "portable/MemMang/heap_4.c"
    "tasks.c"
    "queue.c"
    "list.c"
    "timers.c"
    "event_groups.c"
    "stream_buffer.c"
)

kernel_version() {
    local dir="$1"
    sed -n 's/^[[:space:]]*#define[[:space:]]\+tskKERNEL_VERSION_NUMBER[[:space:]]\+"\([^"]*\)".*/\1/p' \
        "${dir}/include/task.h" | head -1
}

verify_version() {
    local dir="$1"
    if [[ -z "${FREERTOS_KERNEL_VERSION:-}" ]]; then
        return 0
    fi

    local found
    found="$(kernel_version "${dir}")"
    if [[ "${found}" != "${FREERTOS_KERNEL_VERSION}" ]]; then
        die "FreeRTOS-Kernel version mismatch in ${dir}: expected ${FREERTOS_KERNEL_VERSION}, found ${found:-unknown}."
    fi
}

if [[ ! -d "${KERNEL_DIR}" ]]; then
    if [[ ${USER_PROVIDED_DIR} -eq 1 ]]; then
        die "JH_FREERTOS_KERNEL_DIR points to a missing checkout: ${KERNEL_DIR}"
    fi
    [[ ${VERIFY_ONLY} -eq 0 ]] || die "FreeRTOS-Kernel checkout missing: ${KERNEL_DIR}"

    info "Fetching FreeRTOS-Kernel ${FREERTOS_KERNEL_VERSION:-${FREERTOS_KERNEL_REF}} into ${KERNEL_DIR}"
    jh_dep_clone_pinned "${FREERTOS_KERNEL_REPO}" "${FREERTOS_KERNEL_REF}" "${KERNEL_DIR}"
elif [[ -d "${KERNEL_DIR}/.git" ]]; then
    actual="$(git -C "${KERNEL_DIR}" rev-parse HEAD)"
    if [[ "${actual}" != "${FREERTOS_KERNEL_REF}" ]]; then
        if [[ ${USER_PROVIDED_DIR} -eq 1 || ${VERIFY_ONLY} -eq 1 ]]; then
            die "FreeRTOS-Kernel ref mismatch in ${KERNEL_DIR}: expected ${FREERTOS_KERNEL_REF}, found ${actual}."
        fi

        info "Updating FreeRTOS-Kernel checkout in ${KERNEL_DIR} to ${FREERTOS_KERNEL_REF}"
        if ! jh_dep_fetch_ref "${KERNEL_DIR}" "${FREERTOS_KERNEL_REPO}" "${FREERTOS_KERNEL_REF}"; then
            die "Could not fetch FreeRTOS-Kernel ref ${FREERTOS_KERNEL_REF}.
If you are offline, pre-populate ${KERNEL_DIR} or set JH_FREERTOS_KERNEL_DIR to a checkout at that ref."
        fi
    fi
else
    [[ ${VERIFY_ONLY} -eq 0 || ${USER_PROVIDED_DIR} -eq 1 ]] || die "FreeRTOS-Kernel path exists but is not a git checkout: ${KERNEL_DIR}"
fi

jh_dep_verify_paths "${KERNEL_DIR}" "${REQUIRED_PATHS[@]}"
verify_version "${KERNEL_DIR}"
jh_dep_verify_ref "${KERNEL_DIR}" "${FREERTOS_KERNEL_REF}"
ok "FreeRTOS-Kernel ready: ${KERNEL_DIR} (${FREERTOS_KERNEL_VERSION:-${FREERTOS_KERNEL_REF}})"
