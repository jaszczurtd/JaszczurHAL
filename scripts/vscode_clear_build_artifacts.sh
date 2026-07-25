#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "Removing build artifacts from ${REPO_ROOT}"

if [[ -d "${REPO_ROOT}/.build" ]]; then
    printf '%s\n' "${REPO_ROOT}/.build"
    rm -rf -- "${REPO_ROOT}/.build"
fi

echo "Build artifacts removed."
