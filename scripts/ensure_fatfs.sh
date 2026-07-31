#!/usr/bin/env bash
# Fetch or replace the pinned FatFs archive.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_FILE="${REPO_ROOT}/third_party/fatfs_version.conf"

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
            CONFIG_FILE="${REPO_ROOT}/third_party/fatfs_version.conf"
            shift 2
            ;;
        --dir) DIR_ARG="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: scripts/ensure_fatfs.sh [--verify-only] [--repo-root PATH] [--dir PATH]"
            exit 0
            ;;
        *) die "Unknown option: $1" ;;
    esac
done

[[ -f "${CONFIG_FILE}" ]] || die "FatFs config not found: ${CONFIG_FILE}"
# shellcheck source=../third_party/fatfs_version.conf
source "${CONFIG_FILE}"

for var in FATFS_VERSION FATFS_URL FATFS_SHA256 FATFS_DIR; do
    [[ -n "${!var:-}" ]] || die "${var} missing in ${CONFIG_FILE}"
done
[[ "${FATFS_SHA256}" =~ ^[0-9a-f]{64}$ ]] ||
    die "FATFS_SHA256 must be a lowercase SHA-256 digest"

COMPONENT_DIR="${DIR_ARG:-${FATFS_DIR}}"
[[ "${COMPONENT_DIR}" == /* ]] || COMPONENT_DIR="${REPO_ROOT}/${COMPONENT_DIR}"
jh_dep_sync_archive \
    "${FATFS_URL}" "${FATFS_SHA256}" "${COMPONENT_DIR}" "${VERIFY_ONLY}"
jh_dep_verify_paths "${COMPONENT_DIR}" \
    "LICENSE.txt" "source/00history.txt" "source/00readme.txt" \
    "source/diskio.h" "source/ff.c" "source/ff.h" \
    "source/ffsystem.c" "source/ffunicode.c"
ok "FatFs ready: ${COMPONENT_DIR} (${FATFS_VERSION}, ${FATFS_SHA256})"
