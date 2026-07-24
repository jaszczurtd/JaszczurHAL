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
