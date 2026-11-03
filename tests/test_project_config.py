#!/usr/bin/env python3
"""Check the build part of project_config.py: evaluate_build, the feature
resolution of a build and the CMake output.

The reader itself is jh config inspect; tools/jh/tests/test_project_config.cpp
checks it, also against the compiler's preprocessor. This part moves to jh
with the feature and board registries.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile

from repo_root import repo_root

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))
import generate_hal_features  # noqa: E402
import project_config  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def write(directory: Path, text: str) -> Path:
    path = directory / "hal_project_config.h"
    path.parent.mkdir(parents=True, exist_ok=True)
    # Bytes as given: text mode on Windows would turn every LF into CRLF.
    path.write_bytes(text.encode("utf-8"))
    return path


with tempfile.TemporaryDirectory(prefix="jh-project-config-") as temporary:
    root = Path(temporary)

    # Values the compiler computes from names this configuration does not
    # define reach CMake as unresolved, without an integer.
    sdk_header = write(
        root / "sdk_value",
        "#define FLASH_BYTES PICO_FLASH_SIZE_BYTES\n"
        "#define HAL_RP_FLASH_EEPROM_SIZE (FLASH_BYTES / 256)\n"
        "#define HAL_RP_CORE1_STACK_SIZE (2u * 1024u)\n",
    )
    sdk = project_config.read_project_config(sdk_header)
    cmake_text = "\n".join(
        project_config.cmake_lines(
            project_config.BuildConfig(
                sdk_header, project_config.TargetFacts("rp2040", "HAL_TARGET_RP2040", ()),
                None, (), sdk,
            ),
            {},
        )
    )
    require(
        "set(JH_PROJECT_UNRESOLVED_HAL_RP_FLASH_EEPROM_SIZE [=[PICO_FLASH_SIZE_BYTES]=])" in cmake_text
        and "JH_PROJECT_INT_HAL_RP_FLASH_EEPROM_SIZE" not in cmake_text
        and "set(JH_PROJECT_INT_HAL_RP_CORE1_STACK_SIZE [=[2048]=])" in cmake_text,
        "CMake output of unresolved values",
    )

    # The header's final state decides the features: a variant feature the
    # header removes is not requested, so it cannot conflict with its
    # replacement.
    radio = write(
        root / "radio",
        "#define JH_PROJECT_VARIANTS(X) X(NEW_RADIO, \"SX126X\", HAL_ENABLE_SX127X, NEW_RADIO=1)\n"
        "#if defined(NEW_RADIO)\n#undef HAL_ENABLE_SX127X\n#define HAL_ENABLE_SX126X\n#endif\n",
    )
    radio_build = project_config.evaluate_build(
        radio.parent, project_config.load_targets(ROOT)["rp2040"], "NEW_RADIO"
    )
    radio_resolution, radio_findings = project_config.resolve_build_features(
        radio_build, generate_hal_features.load_registry(ROOT / "config")
    )
    require(
        radio_build.requested_features() == ["HAL_ENABLE_SX126X"]
        and not radio_findings
        and "HAL_ENABLE_SX127X" not in radio_resolution.resolved,
        f"a removed variant feature came back: {radio_build.requested_features()} {radio_findings}",
    )

print("project configuration build tests passed")
