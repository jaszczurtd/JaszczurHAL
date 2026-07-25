#!/usr/bin/env bash
# Fetch or replace the pinned lwIP checkout.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_FILE="${REPO_ROOT}/third_party/lwip_version.conf"

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
            CONFIG_FILE="${REPO_ROOT}/third_party/lwip_version.conf"
            shift 2
            ;;
        --dir) DIR_ARG="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: scripts/ensure_lwip.sh [--verify-only] [--repo-root PATH] [--dir PATH]"
            exit 0
            ;;
        *) die "Unknown option: $1" ;;
    esac
done

[[ -f "${CONFIG_FILE}" ]] || die "lwIP config not found: ${CONFIG_FILE}"
# shellcheck source=../third_party/lwip_version.conf
source "${CONFIG_FILE}"

for var in LWIP_REPO LWIP_REF LWIP_VERSION LWIP_DIR; do
    [[ -n "${!var:-}" ]] || die "${var} missing in ${CONFIG_FILE}"
done

COMPONENT_DIR="${DIR_ARG:-${LWIP_DIR}}"
[[ "${COMPONENT_DIR}" == /* ]] || COMPONENT_DIR="${REPO_ROOT}/${COMPONENT_DIR}"
jh_dep_sync_pinned \
    "${LWIP_REPO}" "${LWIP_REF}" "${COMPONENT_DIR}" "${VERIFY_ONLY}"
jh_dep_verify_paths "${COMPONENT_DIR}" \
    "COPYING" "src/core/init.c" "src/include/lwip/init.h" \
    "src/netif/ethernet.c"

FOUND_VERSION="$(
    sed -n \
        -e 's/^#define LWIP_VERSION_MAJOR[[:space:]]\+\([0-9]\+\).*/\1/p' \
        -e 's/^#define LWIP_VERSION_MINOR[[:space:]]\+\([0-9]\+\).*/\1/p' \
        -e 's/^#define LWIP_VERSION_REVISION[[:space:]]\+\([0-9]\+\).*/\1/p' \
        "${COMPONENT_DIR}/src/include/lwip/init.h" | paste -sd.
)"
[[ "${FOUND_VERSION}" == "${LWIP_VERSION}" ]] ||
    die "lwIP version mismatch: expected ${LWIP_VERSION}, found ${FOUND_VERSION:-unknown}."
jh_dep_verify_ref "${COMPONENT_DIR}" "${LWIP_REF}"
ok "lwIP ready: ${COMPONENT_DIR} (${LWIP_VERSION}, ${LWIP_REF})"
