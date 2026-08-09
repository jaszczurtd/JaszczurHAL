#!/usr/bin/env python3
"""Unit, golden, and negative tests for declarative board generation."""

from __future__ import annotations

import copy
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

checked_static = run("--check-static")
require(
    checked_static.stdout.strip() == "verified 2 generated board artifacts",
    "tracked board artifacts are not verified deterministically",
)

validated = run("--validate-only")
require(
    validated.stdout.strip() == "validated 5 targets and 11 boards",
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
        "nucleo-g474re-pim730",
        "pico",
        "pico-rm2",
        "pico2",
        "pico2w",
        "picow",
        "rp2040-lora-lf",
        "rp2040-plus-4mb",
        "rp2040-zero",
    ],
    "board list is not deterministic",
)

first_output = TEST_ROOT / "generated/first"
second_output = TEST_ROOT / "generated/second"
first_output.mkdir(parents=True)
(first_output / "jh_board_registry.h").write_text(
    "stale per-build registry\n", encoding="utf-8"
)
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
    "--define",
    "HAL_ENABLE_WIFI",
    *feature_arguments,
)
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-zero",
    "--output-dir",
    str(second_output),
    "--define=-DHAL_ENABLE_WIFI=1",
    "--feature",
    "HAL_ENABLE_FREERTOS",
    "--feature",
    "HAL_ENABLE_WIFI=1",
)
for name in (
    "jh_board_config.cmake",
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
require(
    not (first_output / "jh_board_registry.h").exists()
    and not (second_output / "jh_board_registry.h").exists(),
    "per-build generation emitted a second board registry",
)

link_reference_text = (first_output / "jh_link_contract_reference.c").read_text(
    encoding="utf-8"
)
require(
    "__attribute__((constructor, used))" in link_reference_text,
    "generated link reference is not retained through a constructor root",
)
require(
    "retain" not in link_reference_text,
    "generated link reference still uses the unsupported retain attribute",
)

resolved = load(first_output / "jh_board_resolved.json")
expected_hash = hashlib.sha256(
    b"hal.profileId=8\nHAL_ENABLE_FREERTOS=1\n"
    b"HAL_ENABLE_NETWORK_CORE=1\nHAL_ENABLE_WIFI=1"
).hexdigest()[:12]
require(
    resolved["requestedFeatures"]
    == ["HAL_ENABLE_FREERTOS", "HAL_ENABLE_WIFI"],
    "requested feature set is not explicit and deterministic",
)
require(
    resolved["resolvedFeatures"]
    == [
        "HAL_ENABLE_FREERTOS",
        "HAL_ENABLE_NETWORK_CORE",
        "HAL_ENABLE_WIFI",
    ],
    "resolved feature closure is incorrect",
)
require(
    resolved["features"] == resolved["resolvedFeatures"],
    "features compatibility alias differs from resolvedFeatures",
)
expected_resolved_digest = hashlib.sha256(
    b"HAL_ENABLE_FREERTOS\nHAL_ENABLE_NETWORK_CORE\nHAL_ENABLE_WIFI"
).hexdigest()
require(
    resolved["resolvedFeaturesDigest"] == expected_resolved_digest,
    "resolved feature digest golden mismatch",
)
require(resolved["featureHash"] == expected_hash, "featureHash golden mismatch")
require(
    resolved["contractSymbol"]
    == f"jh_board_contract_rp2040_rp2040_zero_{expected_hash}",
    "contract symbol golden mismatch",
)
generated_cmake = (first_output / "jh_board_config.cmake").read_text(
    encoding="utf-8"
)
require(
    'set(JH_BOARD_REQUESTED_FEATURES "HAL_ENABLE_FREERTOS;HAL_ENABLE_WIFI")'
    in generated_cmake,
    "generated CMake lacks requested features",
)
require(
    'set(JH_BOARD_RESOLVED_FEATURES "HAL_ENABLE_FREERTOS;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_WIFI")'
    in generated_cmake,
    "generated CMake lacks resolved features",
)
require(
    f'set(JH_BOARD_RESOLVED_FEATURES_DIGEST "{expected_resolved_digest}")'
    in generated_cmake,
    "generated CMake lacks the resolved feature digest",
)
require(
    'set(JH_BOARD_FEATURES "HAL_ENABLE_FREERTOS;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_WIFI")'
    in generated_cmake,
    "generated CMake compatibility alias differs from resolved features",
)

minimal_closure_output = TEST_ROOT / "generated/mqtt-minimal"
redundant_closure_output = TEST_ROOT / "generated/mqtt-redundant"
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-zero",
    "--output-dir",
    str(minimal_closure_output),
    "--requested-feature",
    "HAL_ENABLE_MQTT",
)
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-zero",
    "--output-dir",
    str(redundant_closure_output),
    "--feature",
    "HAL_ENABLE_MQTT",
    "--requested-feature",
    "HAL_ENABLE_TCP",
    "--requested-feature",
    "HAL_ENABLE_WIFI",
)
minimal_closure = load(minimal_closure_output / "jh_board_resolved.json")
redundant_closure = load(redundant_closure_output / "jh_board_resolved.json")
require(
    minimal_closure["requestedFeatures"] == ["HAL_ENABLE_MQTT"],
    "minimal MQTT request was not retained",
)
require(
    redundant_closure["requestedFeatures"]
    == ["HAL_ENABLE_MQTT", "HAL_ENABLE_TCP", "HAL_ENABLE_WIFI"],
    "redundant MQTT request set was not retained",
)
require(
    minimal_closure["resolvedFeatures"]
    == redundant_closure["resolvedFeatures"],
    "equivalent MQTT requests produced different closures",
)
require(
    minimal_closure["features"] == minimal_closure["resolvedFeatures"]
    and redundant_closure["features"]
    == redundant_closure["resolvedFeatures"],
    "features alias is not the resolved closure",
)
require(
    minimal_closure["featureHash"] == redundant_closure["featureHash"],
    "equivalent MQTT closures produced different feature hashes",
)
require(
    minimal_closure["contractSymbol"]
    == redundant_closure["contractSymbol"],
    "equivalent MQTT closures produced different link contracts",
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

lora_output = TEST_ROOT / "generated/rp2040-lora-lf"
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-lora-lf",
    "--output-dir",
    str(lora_output),
)
lora_resolved = load(lora_output / "jh_board_resolved.json")
lora_config = (lora_output / "jh_board_config.h").read_text(encoding="utf-8")
require(lora_resolved["profileId"] == 10, "LoRa board profile ID changed")
require(
    lora_resolved["components"] == ["rp-native", "sx126x-radio"],
    "LoRa board component contract changed",
)
require(
    lora_resolved["capabilities"]["sx1262-radio"]["present"] is True,
    "LoRa board lost its SX1262 capability",
)
require(
    lora_resolved["gpio"]["reservations"]["lora-radio"]["strength"]
    == "hard",
    "LoRa board wiring is not hard-reserved",
)
for expected in (
    "#define HAL_BOARD_PROFILE_RP2040_LORA_LF 1",
    "#define HAL_BOARD_IS_RP2040_LORA_LF 1",
    "#define HAL_BOARD_DECLARED_CAPABILITIES UINT32_C(0x00000009)",
    "#define HAL_BOARD_LORA_RADIO_PRESENT 1",
    "#define HAL_BOARD_LORA_RADIO_SPI_BUS 1u",
    "#define HAL_BOARD_LORA_RADIO_PIN_CS 13u",
    "#define HAL_BOARD_LORA_RADIO_PIN_DIO1 16u",
    "#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_A 17u",
    "#define HAL_BOARD_LORA_RADIO_PIN_BUSY 18u",
    "#define HAL_BOARD_LORA_RADIO_PIN_RESET 23u",
    "#define HAL_BOARD_LORA_RADIO_PIN_MISO 24u",
    "#define HAL_BOARD_LORA_RADIO_MAX_FREQUENCY_HZ UINT32_C(450000000)",
    "#define HAL_BOARD_LORA_RADIO_MAX_SPI_CLOCK_HZ UINT32_C(17999999)",
    "#define HAL_BOARD_LORA_RADIO_DEFAULT_SPI_CLOCK_HZ UINT32_C(8000000)",
    "#define HAL_BOARD_LORA_RADIO_REGULATOR_IS_DCDC 1",
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_DIO2_SINGLE_GPIO 1",
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_TX_LEVEL_A 0",
    "#define HAL_BOARD_LORA_RADIO_TCXO_CONTROL_IS_DIO3 1",
    "#define HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_1V7 1",
    "#define HAL_BOARD_LORA_RADIO_TCXO_STARTUP_US UINT32_C(5000)",
):
    require(expected in lora_config, f"LoRa board config lacks {expected!r}")
