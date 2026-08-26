#!/usr/bin/env bash
# Compatibility wrapper for the repository SBOM freshness check.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "${SCRIPT_DIR}/generate_sbom.py" --check
