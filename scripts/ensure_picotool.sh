#!/usr/bin/env bash
# Fetch and BUILD the pinned upstream picotool for the native RP2040/RP2350
# flashing workflow. Fetch/verify primitives are shared with the other
# dependency scripts via scripts/lib/pinned_repo.sh; picotool additionally needs
# a build step (it links libusb and uses the pinned Pico SDK).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_FILE="${REPO_ROOT}/third_party/picotool_version.conf"

# shellcheck source=lib/pinned_repo.sh
source "${SCRIPT_DIR}/lib/pinned_repo.sh"

usage() {
    cat <<'USAGE'
Fetch and build the pinned upstream picotool for native RP2040/RP2350 flashing.

Usage:
  scripts/ensure_picotool.sh [options]

Options:
  --enable, --build         Ensure (fetch + build) picotool now
  --force                   Ensure even when not explicitly requested
  --verify-only             Verify an existing build only; do not fetch or build
  --rebuild                 Force a clean rebuild even if the binary exists
  --repo-root PATH          Repository root (default: script parent)
  --picotool-dir PATH       picotool source checkout path
  --build-dir PATH          build path (default: .build/tools/picotool)
  --sdk-dir PATH            Pico SDK path to build against (else PICO_SDK_PATH /
                            JH_PICO_SDK_DIR / third_party/pico-sdk)
  -h, --help                Show this help

No-op unless --enable/--build/--force/--verify-only is passed or
JH_ENABLE_PICOTOOL is set. Requires the pinned Pico SDK (run ensure_pico_sdk.sh
first) and, for USB device access, libusb-1.0-0-dev + pkg-config.
USAGE
}

ENABLE=0
VERIFY_ONLY=0
REBUILD=0
PICOTOOL_DIR_ARG=""
BUILD_DIR_ARG=""
SDK_DIR_ARG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --enable|--build) ENABLE=1; shift ;;
        --force) ENABLE=1; shift ;;
        --verify-only) VERIFY_ONLY=1; ENABLE=1; shift ;;
        --rebuild) REBUILD=1; ENABLE=1; shift ;;
        --repo-root)
            REPO_ROOT="$(cd "$2" && pwd)"
            CONFIG_FILE="${REPO_ROOT}/third_party/picotool_version.conf"
            shift 2
            ;;
        --picotool-dir) PICOTOOL_DIR_ARG="$2"; shift 2 ;;
        --build-dir) BUILD_DIR_ARG="$2"; shift 2 ;;
        --sdk-dir) SDK_DIR_ARG="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

[[ -f "${CONFIG_FILE}" ]] || die "picotool config not found: ${CONFIG_FILE}"
# shellcheck source=../third_party/picotool_version.conf
source "${CONFIG_FILE}"

if [[ ${ENABLE} -eq 0 && -n "${JH_ENABLE_PICOTOOL:-}" ]]; then
    ENABLE=1
fi
if [[ ${ENABLE} -eq 0 ]]; then
    info "picotool not requested; skipping picotool ensure."
    exit 0
fi

[[ -n "${PICOTOOL_REPO:-}" ]] || die "PICOTOOL_REPO missing in ${CONFIG_FILE}"
[[ -n "${PICOTOOL_REF:-}" ]] || die "PICOTOOL_REF missing in ${CONFIG_FILE}"
[[ -n "${PICOTOOL_DIR:-}" ]] || die "PICOTOOL_DIR missing in ${CONFIG_FILE}"

