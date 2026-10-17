#!/usr/bin/env bash
# Runs the cppcheck build pinned by the jhbase component (cppcheck_version.conf
# in third_party/jhbase/third_party) with the
# given arguments; fails with the repair command when it is missing or stale.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON="${JH_COMPONENT_PYTHON:-python3}"
CPPCHECK="$("${PYTHON}" "${SCRIPT_DIR}/component_manager.py" tool-path cppcheck)"
exec "${CPPCHECK}" "$@"
