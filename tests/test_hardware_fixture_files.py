#!/usr/bin/env python3
"""Check hardware-fixture files when explicitly requested."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
from unittest import mock

from source_assertions import source_has_fragment


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT))

from scripted_serial_port import ScriptedPort  # noqa: E402
from vscode.runtime import jh_vscode as workflow  # noqa: E402
from vscode.runtime import serial_io  # noqa: E402

HARDWARE = ROOT / "tests" / "hardware"
# Verifiers that read request/response lines through the shared reader.
LINE_VERIFIERS = (
    "rp_flash_transaction/verify_flash_transaction.py",
    "rp_freertos_smp/verify_freertos_smp.py",
    "rp_kv_power_loss/verify_kv_power_loss.py",
    "rp_ota/verify_ota.py",
    "rp_storage/verify_storage.py",
    "rp_usb_multicore/verify_usb_multicore.py",
)
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


def fixture_builds(name: str) -> dict[tuple[str, str | None], object]:
    """Every build a fixture configures, keyed by target and variant."""
    project = HARDWARE / name
    manifest = load_json(project / ".vscode" / "jaszczurhal.project.json")
    return {
        (build.target.id, build.variant.id if build.variant else None): build
        for build in workflow.project_builds(project, manifest)
    }


def fixture_board(name: str, target: str, board: str | None = None) -> str:
    """Board the fixture builds for ``target``; ``board`` must be selectable."""
    return str(
        workflow.load_project_config(
            HARDWARE / name,
            target_override=target,
            board_override=board,
            use_local_state=False,
        )["board"]
    )


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
    builds = fixture_builds("bluetooth_stream")
    # The physical gate needs these eight target, board and runtime images.
    for target, board, runtime in (
        ("rp2040", "picow", "baremetal"),
        ("rp2040", "picow", "freertos"),
        ("rp2040", "pico-rm2", "baremetal"),
        ("rp2040", "pico-rm2", "freertos"),
        ("rp2350-arm", "pico2w", "baremetal"),
        ("rp2350-arm", "pico2w", "freertos"),
        ("stm32g474", "nucleo-g474re-pim730", "baremetal"),
        ("stm32g474", "nucleo-g474re-pim730", "freertos"),
    ):
        variant = None if runtime == "baremetal" else "FREERTOS"
        require(
            (target, variant) in builds and fixture_board("bluetooth_stream", target, board) == board,
            f"bluetooth_stream: no {runtime} image for {target}/{board}",
        )
    display = {"JHBL5_ENABLE_DISPLAY=1", "HAL_ENABLE_ILI9341", "HAL_DISPLAY_ILI9341"}
    for variant, definitions in (
        ("DISPLAY", display),
        ("DISPLAY_FREERTOS", display | {"HAL_ENABLE_FREERTOS"}),
    ):
        require(
            {target for target, item in builds if item == variant} == {"stm32g474"}
            and set(builds[("stm32g474", variant)].definitions) == definitions,
            f"bluetooth_stream:{variant} changed",
        )


def check_rp_manifests() -> None:
    ota = fixture_builds("rp_ota")
    require(
        set(ota) == {(target, variant) for target in ("rp2040", "rp2350-arm") for variant in (None, "FREERTOS")},
        "rp_ota: target or variant matrix changed",
    )
    require(
        {target: fixture_board("rp_ota", target) for target in ("rp2040", "rp2350-arm")}
        == {"rp2040": "picow", "rp2350-arm": "pico2w"},
        "rp_ota: default boards changed",
    )
    require(
        ota[("rp2040", "FREERTOS")].definitions == ("HAL_ENABLE_FREERTOS",),
        "rp_ota: FreeRTOS variant changed",
    )
    manifest = load_json(HARDWARE / "rp_ota" / ".vscode" / "jaszczurhal.project.json")
    require(
        manifest["ota"]["passwordEnv"] == "JH_OTA_TEST_PASSWORD",
        "rp_ota: a tracked password replaced the environment variable",
    )

    targets = ("rp2040", "rp2350-arm", "rp2350-riscv")
    boards = {"rp2040": "pico", "rp2350-arm": "pico2", "rp2350-riscv": "pico2"}
    for name, base_define in (
        ("rp_usb_multicore", "HAL_ENABLE_APP_TASK1"),
        ("rp_sdlogger", "HAL_ENABLE_SDLOGGER"),
    ):
        builds = fixture_builds(name)
        require(
            set(builds) == {(target, variant) for target in targets for variant in (None, "FREERTOS")},
            f"{name}: target or variant matrix changed",
        )
        require(
            {target: fixture_board(name, target) for target in targets} == boards,
            f"{name}: boards changed",
        )
        freertos = builds[("rp2040", "FREERTOS")]
        require(
            freertos.definitions == ("HAL_ENABLE_FREERTOS",)
            and builds[("rp2040", None)].config.defined(base_define)
            and base_define not in freertos.definitions,
            f"{name}: base feature or FreeRTOS variant changed",
        )


def check_bluetooth_stage1() -> None:
    builds = fixture_builds("bluetooth_stage1")
    targets = ("stm32g474", "rp2350-arm", "rp2040")
    require(
        set(builds) == {(target, variant) for target in targets for variant in (None, "WIFI_ONLY")},
        "bluetooth_stage1: targets or variants changed",
    )
    require(
        {target: fixture_board("bluetooth_stage1", target) for target in targets}
        == {
            "stm32g474": "nucleo-g474re-pim730",
            "rp2350-arm": "pico2w",
            "rp2040": "picow",
        },
        "bluetooth_stage1: boards changed",
    )
    require(
        builds[("rp2040", None)].config.defined("JH_BLUETOOTH_STAGE1_PROBE"),
        "bluetooth_stage1: Bluetooth selector is missing",
    )
    require(
        not builds[("rp2040", "WIFI_ONLY")].config.defined("JH_BLUETOOTH_STAGE1_PROBE"),
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
        and fixture_board("bluetooth_gamepad", "rp2350-arm") == "pico2w",
        "bluetooth_gamepad: default board changed",
    )
    require(
        fixture_builds("bluetooth_gamepad")[("rp2350-arm", None)].config.defined(
            "JH_BLUETOOTH_CLASSIC_HID_PROBE"
        ),
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
        "gpio_ctx": 1,
        "ctx_a_hits": 2,
        "ctx_a_pin": 13,
        "ctx_b_hits": 1,
        "ctx_b_pin": 14,
        "ctx_plain": 1,
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


def run_script(*arguments: str) -> None:
    result = subprocess.run(
        [sys.executable, *arguments], cwd=ROOT, check=False,
        capture_output=True, text=True,
    )
    require(
        result.returncode == 0,
        f"{arguments[0]} failed:\n{result.stdout}{result.stderr}",
    )


def check_feature_configs() -> None:
    """The default lint leaves tests/hardware out; check the fixtures here."""
    run_script(
        "scripts/generate_hal_features.py", "--lint", "--effective",
        "--input-root", str(HARDWARE), "--include-hardware-fixtures",
    )


def check_fixture_documentation() -> None:
    """Links and EN/PL parity of the fixture READMEs and the links into them."""
    run_script("scripts/check_documentation_links.py", str(ROOT),
               "--include-hardware-fixtures")
    run_script("scripts/check_documentation_i18n_parity.py", str(ROOT),
               "--include-hardware-fixtures")


def check_line_verifiers() -> None:
    """Verifiers give up at their deadline even while bytes keep arriving."""
    for relative in LINE_VERIFIERS:
        module = load_module(Path(relative).stem, HARDWARE / relative)
        port = ScriptedPort(b"x", step=0.05, repeat=True)
        with mock.patch.object(serial_io.time, "monotonic", port.clock):
            try:
                module.read_line(port, 0.2)
            except TimeoutError:
                pass
            else:
                raise AssertionError(f"{relative}: read_line did not time out")
        require(port.now < 0.3, f"{relative}: read_line overran its deadline")


check_documentation_index()
check_build_layout()
check_bluetooth_stream()
check_rp_manifests()
check_bluetooth_stage1()
check_bluetooth_gamepad()
check_esp32s3_phase2()
check_feature_configs()
check_fixture_documentation()
check_line_verifiers()
