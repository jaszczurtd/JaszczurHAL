#!/usr/bin/env python3
"""Unit, golden, lint, and parity tests for the HAL feature registry."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys


OUTPUTS = (
    Path("src/hal/generated/jh_hal_features.h"),
    Path("cmake/generated/jh_hal_features.cmake"),
)


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
sys.path.insert(0, str(ROOT / "scripts"))
import generate_hal_features  # noqa: E402


def run_generator(
    *arguments: str,
    config_root: Path = CONFIG,
    output_root: Path = ROOT,
    expected_success: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            *arguments,
            "--config-root",
            str(config_root),
            "--output-root",
            str(output_root),
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


def mutate_registry(case: str, callback) -> Path:
    destination = TEST_ROOT / "fixtures" / case
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(CONFIG, destination)
    callback(destination)
    return destination


def update_json(path: Path, callback) -> None:
    value = load(path)
    callback(value)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def require_failure(case: str, config_root: Path, expected: str) -> None:
    result = run_generator(
        "--write",
        config_root=config_root,
        output_root=TEST_ROOT / "negative" / case,
        expected_success=False,
    )
    require(expected in result.stderr, f"{case} did not report {expected!r}")


def reverse_registry_order(destination: Path) -> None:
    for path in sorted((destination / "features").glob("*.json")):
        document = load(path)
        symbols = {}
        for name, record in reversed(list(document["symbols"].items())):
            record = dict(reversed(list(record.items())))
            for relation in ("implies", "requires", "conflicts"):
                if relation in record:
                    record[relation] = list(reversed(record[relation]))
            symbols[name] = record
        document["symbols"] = symbols
        path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


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


def preprocess_feature_macros(
    compiler: str, header: str, feature: str, extra_features: tuple[str, ...]
) -> set[str]:
    target = (
        "HAL_TARGET_STM32G474"
        if feature == "HAL_ENABLE_STM32G474_FDCAN"
        else "HAL_TARGET_MOCK"
    )
    source = [
        "#define HAL_EEPROM_TYPE 4",
        f"#define {target} 1",
        f"#define {feature} 1",
        *(f"#define {item} 1" for item in extra_features),
        f'#include "{header}"',
        "",
    ]
    result = subprocess.run(
        [compiler, "-std=c11", "-E", "-dM", "-x", "c", "-I", str(ROOT / "src"), "-"],
        input="\n".join(source),
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        result.returncode == 0,
        f"preprocessor failed for {feature} through {header}:\n{result.stderr}",
    )
    return {
        match.group(1)
        for line in result.stdout.splitlines()
        if (match := re.match(r"#define (HAL_ENABLE_[A-Z0-9_]+)(?:\s|$)", line))
    }


def check_macro_dump_parity(compiler: str) -> None:
    if Path(compiler).stem.lower() in {"cl", "cl.exe"}:
        return
    model = generate_hal_features.load_registry(CONFIG)
    registered = set(model.features)
    for name, feature in sorted(model.features.items()):
        if not feature.implies:
            continue
        if name == "HAL_ENABLE_TFT":
            support = ("HAL_ENABLE_ILI9341",)
        elif name == "HAL_ENABLE_LORA_LINK":
            support = ("HAL_ENABLE_SX126X",)
        else:
            support = ()
        legacy = preprocess_feature_macros(
            compiler, "hal/core/hal_config.h", name, support
        ) & registered
        generated = preprocess_feature_macros(
            compiler, "hal/generated/jh_hal_features.h", name, support
        ) & registered
        require(
            legacy == generated,
            f"macro closure differs for {name}: legacy={sorted(legacy)}, "
            f"generated={sorted(generated)}",
        )


def preprocess_feature_set(
    compiler: str,
    header: str,
    features: tuple[str, ...],
    target: str | None,
    board: str | None,
) -> set[str]:
    target_macros = {
        "rp2040": "HAL_TARGET_RP2040",
        "rp2350-arm": "HAL_TARGET_RP2350_ARM",
        "rp2350-riscv": "HAL_TARGET_RP2350_RISCV",
        "stm32g474": "HAL_TARGET_STM32G474",
    }
    target_macro = target_macros.get(target, "HAL_TARGET_MOCK")
    board_macro: str | None = None
    board_defines: list[str] = []
    if board is not None:
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
        if "cyw43-stm32-gspi" in components:
            board_defines.extend(
                [
                    "HAL_NETWORK_BACKEND_CYW43",
                    "HAL_CYW43_BUS_STM32_GSPI",
                    "HAL_CYW43_STACK_LWIP",
                    "HAL_CYW43_PIN_WL_ON=30u",
                    "HAL_CYW43_PIN_CHIP_SELECT=28u",
                    "HAL_CYW43_PIN_DATA=31u",
                    "HAL_CYW43_PIN_CLOCK=29u",
                    "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u",
                ]
            )
    source = [
        "#define HAL_EEPROM_TYPE 4",
        f"#define {target_macro} 1",
        *(
            ["#define __FREERTOS 1"]
            if "HAL_ENABLE_FREERTOS" in features
            and target in {"rp2040", "rp2350-arm", "rp2350-riscv"}
            else []
        ),
        *([f"#define {board_macro} 1"] if board_macro else []),
        *(f"#define {definition.replace('=', ' ', 1)}" for definition in board_defines),
        *(f"#define {feature} 1" for feature in features),
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
            "-I",
            str(ROOT / "src"),
            "-I",
            str(ROOT / "third_party/FreeRTOS-Kernel/include"),
            "-I",
            str(ROOT / "src/hal/impl/rp2040/freertos"),
            "-I",
            str(ROOT / "src/hal/impl/stm32g474/freertos"),
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


def check_effective_matrix_parity(compiler: str, report: dict) -> None:
    if Path(compiler).stem.lower() in {"cl", "cl.exe"}:
        return
    registered = set(generate_hal_features.load_registry(CONFIG).features)
    checked: set[tuple[str | None, str | None, tuple[str, ...]]] = set()
    for record in report["configurations"]:
        requested = tuple(record["requestedFeatures"])
        key = (record["target"], record["board"], requested)
        if key in checked:
            continue
        checked.add(key)
        expected = set(record["resolvedFeatures"])
        legacy = preprocess_feature_set(
            compiler,
            "hal/core/hal_config.h",
            requested,
            record["target"],
            record["board"],
        ) & registered
        generated = preprocess_feature_set(
            compiler,
            "hal/generated/jh_hal_features.h",
            requested,
            record["target"],
            record["board"],
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


def check_direct_compiler_zero_presence(compiler: str) -> None:
    source = "\n".join(
        [
            "#define HAL_TARGET_MOCK 1",
            "#define HAL_ENABLE_TIME 0",
            '#include "hal/core/hal_config.h"',
            "",
        ]
    )
    result = subprocess.run(
        [
            compiler,
            "-std=c11",
            "-E",
            "-dM",
            "-x",
            "c",
            "-I",
            str(ROOT / "src"),
            "-",
        ],
        input=source,
        check=False,
        capture_output=True,
        text=True,
    )
    require(result.returncode == 0, result.stderr)
    macros = result.stdout.splitlines()
    require(
        "#define HAL_ENABLE_TIME 0" in macros,
        "direct compiler changed the unsupported =0 spelling",
    )
    for implied in ("HAL_ENABLE_UDP", "HAL_ENABLE_WIFI"):
        require(
            any(line.startswith(f"#define {implied}") for line in macros),
            f"direct compiler no longer applies presence semantics to {implied}",
        )


def preprocess_hal_config_features(
    compiler: str, definitions: tuple[str, ...]
) -> set[str]:
    source = [
        *(f"#define {definition.replace('=', ' ', 1)}" for definition in definitions),
        '#include "hal/core/hal_config.h"',
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
            "-I",
            str(ROOT / "src"),
            "-",
        ],
        input="\n".join(source),
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        result.returncode == 0,
        f"hal_config.h preprocessor probe failed for {definitions}:\n{result.stderr}",
    )
    return {
        match.group(1)
        for line in result.stdout.splitlines()
        if (match := re.match(r"#define (HAL_ENABLE_[A-Z0-9_]+)(?:\s|$)", line))
    }


def preprocess_hal_config_verbose(
    compiler: str, definitions: tuple[str, ...]
) -> str:
    source = [
        "#define HAL_CONFIG_VERBOSE 1",
        *(f"#define {definition.replace('=', ' ', 1)}" for definition in definitions),
        '#include "hal/core/hal_config.h"',
        "",
    ]
    result = subprocess.run(
        [
            compiler,
            "-std=c11",
            "-E",
            "-x",
            "c",
            "-I",
            str(ROOT / "src"),
            "-",
        ],
        input="\n".join(source),
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        result.returncode == 0,
        f"verbose hal_config.h probe failed for {definitions}:\n{result.stderr}",
    )
    return result.stdout + result.stderr


def require_hal_config_failure(
    compiler: str, definitions: tuple[str, ...], expected: str
) -> None:
    source = [
        *(f"#define {definition.replace('=', ' ', 1)}" for definition in definitions),
        '#include "hal/core/hal_config.h"',
        "",
    ]
    result = subprocess.run(
        [
            compiler,
            "-std=c11",
            "-E",
            "-x",
            "c",
            "-I",
            str(ROOT / "src"),
            "-",
        ],
        input="\n".join(source),
        check=False,
        capture_output=True,
        text=True,
    )
    require(result.returncode != 0, "negative hal_config.h probe unexpectedly passed")
    require(expected in result.stderr, f"hal_config.h did not report {expected!r}")


def check_production_feature_facade(compiler: str) -> None:
    stream = preprocess_hal_config_features(
        compiler, ("HAL_TARGET_MOCK=1", "HAL_ENABLE_BLE_STREAM=1")
    )
    require(
        {"HAL_ENABLE_BLE_STREAM", "HAL_ENABLE_BLE", "HAL_ENABLE_CRYPTO"}
        <= stream,
        "hal_config.h did not expose generated BLE Stream closure",
    )

    sx126x = preprocess_hal_config_features(
        compiler, ("HAL_TARGET_MOCK=1", "HAL_ENABLE_SX126X=1")
    )
    require(
        {"HAL_ENABLE_SX126X", "HAL_ENABLE_LORA", "HAL_ENABLE_SPI"} <= sx126x,
        "hal_config.h did not expose the SX126x provider closure",
    )
    sx127x = preprocess_hal_config_features(
        compiler, ("HAL_TARGET_MOCK=1", "HAL_ENABLE_SX127X=1")
    )
    require(
        {"HAL_ENABLE_SX127X", "HAL_ENABLE_LORA", "HAL_ENABLE_SPI"} <= sx127x,
        "hal_config.h did not expose the SX127x provider closure",
    )
    lora_link = preprocess_hal_config_features(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_SX126X=1",
            "HAL_ENABLE_LORA_LINK=1",
        ),
    )
    require(
        {"HAL_ENABLE_LORA_LINK", "HAL_ENABLE_LORA", "HAL_ENABLE_CRC"}
        <= lora_link,
        "hal_config.h did not expose the reliable LoRa link closure",
    )
    require_hal_config_failure(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_SX126X=1",
            "HAL_ENABLE_SX127X=1",
        ),
        "HAL_ENABLE_SX126X conflicts with HAL_ENABLE_SX127X",
    )
    require_hal_config_failure(
        compiler,
        ("HAL_TARGET_MOCK=1", "HAL_ENABLE_LORA=1"),
        "HAL_ENABLE_LORA requires exactly one provider",
    )
    require_hal_config_failure(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_SX126X=1",
            "HAL_LORA_RADIO_MAX_INSTANCES=0",
        ),
        "HAL_LORA_RADIO_MAX_INSTANCES must be in range 1..255",
    )
    require_hal_config_failure(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_SX126X=1",
            "HAL_ENABLE_LORA_LINK=1",
            "HAL_LORA_LINK_MAX_INSTANCES=0",
        ),
        "HAL_LORA_LINK_MAX_INSTANCES must be in range 1..255",
    )
    require_hal_config_failure(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_SX126X=1",
            "HAL_ENABLE_LORA_LINK=1",
            "HAL_LORA_LINK_MAX_MESSAGE_SIZE=4097",
        ),
        "HAL_LORA_LINK_MAX_MESSAGE_SIZE must be in range 1..4096",
    )
    require_hal_config_failure(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_SX126X=1",
            "HAL_ENABLE_LORA_LINK=1",
            "HAL_LORA_LINK_MAX_PEERS=33",
        ),
        "HAL_LORA_LINK_MAX_PEERS must be in range 1..32",
    )
    require_hal_config_failure(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_SX126X=1",
            "HAL_LORA_SX126X_BUSY_TIMEOUT_MS=0",
        ),
        "HAL_LORA_SX126X_BUSY_TIMEOUT_MS must be in range 1..60000",
    )
    require_hal_config_failure(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_SX126X=1",
            "HAL_LORA_SX126X_BUSY_TIMEOUT_MS=60001",
        ),
        "HAL_LORA_SX126X_BUSY_TIMEOUT_MS must be in range 1..60000",
    )
    require_hal_config_failure(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_SX126X=1",
            "HAL_LORA_RADIO_MAX_INSTANCES=256",
        ),
        "HAL_LORA_RADIO_MAX_INSTANCES must be in range 1..255",
    )

    at24 = preprocess_hal_config_features(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_EEPROM_TYPE=1",
            "HAL_ENABLE_EEPROM=1",
        ),
    )
    require(
        "HAL_ENABLE_I2C" in at24,
        "AT24C256 EEPROM residual no longer enables I2C",
    )
    flash = preprocess_hal_config_features(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_EEPROM_TYPE=4",
            "HAL_ENABLE_EEPROM=1",
        ),
    )
    require(
        "HAL_ENABLE_I2C" not in flash,
        "flash EEPROM unexpectedly enables I2C",
    )

    gps_default = preprocess_hal_config_features(
        compiler, ("HAL_TARGET_MOCK=1", "HAL_ENABLE_GPS=1")
    )
    require(
        "HAL_ENABLE_UART" in gps_default,
        "GPS residual no longer defaults to UART",
    )
    gps_swserial = preprocess_hal_config_features(
        compiler,
        (
            "HAL_TARGET_MOCK=1",
            "HAL_ENABLE_GPS=1",
            "HAL_ENABLE_SWSERIAL=1",
        ),
    )
    require(
        "HAL_ENABLE_UART" not in gps_swserial,
        "GPS with SWSERIAL unexpectedly enables UART",
    )

    verbose_gps = preprocess_hal_config_verbose(
        compiler, ("HAL_TARGET_MOCK=1", "HAL_ENABLE_GPS=1")
    )
    for feature in ("HAL_ENABLE_GPS", "HAL_ENABLE_UART"):
        require(
            f"HAL_CONFIG: {feature}" in verbose_gps,
            f"generated verbose report omitted {feature} after GPS residual",
        )

    verbose_stream = preprocess_hal_config_verbose(
        compiler, ("HAL_TARGET_MOCK=1", "HAL_ENABLE_BLE_STREAM=1")
    )
    for feature in (
        "HAL_ENABLE_BLE_STREAM",
        "HAL_ENABLE_BLE",
        "HAL_ENABLE_CRYPTO",
    ):
        require(
            f"HAL_CONFIG: {feature}" in verbose_stream,
            f"generated verbose report omitted resolved feature {feature}",
        )


def preprocess_generated_header(
    compiler: str,
    header: Path,
    features: tuple[str, ...],
    expected_success: bool,
) -> subprocess.CompletedProcess[str]:
    source = [
        *(f"#define {feature} 1" for feature in features),
        f'#include "{header.as_posix()}"',
        "",
    ]
    result = subprocess.run(
        [compiler, "-std=c11", "-E", "-x", "c", "-"],
        input="\n".join(source),
        check=False,
        capture_output=True,
        text=True,
    )
    if expected_success:
        require(result.returncode == 0, result.stderr)
    else:
        require(result.returncode != 0, "negative preprocessor case unexpectedly passed")
    return result


if TEST_ROOT.exists():
    shutil.rmtree(TEST_ROOT)
TEST_ROOT.mkdir(parents=True)

model = generate_hal_features.load_registry(CONFIG)
require(len(model.features) == 99, "feature registry symbol count drifted")
require(
    sum(bool(feature.implies) for feature in model.features.values()) == 62,
    "feature registry implies-source count drifted",
)
require(
    sum(len(feature.implies) for feature in model.features.values()) == 114,
    "feature registry direct-edge count drifted",
)
require(
    model.features["HAL_ENABLE_LORA"].implies == (),
    "LoRa facade must remain provider-neutral",
)
require(
    model.features["HAL_ENABLE_SX126X"].implies
    == ("HAL_ENABLE_LORA", "HAL_ENABLE_SPI"),
    "SX126x provider dependencies drifted",
)
require(
    model.features["HAL_ENABLE_SX127X"].implies
    == ("HAL_ENABLE_LORA", "HAL_ENABLE_SPI"),
    "SX127x provider dependencies drifted",
)
require(
    model.features["HAL_ENABLE_LORA_LINK"].implies
    == ("HAL_ENABLE_CRC", "HAL_ENABLE_LORA"),
    "reliable LoRa link dependencies drifted",
)
source_symbols: set[str] = set()
for source_path in (ROOT / "src/hal").rglob("*"):
    if not source_path.is_file() or "generated" in source_path.parts:
        continue
    if source_path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
        continue
    source_symbols.update(
        re.findall(
            r"\bHAL_ENABLE_[A-Z0-9_]+\b",
            source_path.read_text(encoding="utf-8", errors="replace"),
        )
    )
require(
    source_symbols <= set(model.features),
    "HAL sources use a feature symbol outside the registry",
)

hal_config_text = (ROOT / "src/hal/core/hal_config.h").read_text(encoding="utf-8")
require(
    re.search(
        r'#include "hal/system/hal_board\.h"[^\n]*\n'
        r"#define JH_HAL_FEATURE_VERBOSE_REPORT_DEFERRED 1\n"
        r'#include "hal/generated/jh_hal_features\.h"',
        hal_config_text,
    )
    is not None,
    "hal_config.h must include the generated feature header after hal_board.h",
)
require(
    hal_config_text.count('#include "hal/generated/jh_hal_features.h"') == 2,
    "hal_config.h must include generated features once for closure and once for report",
)
require(
    "#pragma message" not in hal_config_text,
    "hal_config.h must not maintain a manual verbose feature report",
)
residual_definitions = re.findall(
    r"^#define (HAL_ENABLE_[A-Z0-9_]+)(?:\s|$)",
    hal_config_text,
    flags=re.MULTILINE,
)
require(
    residual_definitions == ["HAL_ENABLE_I2C", "HAL_ENABLE_UART"],
    "hal_config.h feature definitions must contain only EEPROM/I2C and GPS/UART "
    f"residuals, found {residual_definitions}",
)
require(
    hal_config_text.count("residual outside feature registry v1") == 2,
    "hal_config.h residual feature rules must remain explicitly marked",
)
facade_provider_checks = (
    "HAL_ENABLE_RTC",
    "HAL_ENABLE_CELLULAR_MODEM",
    "HAL_ENABLE_LORA",
    "HAL_ENABLE_THERMOCOUPLE",
    "HAL_ENABLE_CAN",
    "HAL_ENABLE_DIGIPOT",
    "HAL_ENABLE_GPS",
    "HAL_ENABLE_DISPLAY",
    "HAL_ENABLE_TFT",
)
for facade in facade_provider_checks:
    require(
        f"#if defined({facade})" in hal_config_text,
        f"hal_config.h lost the provider check for {facade}",
    )
require(
    len(re.findall(r"^#error(?:\s|$)", hal_config_text, flags=re.MULTILINE))
    == 54,
    "hal_config.h retained validation inventory drifted from 54 #error checks",
)

checked = run_generator("--check")
require("verified 2 generated feature artifacts" in checked.stdout, "check summary drift")
generated_header_text = (ROOT / OUTPUTS[0]).read_text(encoding="utf-8")
verbose_report_symbols = re.findall(
    r'^#pragma message\("HAL_CONFIG: (HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)"\)$',
    generated_header_text,
    flags=re.MULTILINE,
)
require(
    verbose_report_symbols == sorted(model.features),
    "generated verbose report differs from the complete feature registry",
)

first_output = TEST_ROOT / "generated/first"
second_output = TEST_ROOT / "generated/second"
run_generator("--write", output_root=first_output)
if os.name != "nt":
    require(
        all(
            stat.S_IMODE((first_output / path).stat().st_mode) == 0o644
            for path in OUTPUTS
        ),
        "generated artifacts must use mode 0644",
    )
mtimes = {path: (first_output / path).stat().st_mtime_ns for path in OUTPUTS}
run_generator("--write", output_root=first_output)
require(
    mtimes == {path: (first_output / path).stat().st_mtime_ns for path in OUTPUTS},
    "no-op generation changed an artifact mtime",
)

reordered = mutate_registry("reordered", reverse_registry_order)
run_generator("--write", config_root=reordered, output_root=second_output)
for relative_path in OUTPUTS:
    require(
        (first_output / relative_path).read_bytes()
        == (second_output / relative_path).read_bytes(),
        f"{relative_path} depends on JSON key or relation order",
    )

header = (first_output / OUTPUTS[0]).read_text(encoding="utf-8")
require("Resolved implications of HAL_ENABLE_BLE_STREAM" in header, "missing BLE closure")
require("#define HAL_ENABLE_CRYPTO 1" in header, "missing BLE crypto implication")
require("Resolved implications of HAL_ENABLE_MQTT" in header, "missing MQTT closure")
require("[JH-CFG-DERIVED]" in header, "derived header rule missing")
require(
    model.schema_digest in header
    and model.schema_digest
    in (first_output / OUTPUTS[1]).read_text(encoding="utf-8"),
    "generated artifacts do not freeze the schema digest",
)


def change_schema_title(root: Path) -> None:
    update_json(
        root / "features.schema.json",
        lambda value: value.update({"title": "Changed feature schema title"}),
    )


schema_changed = mutate_registry("schema-digest", change_schema_title)
schema_changed_output = TEST_ROOT / "generated/schema-digest"
run_generator(
    "--write", config_root=schema_changed, output_root=schema_changed_output
)
require(
    (schema_changed_output / OUTPUTS[0]).read_bytes()
    != (first_output / OUTPUTS[0]).read_bytes(),
    "schema drift did not change generated oracle metadata",
)

stale_output = TEST_ROOT / "generated/stale"
shutil.copytree(first_output, stale_output)
(stale_output / OUTPUTS[0]).write_text("stale\n", encoding="utf-8")
stale = run_generator("--check", output_root=stale_output, expected_success=False)
require(stale.returncode == 1, "stale artifact must use drift exit code 1")
require("stale generated artifact" in stale.stderr, "stale artifact was not named")
(stale_output / OUTPUTS[1]).unlink()
missing = run_generator("--check", output_root=stale_output, expected_success=False)
require(missing.returncode == 1, "missing artifact must use drift exit code 1")
require("missing generated artifact" in missing.stderr, "missing artifact was not named")


def unknown_relation(root: Path) -> None:
    update_json(
        root / "features/connectivity.json",
        lambda value: value["symbols"]["HAL_ENABLE_MQTT"]["implies"].append(
            "HAL_ENABLE_UNKNOWN"
        ),
    )


def self_relation(root: Path) -> None:
    update_json(
        root / "features/core.json",
        lambda value: value["symbols"]["HAL_ENABLE_APP_TASK1"].update(
            {"implies": ["HAL_ENABLE_APP_TASK1"]}
        ),
    )


def cycle_relation(root: Path) -> None:
    def mutate(value: dict) -> None:
        value["symbols"]["HAL_ENABLE_APP_TASK1"]["implies"] = ["HAL_ENABLE_CRC"]
        value["symbols"]["HAL_ENABLE_CRC"]["implies"] = ["HAL_ENABLE_APP_TASK1"]

    update_json(root / "features/core.json", mutate)


def duplicate_symbol(root: Path) -> None:
    update_json(
        root / "features/buses.json",
        lambda value: value["symbols"].update({"HAL_ENABLE_APP_TASK1": {}}),
    )


def unknown_field(root: Path) -> None:
    update_json(
        root / "features/core.json",
        lambda value: value["symbols"]["HAL_ENABLE_APP_TASK1"].update(
            {"type": "boolean"}
        ),
    )


def boolean_schema_version(root: Path) -> None:
    update_json(
        root / "features/core.json",
        lambda value: value.update({"schemaVersion": True}),
    )


def asymmetric_conflict(root: Path) -> None:
    update_json(
        root / "features/core.json",
        lambda value: value["symbols"]["HAL_ENABLE_APP_TASK1"].update(
            {"conflicts": ["HAL_ENABLE_CRC"]}
        ),
    )


def closure_conflict(root: Path) -> None:
    def mutate(value: dict) -> None:
        value["symbols"]["HAL_ENABLE_APP_TASK1"].update(
            {
                "implies": ["HAL_ENABLE_CRC"],
                "conflicts": ["HAL_ENABLE_CRC"],
            }
        )
        value["symbols"]["HAL_ENABLE_CRC"]["conflicts"] = [
            "HAL_ENABLE_APP_TASK1"
        ]

    update_json(root / "features/core.json", mutate)


for case, callback, expected in (
    ("unknown", unknown_relation, "unknown symbol 'HAL_ENABLE_UNKNOWN'"),
    ("self", self_relation, "cannot reference itself"),
    ("cycle", cycle_relation, "implies cycle"),
    ("duplicate", duplicate_symbol, "duplicate symbol"),
    ("field", unknown_field, "only fields"),
    ("boolean-version", boolean_schema_version, "the integer 1"),
    ("asymmetric", asymmetric_conflict, "symmetric conflict"),
    ("closure-conflict", closure_conflict, "resolves to conflicting symbols"),
):
    require_failure(case, mutate_registry(case, callback), expected)


def valid_constraints(root: Path) -> None:
    def mutate(value: dict) -> None:
        value["symbols"]["HAL_ENABLE_APP_TASK1"]["requires"] = ["HAL_ENABLE_CRC"]
        value["symbols"]["HAL_ENABLE_JPEG"]["conflicts"] = ["HAL_ENABLE_PNG"]
        value["symbols"]["HAL_ENABLE_PNG"]["conflicts"] = ["HAL_ENABLE_JPEG"]

    update_json(root / "features/core.json", mutate)


constraints = mutate_registry("constraints", valid_constraints)
constraints_output = TEST_ROOT / "generated/constraints"
run_generator(
    "--write", config_root=constraints, output_root=constraints_output
)
constraints_header = (constraints_output / OUTPUTS[0]).read_text(encoding="utf-8")
require("[JH-CFG-REQUIRES]" in constraints_header, "requires header rule missing")
require("[JH-CFG-CONFLICT]" in constraints_header, "conflict header rule missing")

consumer = TEST_ROOT / "consumer"
(consumer / ".vscode").mkdir(parents=True)
(consumer / "hal_project_config.h").write_text(
    "#pragma once\n#define HAL_ENABLE_MQTT\n#define HAL_ENABLE_BLE_STREAM 1\n",
    encoding="utf-8",
)
(consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps({"cmake": {"cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_I2C"}}}),
    encoding="utf-8",
)
run_generator("--lint", "--input-root", str(consumer))

(consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps(
        {
            "cmake": {
                "cache": {
                    "JH_EXTRA_DEFINES": "-DHAL_ENABLE_UNKNOWN;-DHAL_ENABLE_WIFI=0"
                }
            }
        }
    ),
    encoding="utf-8",
)
semicolon_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require(
    "[JH-CFG-UNKNOWN]" in semicolon_lint.stderr
    and "[JH-CFG-VALUE]" in semicolon_lint.stderr,
    "semicolon-separated CMake definitions were not linted independently",
)
(consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps({"cmake": {"cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_WIFI="}}}),
    encoding="utf-8",
)
empty_assignment_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require(
    "[JH-CFG-VALUE]" in empty_assignment_lint.stderr,
    "empty feature assignment was accepted by raw manifest lint",
)
(consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps(
        {
            "cmake": {
                "cache": {
                    "JH_EXTRA_DEFINES": "$<1:HAL_$<1:ENABLE>_WIFI=0>"
                }
            }
        }
    ),
    encoding="utf-8",
)
genex_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require(
    "[JH-CFG-VALUE]" in genex_lint.stderr,
    "generator-expression feature bypassed raw manifest lint",
)
(consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps({"cmake": {"cache": {"HAL_ENABLE_WIFI": 0}}}),
    encoding="utf-8",
)
direct_cache_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require(
    "[JH-CFG-VALUE]" in direct_cache_lint.stderr,
    "direct CMake feature cache value bypassed raw manifest lint",
)
(consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps({"cmake": {"cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_I2C"}}}),
    encoding="utf-8",
)

second_consumer = TEST_ROOT / "consumer-second"
second_consumer.mkdir()
(second_consumer / "hal_project_config.h").write_text(
    "#define HAL_ENABLE_UNKNOWN\n", encoding="utf-8"
)
multiple_roots = run_generator(
    "--lint",
    "--input-root",
    str(consumer),
    "--input-root",
    str(second_consumer),
    expected_success=False,
)
require(
    str(second_consumer) in multiple_roots.stderr,
    "repeatable --input-root did not inspect the second root",
)

(consumer / "hal_project_config.h").write_text(
    "#define HAL_ENABLE_UNKNOWN\n", encoding="utf-8"
)
unknown_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require("[JH-CFG-UNKNOWN]" in unknown_lint.stderr, "unknown lint ID missing")

(consumer / "hal_project_config.h").write_text(
    "#define HAL_COMMENT_MARKER \"/*\"\n"
    "// another marker /*\n"
    "#define HAL_ENABLE_WIFI /* value follows\n"
    "the multiline comment */ 0\n",
    encoding="utf-8",
)
zero_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require("[JH-CFG-VALUE]" in zero_lint.stderr, "zero-value lint ID missing")
report_only = run_generator(
    "--lint", "--report-only", "--input-root", str(consumer)
)
require("warning:" in report_only.stderr, "report-only lint did not warn")

(consumer / "hal_project_config.h").write_text(
    "#define \\\n HAL_ENABLE_WIFI 0\n", encoding="utf-8"
)
spliced_zero_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require(
    "[JH-CFG-VALUE]" in spliced_zero_lint.stderr,
    "phase-2-spliced zero feature was accepted by raw lint",
)

(consumer / "hal_project_config.h").write_text(
    "// hidden by a continued line comment \\\n"
    "#define HAL_ENABLE_WIFI 0\n",
    encoding="utf-8",
)
run_generator("--lint", "--input-root", str(consumer))

(consumer / "hal_project_config.h").write_text(
    "#define HAL_ENABLE_NETWORK_CORE\n", encoding="utf-8"
)
derived_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require("[JH-CFG-DERIVED]" in derived_lint.stderr, "derived lint ID missing")

(consumer / "hal_project_config.h").write_text(
    "#if defined(ENABLE_WIFI_FOR_PROBE)\n"
    "#define HAL_ENABLE_WIFI\n"
    "#endif\n",
    encoding="utf-8",
)
conditional_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require(
    "[JH-CFG-SCOPE]" in conditional_lint.stderr,
    "conditional feature definition bypassed raw scope lint",
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
(consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps(
        {
            "note": "HAL_ENABLE_UNKNOWN",
            "cmake": {"cache": {"JH_EXTRA_DEFINES": ""}},
        }
    ),
    encoding="utf-8",
)
run_generator("--lint", "--input-root", str(consumer))
(consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps(
        {"cmake": {"cache": {"JH_EXTRA_DEFINES": ["HAL_ENABLE_WIFI"]}}}
    ),
    encoding="utf-8",
)
list_cache_lint = run_generator(
    "--lint", "--input-root", str(consumer), expected_success=False
)
require(
    "semicolon-separated scalar" in list_cache_lint.stderr,
    "JSON list bypassed raw cache grammar lint",
)

run_generator("--lint", "--input-root", str(ROOT))

effective_consumer = TEST_ROOT / "effective-consumer"
(effective_consumer / ".vscode").mkdir(parents=True)
(effective_consumer / "hal_project_config.h").write_text(
    "#pragma once\n#define HAL_ENABLE_CRC\n#define HAL_DISABLE_ASSERTS\n",
    encoding="utf-8",
)
(effective_consumer / ".vscode/jaszczurhal.local.json").write_text(
    json.dumps({"target": "invalid-local-target", "board": "invalid-local-board"}),
    encoding="utf-8",
)
(effective_consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps(
        {
            "target": "rp2040",
            "board": "pico",
            "example": {
                "targets": ["rp2040", "stm32g474"],
                "boards": {
                    "rp2040": "pico",
                    "stm32g474": "nucleo-g474re",
                },
                "variants": [
                    {
                        "id": "stream",
                        "targets": ["rp2040", "stm32g474"],
                        "extraDefines": ["HAL_ENABLE_BLE_STREAM"],
                    },
                    {
                        "id": "rp-tls",
                        "targets": ["rp2040"],
                        "cmake": {
                            "cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_TLS"}
                        },
                    },
                ],
            },
            "cmake": {"cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_WIFI"}},
            "targetProfiles": {
                "rp2040": {
                    "cmake": {"cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_MQTT"}}
                },
                "stm32g474": {
                    "cmake": {"cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_UDP"}}
                },
            },
        },
        indent=2,
    )
    + "\n",
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
    f"target profile was not resolved effectively: {rp_base}",
)
require(
    "$.targetProfiles.rp2040.cmake.cache.JH_EXTRA_DEFINES[0]"
    in rp_base["provenance"]["HAL_ENABLE_MQTT"][0],
    "target-profile provenance is missing",
)
stream = effective_record("stm32g474", "stream")
require(
    stream["requestedFeatures"]
    == ["HAL_DISABLE_ASSERTS", "HAL_ENABLE_BLE_STREAM", "HAL_ENABLE_CRC"],
    f"variant did not replace the profile feature list: {stream}",
)
require(
    "HAL_ENABLE_BLE" in stream["resolvedFeatures"]
    and "HAL_ENABLE_CRYPTO" in stream["resolvedFeatures"],
    "variant closure is incomplete",
)
require(
    "$.example.variants[0].extraDefines[0]"
    in stream["provenance"]["HAL_ENABLE_BLE_STREAM"][0],
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
    == ["HAL_ENABLE_EEPROM", "HAL_ENABLE_KV"],
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
    "#define HAL_ENABLE_WIFI\n", encoding="utf-8"
)
(duplicate_consumer / ".vscode/jaszczurhal.project.json").write_text(
    json.dumps({"cmake": {"cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_WIFI=1"}}}),
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
require(
    "vscode/examples" not in root_effective_projects,
    "the reference example manifest created an effective configuration",
)
require(
    "vscode/neutral_fw/rp_native" not in root_effective_projects,
    "the no-request neutral-firmware header created an effective configuration",
)

manual_inventory: set[str] = set()
for inventory_path in (
    ROOT / "CMakeLists.txt",
    ROOT / "tests/CMakeLists.txt",
    ROOT / "runalltests.sh",
):
    manual_inventory.update(
        re.findall(
            r"\bHAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+\b",
            inventory_path.read_text(encoding="utf-8"),
        )
    )
require(
    manual_inventory <= set(model.features),
    f"manual feature inventory has unknown symbols: "
    f"{sorted(manual_inventory - set(model.features))}",
)
derived_inventory = {
    symbol for symbol in manual_inventory if model.features[symbol].kind == "derived"
}
require(
    not derived_inventory,
    f"manual feature inventory requests derived symbols: {sorted(derived_inventory)}",
)

minimal = generate_hal_features.resolve_feature_requests(
    [generate_hal_features.FeatureRequest("HAL_ENABLE_MQTT", None, "test:minimal")],
    model,
    "test:minimal",
)[0]
redundant = generate_hal_features.resolve_feature_requests(
    [
        generate_hal_features.FeatureRequest("HAL_ENABLE_MQTT", None, "test:mqtt"),
        generate_hal_features.FeatureRequest("HAL_ENABLE_TCP", "1", "test:tcp"),
    ],
    model,
    "test:redundant",
)[0]
require(
    minimal.resolved == redundant.resolved,
    "redundant requested feature changed the resolved closure",
)
require(
    generate_hal_features.resolved_features_digest(minimal.resolved)
    == generate_hal_features.resolved_features_digest(redundant.resolved),
    "equal resolved closures produced different oracle digests",
)

constraints_model = generate_hal_features.load_registry(constraints)
_, missing_requirement_findings = generate_hal_features.resolve_feature_requests(
    [
        generate_hal_features.FeatureRequest(
            "HAL_ENABLE_APP_TASK1", None, "test:requires"
        )
    ],
    constraints_model,
    "test:requires",
)
require(
    any("[JH-CFG-REQUIRES]" in item for item in missing_requirement_findings),
    "effective resolver did not enforce requires after closure",
)
_, conflict_findings = generate_hal_features.resolve_feature_requests(
    [
        generate_hal_features.FeatureRequest("HAL_ENABLE_JPEG", None, "test:jpeg"),
        generate_hal_features.FeatureRequest("HAL_ENABLE_PNG", None, "test:png"),
    ],
    constraints_model,
    "test:conflict",
)
require(
    any("[JH-CFG-CONFLICT]" in item for item in conflict_findings),
    "effective resolver did not enforce conflicts after closure",
)

if ARGS.cmake or shutil.which("cmake"):
    cmake_script = TEST_ROOT / "resolve.cmake"
    cmake_script.write_text(
        "\n".join(
            [
                f'include("{(first_output / OUTPUTS[1]).as_posix()}")',
                "jh_hal_resolve_features(requested resolved",
                "    -DHAL_ENABLE_MQTT HAL_ENABLE_BLE_STREAM=1)",
                'if(NOT "${requested}" STREQUAL "HAL_ENABLE_BLE_STREAM;HAL_ENABLE_MQTT")',
                '    message(FATAL_ERROR "unexpected requested=${requested}")',
                "endif()",
                'set(expected "HAL_ENABLE_BLE;HAL_ENABLE_BLE_STREAM;HAL_ENABLE_CRYPTO;HAL_ENABLE_MQTT;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_TCP;HAL_ENABLE_WIFI")',
                'if(NOT "${resolved}" STREQUAL "${expected}")',
                '    message(FATAL_ERROR "unexpected resolved=${resolved}")',
                "endif()",
                "",
            ]
        ),
        encoding="utf-8",
    )
    run_cmake_script(cmake_script)

    parity_script = TEST_ROOT / "resolve-all.cmake"
    parity_lines = [f'include("{(first_output / OUTPUTS[1]).as_posix()}")']
    for index, (name, feature) in enumerate(sorted(model.features.items())):
        if not feature.implies:
            continue
        expected = ";".join(sorted({name, *model.closure(name)}))
        parity_lines.extend(
            [
                f"jh_hal_resolve_features(requested_{index} resolved_{index} {name})",
                f'if(NOT "${{resolved_{index}}}" STREQUAL "{expected}")',
                f'    message(FATAL_ERROR "{name}: unexpected resolved=${{resolved_{index}}}")',
                "endif()",
            ]
        )
    effective_sets = {
        tuple(record["requestedFeatures"]): tuple(record["resolvedFeatures"])
        for record in root_effective_report["configurations"]
    }
    for index, (requested, expected_features) in enumerate(
        sorted(effective_sets.items()), start=len(model.features)
    ):
        arguments = " ".join(requested)
        expected = ";".join(expected_features)
        parity_lines.extend(
            [
                f"jh_hal_resolve_features(requested_{index} resolved_{index} {arguments})",
                f'if(NOT "${{requested_{index}}}" STREQUAL "{";".join(requested)}")',
                f'    message(FATAL_ERROR "effective requested mismatch: ${{requested_{index}}}")',
                "endif()",
                f'if(NOT "${{resolved_{index}}}" STREQUAL "{expected}")',
                f'    message(FATAL_ERROR "effective resolved mismatch: ${{resolved_{index}}}")',
                "endif()",
            ]
        )
    parity_script.write_text("\n".join(parity_lines) + "\n", encoding="utf-8")
    run_cmake_script(parity_script)

    cmake_zero = TEST_ROOT / "resolve-zero.cmake"
    cmake_zero.write_text(
        f'include("{(first_output / OUTPUTS[1]).as_posix()}")\n'
        "jh_hal_resolve_features(requested resolved HAL_ENABLE_WIFI=0)\n",
        encoding="utf-8",
    )
    require(
        "[JH-CFG-VALUE]" in run_cmake_script(cmake_zero, expected_success=False),
        "CMake resolver did not reject =0",
    )

    cmake_derived = TEST_ROOT / "resolve-derived.cmake"
    cmake_derived.write_text(
        f'include("{(first_output / OUTPUTS[1]).as_posix()}")\n'
        "jh_hal_resolve_features(requested resolved HAL_ENABLE_NETWORK_CORE)\n",
        encoding="utf-8",
    )
    require(
        "[JH-CFG-DERIVED]"
        in run_cmake_script(cmake_derived, expected_success=False),
        "CMake resolver accepted a derived request",
    )

    constraints_cmake = constraints_output / OUTPUTS[1]
    cmake_requires = TEST_ROOT / "resolve-requires.cmake"
    cmake_requires.write_text(
        f'include("{constraints_cmake.as_posix()}")\n'
        "jh_hal_resolve_features(requested resolved HAL_ENABLE_APP_TASK1)\n",
        encoding="utf-8",
    )
    require(
        "[JH-CFG-REQUIRES]"
        in run_cmake_script(cmake_requires, expected_success=False),
        "CMake resolver did not enforce requires",
    )

    cmake_conflict = TEST_ROOT / "resolve-conflict.cmake"
    cmake_conflict.write_text(
        f'include("{constraints_cmake.as_posix()}")\n'
        "jh_hal_resolve_features(requested resolved HAL_ENABLE_JPEG HAL_ENABLE_PNG)\n",
        encoding="utf-8",
    )
    require(
        "[JH-CFG-CONFLICT]"
        in run_cmake_script(cmake_conflict, expected_success=False),
        "CMake resolver did not enforce conflicts",
    )

if ARGS.cc and Path(ARGS.cc).stem.lower() not in {"cl", "cl.exe"}:
    generated_header = first_output / OUTPUTS[0]
    derived = preprocess_generated_header(
        ARGS.cc, generated_header, ("HAL_ENABLE_NETWORK_CORE",), False
    )
    require(
        "[JH-CFG-DERIVED]" in derived.stderr,
        "generated header accepted a directly requested derived symbol",
    )

    constraints_header_path = constraints_output / OUTPUTS[0]
    missing_requirement = preprocess_generated_header(
        ARGS.cc, constraints_header_path, ("HAL_ENABLE_APP_TASK1",), False
    )
    require(
        "[JH-CFG-REQUIRES]" in missing_requirement.stderr,
        "generated header did not enforce requires",
    )
    preprocess_generated_header(
        ARGS.cc,
        constraints_header_path,
        ("HAL_ENABLE_APP_TASK1", "HAL_ENABLE_CRC"),
        True,
    )
    conflict = preprocess_generated_header(
        ARGS.cc,
        constraints_header_path,
        ("HAL_ENABLE_JPEG", "HAL_ENABLE_PNG"),
        False,
    )
    require(
        "[JH-CFG-CONFLICT]" in conflict.stderr,
        "generated header did not enforce conflicts",
    )
    check_direct_compiler_zero_presence(ARGS.cc)
    check_production_feature_facade(ARGS.cc)
    check_macro_dump_parity(ARGS.cc)
    check_effective_matrix_parity(ARGS.cc, root_effective_report)

print("feature registry tests passed")
