#!/usr/bin/env bash
# Fetch or verify the pinned RISC-V bare-metal GCC for the rp2350-risc-v target.
# Prebuilt tarball from the official Raspberry Pi pico-sdk-tools releases, so it
# is independent of arduino-pico. Shares only logging with the other dependency
# scripts (scripts/lib/pinned_repo.sh); the payload is a tarball, not a git repo.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_FILE="${REPO_ROOT}/riscv_toolchain_version.conf"

# shellcheck source=lib/pinned_repo.sh
source "${SCRIPT_DIR}/lib/pinned_repo.sh"

usage() {
    cat <<'USAGE'
Fetch or verify the pinned RISC-V toolchain for the rp2350-risc-v target.

Usage:
  scripts/ensure_riscv_toolchain.sh [options]

Options:
  --enable, --native        Ensure the toolchain now
  --force                   Ensure even when not explicitly requested
  --verify-only             Verify only; do not download or extract
  --repo-root PATH          Repository root (default: script parent)
  --dir PATH                Toolchain install path
  -h, --help                Show this help

No-op unless --enable/--native/--force/--verify-only is passed or
JH_ENABLE_RISCV_TOOLCHAIN is set. The native build points the Pico SDK at the
checkout via PICO_TOOLCHAIN_PATH.
USAGE
}

ENABLE=0
VERIFY_ONLY=0
DIR_ARG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --enable|--native) ENABLE=1; shift ;;
        --force) ENABLE=1; shift ;;
        --verify-only) VERIFY_ONLY=1; ENABLE=1; shift ;;
        --repo-root)
            REPO_ROOT="$(cd "$2" && pwd)"
            CONFIG_FILE="${REPO_ROOT}/riscv_toolchain_version.conf"
            shift 2
            ;;
        --dir) DIR_ARG="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

[[ -f "${CONFIG_FILE}" ]] || die "RISC-V toolchain config not found: ${CONFIG_FILE}"
# shellcheck source=../riscv_toolchain_version.conf
source "${CONFIG_FILE}"

if [[ ${ENABLE} -eq 0 && -n "${JH_ENABLE_RISCV_TOOLCHAIN:-}" ]]; then
    ENABLE=1
fi
if [[ ${ENABLE} -eq 0 ]]; then
    info "rp2350-risc-v target not requested; skipping RISC-V toolchain ensure."
    exit 0
fi

for v in RISCV_TOOLCHAIN_RELEASE_BASE RISCV_TOOLCHAIN_TAG RISCV_TOOLCHAIN_ASSET_STEM \
         RISCV_TOOLCHAIN_GCC_MAJOR RISCV_TOOLCHAIN_TRIPLE RISCV_TOOLCHAIN_DIR; do
    [[ -n "${!v:-}" ]] || die "${v} missing in ${CONFIG_FILE}"
done

case "$(uname -m)" in
    x86_64|amd64)  ARCH="x86_64-lin" ;;
    aarch64|arm64) ARCH="aarch64-lin" ;;
    *) die "Unsupported architecture for the RISC-V toolchain: $(uname -m). Install a ${RISCV_TOOLCHAIN_TRIPLE} GCC manually and pass --dir." ;;
esac

TC_DIR="${DIR_ARG:-${RISCV_TOOLCHAIN_DIR}}"
[[ "${TC_DIR}" == /* ]] || TC_DIR="${REPO_ROOT}/${TC_DIR}"
GCC="${TC_DIR}/bin/${RISCV_TOOLCHAIN_TRIPLE}-gcc"
ASSET="${RISCV_TOOLCHAIN_ASSET_STEM}-${ARCH}.tar.gz"
URL="${RISCV_TOOLCHAIN_RELEASE_BASE}/${RISCV_TOOLCHAIN_TAG}/${ASSET}"

verify_toolchain() {
    [[ -x "${GCC}" ]] || die "RISC-V gcc missing: ${GCC}"
    local ver
    ver="$("${GCC}" --version 2>/dev/null | head -1 || true)"
    case "${ver}" in
        *") ${RISCV_TOOLCHAIN_GCC_MAJOR}."*) : ;;
        *) die "RISC-V gcc version mismatch: expected GCC ${RISCV_TOOLCHAIN_GCC_MAJOR}.x, got '${ver:-none}'." ;;
    esac
}

if [[ ${VERIFY_ONLY} -eq 1 ]]; then
    [[ -x "${GCC}" ]] || die "RISC-V toolchain missing at ${TC_DIR} (verify-only; not downloading)."
    verify_toolchain
    ok "RISC-V toolchain ready: ${TC_DIR} (${RISCV_TOOLCHAIN_TRIPLE}, GCC ${RISCV_TOOLCHAIN_GCC_MAJOR})"
    exit 0
fi

if [[ ! -x "${GCC}" ]]; then
    command -v curl >/dev/null 2>&1 || die "curl is required to fetch the RISC-V toolchain."
    command -v tar  >/dev/null 2>&1 || die "tar is required to extract the RISC-V toolchain."

    info "Fetching RISC-V toolchain ${RISCV_TOOLCHAIN_TAG} (${ASSET}) - this is a large download."
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp}"' EXIT
    if ! curl -fsSL "${URL}" -o "${tmp}/tc.tar.gz"; then
        die "Could not download RISC-V toolchain from ${URL} (offline? bad pin?)."
    fi
    mkdir -p "${tmp}/x"
    tar -xzf "${tmp}/tc.tar.gz" -C "${tmp}/x"

    # Locate the toolchain root (the dir containing bin/<triple>-gcc); the tarball
    # layout may or may not wrap everything in a top-level directory.
    gcc_path="$(find "${tmp}/x" -type f -name "${RISCV_TOOLCHAIN_TRIPLE}-gcc" | head -1)"
    [[ -n "${gcc_path}" ]] || die "RISC-V toolchain archive did not contain ${RISCV_TOOLCHAIN_TRIPLE}-gcc."
    root="$(dirname "$(dirname "${gcc_path}")")"

    rm -rf "${TC_DIR}"
    mkdir -p "$(dirname "${TC_DIR}")"
    mv "${root}" "${TC_DIR}"
    rm -rf "${tmp}"
    trap - EXIT
fi

verify_toolchain
ok "RISC-V toolchain ready: ${TC_DIR} (${RISCV_TOOLCHAIN_TRIPLE}, GCC ${RISCV_TOOLCHAIN_GCC_MAJOR})"
info "Native rp2350-risc-v build:  export PICO_TOOLCHAIN_PATH=${TC_DIR}"
