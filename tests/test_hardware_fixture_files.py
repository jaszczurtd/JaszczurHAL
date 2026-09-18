#!/usr/bin/env python3
"""Check hardware-fixture files when explicitly requested."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import re
import sys

from source_assertions import source_has_fragment


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
HARDWARE = ROOT / "tests" / "hardware"
GAMEPAD_DATA = (
    ROOT
    / "tests"
    / "fixtures"
    / "bluetooth_gamepad"
    / "zero2_android_dinput.json"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    require(spec is not None and spec.loader is not None, f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def check_documentation_index() -> None:
    """Each fixture README carries its own procedure and the guide indexes it."""
    guides = {
        "README.md": (ROOT / "doc" / "api" / "en" / "03_build_tests.md").read_text(
            encoding="utf-8"
        ),
        "README.pl.md": (
            ROOT / "doc" / "api" / "pl" / "03_build_tests.md"
        ).read_text(encoding="utf-8"),
    }
    for fixture_dir in sorted(HARDWARE.iterdir()):
        if not fixture_dir.is_dir():
            continue
        for readme_name, guide in guides.items():
            readme_path = fixture_dir / readme_name
            require(
                readme_path.is_file(), f"{fixture_dir.name}: {readme_name} is missing"
            )
            readme = readme_path.read_text(encoding="utf-8")
            require(
                f"tests/hardware/{fixture_dir.name}" in readme and "```" in readme,
                f"{fixture_dir.name}: {readme_name} does not hold the procedure",
            )
            require(
                "03_build_tests.md#" not in readme,
                f"{fixture_dir.name}: {readme_name} defers to the device-test guide",
            )
            require(
                f"(../../../tests/hardware/{fixture_dir.name}/{readme_name})" in guide,
                f"{fixture_dir.name}: device-test guide does not link {readme_name}",
            )


def check_build_layout() -> None:
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
            HARDWARE / fixture / ".vscode" / "jaszczurhal.project.json"
        )
        require(
            manifest.get("buildDir") == f"${{jhRoot}}/.build/hardware/{fixture}",
            f"{fixture}: build output escapes .build/hardware",
        )
        require(
            manifest.get("cmake", {}).get("cache", {}).get("JH_ARTIFACT_DIR")
            == "${buildDir}",
            f"{fixture}: final artifacts do not follow buildDir",
        )


def check_bluetooth_stream() -> None:
    manifest = load_json(
        HARDWARE / "bluetooth_stream" / ".vscode" / "jaszczurhal.project.json"
    )
    matrix = manifest.get("example", {}).get("hardwareMatrix")
    require(isinstance(matrix, list), "bluetooth_stream: hardwareMatrix is missing")
    require(
        all(
            isinstance(entry, dict)
            and set(entry) == {"target", "board", "runtime"}
            and all(isinstance(value, str) for value in entry.values())
            for entry in matrix
        ),
        "bluetooth_stream: invalid hardwareMatrix entry",
    )
    expected = {
        ("rp2040", "picow", "baremetal"),
        ("rp2040", "picow", "freertos"),
        ("rp2040", "pico-rm2", "baremetal"),
        ("rp2040", "pico-rm2", "freertos"),
        ("rp2350-arm", "pico2w", "baremetal"),
        ("rp2350-arm", "pico2w", "freertos"),
        ("stm32g474", "nucleo-g474re-pim730", "baremetal"),
        ("stm32g474", "nucleo-g474re-pim730", "freertos"),
    }
    actual = {
        (entry["target"], entry["board"], entry["runtime"]) for entry in matrix
    }
    require(
        len(matrix) == len(expected) and actual == expected,
        "bluetooth_stream: hardwareMatrix changed",
    )

    variants = {
        variant.get("id"): variant
        for variant in manifest.get("example", {}).get("variants", [])
        if isinstance(variant, dict)
    }
    expected_variants = {
        "display": {
            "JHBL5_ENABLE_DISPLAY=1",
            "HAL_ENABLE_ILI9341",
            "HAL_DISPLAY_ILI9341",
        },
        "display-freertos": {
            "JHBL5_ENABLE_DISPLAY=1",
            "HAL_ENABLE_ILI9341",
            "HAL_DISPLAY_ILI9341",
            "HAL_ENABLE_FREERTOS",
        },
    }
    for variant_id, defines in expected_variants.items():
        variant = variants.get(variant_id)
        require(
            isinstance(variant, dict)
            and variant.get("module") == f"bluetooth_stream_{variant_id.replace('-', '_')}"
            and variant.get("targets") == ["stm32g474"]
            and set(variant.get("extraDefines", [])) == defines,
            f"bluetooth_stream:{variant_id} changed",
        )


def check_rp_manifests() -> None:
    ota = load_json(HARDWARE / "rp_ota" / ".vscode" / "jaszczurhal.project.json")
    require(
        set(ota["example"]["targets"]) == {"rp2040", "rp2350-arm"},
        "rp_ota: target matrix changed",
    )
    require(
        ota["example"]["boards"] == {"rp2040": "picow", "rp2350-arm": "pico2w"},
        "rp_ota: default boards changed",
    )
    variants = {variant["id"]: variant for variant in ota["example"]["variants"]}
    require(
        set(variants["freertos"]["extraDefines"]) == {"HAL_ENABLE_FREERTOS"},
        "rp_ota: FreeRTOS variant changed",
    )
    require(
        ota["ota"]["passwordEnv"] == "JH_OTA_TEST_PASSWORD",
        "rp_ota: a tracked password replaced the environment variable",
    )

    targets = {"rp2040", "rp2350-arm", "rp2350-riscv"}
    boards = {"rp2040": "pico", "rp2350-arm": "pico2", "rp2350-riscv": "pico2"}
    for name, base_define in (
        ("rp_usb_multicore", "HAL_ENABLE_APP_TASK1"),
        ("rp_sdlogger", "HAL_ENABLE_SDLOGGER"),
    ):
        manifest = load_json(
            HARDWARE / name / ".vscode" / "jaszczurhal.project.json"
        )
        metadata = manifest["example"]
        require(set(metadata["targets"]) == targets, f"{name}: targets changed")
        require(metadata["boards"] == boards, f"{name}: boards changed")
        variants = {variant["id"]: variant for variant in metadata["variants"]}
        require(set(variants) == {"freertos"}, f"{name}: variants changed")
        require(
            set(variants["freertos"]["targets"]) == targets
            and set(variants["freertos"]["extraDefines"])
            == {"HAL_ENABLE_FREERTOS"},
            f"{name}: FreeRTOS matrix changed",
        )
        config = (HARDWARE / name / "hal_project_config.h").read_text(
            encoding="utf-8"
        )
        require(base_define in config, f"{name}: base feature is missing")
        require(
            base_define
            not in manifest["cmake"]["cache"].get("JH_EXTRA_DEFINES", "").split(";"),
            f"{name}: base feature is duplicated",
        )


def check_bluetooth_stage1() -> None:
    manifest = load_json(
        HARDWARE / "bluetooth_stage1" / ".vscode" / "jaszczurhal.project.json"
    )
    require(
        manifest["example"]["targets"] == ["stm32g474", "rp2350-arm", "rp2040"],
        "bluetooth_stage1: targets changed",
    )
    require(
        manifest["example"]["boards"]
        == {
            "stm32g474": "nucleo-g474re-pim730",
            "rp2350-arm": "pico2w",
            "rp2040": "picow",
        },
        "bluetooth_stage1: boards changed",
    )
    variants = {item["id"]: item for item in manifest["example"]["variants"]}
    require(set(variants) == {"bluetooth", "wifi-only"}, "stage1 variants changed")
    require(
        variants["bluetooth"].get("extraDefines")
        == ["JH_BLUETOOTH_STAGE1_PROBE"],
        "bluetooth_stage1: Bluetooth selector is missing",
    )
    require(
        "JH_BLUETOOTH_STAGE1_PROBE"
        not in variants["wifi-only"].get("extraDefines", []),
        "bluetooth_stage1: WiFi-only variant enables Bluetooth",
    )


def check_bluetooth_gamepad() -> None:
    fixture = HARDWARE / "bluetooth_gamepad"
    verifier = (fixture / "verify_zero2.py").read_text(encoding="utf-8")
    for expected in (
        "DISCONNECT_TIMEOUT_S = 60.0",
        "RECONNECT_CYCLES = 5",
        "RECONNECT_SETTLE_MS = 3_000",
        "RECONNECT_TIMEOUT_S = 180.0",
        "STABILITY_DURATION_MS = 30 * 60 * 1000",
        'probe.command("DISCOVER")',
        'probe.command("AUTHORIZE")',
        'probe.command("DISCONNECT")',
        '"--resume-stability"',
        '"hostVerifierResumed"',
        "health_counter",
        '"gamepad": "unavailable"',
        'DEFAULT_RESULT_PATH = Path(__file__).with_name("zero2_pico2w_c6_result.json")',
        '"descriptorsRejected"',
        '"droppedSnapshots"',
        '"reportsRejected"',
    ):
        require(source_has_fragment(verifier, expected), f"gamepad verifier: {expected}")
    require("known-pad reconnect in cycle" in verifier, "reconnect check is missing")
    require(
        all(
            secret not in verifier
            for secret in ("bd_addr_to_str", '"linkKey":', '"deviceAddress":')
        ),
        "gamepad verifier exposes private Bluetooth data",
    )

    for name in ("zero2_pico2w_c5_result.json", "zero2_pico2w_c6_result.json"):
        path = fixture / name
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        result = json.loads(text)
        require(result["result"] == "pass", f"{name}: stored result is not a pass")
        require(
            result["capture"] == GAMEPAD_DATA.name
            and result["firmware"]["gamepad"] == "unavailable",
            f"{name}: stored result does not match the parser data",
        )
        require(
            re.search(r"(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", text) is None,
            f"{name}: stored result contains a Bluetooth address",
        )

    c6 = load_json(fixture / "zero2_pico2w_c6_result.json")["parser"]
    require(
        c6["descriptorLimit"] == 256
        and c6["reportLimit"] == 32
        and c6["queueCapacity"] == 16,
        "C6 parser limits changed",
    )
    require(
        c6["descriptorsAccepted"] == 1
        and c6["reportsAccepted"] > 0
        and c6["descriptorsRejected"] == 0
        and c6["reportsRejected"] == 0
        and c6["droppedSnapshots"] == 0,
        "C6 result reports a parser failure",
    )

    manifest = load_json(fixture / ".vscode" / "jaszczurhal.project.json")
    require(
        manifest["target"] == "rp2350-arm"
        and manifest["board"] == "pico2w"
        and manifest["example"]["boards"]["rp2350-arm"] == "pico2w",
        "bluetooth_gamepad: default board changed",
    )
    require(
        manifest["example"]["variants"][0]["extraDefines"]
        == ["JH_BLUETOOTH_CLASSIC_HID_PROBE"],
        "bluetooth_gamepad: private selector changed",
    )


def valid_phase2_report() -> dict[str, int | str]:
    return {
        "sequence": 7,
        "target": "esp32s3",
        "board": "waveshare-esp32-s3-zero",
        "core0": 0,
        "core1": 1,
        "task1": 12,
        "system": 1,
        "sync": 1,
        "gpio": 1,
        "irq": 2,
        "irq_isr": 1,
        "adc": 1,
        "adc_low": 120,
        "adc_high": 3900,
        "uart": 1,
        "i2c": 1,
        "i2c_found": 0,
        "spi": 1,
        "timer": 1,
        "timer_count": 3,
        "timer_isr": 1,
        "serial_rx": 1,
        "stack_guard": 1,
        "heap": 185000,
        "temp_centi": 3175,
        "status": "PASS",
    }


def check_esp32s3_phase2() -> None:
    fixture = HARDWARE / "esp32s3_phase2"
    verifier = load_module("jh_esp32s3_phase2_verifier", fixture / "verify_phase2.py")
    report = valid_phase2_report()
    payload = " ".join(f"{key}={value}" for key, value in report.items())
    parsed = verifier.parse_report(
        f"I (123) stdout: {verifier.REPORT_PREFIX}{payload}\r\n".encode()
    )
    require(parsed == report, "esp32s3_phase2: report parser changed")
    verifier.validate_report(
        parsed,
        verifier.load_expected_contract("esp32s3", "waveshare-esp32-s3-zero"),
    )

    app = (fixture / "app.cpp").read_text(encoding="utf-8")
    config = (fixture / "hal_project_config.h").read_text(encoding="utf-8")
    for expected in (
        "s_timer_pool = hal_timer_pool_create_auto(1u);",
        "hal_timer_create(s_timer_pool",
        "hal_timer_pool_destroy(s_timer_pool);",
        "hal_stack_guard_init_ex()",
    ):
        require(source_has_fragment(app, expected), f"esp32s3_phase2: {expected}")
    require(
        not source_has_fragment(app, "hal_timer_pool_create_auto(1u) != nullptr")
        and not source_has_fragment(app, "hal_enter_bootloader() == HAL_EUNSUPPORTED")
        and not source_has_fragment(
            app, "hal_stack_guard_init_ex() == HAL_EUNSUPPORTED"
        ),
        "esp32s3_phase2: unsupported fallback returned",
    )
    require(
        source_has_fragment(config, "#define HAL_ENABLE_STACK_GUARD 1"),
        "esp32s3_phase2: stack guard is disabled",
    )


check_documentation_index()
check_build_layout()
check_bluetooth_stream()
check_rp_manifests()
check_bluetooth_stage1()
check_bluetooth_gamepad()
check_esp32s3_phase2()
