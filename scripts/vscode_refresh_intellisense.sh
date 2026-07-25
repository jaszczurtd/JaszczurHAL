#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 <rp2040|stm32>" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage
TARGET="$1"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VSCODE_DIR="${REPO_ROOT}/.vscode"
SETTINGS_FILE="${VSCODE_DIR}/settings.json"

mkdir -p "${VSCODE_DIR}"

configure_rp2040() {
    local build_dir="${REPO_ROOT}/.build/intellisense/rp2040"
    "${REPO_ROOT}/scripts/build_rp_native_lib.sh" \
        --platform rp2040 --board pico --clean --output "${build_dir}"
}

configure_stm32() {
    local build_dir="${REPO_ROOT}/.build/intellisense/stm32g474"

    cmake -S "${REPO_ROOT}/stm32_lib" -B "${build_dir}" \
        -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/stm32_lib/toolchain_stm32g474.cmake" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
}

case "${TARGET}" in
    rp2040)
        configure_rp2040
        COMPILE_COMMANDS="${REPO_ROOT}/.build/intellisense/rp2040/compile_commands.json"
        ;;
    stm32)
        configure_stm32
        COMPILE_COMMANDS="${REPO_ROOT}/.build/intellisense/stm32g474/compile_commands.json"
        ;;
    *)      usage ;;
esac

[[ -f "${COMPILE_COMMANDS}" ]] || {
    echo "compile_commands.json was not generated: ${COMPILE_COMMANDS}" >&2
    exit 1
}

python3 - "${SETTINGS_FILE}" "${COMPILE_COMMANDS}" "${TARGET}" <<'PYEOF'
import json
import os
import sys

settings_path, compile_commands, target = sys.argv[1:4]
try:
    with open(settings_path, "r", encoding="utf-8") as handle:
        settings = json.load(handle)
except FileNotFoundError:
    settings = {}

settings["C_Cpp.default.compileCommands"] = compile_commands
settings["C_Cpp.default.configurationProvider"] = ""
settings["jaszczurhal.activeIntelliSenseTarget"] = target

with open(settings_path, "w", encoding="utf-8") as handle:
    json.dump(settings, handle, indent=4)
    handle.write("\n")
PYEOF

echo "IntelliSense now uses: ${COMPILE_COMMANDS}"
