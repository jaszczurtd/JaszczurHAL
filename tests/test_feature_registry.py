#!/usr/bin/env python3
"""Effective lint of the HAL feature registry: the builds projects configure,
resolved through jh-vscode, against the frozen baseline, the C preprocessor and
the generated CMake resolver. The registry, its artifacts, resolution and the
raw lint are checked natively (tools/jh/tests/check_features.cmake,
tests/test_feature_registry.cmake, tests/test_feature_preprocessor.cmake);
this stays until the hardware resolve moves to jh."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import NamedTuple

from repo_root import hal_include_args


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--cc")
    parser.add_argument("--cmake")
    return parser.parse_args()


ARGS = parse_args()
ROOT = ARGS.root.resolve()
GENERATOR = ROOT / "scripts/generate_hal_features.py"
CONFIG = ROOT / "config"
TEST_ROOT = ROOT / ".build/tests/feature-registry"
ESP_IDF_FAKE_INCLUDE = TEST_ROOT / "esp-idf-include"
GENERATED_CMAKE = ROOT / "cmake/generated/jh_hal_features.cmake"
sys.path.insert(0, str(ROOT / "scripts"))
import generate_hal_features  # noqa: E402


def run_generator(
    *arguments: str, expected_success: bool = True
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, str(GENERATOR), *arguments, "--config-root", str(CONFIG)],
        check=False,
        capture_output=True,
        text=True,
    )
    if expected_success:
        require(result.returncode == 0, result.stderr)
    else:
        require(result.returncode != 0, "negative generator case unexpectedly passed")
    return result


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def run_cmake_script(path: Path, expected_success: bool = True) -> str:
    cmake = ARGS.cmake or shutil.which("cmake")
    require(cmake is not None, "CMake resolver test requested without cmake")
    result = subprocess.run(
        [cmake, "-P", str(path)],
        check=False,
        capture_output=True,
        text=True,
    )
    if expected_success:
        require(result.returncode == 0, result.stderr)
    else:
        require(result.returncode != 0, "negative CMake resolver case passed")
    return result.stdout + result.stderr


def preprocess_feature_set(
    compiler: str,
    header: str,
    features: tuple[str, ...],
    target: str | None,
    board: str | None,
    assembly: "AssemblyInputs | None" = None,
) -> set[str]:
    """Feature closure of ``header`` for one configuration. A device tree
    assembly brings its generated headers and board definitions instead of
    the board profile's components."""
    target_macros = {
        "rp2040": "HAL_TARGET_RP2040",
        "rp2350-arm": "HAL_TARGET_RP2350_ARM",
        "rp2350-riscv": "HAL_TARGET_RP2350_RISCV",
        "stm32g474": "HAL_TARGET_STM32G474",
        "esp32": "HAL_TARGET_ESP32",
        "esp32s3": "HAL_TARGET_ESP32_S3",
    }
    target_macro = target_macros.get(target, "HAL_TARGET_MOCK")
    compile_features = set(features)
    if target is not None:
        target_descriptor = load(ROOT / f"boards/targets/{target}.json")
        compile_features.update(
            item.removesuffix("=1")
            for item in target_descriptor.get("requiredFeatures", [])
        )
    board_macro: str | None = None
    board_defines: list[str] = []
    include_dirs: list[str] = []
    header_lines: list[str] = []
    if assembly is not None:
        board_defines = list(assembly.definitions)
        include_dirs = ["-I", str(assembly.include_dir)]
        header_lines = [f'#define JH_HARDWARE_HEADER "{assembly.header.as_posix()}"']
    elif board is not None:
        board_descriptor = load(ROOT / f"boards/profiles/{board}.json")
        board_macro = board_descriptor["hal"]["selector"]
        components = set(board_descriptor["components"])
        if "cyw43-pico-pio" in components:
            board_defines.extend(
                [
                    "HAL_NETWORK_BACKEND_CYW43",
                    "HAL_CYW43_BUS_PICO_PIO",
                    "HAL_CYW43_STACK_LWIP",
                    "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u",
                ]
            )
    source = [
        "#define HAL_EEPROM_TYPE 4",
        f"#define {target_macro} 1",
        *(
            ["#define __FREERTOS 1"]
            if "HAL_ENABLE_FREERTOS" in compile_features
            and target in {"rp2040", "rp2350-arm", "rp2350-riscv"}
            else []
        ),
        *([f"#define {board_macro} 1"] if board_macro else []),
        *(f"#define {definition.replace('=', ' ', 1)}" for definition in board_defines),
        *header_lines,
        *(f"#define {feature} 1" for feature in sorted(compile_features)),
        f'#include "{header}"',
        "",
    ]
    result = subprocess.run(
        [
            compiler,
            "-std=c11",
            "-E",
            "-dM",
            "-x",
            "c",
            *include_dirs,
            *hal_include_args(ROOT),
            "-I",
            str(ROOT / "third_party/FreeRTOS-Kernel/include"),
            "-I",
            str(ROOT / "src/hal/impl/rp2040/freertos"),
            "-I",
            str(ROOT / "src/hal/impl/stm32g474/freertos"),
            "-I",
            str(ESP_IDF_FAKE_INCLUDE),
            "-",
        ],
        input="\n".join(source),
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        result.returncode == 0,
        f"preprocessor failed for {target}/{board}/{features} through {header}:\n"
        f"{result.stderr}",
    )
    return {
        match.group(1)
        for line in result.stdout.splitlines()
        if (
            match := re.match(
                r"#define (HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)(?:\s|$)",
                line,
            )
        )
    }