require(
    'set(PICO_BOARD "pico")'
    in (lora_output / "jh_board_config.cmake").read_text(encoding="utf-8"),
    "LoRa board must use the compatible Pico SDK board definition",
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

pim730_output = TEST_ROOT / "generated/nucleo-pim730"
run(
    "--target",
    "stm32g474",
    "--board",
    "nucleo-g474re-pim730",
    "--output-dir",
    str(pim730_output),
)
pim730_resolved = load(pim730_output / "jh_board_resolved.json")
require(
    pim730_resolved["components"]
    == ["btstack-ble", "cyw43-lwip", "cyw43-stm32-gspi", "stm32g474-native"],
    "NUCLEO PIM730 component set mismatch",
)
require(
    pim730_resolved["capabilities"]["cyw43"]["present"] is True,
    "NUCLEO PIM730 lost its CYW43 capability",
)
require(
    pim730_resolved["capabilities"]["bluetooth-controller"]["present"] is True,
    "NUCLEO PIM730 lost its Bluetooth controller capability",
)
expected_pim730_definitions = [
    "HAL_NETWORK_BACKEND_CYW43",
    "HAL_CYW43_BUS_STM32_GSPI",
    "HAL_CYW43_STACK_LWIP",
    "HAL_CYW43_PIN_WL_ON=30u",
    "HAL_CYW43_PIN_CHIP_SELECT=28u",
    "HAL_CYW43_PIN_DATA=31u",
    "HAL_CYW43_PIN_CLOCK=29u",
    "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u",
]
require(
    pim730_resolved["boardCompileDefinitions"]
    == expected_pim730_definitions,
    "NUCLEO PIM730 resolved JSON lost provider compile definitions",
)
pim730_cmake = (pim730_output / "jh_board_config.cmake").read_text(
    encoding="utf-8"
)
for expected_define in expected_pim730_definitions:
    require(expected_define in pim730_cmake, f"missing {expected_define}")
pim730_header = (pim730_output / "jh_board_config.h").read_text(
    encoding="utf-8"
)
for expected_define in expected_pim730_definitions:
    name, separator, value = expected_define.partition("=")
    expected_header_define = f"#define {name} {value if separator else '1'}"
    require(
        expected_header_define in pim730_header,
        f"installed board header would omit {expected_header_define}",
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


def macro_dump(include_dirs: list[Path], definitions: list[str]) -> dict[str, str]:
    result = subprocess.run(
        [
            compiler,
            "-std=c17",
            "-dM",
            "-E",
            "-x",
            "c",
            *(argument for path in include_dirs for argument in ("-I", str(path))),
            *(f"-D{definition}" for definition in definitions),
            "-",
        ],
        input='#include "hal/hal_board.h"\n',
        check=False,
        capture_output=True,
        text=True,
    )
    require(result.returncode == 0, result.stderr)
    macros: dict[str, str] = {}
    for line in result.stdout.splitlines():
        match = re.fullmatch(r"#define ([A-Za-z0-9_]+)(?: (.*))?", line)
        if match:
            macros[match.group(1)] = match.group(2) or ""
    return macros


def board_contract_macros(macros: dict[str, str]) -> dict[str, str]:
    prefixes = (
        "HAL_BOARD_",
        "HAL_CYW43_PROFILE_",
        "HAL_NETWORK_BACKEND_",
        "HAL_CYW43_BUS_",
        "HAL_CYW43_STACK_",
        "HAL_CYW43_PIN_",
        "HAL_CYW43_MAX_",
    )
    selected = {
        name: value
        for name, value in macros.items()
        if name.startswith(prefixes) or name in {"HAL_LED_BUILTIN", "LED_BUILTIN"}
    }
    selected.pop("HAL_BOARD_PROFILE_TARGET", None)
    return selected


target_descriptors = {
    path.stem: load(path) for path in sorted((BOARDS / "targets").glob("*.json"))
}
board_descriptors = {
    path.stem: load(path) for path in sorted((BOARDS / "profiles").glob("*.json"))
}
for board_id, descriptor in board_descriptors.items():
    for target_id in descriptor["compatibleTargets"]:
        parity_output = TEST_ROOT / "generated/parity" / target_id / board_id
        run(
            "--target",
            target_id,
            "--board",
            board_id,
            "--output-dir",
            str(parity_output),
        )
        target_selector = target_descriptors[target_id]["hal"]["targetSelector"]
        board_selector = descriptor["hal"]["selector"]
        generated_macros = board_contract_macros(
            macro_dump([parity_output, ROOT / "src"], [f"{target_selector}=1"])
        )
        fallback_macros = board_contract_macros(
            macro_dump(
                [ROOT / "src"],
                [f"{target_selector}=1", f"{board_selector}=1"],
            )
        )
        require(
            fallback_macros == generated_macros,
            f"fallback differs from generated config for {target_id}/{board_id}",
        )

link_root = TEST_ROOT / "link-contract"
link_root.mkdir(parents=True)
main_source = link_root / "main.c"
main_source.write_text("int main(void) { return 0; }\n", encoding="utf-8")


def compile_object(source: Path, output: Path, include: Path) -> None:
    subprocess.run(
        [
            compiler,
            "-std=c17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-ffunction-sections",
            "-fdata-sections",
            "-I",
            str(include),
            "-c",
            str(source),
            "-o",
            str(output),
        ],
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
        "-Wl,--gc-sections",
        str(main_object),
        str(reference_object),
        str(matching_archive),
        "-o",
        str(link_root / "matching"),
    ],
    check=True,
)

missing_contract_link = subprocess.run(
    [
        compiler,
        *(["-nostdlib", "-Wl,-e,main"] if cross_link else []),
        "-Wl,--gc-sections",
        str(main_object),
        str(reference_object),
        "-o",
        str(link_root / "missing-contract"),
    ],
    check=False,
    capture_output=True,
    text=True,
)
require(
    missing_contract_link.returncode != 0,
    "missing board contract linked successfully with section GC enabled",
)
require(
    resolved["contractSymbol"] in missing_contract_link.stderr,
    "missing-contract link failure does not name the expected contract symbol",
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
        "-Wl,--gc-sections",
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
    [
        compiler,
        *(["-nostdlib", "-Wl,-e,main"] if cross_link else []),
        "-Wl,--gc-sections",
        str(main_object),
        str(reference_object),
        str(different_feature_archive),
        "-o",
        str(link_root / "different-feature"),
    ],
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

for option, value in (
    ("--feature", "HAL_ENABLE_WIFI=0"),
    ("--feature", "HAL_ENABLE_WIFI=2"),
    ("--define", "HAL_ENABLE_WIFI=0"),
    ("--define", "HAL_ENABLE_WIFI=2"),
    ("--define", "$<1:HAL_$<1:ENABLE>_WIFI=0>"),
):
    invalid_value = run(
        "--target",
        "rp2040",
        "--board",
        "pico",
        "--output-dir",
        str(TEST_ROOT / "negative/feature-value"),
        option,
        value,
        expected_success=False,
    )
    require(
        "[JH-CFG-VALUE]" in invalid_value.stderr,
        f"{option} {value} lacks the stable value diagnostic",
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
duplicate_auto_detect = mutate(
    "duplicate-auto-detect",
    "profiles/picow.json",
    lambda value: value["hal"].update(
        autoDetectSelectors=["RASPBERRYPI_PICO"]
    ),
)
require(
    "globally unique auto-detection selector"
    in run(
        "--validate-only",
        boards_root=duplicate_auto_detect,
        expected_success=False,
    ).stderr,
    "duplicate auto-detection selector was not rejected",
)
ambiguous_legacy_selector = mutate(
    "ambiguous-legacy-selector",
    "profiles/pico-rm2.json",
    lambda value: value["hal"].update(
        legacySelectors=["HAL_CYW43_PROFILE_PICOW"]
    ),
)
require(
    "an unambiguous selector for target 'rp2040'"
    in run(
        "--validate-only",
        boards_root=ambiguous_legacy_selector,
        expected_success=False,
    ).stderr,
    "target-ambiguous legacy selector was not rejected",
)
invalid_source_fallback = mutate(
    "invalid-source-fallback",
    "targets/rp2040.json",
    lambda value: value.update(sourceFallbackBoard="pico2"),
)
require(
    "$.sourceFallbackBoard"
    in run(
        "--validate-only",
        boards_root=invalid_source_fallback,
        expected_success=False,
    ).stderr,
    "incompatible source fallback board was not rejected",
)
missing_cyw43_component = mutate(
    "missing-cyw43-component",
    "profiles/nucleo-g474re-pim730.json",
    lambda value: value["components"].pop("cyw43-lwip"),
)
require(
    "CYW43 components"
    in run(
        "--validate-only",
        boards_root=missing_cyw43_component,
        expected_success=False,
    ).stderr,
    "CYW43 capability without its complete provider was not rejected",
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

# Typed multi-pin bus devices. rp2040-zero gains a synthetic SX1262 so the role
# model is covered before any board profile ships one.
RADIO_PINS = {
    "sck": 14,
    "mosi": 15,
    "miso": 24,
    "cs": 13,
    "reset": 23,
    "busy": 18,
    "dio1": 19,
    "rfSwitchA": 17,
}
RADIO_DEVICE = {
    "kind": "bus-device",
    "role": "sx1262-radio",
    "bus": {"kind": "spi", "index": 1},
    "signals": {
        name: {"domain": "soc-gpio", "id": pin} for name, pin in RADIO_PINS.items()
    },
    "attributes": {
        "minFrequencyHz": 410000000,
        "maxFrequencyHz": 525000000,
        "maxSpiClockHz": 16000000,
        "defaultSpiClockHz": 8000000,
        "minTxPowerDbm": -9,
        "maxTxPowerDbm": 22,
        "regulator": "dcdc",
        "rfSwitchMode": "single-gpio",
        "rfSwitchIdleLevelA": True,
        "rfSwitchRxLevelA": True,
        "rfSwitchTxLevelA": False,
        "tcxoControl": "dio3",
        "tcxoVoltage": "1v8",
        "tcxoStartupUs": 5000,
    },
}


def radio_fixture(case: str, adjust=None, reserve: bool = True) -> Path:
    device = copy.deepcopy(RADIO_DEVICE)
    pins = sorted(set(RADIO_PINS.values()) | {20})
    if adjust is not None:
        adjust(device)

    def apply(value: dict) -> None:
        if reserve:
            value["gpio"]["reservations"]["lora-radio"] = {
                "pins": [{"domain": "soc-gpio", "id": pin} for pin in pins],
                "owner": "board.lora-radio",
                "strength": "hard",
                "reason": "Synthetic SX1262 radio wiring under test.",
            }
        value["devices"]["loraRadio"] = device

    return mutate(case, "profiles/rp2040-zero.json", apply)


radio_root = radio_fixture("radio-valid")
run("--validate-only", boards_root=radio_root)
radio_output = TEST_ROOT / "fixtures/.build/radio"
run(
    "--target",
    "rp2040",
    "--board",
    "rp2040-zero",
    "--output-dir",
    str(radio_output),
    boards_root=radio_root,
)
radio_header = (radio_output / "jh_board_config.h").read_text(encoding="utf-8")
for expected in (
    "#define HAL_BOARD_DEVICE_PIN_NONE 0xFFu",
    "#define HAL_BOARD_LORA_RADIO_PRESENT 1",
    "#define HAL_BOARD_LORA_RADIO_SPI_BUS 1u",
    "#define HAL_BOARD_LORA_RADIO_PIN_CS 13u",
    "#define HAL_BOARD_LORA_RADIO_PIN_DIO1 19u",
    "#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_A 17u",
    "#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_B HAL_BOARD_DEVICE_PIN_NONE",
    "#define HAL_BOARD_LORA_RADIO_MIN_FREQUENCY_HZ UINT32_C(410000000)",
    "#define HAL_BOARD_LORA_RADIO_MIN_TX_POWER_DBM (-9)",
    "#define HAL_BOARD_LORA_RADIO_DEFAULT_SPI_CLOCK_HZ UINT32_C(8000000)",
    "#define HAL_BOARD_LORA_RADIO_REGULATOR_IS_DCDC 1",
    "#define HAL_BOARD_LORA_RADIO_REGULATOR_IS_LDO 0",
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_SINGLE_GPIO 1",
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_DUAL_GPIO 0",
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_TX_LEVEL_A 0",
    "#define HAL_BOARD_LORA_RADIO_TCXO_CONTROL_IS_DIO3 1",
    "#define HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_1V8 1",
    "#define HAL_BOARD_LORA_RADIO_TCXO_STARTUP_US UINT32_C(5000)",
):
    require(expected in radio_header, f"generated board config lacks {expected!r}")
require(
    load(radio_output / "jh_board_resolved.json")["devices"]["loraRadio"]
    == RADIO_DEVICE,
    "resolved JSON lost the bus-device descriptor",
)


def radio_negative(case: str, adjust, diagnostic: str, reserve: bool = True) -> None:
    stderr = run(
        "--validate-only",
        boards_root=radio_fixture(case, adjust, reserve),
        expected_success=False,
    ).stderr
    require(
        diagnostic in stderr,
        f"{case} diagnostic lacks {diagnostic!r}; got {stderr.strip()!r}",
    )


def drop_switch_levels(device: dict) -> None:
    for suffix in ("IdleLevelA", "RxLevelA", "TxLevelA"):
        device["attributes"].pop(f"rfSwitch{suffix}")


radio_negative(
    "radio-missing-signal",
    lambda device: device["signals"].pop("busy"),
    "$.devices.loraRadio.signals",
)
radio_negative(
    "radio-unknown-signal",
    lambda device: device["signals"].update(
        dio9={"domain": "soc-gpio", "id": 20}
    ),
    "$.devices.loraRadio.signals",
)
radio_negative(
    "radio-duplicate-pin",
    lambda device: device["signals"].update(
        dio1={"domain": "soc-gpio", "id": 13}
    ),
    "a pin not already used by cs",
)
radio_negative(
    "radio-unreserved-pin",
    None,
    "a pin covered by a hard gpio reservation",
    reserve=False,
)
radio_negative(
    "radio-unordered-frequency",
    lambda device: device["attributes"].update(maxFrequencyHz=400000000),
    "$.devices.loraRadio.attributes.minFrequencyHz",
)
radio_negative(
    "radio-clock-above-max",
    lambda device: device["attributes"].update(defaultSpiClockHz=20000000),
    "$.devices.loraRadio.attributes.defaultSpiClockHz",
)
radio_negative(
    "radio-missing-attribute",
    lambda device: device["attributes"].pop("regulator"),
    "required sx1262-radio attributes",
)
radio_negative(
    "radio-unknown-attribute",
    lambda device: device["attributes"].update(bogus=1),
    "only attributes required by the active sx1262-radio configuration",
)
radio_negative(
    "radio-bad-enum",
    lambda device: device["attributes"].update(regulator="buck"),
    "$.devices.loraRadio.attributes.regulator",
)
radio_negative(
    "radio-int8-range",
    lambda device: device["attributes"].update(maxTxPowerDbm=500),
    "an int8 integer in [-128, 127]",
)
radio_negative(
    "radio-bool-type",
    lambda device: device["attributes"].update(rfSwitchTxLevelA=1),
    "a boolean",
)
radio_negative(
    "radio-unknown-role",
    lambda device: device.update(role="sx1276-radio"),
    "a known device role",
)
radio_negative(
    "radio-bad-bus-kind",
    lambda device: device["bus"].update(kind="i2c"),
    "$.devices.loraRadio.bus.kind",
)
radio_negative(
    "radio-unknown-kind",
    lambda device: device.update(kind="spi-thing"),
    "$.devices.loraRadio.kind",
)
non_camel_device = mutate(
    "radio-non-camel-id",
    "profiles/rp2040-zero.json",
    lambda value: value["devices"].update(
        lora_radio={"kind": "gpio", "endpoint": {"domain": "soc-gpio", "id": 20}}
    ),
)
require(
    "a camelCase device ID"
    in run(
        "--validate-only", boards_root=non_camel_device, expected_success=False
    ).stderr,
    "non-camelCase device ID was not diagnosed",
)
radio_negative(
    "radio-switch-signal-without-mode",
    lambda device: (
        device["attributes"].update(rfSwitchMode="dio2"),
        drop_switch_levels(device),
    ),
    "only signals required by the active sx1262-radio configuration",
)
radio_negative(
    "radio-tcxo-none-keeps-voltage",
    lambda device: device["attributes"].update(tcxoControl="none"),
    "only attributes required by the active sx1262-radio configuration",
)
radio_negative(
    "radio-tcxo-dio3-without-voltage",
    lambda device: device["attributes"].pop("tcxoVoltage"),
    "required sx1262-radio attributes",
)

for case, adjust in (
    (
        "radio-dio2-switch",
        lambda device: (
            device["attributes"].update(rfSwitchMode="dio2"),
            drop_switch_levels(device),
            device["signals"].pop("rfSwitchA"),
        ),
    ),
    (
        "radio-dio2-single-gpio-switch",
        lambda device: device["attributes"].update(
            rfSwitchMode="dio2-single-gpio"
        ),
    ),
    (
        "radio-dual-gpio-switch",
        lambda device: (
            device["attributes"].update(
                rfSwitchMode="dual-gpio",
                rfSwitchIdleLevelB=False,
                rfSwitchRxLevelB=True,
                rfSwitchTxLevelB=False,
            ),
            device["signals"].update(rfSwitchB={"domain": "soc-gpio", "id": 20}),
        ),
    ),
    (
        "radio-tcxo-none",
        lambda device: (
            device["attributes"].update(tcxoControl="none"),
            device["attributes"].pop("tcxoVoltage"),
            device["attributes"].pop("tcxoStartupUs"),
        ),
    ),
):
    run("--validate-only", boards_root=radio_fixture(case, adjust))

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
            provider
            in re.search(
                rf"set\(JH_BOARD_COMPONENT_{component_key}_PROVIDERS\s+([^)]*)\)",
                cmake_registry_text,
            ).group(1),
            f"cmake registry lost provider {provider} for {component_id}",
        )
    require(
        f"set(JH_BOARD_COMPONENT_{component_key}_SLOT \"{entry['slot']}\")"
        in cmake_registry_text,
        f"cmake registry lost the slot of {component_id}",
    )
