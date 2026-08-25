"""Named repository artifact paths loaded from the tooling contract."""

from __future__ import annotations

from pathlib import Path

from tooling_contract import (
    ToolingContractError,
    load_tooling_contract,
    require_object,
    require_string,
)


_SOURCE = "artifacts.json"
_CONTRACT = load_tooling_contract(_SOURCE)
_ARCHIVE = require_object(_CONTRACT, "archiveMetadata", source=_SOURCE)
_GENERATED = require_object(_CONTRACT, "generatedArtifacts", source=_SOURCE)


def _archive_name(field: str) -> str:
    return require_string(_ARCHIVE.get(field), field, source=_SOURCE)


def _generated_path(field: str) -> Path:
    value = require_string(_GENERATED.get(field), field, source=_SOURCE)
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        raise ToolingContractError(
            f"{_SOURCE}: {field} must be repository-relative"
        )
    return path


ARCHIVE_PIN_FILE = _archive_name("pinFile")
ARCHIVE_MANIFEST_FILE = _archive_name("manifestFile")
COMPONENT_VERSION_STAMP = _archive_name("versionStamp")
ARCHIVE_EXCLUSIONS_FILE = _archive_name("exclusionsFile")

FEATURE_HEADER_OUTPUT = _generated_path("featureHeader")
FEATURE_CMAKE_OUTPUT = _generated_path("featureCmake")
BOARD_REGISTRY_HEADER_OUTPUT = _generated_path("boardRegistryHeader")
BOARD_FALLBACK_HEADER_OUTPUT = _generated_path("boardFallbackHeader")
BOARD_COMPONENTS_CMAKE_OUTPUT = _generated_path("boardComponentsCmake")

FEATURE_OUTPUTS = (FEATURE_HEADER_OUTPUT, FEATURE_CMAKE_OUTPUT)
