#!/usr/bin/env python3
"""Generate and build dispatcher-backed JaszczurHAL examples."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from typing import Any

from vscode_task_config import write_text_lf


REPO_ROOT = Path(__file__).resolve().parents[1]
EXAMPLES_DIR = REPO_ROOT / "examples"
JH_VSCODE = REPO_ROOT / "vscode" / "entry" / (
    "jh-vscode.cmd" if os.name == "nt" else "jh-vscode"
)
REFERENCE_VSCODE_DIR = REPO_ROOT / "vscode" / "examples"

RP_NATIVE_TARGETS = ["rp2040", "rp2350-arm", "rp2350-riscv"]
GATE_PRIMARY_TARGETS = ["rp2040", "stm32g474"]
LEGACY_EXAMPLE_IDS = [
    f"{index:02d}_{name}"
    for index, name in enumerate(
        [
            "blink",
            "debug_helper",
            "soft_timer_table",
            "crypto",
            "modem_A7670E",
            "ds18b20",
            "gps",
            "thermocouple",
            "display_tft",
            "mqtt",
            "wireguard",
            "kv_store",
            "i2c_slave",
            "uart",
            "wifi",
            "littlefs",
            "pid_controller",
            "rgb_led",
            "timer_ext",
            "i2c_scan",
            "adc_read",
            "gps_uart",
            "external_adc_ads1115",
            "can_mcp2515",
            "display_oled",
            "rtc_clock",
            "rtc_ds3231",
            "pga2311",
            "freertos_smoke",
            "bh1750_light",
            "hd44780",
            "tsc2007_touch",
            "stmpe610_touch",
            "irsmall_decoder",
            "cJSON",
            "lodePNG",
            "lodePNG_ili9341_base64",
            "stm32g474_fdcan_native",
            "sdlogger",
            "jpeg",
            "jpeg_ili931_base64",
            "bsd_sockets_tcp_udp",
            "dht_temperature_humidity",
            "dacless_audio",
            "swserial_loopback",
            "mfrc522_rfid",
            "pn532_nfc",
            "http_server",
            "websocket",
            "net_console",
            "net_commands",
            "http_files",
            "simple_io_chips",
            "adp5360_pmic",
            "epd_display",
            "http_https_client",
            "ota",
            "ble_peripheral",
            "ble_stream",
            "lora_point_to_point",
        ],
        start=1,
    )
]
EXAMPLES: list[dict[str, Any]] = [
    {
        "dir": "01_core_runtime",
        "targets": ["rp2040", "stm32g474"],
        "covers": [
            "01_blink",
            "02_debug_helper",
            "03_soft_timer_table",
            "17_pid_controller",
            "19_timer_ext",
        ],
    },
    {
        "dir": "02_crypto",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["04_crypto"],
    },
    {
        "dir": "03_modem_A7670E",
        "targets": ["rp2040"],
        "covers": ["05_modem_A7670E"],
    },
    {
        "dir": "04_sensor_hub",
        "targets": ["rp2040", "stm32g474"],
        "covers": [
            "06_ds18b20",
            "30_bh1750_light",
            "43_dht_temperature_humidity",
        ],
    },
    {
        "dir": "05_serial_gps",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["07_gps", "14_uart", "22_gps_uart", "45_swserial_loopback"],
        "sources": ["app.c"],
        "variants": [
            {
                "id": "swserial",
                "module": "05_serial_gps_swserial",
                "sources": ["swserial_app.c"],
                "targets": ["rp2040"],
                "extraDefines": ["EXAMPLE_SERIAL_GPS_USE_SWSERIAL=1"],
            }
        ],
    },
    {
        "dir": "06_thermocouple",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["08_thermocouple"],
    },
    {
        "dir": "07_display_media",
        "targets": ["rp2040", "stm32g474"],
        "covers": [
            "09_display_tft",
            "36_lodePNG",
            "37_lodePNG_ili9341_base64",
            "40_jpeg",
            "41_jpeg_ili931_base64",
        ],
    },
    {
        "dir": "08_mqtt",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["10_mqtt"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {
        "dir": "09_wireguard",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["11_wireguard"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {
        "dir": "10_storage",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["12_kv_store", "16_littlefs", "39_sdlogger"],
    },
    {
        "dir": "11_i2c_slave",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["13_i2c_slave"],
    },
    {
        "dir": "12_i2c_scan",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["20_i2c_scan"],
    },
    {
        "dir": "13_adc",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["21_adc_read", "23_external_adc_ads1115"],
    },
    {
        "dir": "14_can_mcp2515",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["24_can_mcp2515"],
    },
    {
        "dir": "15_display_oled_lcd",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["25_display_oled", "31_hd44780"],
    },
    {
        "dir": "16_rtc_backends",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["26_rtc_clock", "27_rtc_ds3231"],
    },
    {
        "dir": "17_audio_output",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["28_pga2311", "44_dacless_audio"],
    },
    {
        "dir": "18_freertos_suite",
        "targets": ["rp2040", "rp2350-arm", "rp2350-riscv", "stm32g474"],
        "expandRpTargets": False,
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
        "covers": [
            "15_wifi",
            "29_freertos_smoke",
            "35_cJSON",
            "42_bsd_sockets_tcp_udp",
            "48_http_server",
            "49_websocket",
            "50_net_console",
            "51_net_commands",
            "52_http_files",
            "56_http_https_client",
        ],
        "sources": ["app.c"],
        "cache": {"JH_RP2040_FREERTOS": True},
        "variants": [
            {
                "id": "network",
                "module": "18_freertos_network",
                "sources": ["network_app.cpp"],
                "targets": ["rp2040", "rp2350-arm", "stm32g474"],
                "extraDefines": [
                    "HAL_ENABLE_BSD_SOCKETS",
                    "HAL_ENABLE_CJSON",
                    "HAL_ENABLE_HTTP_CLIENT",
                    "HAL_ENABLE_HTTP_FILES",
                    "HAL_ENABLE_HTTP_SERVER",
                    "HAL_ENABLE_NET_COMMANDS",
                    "HAL_ENABLE_NET_CONSOLE",
                    "HAL_ENABLE_TIME",
                    "HAL_ENABLE_TLS",
                    "HAL_ENABLE_WEBSOCKET",
                    "HAL_ENABLE_WIFI",
                    "HAL_FREERTOS_TASK0_STACK=1536",
                    "HAL_FREERTOS_TASK1_STACK=384",
                    "HAL_HTTP_SERVER_MAX_CLIENTS=1",
                    "HAL_NET_CONSOLE_MAX_CLIENTS=1",
                    "HAL_TCP_LISTENER_MAX_INSTANCES=4",
                    "HAL_TCP_LISTENER_BACKLOG_MAX=2",
                    "HAL_TCP_SOCKET_MAX_INSTANCES=6",
                    "HAL_TLS_MAX_CLIENTS=1",
                    "HAL_WEBSOCKET_MAX_CLIENTS=1",
                ],
            }
        ],
    },
    {
        "dir": "19_touch",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["32_tsc2007_touch", "33_stmpe610_touch"],
    },
    {
        "dir": "20_irsmall_decoder",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["34_irsmall_decoder"],
    },
    {
        "dir": "21_stm32g474_fdcan_native",
        "targets": ["stm32g474"],
        "target": "stm32g474",
        "board": "nucleo-g474re",
        "covers": ["38_stm32g474_fdcan_native"],
    },
    {
        "dir": "22_rfid_nfc",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["46_mfrc522_rfid", "47_pn532_nfc"],
    },
    {
        "dir": "23_io_pmic",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["18_rgb_led", "53_simple_io_chips", "54_adp5360_pmic"],
    },
    {
        "dir": "24_epd_display",
        "targets": ["rp2040", "stm32g474"],
        "covers": ["55_epd_display"],
    },
    {
        "dir": "25_ota",
        "targets": ["rp2040"],
        "covers": ["57_ota"],
        "board": "picow",
        "cache": {"JH_OTA_GENERATION": 1, "JH_OTA_VERSION": "example"},
        "ota": {
            "hostname": "jaszczurhal-ota",
            "port": 8266,
            "listenPort": 8266,
            "password": "change-this-ota-password",
        },
    },
    {
        "dir": "26_ble_stream",
        "targets": ["rp2040", "stm32g474"],
        "expandRpTargets": False,
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
        "covers": ["58_ble_peripheral", "59_ble_stream"],
    },
    {
        "dir": "27_lora_point_to_point",
        "targets": ["rp2040", "stm32g474"],
        "expandRpTargets": False,
        "board": "pico-core1262-hf",
        "stm32Board": "nucleo-g474re-core1262-hf",
        "covers": ["60_lora_point_to_point"],
        "variants": [
            {
                "id": "probe",
                "module": "27_lora_point_to_point_probe",
                "targets": ["rp2040", "stm32g474"],
                "extraDefines": ["HAL_LORA_EXAMPLE_PROBE_ONLY=1"],
            },
            {
                "id": "responder",
                "module": "27_lora_point_to_point_responder",
                "targets": ["rp2040", "stm32g474"],
                "extraDefines": ["HAL_LORA_EXAMPLE_RESPONDER=1"],
            },
            {
                "id": "sf7",
                "module": "27_lora_point_to_point_sf7",
                "targets": ["rp2040", "stm32g474"],
                "gateTargets": [],
                "extraDefines": [
                    "HAL_LORA_EXAMPLE_SF=7",
                    "HAL_LORA_EXAMPLE_TX_POWER_DBM=6",
                ],
            },
            {
                "id": "responder-sf7",
                "module": "27_lora_point_to_point_responder_sf7",
                "targets": ["rp2040", "stm32g474"],
                "gateTargets": [],
                "extraDefines": [
                    "HAL_LORA_EXAMPLE_RESPONDER=1",
                    "HAL_LORA_EXAMPLE_SF=7",
                    "HAL_LORA_EXAMPLE_TX_POWER_DBM=6",
                ],
            },
        ],
    },
]


def json_text(data: Any) -> str:
    return json.dumps(data, indent=4, ensure_ascii=False) + "\n"


def target_registry() -> dict[str, dict[str, Any]]:
    from board_registry import tooling_target_registry

    return tooling_target_registry(REPO_ROOT)


def example_cache(entry: dict[str, Any], module: str) -> dict[str, Any]:
    cache: dict[str, Any] = {
        "JH_ARTIFACT_DIR": "${buildDir}",
        "JH_PROJECT_DIR": "${project}",
        "JH_MODULE_NAME": module,
    }
    if entry.get("sources"):
        cache["JH_PROJECT_SOURCES"] = ";".join(str(item) for item in entry["sources"])
    if entry.get("extraDefines"):
        cache["JH_EXTRA_DEFINES"] = ";".join(str(item) for item in entry["extraDefines"])
    cache.update(entry.get("cache") or {})
    return cache


def example_targets(entry: dict[str, Any], targets: list[str] | None = None) -> list[str]:
    declared = [str(item) for item in (targets if targets is not None else entry["targets"])]
    if "rp2040" not in declared or not entry.get("expandRpTargets", True):
        return declared

    expanded = [target for target in declared if target != "rp2040"]
    expanded.extend(
        target
        for target in RP_NATIVE_TARGETS
        if target != "rp2350-riscv" or entry.get("board") != "picow"
    )
    return expanded


def example_boards(entry: dict[str, Any]) -> dict[str, str]:
    boards: dict[str, str] = {}
    targets = example_targets(entry)
    rp_board = str(entry.get("board") or "pico")
    if "rp2040" in targets:
        boards["rp2040"] = rp_board
    if "rp2350-arm" in targets:
        boards["rp2350-arm"] = "pico2w" if rp_board == "picow" else "pico2"
    if "rp2350-riscv" in targets:
        boards["rp2350-riscv"] = "pico2"
    if "stm32g474" in targets:
        boards["stm32g474"] = str(entry.get("stm32Board") or "nucleo-g474re")
    return boards


def gate_targets(
    supported_targets: list[str], configured_targets: Any = None
) -> list[str]:
    if configured_targets is None:
        return [
            target for target in supported_targets if target in GATE_PRIMARY_TARGETS
        ]
    if not isinstance(configured_targets, list):
        raise ValueError("gateTargets must be an array")

    requested = [str(item) for item in configured_targets]
    unknown = sorted(set(requested).difference(supported_targets))
    if unknown:
        raise ValueError(
            "gateTargets escape supported targets: " + ", ".join(unknown)
        )
    return [target for target in supported_targets if target in requested]


def example_covers(entry: dict[str, Any]) -> list[str]:
    covers = entry.get("covers")
    if covers is None:
        return [str(entry["dir"])]
    if not isinstance(covers, list) or not covers:
        raise ValueError(f"{entry['dir']}: covers must be a non-empty array")
    return [str(item) for item in covers]


def validate_example_registry() -> None:
    directories: set[str] = set()
    coverage_owner: dict[str, str] = {}
    for entry in EXAMPLES:
        name = str(entry["dir"])
        if name in directories:
            raise ValueError(f"duplicate example directory: {name}")
        directories.add(name)

        supported = example_targets(entry)
        gate_targets(supported, entry.get("gateTargets"))
        for covered in example_covers(entry):
            owner = coverage_owner.get(covered)
            if owner is not None:
                raise ValueError(
                    f"legacy example {covered} is covered by both {owner} and {name}"
                )
            coverage_owner[covered] = name

        for variant in entry.get("variants", []):
            variant_supported = example_targets(
                entry,
                [str(target) for target in variant.get("targets", entry["targets"])],
            )
            gate_targets(variant_supported, variant.get("gateTargets"))

    expected = set(LEGACY_EXAMPLE_IDS)
    actual = set(coverage_owner)
    missing = sorted(expected.difference(actual))
    unknown = sorted(actual.difference(expected))
    if missing or unknown:
        details = []
        if missing:
            details.append("missing: " + ", ".join(missing))
        if unknown:
            details.append("unknown: " + ", ".join(unknown))
        raise ValueError("legacy example coverage mismatch; " + "; ".join(details))


def default_target_board(entry: dict[str, Any]) -> tuple[str, str]:
    targets = example_targets(entry)
    target = str(entry.get("target") or "rp2040")
    if target not in targets:
        target = targets[0]
    return target, example_boards(entry).get(
        target,
        "nucleo-g474re" if target == "stm32g474" else "pico",
    )


def manifest_for(entry: dict[str, Any]) -> dict[str, Any]:
    name = str(entry["dir"])
    module = str(entry.get("module") or name)
    target, board = default_target_board(entry)
    example = {
        "targets": example_targets(entry),
        "boards": example_boards(entry),
        "gateTargets": gate_targets(
            example_targets(entry), entry.get("gateTargets")
        ),
        "covers": example_covers(entry),
    }
    if entry.get("variants"):
        variants = []
        for item in entry["variants"]:
            variant = dict(item)
            variant["targets"] = example_targets(
                entry,
                [str(target) for target in item.get("targets", entry["targets"])],
            )
            variant["gateTargets"] = gate_targets(
                variant["targets"], item.get("gateTargets")
            )
            variants.append(variant)
        example["variants"] = variants
    manifest = {
        "$schema": "../../../vscode/schema/jh_vscode_project.schema.json",
        "project": "JaszczurHAL examples",
        "module": module,
        "example": example,
        "toolchain": "cmake",
        "target": target,
        "board": board,
        "buildDir": f"${{jhRoot}}/.build/examples/{name}",
        "cmakeBuildDir": "${buildDir}/cmake",
        "cmake": {
            "sourceDir": "${project}/../../cmake/jh_firmware_project",
            "cache": example_cache(entry, module),
        },
        "artifacts": {
            "elf": "${buildDir}/firmware.elf",
            "uf2": "${buildDir}/firmware.uf2",
            "compileCommands": "${buildDir}/compile_commands_patched.json",
        },
    }
    if entry.get("ota"):
        manifest["ota"] = dict(entry["ota"])
        manifest["artifacts"]["ota"] = "${buildDir}/firmware.ota"
    return manifest


def settings_for(name: str) -> dict[str, Any]:
    from vscode_task_config import vscode_entry_settings

    build_dir = f"${{workspaceFolder}}/../../.build/examples/{name}"
    return {
        "jaszczurhal.buildDir": build_dir,
        "jaszczurhal.verbose": False,
        "jaszczurhal.root": "../..",
        **vscode_entry_settings("../../vscode/entry/jh-vscode"),
        "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
        "C_Cpp.default.compileCommands": f"{build_dir}/compile_commands_patched.json",
        "C_Cpp.errorSquiggles": "enabled",
        "cmake.sourceDirectory": "${workspaceFolder}/../../cmake/jh_firmware_project",
        "cmake.buildDirectory": f"{build_dir}/cmake",
        "files.exclude": {"**/.build": True},
        "search.exclude": {"**/.build": True},
    }


def base_tasks(default_target: str, default_board: str, variants: list[dict[str, Any]]) -> dict[str, Any]:
    from vscode_task_config import project_tasks_document

    return project_tasks_document(
        target_registry(),
        default_target,
        default_board,
        variants=variants,
    )


def launch_for(name: str) -> dict[str, Any]:
    from vscode_task_config import cortex_debug_launch_document

    executable = f"${{workspaceFolder}}/../../.build/examples/{name}/firmware.elf"
    return cortex_debug_launch_document(executable)


def extensions_for() -> dict[str, Any]:
    from vscode_task_config import extensions_recommendations

    return extensions_recommendations()


def keybindings_for() -> list[dict[str, str]]:
    from vscode_task_config import keybindings_reference

    return keybindings_reference()


def reference_settings() -> dict[str, Any]:
    from vscode_task_config import vscode_entry_settings

    return {
        "jaszczurhal.root": "../../libraries/JaszczurHAL",
        **vscode_entry_settings(
            "../../libraries/JaszczurHAL/vscode/entry/jh-vscode"
        ),
        "jaszczurhal.buildDir": "${workspaceFolder}/.build",
        "jaszczurhal.verbose": False,
        "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
        "C_Cpp.default.compileCommands": (
            "${workspaceFolder}/.build/compile_commands_patched.json"
        ),
        "C_Cpp.errorSquiggles": "enabled",
        "cmake.sourceDirectory": (
            "${workspaceFolder}/../../libraries/JaszczurHAL/cmake/jh_firmware_project"
        ),
        "cmake.buildDirectory": "${workspaceFolder}/.build/cmake",
        "files.exclude": {"**/.build": True},
        "search.exclude": {"**/.build": True},
    }


def reference_template_files() -> dict[str, Any]:
    from vscode_task_config import cortex_debug_launch_document

    return {
        "settings.json": reference_settings(),
        "tasks.json": base_tasks("rp2040", "pico", []),
        "launch.json": cortex_debug_launch_document(
            "${workspaceFolder}/.build/firmware.elf"
        ),
        "extensions.json": extensions_for(),
        "keybindings.reference.json": keybindings_for(),
    }


def example_vscode_files(entry: dict[str, Any]) -> dict[str, Any]:
    target, board = default_target_board(entry)
    variants = entry.get("variants") if isinstance(entry.get("variants"), list) else []
    return {
        "jaszczurhal.project.json": manifest_for(entry),
        "settings.json": settings_for(str(entry["dir"])),
        "tasks.json": base_tasks(target, board, variants),
        "launch.json": launch_for(str(entry["dir"])),
        "keybindings.reference.json": keybindings_for(),
        "extensions.json": extensions_for(),
    }


def generated_file_mismatches() -> list[str]:
    mismatches: list[str] = []
    for name, data in reference_template_files().items():
        path = REFERENCE_VSCODE_DIR / name
        actual = path.read_text(encoding="utf-8") if path.is_file() else ""
        if actual != json_text(data):
            mismatches.append(path.relative_to(REPO_ROOT).as_posix())
    for entry in EXAMPLES:
        vscode_dir = EXAMPLES_DIR / str(entry["dir"]) / ".vscode"
        for name, data in example_vscode_files(entry).items():
            path = vscode_dir / name
            actual = path.read_text(encoding="utf-8") if path.is_file() else ""
            if actual != json_text(data):
                mismatches.append(path.relative_to(REPO_ROOT).as_posix())
    return mismatches


def sync_reference_template(*, check: bool) -> int:
    if check:
        mismatches = generated_file_mismatches()
        if mismatches:
            print(
                "error: generated VS Code file drift: " + ", ".join(mismatches),
                file=sys.stderr,
            )
            print(
                "Run: scripts/examples_dispatcher.py generate-template && "
                "scripts/examples_dispatcher.py generate",
                file=sys.stderr,
            )
            return 1
        return 0

    for name, data in reference_template_files().items():
        path = REFERENCE_VSCODE_DIR / name
        expected = json_text(data)
        write_text_lf(path, expected)
        print(f"generated {path.relative_to(REPO_ROOT)}", flush=True)
    return 0


def generate() -> int:
    for entry in EXAMPLES:
        example_dir = EXAMPLES_DIR / str(entry["dir"])
        if not example_dir.is_dir():
            print(f"error: missing example directory: {example_dir}", file=sys.stderr)
            return 1
        vscode_dir = example_dir / ".vscode"
        vscode_dir.mkdir(exist_ok=True)
        for name, data in example_vscode_files(entry).items():
            write_text_lf(vscode_dir / name, json_text(data))
        print(f"generated {example_dir.relative_to(REPO_ROOT)}", flush=True)
    return 0


def read_manifest(example_dir: Path) -> dict[str, Any]:
    return json.loads((example_dir / ".vscode" / "jaszczurhal.project.json").read_text(encoding="utf-8"))


def selected_example_dirs(names: list[str]) -> list[Path]:
    if names:
        return [EXAMPLES_DIR / name for name in names]
    return [EXAMPLES_DIR / str(entry["dir"]) for entry in EXAMPLES]


def command_label(example_dir: Path, target: str, variant: str | None) -> str:
    suffix = f":{variant}" if variant else ""
    return f"{example_dir.name}{suffix}@{target}"


def tail(path: Path, lines: int = 80) -> str:
    try:
        data = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    return "\n".join(data[-lines:])


def dispatcher_log_path(target: str, example_name: str) -> Path:
    return Path(tempfile.gettempdir()) / (
        f"jh_examples_dispatcher_{target}_{example_name}.log"
    )


def run_one_example(
    example_dir: Path,
    target: str,
    board: str | None,
    include_base: bool,
    variants: list[str],
    parallel_level: int,
    verbose: bool,
) -> tuple[bool, str, Path, float]:
    started = time.monotonic()
    log_path = dispatcher_log_path(target, example_dir.name)
    label = f"{example_dir.name}@{target}"
    base_command = [
        str(JH_VSCODE),
        "build",
        "--project",
        str(example_dir),
        "--target",
        target,
    ]
    if board:
        base_command.extend(["--board", board])
    commands: list[list[str]] = [base_command] if include_base else []
    for variant in variants:
        commands.append([*base_command, "--variant", variant])

    with log_path.open("w", encoding="utf-8") as log:
        for cmd in commands:
            if verbose:
                print("+ " + " ".join(cmd), file=log)
            result = subprocess.run(
                cmd,
                cwd=REPO_ROOT,
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
                env={
                    **os.environ,
                    "CMAKE_BUILD_PARALLEL_LEVEL": str(parallel_level),
                    "JH_VSCODE_MEMORY_OVERVIEW": "0",
                },
                check=False,
            )
            if result.returncode != 0:
                failed_variant = cmd[cmd.index("--variant") + 1] if "--variant" in cmd else None
                return (
                    False,
                    command_label(example_dir, target, failed_variant),
                    log_path,
                    time.monotonic() - started,
                )
    return True, label, log_path, time.monotonic() - started


def build(args: argparse.Namespace) -> int:
    examples = selected_example_dirs(args.example or [])
    groups: list[tuple[Path, str | None, bool, list[str]]] = []
    skipped: list[str] = []
    for example_dir in examples:
        if not example_dir.is_dir():
            print(f"error: missing example directory: {example_dir}", file=sys.stderr)
            return 1
        manifest = read_manifest(example_dir)
        example_meta = manifest.get("example") if isinstance(manifest.get("example"), dict) else {}
        targets = [str(item) for item in example_meta.get("targets", [])]
        if args.target not in targets:
            skipped.append(command_label(example_dir, args.target, None))
            continue
        declared_gate_targets = [
            str(item) for item in example_meta.get("gateTargets", targets)
        ]
        include_base = not args.gate or args.target in declared_gate_targets
        variants: list[str] = []
        for variant in example_meta.get("variants", []) if isinstance(example_meta.get("variants"), list) else []:
            if not isinstance(variant, dict) or not variant.get("id"):
                continue
            variant_targets = [str(item) for item in variant.get("targets", targets)]
            variant_gate_targets = [
                str(item)
                for item in variant.get("gateTargets", variant_targets)
            ]
            if args.target in variant_targets and (
                not args.gate or args.target in variant_gate_targets
            ):
                variants.append(str(variant["id"]))
        if not include_base and not variants:
            skipped.append(command_label(example_dir, args.target, None))
            continue
        boards = example_meta.get("boards")
        board = str(boards.get(args.target)) if isinstance(boards, dict) and boards.get(args.target) else None
        groups.append((example_dir, board, include_base, variants))

    if skipped:
        for item in skipped:
            print(
                f"skip {item} (unsupported target or outside gate)",
                flush=True,
            )
    if not groups:
        print(f"no examples to build for target {args.target}")
        return 0

    compiler_budget = max(1, int(args.jobs or 1))
    workers = min(2, compiler_budget, len(groups))
    parallel_level = max(1, compiler_budget // workers)
    print(
        f"scheduler: {workers} project worker(s), "
        f"{parallel_level} compiler job(s) each",
        flush=True,
    )
    failures: list[tuple[str, Path]] = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {
            pool.submit(
                run_one_example,
                example_dir,
                args.target,
                board,
                include_base,
                variants,
                parallel_level,
                args.verbose,
            ): example_dir
            for example_dir, board, include_base, variants in groups
        }
        for future in as_completed(futures):
            ok, label, log_path, elapsed = future.result()
            if ok:
                print(f"pass {label} ({elapsed:.1f}s)", flush=True)
            else:
                print(
                    f"fail {label} ({elapsed:.1f}s, log: {log_path})",
                    flush=True,
                )
                failures.append((label, log_path))

    if failures:
        label, log_path = failures[0]
        print(f"\nfirst failure: {label}", file=sys.stderr)
        print(tail(log_path), file=sys.stderr)
        return 1
    configurations = sum(
        (1 if include_base else 0) + len(variants)
        for _, _, include_base, variants in groups
    )
    print(
        f"built {configurations} configuration(s) from {len(groups)} "
        f"example project(s) for {args.target}",
        flush=True,
    )
    return 0


def list_examples() -> int:
    for entry in EXAMPLES:
        supported = example_targets(entry)
        selected_gate_targets = gate_targets(supported, entry.get("gateTargets"))
        print(
            f"{entry['dir']}: {', '.join(supported)} "
            f"(gate: {', '.join(selected_gate_targets)})"
        )
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("generate", help="Generate .vscode project files for all examples.")
    sub.add_parser(
        "generate-template",
        help="Regenerate shared files under vscode/examples.",
    )
    sub.add_parser(
        "check-template",
        help="Fail when shared or checked-in example VS Code files have drifted.",
    )
    sub.add_parser("list", help="List known examples and supported targets.")
    build_parser = sub.add_parser("build", help="Build examples through jh-vscode and the dispatcher.")
    build_parser.add_argument(
        "--target",
        required=True,
        choices=[
            "rp2040",
            "rp2350-arm",
            "rp2350-riscv",
            "stm32g474",
        ],
    )
    build_parser.add_argument("--example", action="append", help="Build only this example directory name.")
    build_parser.add_argument("--jobs", type=int, default=1)
    build_parser.add_argument(
        "--gate",
        action="store_true",
        help="Build only the representative gateTargets matrix.",
    )
    build_parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    try:
        validate_example_registry()
    except ValueError as error:
        parser.error(str(error))

    if args.command == "generate":
        return generate()
    if args.command == "generate-template":
        return sync_reference_template(check=False)
    if args.command == "check-template":
        return sync_reference_template(check=True)
    if args.command == "build":
        return build(args)
    if args.command == "list":
        return list_examples()
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
