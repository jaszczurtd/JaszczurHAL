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

# A device tree in the configuration directory selects the assembly path of a
# library build. Returns 0 for that path. Checks the selectors of both paths:
# a board with a device tree, or an assembly or variant without one, is an
# error printed on stderr (exit status 2).
jh_device_tree_library() {
    local config_dir="$1"
    local board="$2"
    local assembly="$3"
    local variant="$4"
    if [[ -n "${config_dir}" && -f "${config_dir}/device_tree.json" ]]; then
        if [[ -n "${board}" ]]; then
            echo "[JH-HW-BOARD] ${config_dir}/device_tree.json selects the board through its assemblies; use --assembly instead of --board" >&2
            return 2
        fi
        return 0
    fi
    if [[ -n "${assembly}" || -n "${variant}" ]]; then
        echo "[JH-HW-ASSEMBLY] --assembly and --variant need -p DIR with device_tree.json" >&2
        return 2
    fi
    return 1
}

# Build directory of a device tree library below .build/static/<target>:
# <assembly or "auto">/<16 hex digits naming the configuration directory,
# variant and requests>, so independent projects with the same assembly key
# never share a build, while edits of one rebuild in place.
jh_device_tree_build_key() {
    python3 - "$@" <<'PY'
import hashlib
import sys
from pathlib import Path

config_dir, assembly, *rest = sys.argv[1:]
text = "\0".join([Path(config_dir).resolve().as_posix(), assembly, *sorted(rest)])
print(f"{assembly or 'auto'}/{hashlib.sha256(text.encode()).hexdigest()[:16]}")
PY
}

# The generated headers of a library build in <output>/include/generated: the
# board path copies them from its board directory, a device tree build gets
# them from the CMake project and adds jh_hardware.h. Fails when one is missing.
jh_collect_generated_headers() {
    local output="$1"
    local target="$2"
    local board="$3"
    local device_tree="$4"
    local include="${output}/include/generated"
    local header
    mkdir -p "${include}"
    if [[ "${device_tree}" -eq 1 ]]; then
        for header in jh_board_config.h jh_link_contract.h jh_hardware.h; do
            [[ -f "${include}/${header}" ]] || {
                echo "Generated header not found: ${include}/${header}" >&2
                return 1
            }
        done
        return 0
    fi
    for header in jh_board_config.h jh_link_contract.h; do
        [[ -f "${output}/generated/boards/${target}/${board}/${header}" ]] || {
            echo "Generated board header not found: ${output}/generated/boards/${target}/${board}/${header}" >&2
            return 1
        }
        cp -f "${output}/generated/boards/${target}/${board}/${header}" "${include}/"
    done
}

# The selection a runner reports: the board, or the assembly of a device tree.
jh_build_selection() {
    if [[ "$1" -eq 1 ]]; then
        echo "assembly ${3:-chosen by the target}"
    else
        echo "board $2"
    fi
}
