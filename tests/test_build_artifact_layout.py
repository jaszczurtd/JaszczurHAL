#!/usr/bin/env python3
"""Validate the repository-wide managed build artifact layout."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(sys.argv[1]).resolve()
BUILD_ROOT = ROOT / ".build"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


for example_dir in sorted((ROOT / "examples").glob("[0-9][0-9]_*")):
    manifest_path = example_dir / ".vscode" / "jaszczurhal.project.json"
    manifest = load_json(manifest_path)
    expected = f"${{jhRoot}}/.build/examples/{example_dir.name}"
    require(
        manifest.get("buildDir") == expected,
        f"{example_dir.name}: buildDir escapes the central .build tree",
    )
    require(
        manifest.get("cmakeBuildDir") == "${buildDir}/cmake",
        f"{example_dir.name}: CMake output is not below buildDir",
    )
    cache = manifest.get("cmake", {}).get("cache", {})
    require(
        cache.get("JH_ARTIFACT_DIR") == "${buildDir}",
        f"{example_dir.name}: final artifacts do not follow buildDir",
    )

for fixture in (
    "rp_usb_cdc_echo",
    "rp_usb_multicore",
    "rp_freertos_smp",
    "rp_flash_transaction",
    "rp_storage",
    "rp_sdlogger",
    "rp_ota",
):
    manifest = load_json(
        ROOT
        / "tests"
        / "hardware"
        / fixture
        / ".vscode"
        / "jaszczurhal.project.json"
    )
    require(
        manifest.get("buildDir") == f"${{jhRoot}}/.build/hardware/{fixture}",
        f"{fixture}: hardware artifacts escape the central .build tree",
    )
    require(
        manifest.get("cmake", {}).get("cache", {}).get("JH_ARTIFACT_DIR")
        == "${buildDir}",
        f"{fixture}: final artifacts do not follow buildDir",
    )

sys.path.insert(0, str(ROOT / "scripts"))
from board_registry import tooling_target_registry

tooling_registry = tooling_target_registry(ROOT)
for target in ("rp2040", "rp2350-arm", "rp2350-riscv"):
    descriptor = tooling_registry[target]
    require(
        descriptor.get("cache", {}).get("JH_PICOTOOL_EXECUTABLE")
        == "${jhRoot}/.build/tools/picotool/picotool",
        f"{target}: picotool build escapes the central .build tree",
    )

for script in (
    "build_rp_native_lib.sh",
    "build_stm32_lib.sh",
):
    text = (ROOT / "scripts" / script).read_text(encoding="utf-8")
    require(
        "jh_resolve_build_output" in text,
        f"{script}: output path is not constrained by the shared helper",
    )

quality_gate = (ROOT / "runalltests.sh").read_text(encoding="utf-8")
require(
    'LOG_ROOT="${GATE_BUILD_ROOT}/logs"' in quality_gate,
    "runalltests.sh logs are not below .build/gate",
)
require(
    'PYTHONPYCACHEPREFIX="${BUILD_ROOT}/python-cache"' in quality_gate,
    "runalltests.sh Python cache is not below .build",
)
require(
    "/tmp/jh_" not in quality_gate,
    "runalltests.sh still writes logs outside .build",
)

helper = ROOT / "scripts" / "lib" / "build_artifacts.sh"
accepted = subprocess.run(
    [
        "bash",
        "-c",
        'source "$1"; jh_resolve_build_output "$2" ".build/custom/test" unused',
        "bash",
        str(helper),
        str(ROOT),
    ],
    check=False,
    capture_output=True,
    text=True,
)
require(accepted.returncode == 0, "shared helper rejected a .build output")
require(
    Path(accepted.stdout.strip()) == BUILD_ROOT / "custom" / "test",
    "shared helper resolved an unexpected managed output",
)
rejected = subprocess.run(
    [
        "bash",
        "-c",
        'source "$1"; jh_resolve_build_output "$2" "build_legacy" unused',
        "bash",
        str(helper),
        str(ROOT),
    ],
    check=False,
    capture_output=True,
    text=True,
)
require(rejected.returncode != 0, "shared helper accepted output outside .build")

for probe in (
    "test_target_selection.cmake",
    "test_board_selection.cmake",
    "test_network_backend_selection.cmake",
):
    text = (ROOT / "tests" / probe).read_text(encoding="utf-8")
    require(
        "jh_test_artifact_dir" in text,
        f"{probe}: compiler probes are not redirected below .build",
    )
    require(
        "CMAKE_CURRENT_BINARY_DIR" not in text,
        f"{probe}: script-mode output still depends on the caller directory",
    )

gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
require("build_*/" not in gitignore, ".gitignore hides legacy root build directories")
require("*.o" not in gitignore, ".gitignore hides misplaced object files")
require(BUILD_ROOT.name == ".build", "invalid managed build root")