class AssemblyInputs(NamedTuple):
    """What the build of one device tree assembly gives the compiler."""

    header: Path
    include_dir: Path
    definitions: tuple[str, ...]


def assembly_inputs(record: dict) -> AssemblyInputs:
    """Generate the headers of a record's assembly as the build does
    (resolve, then finalize with its variant) and read its board definitions."""
    name = "-".join(
        part.replace("/", "_")
        for part in (record["project"], record["assembly"], record["variant"] or "base")
    )
    output = TEST_ROOT / "assemblies" / name
    generator = ROOT / "scripts/generate_hardware_config.py"
    config_dir = ROOT / record["project"]
    for arguments in (
        ["resolve", "--config-dir", str(config_dir), "--assembly", record["assembly"],
         "--output-dir", str(output / "early")],
        ["finalize", "--hardware", str(output / "early/jh_hardware_resolved.json"),
         "--hardware-header", str(output / "early/jh_hardware.h"),
         "--config-dir", str(config_dir), "--output-dir", str(output / "final"),
         *(["--variant", record["variant"]] if record["variant"] else [])],
    ):
        result = subprocess.run([sys.executable, str(generator), *arguments],
                                check=False, capture_output=True, text=True)
        require(result.returncode == 0, f"{name}: {arguments[0]} failed:\n{result.stderr}")
    cmake = (output / "final/jh_hardware_config.cmake").read_text(encoding="utf-8")
    match = re.search(r'set\(JH_BOARD_COMPILE_DEFINITIONS "([^"]*)"\)', cmake)
    require(match is not None, f"{name}: no JH_BOARD_COMPILE_DEFINITIONS")
    return AssemblyInputs(
        output / "early/jh_hardware.h",
        output / "final",
        tuple(item for item in match.group(1).split(";") if item),
    )


