#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "Removing build artifacts from ${REPO_ROOT}"

find "${REPO_ROOT}" -maxdepth 1 -type d \
    \( -name "build" -o -name "build_*" -o -name ".build" \) \
    -print -exec rm -rf {} +

echo "Build artifacts removed."