PT_DIR="${PICOTOOL_DIR_ARG:-${PICOTOOL_DIR}}"
[[ "${PT_DIR}" == /* ]] || PT_DIR="${REPO_ROOT}/${PT_DIR}"
USER_PROVIDED_DIR=0
[[ -n "${PICOTOOL_DIR_ARG}" ]] && USER_PROVIDED_DIR=1
BUILD_DIR="${BUILD_DIR_ARG:-${REPO_ROOT}/.build/tools/picotool}"
[[ "${BUILD_DIR}" == /* ]] || BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}"
BUILD_DIR="$(realpath -m -- "${BUILD_DIR}")"
BUILD_ROOT="$(realpath -m -- "${REPO_ROOT}/.build")"
case "${BUILD_DIR}" in
    "${BUILD_ROOT}"|"${BUILD_ROOT}/"*) ;;
    *) die "picotool build output must be inside ${BUILD_ROOT}" ;;
esac
BIN="${BUILD_DIR}/picotool"

# Resolve the Pico SDK to build against.
if [[ -n "${SDK_DIR_ARG}" ]]; then
    PICO_SDK_PATH="${SDK_DIR_ARG}"
elif [[ -z "${PICO_SDK_PATH:-}" ]]; then
    if [[ -n "${JH_PICO_SDK_DIR:-}" ]]; then
        PICO_SDK_PATH="${JH_PICO_SDK_DIR}"
    else
        PICO_SDK_PATH="$(sed -n 's/^PICO_SDK_DIR=//p' \
            "${REPO_ROOT}/third_party/pico_sdk_version.conf" 2>/dev/null | head -1)"
        : "${PICO_SDK_PATH:=third_party/pico-sdk}"
    fi
fi
[[ "${PICO_SDK_PATH}" == /* ]] || PICO_SDK_PATH="${REPO_ROOT}/${PICO_SDK_PATH}"

binary_matches_version() {
    [[ -x "${BIN}" ]] || return 1
    local out
    out="$("${BIN}" version 2>/dev/null || true)"
    case "${out}" in
        *"${PICOTOOL_VERSION}"*) return 0 ;;
        *) return 1 ;;
    esac
}

verify_binary() {
    binary_matches_version ||
        die "picotool version mismatch or binary missing: expected ${PICOTOOL_VERSION} at ${BIN}."
}

# A picotool built without libusb prints "without USB support" in `version` and
# cannot flash a device - only do file ops.
binary_lacks_usb() {
    "${BIN}" version 2>/dev/null | grep -qi "without USB support"
}

# picotool built without the SDK's mbedtls target lacks the seal/encrypt
# (hash/sign) commands; if the SDK now has mbedtls, an old build is stale.
sdk_has_mbedtls() { [[ -e "${PICO_SDK_PATH}/lib/mbedtls/library" ]]; }
binary_lacks_signing() { ! "${BIN}" help 2>/dev/null | grep -qiw seal; }

if [[ ${VERIFY_ONLY} -eq 1 || ${USER_PROVIDED_DIR} -eq 1 ]]; then
    [[ -d "${PT_DIR}" ]] || die "picotool checkout missing at ${PT_DIR} (verify-only)."
    jh_dep_verify_ref "${PT_DIR}" "${PICOTOOL_REF}"
    if [[ ${VERIFY_ONLY} -eq 0 ]]; then
        JH_DEP_CHANGED=0
    fi
else
    jh_dep_sync_pinned \
        "${PICOTOOL_REPO}" "${PICOTOOL_REF}" "${PT_DIR}" "${VERIFY_ONLY}"
fi

if [[ ${VERIFY_ONLY} -eq 1 ]]; then
    verify_binary
    ok "picotool ready: ${BIN} (v${PICOTOOL_VERSION}, ${PICOTOOL_REF})"
    exit 0
fi

jh_dep_verify_ref "${PT_DIR}" "${PICOTOOL_REF}"
[[ ${JH_DEP_CHANGED:-0} -eq 1 ]] && REBUILD=1

# ── Build (self-heal: rebuild if USB or signing support is now available) ────
# Signing comes from the SDK's mbedtls target (PICO_SDK_SUBMODULES=lib/mbedtls);
# picotool has no build submodules of its own.
if [[ -x "${BIN}" && ${REBUILD} -eq 0 ]]; then
    if ! binary_matches_version \
       || { pkg-config --exists libusb-1.0 2>/dev/null && binary_lacks_usb; } \
       || { sdk_has_mbedtls && binary_lacks_signing; }; then
        warn "Rebuilding picotool (USB and/or signing support now available)."
        REBUILD=1
    else
        verify_binary
        ok "picotool already built: ${BIN} (v${PICOTOOL_VERSION})"
        exit 0
    fi
fi

[[ -d "${PICO_SDK_PATH}" ]] || die "Pico SDK not found at ${PICO_SDK_PATH}. Run scripts/ensure_pico_sdk.sh first (or pass --sdk-dir)."

if ! pkg-config --exists libusb-1.0 2>/dev/null; then
    warn "libusb-1.0 dev headers not found - picotool will build without USB device access."
    warn "For USB flashing install: sudo apt-get install -y libusb-1.0-0-dev pkg-config"
fi

[[ ${REBUILD} -eq 1 ]] && rm -rf "${BUILD_DIR}"

info "Building picotool against Pico SDK at ${PICO_SDK_PATH}"
if ! cmake -S "${PT_DIR}" -B "${BUILD_DIR}" \
        -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
        -DCMAKE_BUILD_TYPE=Release >/dev/null; then
    die "picotool CMake configure failed. If it is about libusb, install libusb-1.0-0-dev + pkg-config."
fi
if ! cmake --build "${BUILD_DIR}" -j "$(nproc 2>/dev/null || echo 4)" >/dev/null; then
    die "picotool build failed. Check libusb-1.0-0-dev / pkg-config and the build log."
fi

verify_binary
ok "picotool ready: ${BIN} (v${PICOTOOL_VERSION}, ${PICOTOOL_REF})"
info "Use for flashing:  ${BIN} info -a"