def check_effective_matrix_parity(compiler: str, report: dict) -> None:
    if Path(compiler).stem.lower() in {"cl", "cl.exe"}:
        return
    registered = set(generate_hal_features.load_registry(CONFIG).features)
    checked: set[tuple] = set()
    for record in report["configurations"]:
        requested = tuple(record["requestedFeatures"])
        # Device tree records differ by assembly hardware as well.
        key = (record["target"], record["board"], record.get("assembly") and
               (record["project"], record["assembly"], record["variant"]), requested)
        if key in checked:
            continue
        checked.add(key)
        expected = set(record["resolvedFeatures"])
        inputs = assembly_inputs(record) if record.get("assembly") else None
        legacy = preprocess_feature_set(
            compiler,
            "hal/core/hal_config.h",
            requested,
            record["target"],
            record["board"],
            inputs,
        ) & registered
        generated = preprocess_feature_set(
            compiler,
            "hal/generated/jh_hal_features.h",
            requested,
            record["target"],
            record["board"],
            inputs,
        ) & registered
        require(
            legacy == expected,
            f"effective legacy closure differs for {key}: "
            f"legacy={sorted(legacy)}, expected={sorted(expected)}",
        )
        require(
            generated == expected,
            f"effective generated closure differs for {key}: "
            f"generated={sorted(generated)}, expected={sorted(expected)}",
        )


if TEST_ROOT.exists():
    shutil.rmtree(TEST_ROOT)
TEST_ROOT.mkdir(parents=True)
(ESP_IDF_FAKE_INCLUDE / "freertos").mkdir(parents=True)
(ESP_IDF_FAKE_INCLUDE / "freertos/FreeRTOS.h").write_text(
    "#pragma once\n", encoding="utf-8"
)

consumer = TEST_ROOT / "consumer"
(consumer / ".vscode").mkdir(parents=True)
(consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps({"target": "rp2040", "board": "pico"}), encoding="utf-8"
)

# A condition the build decides is fine; one on an identifier the build never
# passes leaves the feature undecided for every build.
(consumer / "hal_project_config.h").write_text(
    "#define JH_PROJECT_VARIANTS(X) X(PROBE, \"Probe\", ENABLE_WIFI_FOR_PROBE=1)\n"
    "#if defined(ENABLE_WIFI_FOR_PROBE) && defined(HAL_TARGET_RP2040)\n"
    "#define HAL_ENABLE_WIFI\n"
    "#endif\n",
    encoding="utf-8",
)
run_generator("--lint", "--effective", "--input-root", str(consumer))
(consumer / "hal_project_config.h").write_text(
    "#if defined(SDK_ONLY_SWITCH)\n#define HAL_ENABLE_WIFI\n#endif\n",
    encoding="utf-8",
)
conditional_lint = run_generator(
    "--lint", "--effective", "--input-root", str(consumer), expected_success=False
)
require(
    "[JH-CFG-SCOPE] HAL_ENABLE_WIFI depends on SDK_ONLY_SWITCH" in conditional_lint.stderr,
    "a feature decided by an unknown identifier passed the effective lint",
)
(consumer / "hal_project_config.h").write_text(
    "#ifndef HAL_PROJECT_CONFIG_H\n"
    "#define HAL_PROJECT_CONFIG_H\n"
    "#ifndef HAL_DISABLE_ASSERTS\n"
    "#define HAL_DISABLE_ASSERTS 1\n"
    "#endif\n"
    "#endif\n",
    encoding="utf-8",
)
run_generator("--lint", "--effective", "--input-root", str(consumer))


