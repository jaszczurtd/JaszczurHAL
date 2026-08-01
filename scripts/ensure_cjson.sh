#!/usr/bin/env bash
# Compatibility launcher for the cross-platform cjson component manager.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON="${JH_COMPONENT_PYTHON:-python3}"
exec "${PYTHON}" "${SCRIPT_DIR}/component_manager.py" component cjson "$@"
