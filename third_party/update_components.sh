#!/usr/bin/env bash
# Compatibility launcher for the cross-platform component manager.
set -euo pipefail

THIRD_PARTY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${THIRD_PARTY_DIR}/.." && pwd)"
PYTHON="${JH_COMPONENT_PYTHON:-python3}"
exec "${PYTHON}" "${REPO_ROOT}/scripts/component_manager.py" all --repo-root "${REPO_ROOT}" "$@"