effective_consumer = TEST_ROOT / "effective-consumer"
(effective_consumer / ".vscode").mkdir(parents=True)
(effective_consumer / "hal_project_config.h").write_text(
    "#pragma once\n"
    "#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_RP2040) X(HAL_TARGET_STM32G474)\n"
    "#define JH_PROJECT_VARIANTS(X) \\\n"
    "    X(STREAM, \"BLE stream\", HAL_ENABLE_BLE_STREAM) \\\n"
    "    X(RP_TLS, \"TLS on RP2040\", EXAMPLE_TLS=1)\n"
    "#define HAL_ENABLE_CRC\n"
    "#define HAL_DISABLE_ASSERTS\n"
    "#if defined(HAL_TARGET_RP2040)\n"
    "#define HAL_ENABLE_MQTT\n"
    "#else\n"
    "#define HAL_ENABLE_UDP\n"
    "#endif\n"
    "#if defined(EXAMPLE_TLS)\n"
    "#if !defined(HAL_TARGET_RP2040)\n"
    "#error \"RP_TLS: RP2040 only\"\n"
    "#endif\n"
    "#define HAL_ENABLE_TLS\n"
    "#endif\n",
    encoding="utf-8",
)
(effective_consumer / ".vscode/jaszczurhal.local.json").write_text(
    json.dumps({"target": "invalid-local-target", "board": "invalid-local-board"}),
    encoding="utf-8",
)
(effective_consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps({"target": "rp2040", "board": "pico"}, indent=2) + "\n",
    encoding="utf-8",
)
effective_report_path = TEST_ROOT / "effective-report.json"
effective_run = run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(effective_consumer),
    "--resolution-output",
    str(effective_report_path),
)
require(
    "linted 5 effective feature configurations: no findings"
    in effective_run.stdout,
    "effective target/variant matrix size drifted",
)
effective_report = load(effective_report_path)
require(len(effective_report["configurations"]) == 5, "effective matrix drifted")
require(
    all(
        record["target"] != "invalid-local-target"
        for record in effective_report["configurations"]
    ),
    "gitignored local target leaked into the effective oracle",
)


def effective_record(target: str, variant: str | None) -> dict:
    return next(
        record
        for record in effective_report["configurations"]
        if record["target"] == target and record["variant"] == variant
    )


rp_base = effective_record("rp2040", None)
require(
    rp_base["requestedFeatures"]
    == ["HAL_DISABLE_ASSERTS", "HAL_ENABLE_CRC", "HAL_ENABLE_MQTT"],
    f"the target condition was not resolved effectively: {rp_base}",
)
require(
    rp_base["provenance"]["HAL_ENABLE_MQTT"] == ["hal_project_config.h:9"],
    "header provenance is missing",
)
require(
    effective_record("rp2040", "RP_TLS")["requestedFeatures"]
    == ["HAL_DISABLE_ASSERTS", "HAL_ENABLE_CRC", "HAL_ENABLE_MQTT", "HAL_ENABLE_TLS"],
    "a variant switch did not reach the header",
)
stream = effective_record("stm32g474", "STREAM")
require(
    stream["requestedFeatures"]
    == ["HAL_DISABLE_ASSERTS", "HAL_ENABLE_BLE_STREAM", "HAL_ENABLE_CRC", "HAL_ENABLE_UDP"],
    f"variant did not add to the base configuration: {stream}",
)
require(
    "HAL_ENABLE_BLE" in stream["resolvedFeatures"]
    and "HAL_ENABLE_CRYPTO" in stream["resolvedFeatures"],
    "variant closure is incomplete",
)
require(
    stream["provenance"]["HAL_ENABLE_BLE_STREAM"]
    == ["hal_project_config.h:JH_PROJECT_VARIANTS(STREAM)"],
    "variant provenance is missing",
)
second_effective_report = TEST_ROOT / "effective-report-second.json"
run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(effective_consumer),
    "--resolution-output",
    str(second_effective_report),
)
require(
    effective_report_path.read_bytes() == second_effective_report.read_bytes(),
    "effective resolution JSON is nondeterministic",
)

profile_board_consumer = TEST_ROOT / "effective-profile-board"
(profile_board_consumer / ".vscode").mkdir(parents=True)
(profile_board_consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps(
        {
            "target": "rp2040",
            "board": "pico",
            "targetProfiles": {
                "rp2040": {"board": "rp2040-zero"},
                "rp2350-arm": {"board": "pico2w"},
                "stm32g474": {},
            },
        }
    )
    + "\n",
    encoding="utf-8",
)
profile_board_report_path = TEST_ROOT / "effective-profile-board.json"
run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(profile_board_consumer),
    "--resolution-output",
    str(profile_board_report_path),
)
profile_boards = {
    record["target"]: record["board"]
    for record in load(profile_board_report_path)["configurations"]
}
require(
    profile_boards
    == {"rp2040": "rp2040-zero", "rp2350-arm": "pico2w", "stm32g474": "nucleo-g474re"},
    f"effective lint did not resolve boards like jh-vscode: {profile_boards}",
)

