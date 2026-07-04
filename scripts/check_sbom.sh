#!/usr/bin/env bash
# Verify that the committed CycloneDX SBOM matches security/third_party.json.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SBOM="${REPO_ROOT}/security/sbom.cdx.json"
TMP_SBOM="$(mktemp)"

cleanup() {
    rm -f "${TMP_SBOM}"
}
trap cleanup EXIT

"${REPO_ROOT}/scripts/generate_sbom.py" --output "${TMP_SBOM}" >/dev/null

if ! cmp -s "${TMP_SBOM}" "${SBOM}"; then
    echo "security/sbom.cdx.json is out of date." >&2
    echo "Run ./scripts/generate_sbom.py and commit the regenerated SBOM." >&2
    diff -u "${SBOM}" "${TMP_SBOM}" || true
    exit 1
fi

echo "security/sbom.cdx.json is up to date."
