#!/usr/bin/env bash
# Build the linkable JaszczurHAL library for any registry target.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=lib/build_artifacts.sh
source "${SCRIPT_DIR}/lib/build_artifacts.sh"

die() { echo -e "\033[0;31m[ERROR]\033[0m $*" >&2; exit 1; }

usage() {
    cat <<'USAGE'
Build the linkable JaszczurHAL library for any registry target.

Usage:
  scripts/build_link_library.sh --target NAME [runner options]

The target's build provider selects the family runner under link_libraries/:
  pico-sdk            scripts/build_rp_pico_lib.sh   (rp2040, rp2350-arm, rp2350-riscv)
  jh-stm32-baremetal  scripts/build_stm32_lib.sh     (stm32g474)
  esp-idf             scripts/build_esp32_lib.sh     (esp32s3, esp32)

Every runner accepts --target, --board, --all-features, --library-only,
--freertos, -p/--project-config, -D, -o/--output, --clean and -j/--jobs.
Remaining options are passed to the selected runner unchanged.

Targets with a library runner:
USAGE
    python3 "${REPO_ROOT}/scripts/board_registry.py" list-targets |
        sed 's/^/  /'
}

TARGET=""
ARGS=("$@")
index=0
while [[ ${index} -lt ${#ARGS[@]} ]]; do
    case "${ARGS[${index}]}" in
        --target)
            [[ $((index + 1)) -lt ${#ARGS[@]} ]] || die "--target requires a value"
            TARGET="${ARGS[$((index + 1))]}"
            index=$((index + 2))
            ;;
        --target=*)
            TARGET="${ARGS[${index}]#--target=}"
            ARGS[${index}]="--target"
            ARGS=("${ARGS[@]:0:${index}+1}" "${TARGET}" "${ARGS[@]:${index}+1}")
            index=$((index + 2))
            ;;
        -h|--help)
            if [[ -z "${TARGET}" ]]; then
                usage
                exit 0
            fi
            index=$((index + 1))
            ;;
        *) index=$((index + 1)) ;;
    esac
done
[[ -n "${TARGET}" ]] || { usage >&2; die "--target is required"; }

TARGET_PROVIDER=""
facts="$(jh_target_facts "${REPO_ROOT}" "${TARGET}")" ||
    die "Unknown target '${TARGET}'"
while IFS='=' read -r key value; do
    [[ "${key}" == "provider" ]] && TARGET_PROVIDER="${value}"
done <<< "${facts}"
RUNNER="$(jh_link_library_runner "${TARGET_PROVIDER}")" ||
    die "Target '${TARGET}' (provider '${TARGET_PROVIDER}') has no linkable-library runner"

exec "${SCRIPT_DIR}/${RUNNER}" "${ARGS[@]}"