target_required_consumer = TEST_ROOT / "effective-target-required"
(target_required_consumer / ".vscode").mkdir(parents=True)
(target_required_consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps(
        {"target": "esp32s3", "board": "waveshare-esp32-s3-zero"}
    )
    + "\n",
    encoding="utf-8",
)
target_required_report_path = TEST_ROOT / "effective-target-required.json"
run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(target_required_consumer),
    "--resolution-output",
    str(target_required_report_path),
)
target_required_record = load(target_required_report_path)["configurations"][0]
require(
    target_required_record["requestedFeatures"] == [],
    "a target-required feature leaked into explicit requested features",
)
require(
    target_required_record["resolvedFeatures"] == ["HAL_ENABLE_FREERTOS"],
    "a target-required feature was not merged into effective resolution",
)
require(
    target_required_record["provenance"]["HAL_ENABLE_FREERTOS"]
    == ["target:esp32s3:requiredFeatures[0]"],
    "target-required feature provenance is missing",
)
(target_required_consumer / "hal_project_config.h").write_text(
    "#define HAL_ENABLE_FREERTOS\n", encoding="utf-8"
)
explicit_target_report_path = TEST_ROOT / "effective-target-explicit.json"
run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(target_required_consumer),
    "--resolution-output",
    str(explicit_target_report_path),
)
explicit_target_record = load(explicit_target_report_path)["configurations"][0]
require(
    explicit_target_record["requestedFeatures"] == ["HAL_ENABLE_FREERTOS"],
    "an explicit target-required feature was removed from requested features",
)
require(
    explicit_target_record["provenance"]["HAL_ENABLE_FREERTOS"]
    == [
        "hal_project_config.h:1",
        "target:esp32s3:requiredFeatures[0]",
    ],
    "explicit target-required feature provenance is incomplete",
)
(target_required_consumer / "hal_project_config.h").write_text(
    "#define HAL_DISABLE_FREERTOS\n", encoding="utf-8"
)
disabled_target_required = run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(target_required_consumer),
    expected_success=False,
)
require(
    "[JH-CFG-TARGET-REQUIRED] esp32s3 requires HAL_ENABLE_FREERTOS; "
    "HAL_DISABLE_FREERTOS cannot be requested"
    in disabled_target_required.stderr,
    "a disabled target-required feature was not rejected",
)

local_only_consumer = TEST_ROOT / "effective-local-state"
(local_only_consumer / ".vscode").mkdir(parents=True)
(local_only_consumer / "hal_project_config.h").write_text(
    "#define HAL_ENABLE_CRC\n", encoding="utf-8"
)
(local_only_consumer / ".vscode/jaszczurhal.project.json").write_text(
    "{}\n", encoding="utf-8"
)
(local_only_consumer / ".vscode/jaszczurhal.local.json").write_text(
    json.dumps({"target": "invalid-local-target", "board": "invalid-local-board"}),
    encoding="utf-8",
)
local_only_report_path = TEST_ROOT / "effective-local-state.json"
run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(local_only_consumer),
    "--resolution-output",
    str(local_only_report_path),
)
local_only_record = load(local_only_report_path)["configurations"][0]
require(
    local_only_record["target"] is None and local_only_record["board"] is None,
    "gitignored local target/board changed an axis-free effective oracle",
)

reference_manifest_consumer = TEST_ROOT / "effective-reference-manifest"
reference_manifest_consumer.mkdir()
(reference_manifest_consumer / "jaszczurhal.project.json").write_text(
    json.dumps({"target": "rp2040", "board": "pico"}) + "\n",
    encoding="utf-8",
)
reference_manifest_report_path = TEST_ROOT / "effective-reference-manifest.json"
reference_manifest_run = run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(reference_manifest_consumer),
    "--resolution-output",
    str(reference_manifest_report_path),
)
require(
    "linted 0 effective feature configurations: no findings"
    in reference_manifest_run.stdout,
    "a reference manifest created a synthetic effective project",
)
require(
    load(reference_manifest_report_path)["configurations"] == [],
    "a reference manifest leaked into the effective report",
)

