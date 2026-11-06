#!/usr/bin/env bash
# Compatibility launcher for the cross-platform component manager.
#
# After a successful synchronization it refreshes security/sbom.cdx.json: the
# SBOM records the versions and commits of the pins, jhbase's own pins read
# from its fresh checkout included. The file is rewritten only when its content
# changes. --verify-only and --help leave it alone, and --no-sbom skips the
# refresh: scripts/install_host_tools.sh (runmefirst.sh, every Linux CI job)
# passes it, so CI still rejects a commit with a stale SBOM.
set -euo pipefail

THIRD_PARTY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${THIRD_PARTY_DIR}/.." && pwd)"
PYTHON="${JH_COMPONENT_PYTHON:-python3}"
SBOM="${REPO_ROOT}/security/sbom.cdx.json"

refresh_sbom=1
manager_args=()
for arg in "$@"; do
    case "${arg}" in
        --no-sbom)
            refresh_sbom=0
            continue
            ;;
        # argparse also takes unambiguous prefixes, such as --verify for
        # --verify-only. Another --repo-root synchronizes a different checkout,
        # whose own launcher refreshes its SBOM.
        -h|--h*|--v*|--r*)
            refresh_sbom=0
            ;;
    esac
    manager_args+=("${arg}")
done

"${PYTHON}" "${REPO_ROOT}/scripts/component_manager.py" all \
    --repo-root "${REPO_ROOT}" ${manager_args[@]+"${manager_args[@]}"}

if [[ "${refresh_sbom}" -eq 0 ]]; then
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
if ! "${PYTHON}" "${REPO_ROOT}/scripts/generate_sbom.py" \
        --output "${work}/sbom.cdx.json" >/dev/null; then
    echo "update_components.sh: components are synchronized, but the SBOM refresh failed" >&2
    exit 1
fi
if cmp -s "${work}/sbom.cdx.json" "${SBOM}"; then
    echo "SBOM is current: security/sbom.cdx.json"
else
    cp "${work}/sbom.cdx.json" "${SBOM}"
    echo "Refreshed security/sbom.cdx.json from the component pins"
fi
