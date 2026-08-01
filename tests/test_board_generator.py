#!/usr/bin/env python3
"""Unit, golden, and negative tests for declarative board generation."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(sys.argv[1]).resolve()
GENERATOR = ROOT / "scripts/generate_board_config.py"
BOARDS = ROOT / "boards"
TEST_ROOT = ROOT / ".build/tests/board-generator"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(
    *arguments: str,
    boards_root: Path = BOARDS,
    expected_success: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--boards-root",
            str(boards_root),
            *arguments,
        ],
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


def mutate(
    case: str,
    relative_path: str,
    callback,
) -> Path:
    destination = TEST_ROOT / "fixtures" / case
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(BOARDS, destination)
    path = destination / relative_path
    value = load(path)
    callback(value)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    return destination


if TEST_ROOT.exists():
    shutil.rmtree(TEST_ROOT)
TEST_ROOT.mkdir(parents=True)

validated = run("--validate-only")
require(
    validated.stdout.strip() == "validated 5 targets and 9 boards",
    "unexpected validated registry size",
)
require(
    run("--list", "targets").stdout.splitlines()
    == ["mock", "rp2040", "rp2350-arm", "rp2350-riscv", "stm32g474"],
    "target list is not deterministic",
)
require(
    run("--list", "boards").stdout.splitlines()
    == [
        "host-mock",
        "nucleo-g474re",
        "pico",
        "pico-rm2",
        "pico2",
        "pico2w",
        "picow",
        "rp2040-plus-4mb",
        "rp2040-zero",
    ],
    "board list is not deterministic",
)

first_output = TEST_ROOT / "generated/first"
second_output = TEST_ROOT / "generated/second"
feature_arguments = [
    "--feature",
    "HAL_ENABLE_WIFI",
    "--feature",
    "HAL_ENABLE_FREERTOS=1",
]
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-zero",
    "--output-dir",
    str(first_output),
    *feature_arguments,
)
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-zero",
    "--output-dir",
    str(second_output),
    "--feature",
    "HAL_ENABLE_FREERTOS",
    "--feature",
    "HAL_ENABLE_WIFI=1",
)
for name in (
    "jh_board_config.cmake",
    "jh_board_registry.h",
    "jh_board_config.h",
    "jh_board_resolved.json",
    "jh_link_contract.h",
    "jh_link_contract_definition.c",
    "jh_link_contract_reference.c",
):
    require(
        (first_output / name).read_bytes() == (second_output / name).read_bytes(),
        f"{name} is not deterministic for equivalent feature sets",
    )

resolved = load(first_output / "jh_board_resolved.json")
expected_hash = hashlib.sha256(
    b"hal.profileId=8\nHAL_ENABLE_FREERTOS=1\nHAL_ENABLE_WIFI=1"
).hexdigest()[:12]
require(resolved["featureHash"] == expected_hash, "featureHash golden mismatch")
require(
    resolved["contractSymbol"]
    == f"jh_board_contract_rp2040_rp2040_zero_{expected_hash}",
    "contract symbol golden mismatch",
)
config = (first_output / "jh_board_config.h").read_text(encoding="utf-8")
require("HAL_BOARD_STATUS_LED_KIND_ADDRESSABLE 1" in config, "missing WS2812 fact")
require("HAL_BOARD_STATUS_LED_PIN 16u" in config, "incorrect WS2812 pin")
require("HAL_LED_BUILTIN" not in config, "RP2040-Zero exposes a GPIO LED")

plus_output = TEST_ROOT / "generated/plus"
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-plus-4mb",
    "--output-dir",
    str(plus_output),
)
require(
    load(plus_output / "jh_board_resolved.json")["flashBytes"] == 4194304,
    "RP2040-Plus flash golden mismatch",
)
require(
    "#define HAL_LED_BUILTIN HAL_BOARD_STATUS_LED_PIN"
    in (plus_output / "jh_board_config.h").read_text(encoding="utf-8"),
    "GPIO status LED must provide HAL_LED_BUILTIN",
)

picow_output = TEST_ROOT / "generated/picow"
run(
    "--target",
    "rp2040",
    "--board",
    "picow",
    "--output-dir",
    str(picow_output),
)
require(
    "#define HAL_LED_BUILTIN HAL_BOARD_STATUS_LED_PIN"
    in (picow_output / "jh_board_config.h").read_text(encoding="utf-8"),
    "component GPIO status LED must provide HAL_LED_BUILTIN",
)

nucleo_output = TEST_ROOT / "generated/nucleo"
run(
    "--target",
    "stm32g474",
    "--board",
    "nucleo-g474re",
    "--output-dir",
    str(nucleo_output),
)
require(
    "#define HAL_BOARD_STATUS_LED_PIN 5u"
    in (nucleo_output / "jh_board_config.h").read_text(encoding="utf-8"),
    "NUCLEO PA5 HAL encoding mismatch",
)

compiler = shutil.which("cc")
archiver = shutil.which("ar")
cross_link = False
if (compiler is None or archiver is None) and sys.platform == "win32":
    host_state_path = ROOT / ".build/windows/host-environment.json"
    if host_state_path.is_file():
        host_state = load(host_state_path)
        arm_gcc = Path(str(host_state.get("tools", {}).get("gnu-arm", "")))
        arm_ar = arm_gcc.with_name("arm-none-eabi-ar.exe")
        if arm_gcc.is_file() and arm_ar.is_file():
            compiler = str(arm_gcc)
            archiver = str(arm_ar)
            cross_link = True
require(compiler is not None and archiver is not None, "host C toolchain missing")
link_root = TEST_ROOT / "link-contract"
link_root.mkdir(parents=True)
main_source = link_root / "main.c"
main_source.write_text("int main(void) { return 0; }\n", encoding="utf-8")


def compile_object(source: Path, output: Path, include: Path) -> None:
    subprocess.run(
        [compiler, "-std=c17", "-Wall", "-Wextra", "-Werror", "-I", str(include), "-c", str(source), "-o", str(output)],
        check=True,
    )


definition_object = link_root / "definition.o"
reference_object = link_root / "reference.o"
main_object = link_root / "main.o"
compile_object(first_output / "jh_link_contract_definition.c", definition_object, first_output)
compile_object(first_output / "jh_link_contract_reference.c", reference_object, first_output)
compile_object(main_source, main_object, first_output)
matching_archive = link_root / "libmatching.a"
subprocess.run([archiver, "rcs", str(matching_archive), str(definition_object)], check=True)
subprocess.run(
    [
        compiler,
        *(["-nostdlib", "-Wl,-e,main"] if cross_link else []),
        str(main_object),
        str(reference_object),
        str(matching_archive),
        "-o",
        str(link_root / "matching"),
    ],
    check=True,
)

wrong_definition = link_root / "wrong-definition.o"
compile_object(
    plus_output / "jh_link_contract_definition.c",
    wrong_definition,
    plus_output,
)
wrong_archive = link_root / "libwrong-board.a"
subprocess.run([archiver, "rcs", str(wrong_archive), str(wrong_definition)], check=True)
wrong_link = subprocess.run(
    [
        compiler,
        *(["-nostdlib", "-Wl,-e,main"] if cross_link else []),
        str(main_object),
        str(reference_object),
        str(wrong_archive),
        "-o",
        str(link_root / "wrong-board"),
    ],
    check=False,
    capture_output=True,
    text=True,
)
require(wrong_link.returncode != 0, "wrong-board archive linked successfully")
require(
    resolved["contractSymbol"] in wrong_link.stderr,
    "wrong-board link failure does not name the expected contract symbol",
)

different_feature_output = TEST_ROOT / "generated/different-feature"
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-zero",
    "--output-dir",
    str(different_feature_output),
    "--feature",
    "HAL_ENABLE_WIFI",
)
different_feature_object = link_root / "different-feature.o"
compile_object(
    different_feature_output / "jh_link_contract_definition.c",
    different_feature_object,
    different_feature_output,
)
different_feature_archive = link_root / "libdifferent-feature.a"
subprocess.run(
    [archiver, "rcs", str(different_feature_archive), str(different_feature_object)],
    check=True,
)
different_feature_link = subprocess.run(
    [compiler, str(main_object), str(reference_object), str(different_feature_archive), "-o", str(link_root / "different-feature")],
    check=False,
    capture_output=True,
)
require(
    different_feature_link.returncode != 0,
    "archive with a different feature set linked successfully",
)

run(
    "--target",
    "rp2040",
    "--board",
    "unknown",
    "--output-dir",
    str(TEST_ROOT / "negative/unknown"),
    expected_success=False,
)
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-zero",
    "--output-dir",
    str(TEST_ROOT / "negative/selector-conflict"),
    "--define",
    "HAL_BOARD_PROFILE_RP_PICO",
    expected_success=False,
)
run(
    "--target",
    "stm32g474",
    "--board",
    "pico",
    "--output-dir",
    str(TEST_ROOT / "negative/mismatch"),
    expected_success=False,
)
run(
    "--target",
    "rp2350-riscv",
    "--board",
    "pico2w",
    "--output-dir",
    str(TEST_ROOT / "negative/unsupported"),
    expected_success=False,
)
run(
    "--target",
    "rp2040",
    "--board",
    "pico",
    "--output-dir",
    str(ROOT / "generated-outside-build"),
    expected_success=False,
)
with tempfile.TemporaryDirectory(prefix="jh-board-generator-") as temporary:
    external_root = Path(temporary) / ".build"
    external_output = external_root / "cmake/generated"
    run(
        "--target",
        "rp2040",
        "--board",
        "rp2040-zero",
        "--output-root",
        str(external_root),
        "--output-dir",
        str(external_output),
    )
    require(
        (external_output / "jh_board_config.cmake").is_file(),
        "explicit external consumer .build root was rejected",
    )
    run(
        "--target",
        "rp2040",
        "--board",
        "rp2040-zero",
        "--output-root",
        temporary,
        "--output-dir",
        str(Path(temporary) / "generated"),
        expected_success=False,
    )
    managed_host_root = Path(temporary) / "short-host-root"
    previous_host_root = os.environ.get("JH_MANAGED_BUILD_ROOT")
    os.environ["JH_MANAGED_BUILD_ROOT"] = str(managed_host_root)
    try:
        run(
            "--target",
            "rp2040",
            "--board",
            "rp2040-zero",
            "--output-root",
            str(managed_host_root / "project/cmake"),
            "--output-dir",
            str(managed_host_root / "project/cmake/generated"),
        )
    finally:
        if previous_host_root is None:
            os.environ.pop("JH_MANAGED_BUILD_ROOT", None)
        else:
            os.environ["JH_MANAGED_BUILD_ROOT"] = previous_host_root
run(
    "--target",
    "rp2040",
    "--board",
    "pico",
    "--output-dir",
    str(TEST_ROOT / "negative/feature"),
    "--feature",
    "PROJECT_SECRET=value",
    expected_success=False,
)

duplicate_id = mutate(
    "duplicate-id",
    "profiles/rp2040-zero.json",
    lambda value: value["hal"].update(profileId=1),
)
require(
    "$.hal.profileId" in run("--validate-only", boards_root=duplicate_id, expected_success=False).stderr,
    "duplicate profile ID diagnostic lacks JSON path",
)
unknown_field = mutate(
    "unknown-field",
    "profiles/pico.json",
    lambda value: value.update(typo=True),
)
require(
    "only fields" in run("--validate-only", boards_root=unknown_field, expected_success=False).stderr,
    "unknown field was not diagnosed",
)
invalid_pin = mutate(
    "invalid-pin",
    "profiles/rp2040-zero.json",
    lambda value: value["gpio"]["reservations"]["status-led"]["pins"][0].update(
        id=30
    ),
)
require(
    "target valid pin" in run("--validate-only", boards_root=invalid_pin, expected_success=False).stderr,
    "invalid endpoint pin was not diagnosed",
)
unknown_capability = mutate(
    "unknown-capability",
    "profiles/pico.json",
    lambda value: value["capabilities"].update({"unknown-radio": {"present": True}}),
)
require(
    "known capability IDs"
    in run("--validate-only", boards_root=unknown_capability, expected_success=False).stderr,
    "unknown capability was not diagnosed",
)
unknown_component = mutate(
    "unknown-component",
    "profiles/pico.json",
    lambda value: value["components"].update({"raw-source": {"mode": "board-owned"}}),
)
require(
    "known component ID"
    in run("--validate-only", boards_root=unknown_component, expected_success=False).stderr,
    "unknown component was not diagnosed",
)
flash_mismatch = mutate(
    "flash-mismatch",
    "profiles/rp2040-plus-4mb.json",
    lambda value: value["memory"]["flash"].update(expectedBytes=2097152),
)
result = run("--validate-only", boards_root=flash_mismatch)
require(result.returncode == 0, "descriptor-only validation should not guess SDK facts")

pico_output = TEST_ROOT / "generated/drift-pico"
rm2_output = TEST_ROOT / "generated/drift-pico-rm2"
run("--target", "rp2040", "--board", "pico", "--output-dir", str(pico_output))
run("--target", "rp2040", "--board", "pico-rm2", "--output-dir", str(rm2_output))
pico_resolved = load(pico_output / "jh_board_resolved.json")
rm2_resolved = load(rm2_output / "jh_board_resolved.json")
for section in ("reservations", "aliases"):
    for entry_id, entry in pico_resolved["gpio"][section].items():
        require(
            rm2_resolved["gpio"][section].get(entry_id) == entry,
            f"pico-rm2 drifted from pico: gpio.{section}.{entry_id}",
        )
for device_id, device in pico_resolved["devices"].items():
    require(
        rm2_resolved["devices"].get(device_id) == device,
        f"pico-rm2 drifted from pico: devices.{device_id}",
    )
require(
    rm2_resolved["capabilities"]["external-radio-frontend"]["present"] is True,
    "pico-rm2 lost its external radio frontend capability",
)

sys.path.insert(0, str(ROOT / "scripts"))
import generate_board_config  # noqa: E402

cmake_registry_text = (ROOT / "cmake/jh_board_components.cmake").read_text(
    encoding="utf-8"
)
cmake_ids_match = re.search(
    r"set\(JH_BOARD_COMPONENT_IDS\s+([^)]*)\)", cmake_registry_text
)
require(cmake_ids_match is not None, "JH_BOARD_COMPONENT_IDS missing from registry")
cmake_component_ids = set(cmake_ids_match.group(1).split())
generator_registry = generate_board_config.COMPONENT_REGISTRY
require(
    cmake_component_ids == set(generator_registry),
    "cmake component registry drifted from the generator registry",
)
for component_id, entry in generator_registry.items():
    component_key = component_id.replace("-", "_")
    for provider in entry["providers"]:
        require(
            f"set(JH_BOARD_COMPONENT_{component_key}_PROVIDERS \"{provider}\")"
            in cmake_registry_text,
            f"cmake registry lost provider {provider} for {component_id}",
        )
    require(
        f"set(JH_BOARD_COMPONENT_{component_key}_SLOT \"{entry['slot']}\")"
        in cmake_registry_text,
        f"cmake registry lost the slot of {component_id}",
    )