no_request_header_consumer = TEST_ROOT / "effective-no-request-header"
no_request_header_consumer.mkdir()
(no_request_header_consumer / "hal_project_config.h").write_text(
    "#pragma once\n", encoding="utf-8"
)
no_request_header_report_path = TEST_ROOT / "effective-no-request-header.json"
no_request_header_run = run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(no_request_header_consumer),
    "--resolution-output",
    str(no_request_header_report_path),
)
require(
    "linted 0 effective feature configurations: no findings"
    in no_request_header_run.stdout,
    "a standalone header without requests created an effective configuration",
)
require(
    load(no_request_header_report_path)["configurations"] == [],
    "a standalone header without requests leaked into the effective report",
)

standalone_header_consumer = TEST_ROOT / "effective-standalone-header"
standalone_header_consumer.mkdir()
(standalone_header_consumer / "hal_project_config.h").write_text(
    "#define HAL_ENABLE_KV\n", encoding="utf-8"
)
standalone_header_report_path = TEST_ROOT / "effective-standalone-header.json"
standalone_header_run = run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(standalone_header_consumer),
    "--resolution-output",
    str(standalone_header_report_path),
)
require(
    "linted 1 effective feature configurations: no findings"
    in standalone_header_run.stdout,
    "a standalone header request did not create a direct context",
)
standalone_header_record = load(standalone_header_report_path)["configurations"][
    0
]
require(
    standalone_header_record["target"] is None
    and standalone_header_record["board"] is None
    and standalone_header_record["variant"] is None,
    "a standalone header invented target, board, or variant axes",
)
require(
    standalone_header_record["requestedFeatures"] == ["HAL_ENABLE_KV"]
    and standalone_header_record["resolvedFeatures"]
    == ["HAL_ENABLE_CRC", "HAL_ENABLE_EEPROM", "HAL_ENABLE_KV"],
    "a standalone header was not resolved as a direct context",
)
require(
    standalone_header_record["provenance"]["HAL_ENABLE_KV"]
    == ["hal_project_config.h:1"],
    "standalone-header provenance is missing",
)

duplicate_consumer = TEST_ROOT / "effective-duplicate"
(duplicate_consumer / ".vscode").mkdir(parents=True)
(duplicate_consumer / "hal_project_config.h").write_text(
    "#define JH_PROJECT_VARIANTS(X) X(AGAIN, \"Again\", HAL_ENABLE_WIFI=1)\n"
    "#define HAL_ENABLE_WIFI\n",
    encoding="utf-8",
)
(duplicate_consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps({"target": "rp2040", "board": "pico"}),
    encoding="utf-8",
)
duplicate_effective = run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(duplicate_consumer),
    expected_success=False,
)
require(
    "[JH-CFG-REDUNDANT]" in duplicate_effective.stderr,
    "effective duplicate was not diagnosed",
)

