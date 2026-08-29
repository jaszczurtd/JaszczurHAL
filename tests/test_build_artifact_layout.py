#!/usr/bin/env python3
"""Validate the repository-wide managed build artifact layout."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(sys.argv[1]).resolve()
BUILD_ROOT = ROOT / ".build"
sys.path.insert(0, str(ROOT / "scripts"))
import examples_dispatcher


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


hardware_reference = (ROOT / "doc" / "api" / "en" / "03_build_tests.md").read_text(
    encoding="utf-8"
)
for fixture_dir in sorted((ROOT / "tests" / "hardware").iterdir()):
    if not fixture_dir.is_dir():
        continue
    readme_path = fixture_dir / "README.md"
    require(
        readme_path.is_file(),
        f"{fixture_dir.name}: local README link is missing",
    )
    readme = readme_path.read_text(encoding="utf-8")
    require(
        "../../../doc/api/en/03_build_tests.md#" in readme,
        f"{fixture_dir.name}: README does not link to the central fixture reference",
    )
    require(
        f"`tests/hardware/{fixture_dir.name}`" in hardware_reference,
        f"{fixture_dir.name}: fixture is missing from the central reference index",
    )


for example_dir in examples_dispatcher.selected_example_dirs([]):
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
    "bluetooth_stage1",
    "bluetooth_gamepad",
    "bluetooth_stream",
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

bluetooth_stream_manifest = load_json(
    ROOT
    / "tests"
    / "hardware"
    / "bluetooth_stream"
    / ".vscode"
    / "jaszczurhal.project.json"
)
hardware_matrix = bluetooth_stream_manifest.get("example", {}).get(
    "hardwareMatrix"
)
require(
    isinstance(hardware_matrix, list),
    "bluetooth_stream: example.hardwareMatrix is missing",
)
require(
    all(
        isinstance(entry, dict)
        and set(entry) == {"target", "board", "runtime"}
        and all(isinstance(value, str) for value in entry.values())
        for entry in hardware_matrix
    ),
    "bluetooth_stream: hardwareMatrix entries must be exact string tuples",
)
expected_bluetooth_stream_matrix = {
    ("rp2040", "picow", "baremetal"),
    ("rp2040", "picow", "freertos"),
    ("rp2040", "pico-rm2", "baremetal"),
    ("rp2040", "pico-rm2", "freertos"),
    ("rp2350-arm", "pico2w", "baremetal"),
    ("rp2350-arm", "pico2w", "freertos"),
    ("stm32g474", "nucleo-g474re-pim730", "baremetal"),
    ("stm32g474", "nucleo-g474re-pim730", "freertos"),
}
actual_bluetooth_stream_matrix = {
    (entry["target"], entry["board"], entry["runtime"])
    for entry in hardware_matrix
}
require(
    len(hardware_matrix) == len(expected_bluetooth_stream_matrix)
    and actual_bluetooth_stream_matrix == expected_bluetooth_stream_matrix,
    "bluetooth_stream: hardwareMatrix must declare exactly the eight gate images",
)

bluetooth_stream_variants = {
    variant.get("id"): variant
    for variant in bluetooth_stream_manifest.get("example", {}).get(
        "variants", []
    )
    if isinstance(variant, dict)
}
expected_display_variants = {
    "display": {
        "module": "bluetooth_stream_display",
        "defines": {
            "JHBL5_ENABLE_DISPLAY=1",
            "HAL_ENABLE_ILI9341",
            "HAL_DISPLAY_ILI9341",
        },
    },
    "display-freertos": {
        "module": "bluetooth_stream_display_freertos",
        "defines": {
            "JHBL5_ENABLE_DISPLAY=1",
            "HAL_ENABLE_ILI9341",
            "HAL_DISPLAY_ILI9341",
            "HAL_ENABLE_FREERTOS",
        },
    },
}
for variant_id, expected_variant in expected_display_variants.items():
    variant = bluetooth_stream_variants.get(variant_id)
    require(
        isinstance(variant, dict)
        and variant.get("module") == expected_variant["module"]
        and variant.get("targets") == ["stm32g474"]
        and set(variant.get("extraDefines", []))
        == expected_variant["defines"],
        f"bluetooth_stream:{variant_id} display load contract changed",
    )

from board_registry import tooling_target_registry

tooling_registry = tooling_target_registry(ROOT)
for target in ("rp2040", "rp2350-arm", "rp2350-riscv"):
    descriptor = tooling_registry[target]
    require(
        "JH_PICOTOOL_EXECUTABLE" not in descriptor.get("cache", {}),
        f"{target}: registry hardcodes a host-specific picotool executable",
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
    "cmake ninja g++ gcc make" in quality_gate,
    "runalltests.sh gate 1 does not verify the default Ninja generator",
)
require(
    'scripts/run_cpd.py --output-dir "${GATE_BUILD_ROOT}/cpd"' in quality_gate,
    "runalltests.sh does not keep CPD reports below .build/gate",
)
memcheck_gate = quality_gate.split('header "Gate 3/8', 1)[1].split(
    'header "Gate 4/8', 1
)[0]
require(
    'ctest --test-dir "${BUILD_DIR}" -N' in memcheck_gate
    and "-L '^memcheck$'" in memcheck_gate
    and "-LE '^no_memcheck$'" not in memcheck_gate
    and '-R "${memcheck_regex}"' not in memcheck_gate,
    "runalltests.sh memcheck does not select every labelled native test",
)
require(
    '| tee "${LOG_ROOT}/jh_memcheck.log"' in memcheck_gate
    and "| grep -E '(^[0-9]|Memory|passed|failed|Defects)'"
    not in memcheck_gate,
    "runalltests.sh memcheck progress is filtered or not logged live",
)
tests_cmake = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
generated_runner = (ROOT / "scripts" / "sync_generated.py").read_text(
    encoding="utf-8"
)
require(
    "DIRECTORY PROPERTY TESTS" in tests_cmake
    and "DIRECTORY PROPERTY BUILDSYSTEM_TARGETS" in tests_cmake
    and 'APPEND PROPERTY LABELS memcheck' in tests_cmake
    and "no_memcheck" not in tests_cmake,
    "native CTest executables are not automatically labelled for memcheck",
)
require(
    'scripts/sync_generated.py --write --report-file "${GENERATED_REPORT}"'
    in quality_gate,
    "runalltests.sh does not use the shared generated-artifact writer",
)
require(
    "--check-generated) CHECK_GENERATED=1" in quality_gate
    and 'scripts/sync_generated.py --check --report-file "${GENERATED_REPORT}"'
    in quality_gate,
    "runalltests.sh does not expose strict generated-artifact verification",
)
require(
    'done < "${GENERATED_REPORT}"' in quality_gate,
    "runalltests.sh does not include generated changes in its final summary",
)
require(
    '("scripts/generate_sbom.py",)' in generated_runner
    and '("scripts/generate_sbom.py", "--check")' in generated_runner,
    "shared generated-artifact runner does not refresh and verify the SBOM",
)
for duplicated_generator in (
    "scripts/generate_hal_features.py --write",
    "scripts/generate_board_config.py --boards-root boards --write-static",
    "scripts/examples_dispatcher.py generate-template",
    "scripts/examples_dispatcher.py generate",
    "scripts/vscode_library_workspace.py sync-vscode",
    "scripts/generate_sbom.py",
):
    require(
        duplicated_generator not in quality_gate,
        f"runalltests.sh bypasses the shared generated-artifact runner: "
        f"{duplicated_generator}",
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

target_artifacts = {
    "cmake/targets/rp-native.cmake": ("firmware.elf", "firmware.bin", "firmware.uf2", "firmware.hex", "firmware.map"),
    "cmake/targets/stm32g474.cmake": ("firmware.elf", "firmware.bin", "firmware.hex", "firmware.map"),
}
for recipe, artifacts in target_artifacts.items():
    text = (ROOT / recipe).read_text(encoding="utf-8")
    for artifact in artifacts:
        require(
            artifact in text,
            f"{recipe}: managed artifact layout omits {artifact}",
        )

stm32_linker_script = (ROOT / "stm32_lib" / "STM32G474RETx_FLASH.ld").read_text(
    encoding="utf-8"
)
for section in (".preinit_array", ".init_array"):
    require(
        f"{section} (READONLY)" in stm32_linker_script,
        f"STM32 linker script leaves {section} writable in the executable segment",
    )
stm32_recipe = (ROOT / "stm32_lib" / "jh_stm32g474_firmware.cmake").read_text(
    encoding="utf-8"
)
require(
    'LINK_DEPENDS "${_ldscript}"' in stm32_recipe,
    "STM32 firmware does not relink when its linker script changes",
)
require(
    "-Wl,-u,_printf_float" in stm32_recipe,
    "STM32 firmware does not enable newlib-nano floating-point formatting",
)
stm32_library = (ROOT / "stm32_lib" / "CMakeLists.txt").read_text(encoding="utf-8")
require(
    'OUTPUT_ROOT "${CMAKE_BINARY_DIR}"' in stm32_library,
    "STM32 static-library generator is not scoped to its CMake build tree",
)
require(
    'set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH' in stm32_library,
    "STM32 static-library configuration does not consume its toolchain cache value",
)

gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
require("build_*/" not in gitignore, ".gitignore hides legacy root build directories")
require("*.o" not in gitignore, ".gitignore hides misplaced object files")
require(BUILD_ROOT.name == ".build", "invalid managed build root")
