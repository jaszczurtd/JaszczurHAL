#!/usr/bin/env bash
# Synchronize every managed third-party component to its tracked version file.
set -euo pipefail

THIRD_PARTY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${THIRD_PARTY_DIR}/.." && pwd)"
VERIFY_ONLY=0

usage() {
    echo "Usage: third_party/update_components.sh [--verify-only]"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --verify-only) VERIFY_ONLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

ARGS=(--force --repo-root "${REPO_ROOT}")
if [[ ${VERIFY_ONLY} -eq 1 ]]; then
    ARGS=(--verify-only --repo-root "${REPO_ROOT}")
fi

"${REPO_ROOT}/scripts/ensure_bearssl.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_cjson.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_lodepng.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_jpeg.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_fatfs.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_unity.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_lwip.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_littlefs.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_freertos_kernel.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_pico_sdk.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_picotool.sh" "${ARGS[@]}"
"${REPO_ROOT}/scripts/ensure_riscv_toolchain.sh" "${ARGS[@]}"

printf 'All managed third-party components are synchronized.\n'
