#!/usr/bin/env bash
# Fetch or replace the pinned littlefs checkout.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_FILE="${REPO_ROOT}/third_party/littlefs_version.conf"

# shellcheck source=lib/pinned_repo.sh
source "${SCRIPT_DIR}/lib/pinned_repo.sh"

VERIFY_ONLY=0
DIR_ARG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --enable|--force) shift ;;
        --verify-only) VERIFY_ONLY=1; shift ;;
        --repo-root)
            REPO_ROOT="$(cd "$2" && pwd)"
            CONFIG_FILE="${REPO_ROOT}/third_party/littlefs_version.conf"
            shift 2
            ;;
        --dir) DIR_ARG="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: scripts/ensure_littlefs.sh [--verify-only] [--repo-root PATH] [--dir PATH]"
            exit 0
            ;;
        *) die "Unknown option: $1" ;;
    esac
done

[[ -f "${CONFIG_FILE}" ]] || die "littlefs config not found: ${CONFIG_FILE}"
# shellcheck source=../third_party/littlefs_version.conf
source "${CONFIG_FILE}"

for var in LITTLEFS_REPO LITTLEFS_REF LITTLEFS_VERSION LITTLEFS_DIR; do
    [[ -n "${!var:-}" ]] || die "${var} missing in ${CONFIG_FILE}"
done

COMPONENT_DIR="${DIR_ARG:-${LITTLEFS_DIR}}"
[[ "${COMPONENT_DIR}" == /* ]] || COMPONENT_DIR="${REPO_ROOT}/${COMPONENT_DIR}"
jh_dep_sync_pinned \
    "${LITTLEFS_REPO}" "${LITTLEFS_REF}" "${COMPONENT_DIR}" "${VERIFY_ONLY}"
jh_dep_verify_paths "${COMPONENT_DIR}" \
    "LICENSE.md" "lfs.c" "lfs.h" "lfs_util.c" "lfs_util.h"

IFS=. read -r LITTLEFS_VERSION_MAJOR LITTLEFS_VERSION_MINOR _ \
    <<< "${LITTLEFS_VERSION}"
EXPECTED_VERSION_HEX="$(
    printf '0x%04x%04x' \
        "${LITTLEFS_VERSION_MAJOR}" "${LITTLEFS_VERSION_MINOR}"
)"
FOUND_VERSION_HEX="$(
    sed -n 's/^#define LFS_VERSION[[:space:]]\+\(0x[0-9a-fA-F]\+\).*/\1/p' \
        "${COMPONENT_DIR}/lfs.h"
)"
[[ "${FOUND_VERSION_HEX,,}" == "${EXPECTED_VERSION_HEX,,}" ]] ||
    die "littlefs API version mismatch: expected ${EXPECTED_VERSION_HEX}, found ${FOUND_VERSION_HEX:-unknown}."
jh_dep_verify_ref "${COMPONENT_DIR}" "${LITTLEFS_REF}"
ok "littlefs ready: ${COMPONENT_DIR} (${LITTLEFS_VERSION}, ${LITTLEFS_REF})"
