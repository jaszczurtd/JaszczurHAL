#!/usr/bin/env bash
# Shared helpers for fetching a pinned, git-ignored third-party dependency
# (FreeRTOS-Kernel, Pico SDK, picotool, ...). Sourced by scripts/ensure_*.sh so
# the fetch/verify logic lives in one place. Sourcing only defines functions and
# the color variables; it has no side effects.

# Colors (only set if the sourcing script did not define them).
: "${RED:=$'\033[0;31m'}"
: "${GREEN:=$'\033[0;32m'}"
: "${YELLOW:=$'\033[1;33m'}"
: "${CYAN:=$'\033[0;36m'}"
: "${NC:=$'\033[0m'}"

info() { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()   { echo -e "${GREEN}[OK]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
die()  { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# jh_dep_fetch_ref DIR REPO REF
# Shallow-fetch REF into an existing git checkout at DIR and detach onto it.
jh_dep_fetch_ref() {
    local dir="$1" repo="$2" ref="$3"
    git -C "${dir}" fetch --depth 1 origin "${ref}"
    git -C "${dir}" checkout --detach FETCH_HEAD
}

# jh_dep_clone_pinned REPO REF DEST
# Clone REPO at exactly REF (shallow) into a temp dir, then move it to DEST.
# Dies with an offline-friendly hint on failure.
jh_dep_clone_pinned() {
    local repo="$1" ref="$2" dest="$3"
    local parent base tmp
    parent="$(dirname "${dest}")"
    base="$(basename "${dest}")"
    tmp="${parent}/.${base}.tmp.$$"

    mkdir -p "${parent}"
    rm -rf "${tmp}"
    git init -q "${tmp}"
    git -C "${tmp}" remote add origin "${repo}"
    if ! jh_dep_fetch_ref "${tmp}" "${repo}" "${ref}"; then
        rm -rf "${tmp}"
        die "Could not fetch ${repo} ref ${ref}.
If you are offline, pre-populate ${dest} or point the ensure script at a checkout at that ref."
    fi
    mv "${tmp}" "${dest}"
}

# jh_dep_sync_pinned REPO REF DEST VERIFY_ONLY
# Keep a managed checkout exactly at REF. A missing, mismatched, or non-git
# directory is replaced atomically; verify-only never changes it.
jh_dep_sync_pinned() {
    local repo="$1" ref="$2" dest="$3" verify_only="${4:-0}"
    local actual=""

    JH_DEP_CHANGED=0
    if [[ ! -e "${dest}" ]]; then
        [[ "${verify_only}" -eq 0 ]] ||
            die "Pinned checkout missing at ${dest} (verify-only)."
        jh_dep_clone_pinned "${repo}" "${ref}" "${dest}"
        JH_DEP_CHANGED=1
        return 0
    fi

    if [[ -d "${dest}/.git" ]]; then
        actual="$(git -C "${dest}" rev-parse HEAD 2>/dev/null || true)"
    fi
    if [[ "${actual}" == "${ref}" ]]; then
        return 0
    fi

    [[ "${verify_only}" -eq 0 ]] ||
        die "Pinned checkout mismatch at ${dest}: expected ${ref}, found ${actual:-non-git directory}."
    case "${dest}" in
        ""|"/"|".")
            die "Refusing unsafe component replacement path: ${dest:-empty}"
            ;;
    esac

    local parent base replacement
    parent="$(dirname "${dest}")"
    base="$(basename "${dest}")"
    replacement="${parent}/.${base}.replacement.$$"
    rm -rf "${replacement}"
    jh_dep_clone_pinned "${repo}" "${ref}" "${replacement}"
    rm -rf "${dest}"
    mv "${replacement}" "${dest}"
    JH_DEP_CHANGED=1
}

# jh_dep_ensure_clean DIR REF VERIFY_ONLY
# Keep a generated managed checkout free of local and untracked changes.
jh_dep_ensure_clean() {
    local dir="$1" ref="$2" verify_only="${3:-0}"
    local status
    [[ -d "${dir}/.git" ]] || return 0
    status="$(git -C "${dir}" status --porcelain)"
    [[ -n "${status}" ]] || return 0
    [[ "${verify_only}" -eq 0 ]] ||
        die "Pinned checkout has local changes at ${dir} (verify-only)."
    git -C "${dir}" reset --hard "${ref}"
    git -C "${dir}" clean -fdx
}

# jh_dep_ensure_origin DIR REPO VERIFY_ONLY
# Keep the checkout origin aligned with the repository recorded in its pin.
jh_dep_ensure_origin() {
    local dir="$1" repo="$2" verify_only="${3:-0}"
    local actual
    [[ -d "${dir}/.git" ]] || return 0
    actual="$(git -C "${dir}" remote get-url origin 2>/dev/null || true)"
    [[ "${actual}" == "${repo}" ]] && return 0
    [[ "${verify_only}" -eq 0 ]] ||
        die "Pinned checkout origin mismatch at ${dir}: expected ${repo}, found ${actual:-missing origin}."
    if [[ -n "${actual}" ]]; then
        git -C "${dir}" remote set-url origin "${repo}"
    else
        git -C "${dir}" remote add origin "${repo}"
    fi
}

# jh_dep_verify_ref DIR REF
# Assert that the checkout at DIR is exactly REF (warns if DIR is not a git repo).
jh_dep_verify_ref() {
    local dir="$1" ref="$2"
    if [[ ! -d "${dir}/.git" ]]; then
        warn "${dir} is not a git checkout; exact ref ${ref} cannot be verified."
        return 0
    fi
    local actual
    actual="$(git -C "${dir}" rev-parse HEAD)"
    if [[ "${actual}" != "${ref}" ]]; then
        die "Ref mismatch in ${dir}: expected ${ref}, found ${actual}."
    fi
}

# jh_dep_verify_paths DIR PATH...
# Assert that every PATH exists under DIR.
jh_dep_verify_paths() {
    local dir="$1"; shift
    local missing=0 path
    for path in "$@"; do
        if [[ ! -e "${dir}/${path}" ]]; then
            echo "  missing: ${dir}/${path}" >&2
            missing=1
        fi
    done
    [[ ${missing} -eq 0 ]] || die "Checkout at ${dir} is incomplete (required paths missing)."
}

# jh_dep_archive_manifest DIR OUTPUT
# Write a deterministic SHA-256 manifest for every managed file in DIR.
jh_dep_archive_manifest() {
    local dir="$1" output="$2"
    (
        cd "${dir}"
        LC_ALL=C find . -type f \
            ! -name '.jh-archive-pin' \
            ! -name '.jh-content.sha256' \
            -print0 \
            | LC_ALL=C sort -z \
            | xargs -0 sha256sum
    ) > "${output}"
}

# jh_dep_archive_matches DIR URL SHA256
# Return success when the extracted archive identity and complete file tree match.
jh_dep_archive_matches() {
    local dir="$1" url="$2" sha256="$3"
    local expected actual_manifest
    [[ -d "${dir}" ]] || return 1
    [[ -f "${dir}/.jh-archive-pin" ]] || return 1
    [[ -f "${dir}/.jh-content.sha256" ]] || return 1

    expected="$(printf 'url=%s\nsha256=%s' "${url}" "${sha256}")"
    [[ "$(cat "${dir}/.jh-archive-pin")" == "${expected}" ]] || return 1

    actual_manifest="$(mktemp "${TMPDIR:-/tmp}/jh-archive-manifest.XXXXXX")"
    jh_dep_archive_manifest "${dir}" "${actual_manifest}"
    if cmp -s "${dir}/.jh-content.sha256" "${actual_manifest}"; then
        rm -f "${actual_manifest}"
        return 0
    fi
    rm -f "${actual_manifest}"
    return 1
}

# jh_dep_install_archive URL SHA256 DEST
# Download, authenticate, and extract an archive into DEST.
jh_dep_install_archive() {
    local url="$1" sha256="$2" dest="$3"
    local parent base staging archive extract
    parent="$(dirname "${dest}")"
    base="$(basename "${dest}")"
    staging="$(mktemp -d "${parent}/.${base}.archive.XXXXXX")"
    archive="${staging}/component.archive"
    extract="${staging}/extract"
    mkdir -p "${extract}"

    if ! curl -fsSL "${url}" -o "${archive}"; then
        rm -rf "${staging}"
        die "Could not download ${url}. If you are offline, pre-populate ${dest}."
    fi
    if ! printf '%s  %s\n' "${sha256}" "${archive}" | sha256sum -c - >/dev/null; then
        rm -rf "${staging}"
        die "Archive checksum mismatch for ${url}; expected SHA-256 ${sha256}."
    fi
    if ! (cd "${extract}" && cmake -E tar xf "${archive}"); then
        rm -rf "${staging}"
        die "Could not extract managed archive ${url}."
    fi

    printf 'url=%s\nsha256=%s\n' "${url}" "${sha256}" > "${extract}/.jh-archive-pin"
    jh_dep_archive_manifest "${extract}" "${extract}/.jh-content.sha256"
    mv "${extract}" "${dest}"
    rm -rf "${staging}"
}

# jh_dep_sync_archive URL SHA256 DEST VERIFY_ONLY
# Keep a managed extracted archive byte-for-byte aligned with its authenticated pin.
jh_dep_sync_archive() {
    local url="$1" sha256="$2" dest="$3" verify_only="${4:-0}"
    local parent base replacement

    JH_DEP_CHANGED=0
    if jh_dep_archive_matches "${dest}" "${url}" "${sha256}"; then
        return 0
    fi
    [[ "${verify_only}" -eq 0 ]] ||
        die "Pinned archive installation missing or modified at ${dest} (verify-only)."
    case "${dest}" in
        ""|"/"|".")
            die "Refusing unsafe component replacement path: ${dest:-empty}"
            ;;
    esac

    parent="$(dirname "${dest}")"
    base="$(basename "${dest}")"
    mkdir -p "${parent}"
    replacement="${parent}/.${base}.replacement.$$"
    rm -rf "${replacement}"
    jh_dep_install_archive "${url}" "${sha256}" "${replacement}"
    rm -rf "${dest}"
    mv "${replacement}" "${dest}"
    JH_DEP_CHANGED=1
}

# jh_dep_init_submodules DIR SUBMODULES
# Shallow-init the listed submodules under DIR, skipping any already present.
jh_dep_init_submodules() {
    local dir="$1" subs="$2" sub
    [[ -n "${subs}" ]] || return 0
    [[ -d "${dir}/.git" ]] || { warn "${dir} is not a git checkout; cannot init submodules."; return 0; }
    for sub in ${subs}; do
        if [[ -f "${dir}/${sub}/.git" || -d "${dir}/${sub}/.git" ]]; then
            continue
        fi
        info "Initialising submodule: $(basename "${dir}")/${sub}"
        git -C "${dir}" submodule update --init --depth 1 -- "${sub}" \
            || die "Could not init submodule ${sub} in ${dir}. Offline? Retry with network."
    done
}
