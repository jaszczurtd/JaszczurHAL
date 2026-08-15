#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
    echo "Usage: $0 <mock|rp2040|rp2350-arm|rp2350-riscv|stm32|stm32g474>" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage

case "$1" in
    mock) selection="mock:host-mock" ;;
    rp2040) selection="rp2040:pico" ;;
    rp2350-arm) selection="rp2350-arm:pico2" ;;
    rp2350-riscv) selection="rp2350-riscv:pico2" ;;
    stm32|stm32g474) selection="stm32g474:nucleo-g474re" ;;
    *) usage ;;
esac

python3 "${REPO_ROOT}/scripts/vscode_library_workspace.py" \
    select --selection "${selection}"
python3 "${REPO_ROOT}/scripts/vscode_library_workspace.py" \
    refresh-intellisense
