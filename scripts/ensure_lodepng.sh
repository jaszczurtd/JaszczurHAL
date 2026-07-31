#!/usr/bin/env bash
# Fetch or replace the pinned LodePNG checkout.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_FILE="${REPO_ROOT}/third_party/lodepng_version.conf"

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
            CONFIG_FILE="${REPO_ROOT}/third_party/lodepng_version.conf"
            shift 2
            ;;
        --dir) DIR_ARG="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: scripts/ensure_lodepng.sh [--verify-only] [--repo-root PATH] [--dir PATH]"
            exit 0
            ;;
        *) die "Unknown option: $1" ;;
    esac
done

[[ -f "${CONFIG_FILE}" ]] || die "LodePNG config not found: ${CONFIG_FILE}"
# shellcheck source=../third_party/lodepng_version.conf
source "${CONFIG_FILE}"

for var in LODEPNG_REPO LODEPNG_REF LODEPNG_VERSION LODEPNG_DIR; do
    [[ -n "${!var:-}" ]] || die "${var} missing in ${CONFIG_FILE}"
done

COMPONENT_DIR="${DIR_ARG:-${LODEPNG_DIR}}"
[[ "${COMPONENT_DIR}" == /* ]] || COMPONENT_DIR="${REPO_ROOT}/${COMPONENT_DIR}"
jh_dep_sync_pinned \
    "${LODEPNG_REPO}" "${LODEPNG_REF}" "${COMPONENT_DIR}" "${VERIFY_ONLY}"
jh_dep_ensure_origin "${COMPONENT_DIR}" "${LODEPNG_REPO}" "${VERIFY_ONLY}"
jh_dep_ensure_clean "${COMPONENT_DIR}" "${LODEPNG_REF}" "${VERIFY_ONLY}"
jh_dep_verify_paths "${COMPONENT_DIR}" \
    "LICENSE" "lodepng.cpp" "lodepng.h"
jh_dep_verify_ref "${COMPONENT_DIR}" "${LODEPNG_REF}"
ok "LodePNG ready: ${COMPONENT_DIR} (${LODEPNG_VERSION}, ${LODEPNG_REF})"
