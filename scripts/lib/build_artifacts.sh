#!/usr/bin/env bash

# The jh host CLI of a checkout: JH_EXECUTABLE when it names an executable
# and JH_EXECUTABLE_ROOT, if set, is that checkout (the gate prepares the tool
# once), otherwise scripts/jh, which builds the tool when its inputs changed.
jh_tool_path() {
    local repo_root="$1"
    if [[ -n "${JH_EXECUTABLE:-}" && -x "${JH_EXECUTABLE}" ]]; then
        if [[ -z "${JH_EXECUTABLE_ROOT:-}" ]] ||
           [[ "$(cd "${JH_EXECUTABLE_ROOT}" 2>/dev/null && pwd -P)" == \
              "$(cd "${repo_root}" && pwd -P)" ]]; then
            printf '%s\n' "${JH_EXECUTABLE}"
            return 0
        fi
    fi
    printf '%s\n' "${repo_root}/scripts/jh"
}

# Print KEY=VALUE facts about a registry target (provider, defaultBoard, isa,
# status, supportedFeatures). Fails for an unknown target.
jh_target_facts() {
    local repo_root="$1"
    local target="$2"
    "$(jh_tool_path "${repo_root}")" target facts --root "${repo_root}" \
        "--target=${target}"
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

# Options of a library launcher, kept as the family runners read them: the
# last value of an option wins, a repeated flag counts once and the -D values
# (JH_DEFINES) keep their order.
JH_DEFINES=()
jh_launcher_option() { printf -v "JH_OPTION_${1//-/_}" '%s' "$2"; }
jh_launcher_flag() { printf -v "JH_FLAG_${1//-/_}" '%s' 1; }

# jh_launch_library_build REPO TARGET PROVIDER MESSAGE NAME...: check that the
# target uses PROVIDER (otherwise die with MESSAGE), then replace the shell
# with jh library build and every option or flag NAME that was given.
jh_launch_library_build() {
    local repo_root="$1" target="$2" provider="$3" message="$4"
    shift 4
    local facts key value actual="" name variable
    local -a arguments=()
    facts="$(jh_target_facts "${repo_root}" "${target}")" ||
        die "Unknown target '${target}'"
    while IFS='=' read -r key value; do
        [[ "${key}" == "provider" ]] && actual="${value}"
    done <<< "${facts}"
    [[ "${actual}" == "${provider}" ]] || die "${message}"
    for name in "$@"; do
        variable="JH_OPTION_${name//-/_}"
        if [[ -n "${!variable+set}" ]]; then
            arguments+=("--${name}=${!variable}")
            continue
        fi
        variable="JH_FLAG_${name//-/_}"
        [[ -z "${!variable+set}" ]] || arguments+=("--${name}")
    done
    exec "$(jh_tool_path "${repo_root}")" library build --root "${repo_root}" \
        "--target=${target}" "${arguments[@]}" "${JH_DEFINES[@]}"
}
