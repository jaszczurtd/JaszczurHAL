#!/usr/bin/env bash

jh_build_root() {
    realpath -m -- "$1/.build"
}

jh_resolve_build_output() {
    local repo_root="$1"
    local requested="$2"
    local default_relative="$3"
    local build_root
    local resolved

    build_root="$(jh_build_root "${repo_root}")"
    if [[ -z "${requested}" ]]; then
        resolved="${build_root}/${default_relative}"
    elif [[ "${requested}" == /* ]]; then
        resolved="$(realpath -m -- "${requested}")"
    else
        resolved="$(realpath -m -- "${repo_root}/${requested}")"
    fi

    case "${resolved}" in
        "${build_root}"|"${build_root}/"*)
            printf '%s\n' "${resolved}"
            ;;
        *)
            return 1
            ;;
    esac
}

# Reject HAL_ENABLE_* spellings that the feature registry cannot resolve.
jh_validate_hal_defines() {
    local definition normalized
    for definition in "$@"; do
        normalized="${definition#-D}"
        if [[ "${normalized}" == *'$<'* ]]; then
            echo "[JH-CFG-VALUE] ${definition} is unsupported; generator expressions are not accepted" >&2
            return 1
        fi
        if [[ "${normalized}" == *HAL_ENABLE_* ]] &&
           [[ ! "${normalized}" =~ ^HAL_ENABLE_[A-Z0-9_]+(=1)?$ ]]; then
            echo "[JH-CFG-VALUE] ${definition} is unsupported; use a standalone bare symbol or an explicit value of 1" >&2
            return 1
        fi
    done
}

# Print KEY=VALUE facts about a registry target (provider, defaultBoard, isa,
# status, supportedFeatures). Fails for an unknown target.
jh_target_facts() {
    local repo_root="$1"
    local target="$2"
    python3 "${repo_root}/scripts/board_registry.py" \
        --repo-root "${repo_root}" target-facts "${target}"
}

# Map a registry build provider to its linkable-library runner script.
jh_link_library_runner() {
    case "$1" in
        pico-sdk) echo "build_rp_pico_lib.sh" ;;
        jh-stm32-baremetal) echo "build_stm32_lib.sh" ;;
        esp-idf) echo "build_esp32_lib.sh" ;;
        *) return 1 ;;
    esac
}
