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