root_effective_report_path = TEST_ROOT / "root-effective-report.json"
run_generator(
    "--lint",
    "--effective",
    "--input-root",
    str(ROOT),
    "--resolution-output",
    str(root_effective_report_path),
)
root_effective_report = load(root_effective_report_path)
baseline = load(CONFIG / "effective-features-baseline.json")
require(
    len(root_effective_report["configurations"])
    == baseline["configurationCount"],
    "effective configuration inventory differs from the frozen baseline",
)
require(
    root_effective_report["matrixDigest"] == baseline["matrixDigest"],
    "effective requested/resolved matrix differs from the frozen baseline",
)
root_effective_projects = {
    record["project"] for record in root_effective_report["configurations"]
}
phase3_fixture_records = [
    record
    for record in root_effective_report["configurations"]
    if record["project"] == "tests/fixtures/esp32s3_phase3"
]
esp32_gamepad_fixture_records = [
    record
    for record in root_effective_report["configurations"]
    if record["project"] == "tests/fixtures/esp32_gamepad"
]
require(
    len(phase3_fixture_records) == 1
    and phase3_fixture_records[0]["target"] == "esp32s3"
    and phase3_fixture_records[0]["board"] == "waveshare-esp32-s3-zero"
    and "HAL_ENABLE_FREERTOS" in phase3_fixture_records[0]["resolvedFeatures"],
    "the ESP32-S3 Phase 3 fixture lost its tracked target/board contract",
)
require(
    len(esp32_gamepad_fixture_records) == 1
    and esp32_gamepad_fixture_records[0]["target"] == "esp32"
    and esp32_gamepad_fixture_records[0]["board"] == "esp32-devkitc-v4"
    and "HAL_ENABLE_BLUETOOTH_GAMEPAD"
    in esp32_gamepad_fixture_records[0]["resolvedFeatures"],
    "the ESP32 gamepad fixture lost its tracked target/board selection",
)
require(
    "vscode/examples" not in root_effective_projects,
    "the reference example manifest created an effective configuration",
)
require(
    "vscode/neutral_fw/rp_pico" not in root_effective_projects,
    "the no-request neutral-firmware header created an effective configuration",
)

# Each request repeats the header path of its source; together they outgrow a
# Windows command line (32767 characters) and here also ARG_MAX of Linux.
long_model = generate_hal_features.load_registry(CONFIG)
long_header = "/" + "p" * 24000 + "/hal_project_config.h"
long_requests = [
    generate_hal_features.FeatureRequest(symbol, None, f"{long_header}:{line}")
    for line, symbol in enumerate(
        sorted(name for name, item in long_model.features.items() if item.kind == "feature"), 1
    )
]
long_resolution = generate_hal_features.resolve_feature_requests(
    long_requests, long_model, "test:long sources"
)[0]
require(
    len(long_requests) > 90
    and all(
        long_resolution.provenance.get(request.symbol) == (request.source,)
        for request in long_requests
    ),
    "resolver lost requests with long sources",
)

if ARGS.cmake or shutil.which("cmake"):
    parity_script = TEST_ROOT / "resolve-effective.cmake"
    parity_lines = [f'include("{GENERATED_CMAKE.as_posix()}")']
    effective_sets = {}
    for record in root_effective_report["configurations"]:
        target_required = {
            feature
            for feature, sources in record["provenance"].items()
            if any(source.startswith("target:") for source in sources)
        }
        resolver_input = tuple(
            sorted({*record["requestedFeatures"], *target_required})
        )
        expected_features = tuple(record["resolvedFeatures"])
        previous = effective_sets.setdefault(resolver_input, expected_features)
        require(
            previous == expected_features,
            "equal CMake resolver inputs produced different effective closures",
        )
    for index, (resolver_input, expected_features) in enumerate(
        sorted(effective_sets.items())
    ):
        arguments = " ".join(resolver_input)
        expected = ";".join(expected_features)
        parity_lines.extend(
            [
                f"jh_hal_resolve_features(requested_{index} resolved_{index} {arguments})",
                f'if(NOT "${{requested_{index}}}" STREQUAL "{";".join(resolver_input)}")',
                f'    message(FATAL_ERROR "effective requested mismatch: ${{requested_{index}}}")',
                "endif()",
                f'if(NOT "${{resolved_{index}}}" STREQUAL "{expected}")',
                f'    message(FATAL_ERROR "effective resolved mismatch: ${{resolved_{index}}}")',
                "endif()",
            ]
        )
    parity_script.write_text("\n".join(parity_lines) + "\n", encoding="utf-8")
    run_cmake_script(parity_script)


if ARGS.cc and Path(ARGS.cc).stem.lower() not in {"cl", "cl.exe"}:
    check_effective_matrix_parity(ARGS.cc, root_effective_report)

print("feature registry tests passed")
