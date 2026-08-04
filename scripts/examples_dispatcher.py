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
from typing import Any

from vscode_task_config import write_text_lf


REPO_ROOT = Path(__file__).resolve().parents[1]
EXAMPLES_DIR = REPO_ROOT / "examples"
JH_VSCODE = REPO_ROOT / "vscode" / "entry" / (
    "jh-vscode.cmd" if os.name == "nt" else "jh-vscode"
)
REFERENCE_VSCODE_DIR = REPO_ROOT / "vscode" / "examples"

RP_NATIVE_TARGETS = ["rp2040", "rp2350-arm", "rp2350-riscv"]
EXAMPLES: list[dict[str, Any]] = [
    {"dir": "01_blink", "targets": ["rp2040", "stm32g474"]},
    {"dir": "02_debug_helper", "targets": ["rp2040", "stm32g474"]},
    {"dir": "03_soft_timer_table", "targets": ["rp2040", "stm32g474"]},
    {"dir": "04_crypto", "targets": ["rp2040", "stm32g474"]},
    {"dir": "05_modem_A7670E", "targets": ["rp2040"]},
    {"dir": "06_ds18b20", "targets": ["rp2040", "stm32g474"]},
    {"dir": "07_gps", "targets": ["rp2040"]},
    {"dir": "08_thermocouple", "targets": ["rp2040", "stm32g474"]},
    {"dir": "09_display_tft", "targets": ["rp2040", "stm32g474"]},
    {
        "dir": "10_mqtt",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {
        "dir": "11_wireguard",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {"dir": "12_kv_store", "targets": ["rp2040", "stm32g474"]},
    {"dir": "13_i2c_slave", "targets": ["rp2040", "stm32g474"]},
    {"dir": "14_uart", "targets": ["rp2040", "stm32g474"]},
    {
        "dir": "15_wifi",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {"dir": "16_littlefs", "targets": ["rp2040", "stm32g474"], "extraDefines": ["HAL_ENABLE_LITTLEFS"]},
    {"dir": "17_pid_controller", "targets": ["rp2040", "stm32g474"]},
    {"dir": "18_rgb_led", "targets": ["rp2040", "stm32g474"]},
    {"dir": "19_timer_ext", "targets": ["rp2040", "stm32g474"]},
    {"dir": "20_i2c_scan", "targets": ["rp2040", "stm32g474"]},
    {"dir": "21_adc_read", "targets": ["rp2040", "stm32g474"]},
    {"dir": "22_gps_uart", "targets": ["rp2040", "stm32g474"]},
    {"dir": "23_external_adc_ads1115", "targets": ["rp2040", "stm32g474"]},
    {"dir": "24_can_mcp2515", "targets": ["rp2040", "stm32g474"]},
    {"dir": "25_display_oled", "targets": ["rp2040", "stm32g474"]},
    {"dir": "26_rtc_clock", "targets": ["rp2040", "stm32g474"]},
    {"dir": "27_rtc_ds3231", "targets": ["rp2040", "stm32g474"]},
    {"dir": "28_pga2311", "targets": ["rp2040", "stm32g474"]},
    {
        "dir": "29_freertos_smoke",
        "targets": ["rp2040", "stm32g474"],
        "extraDefines": ["HAL_ENABLE_FREERTOS", "HAL_ENABLE_APP_TASK1"],
        "cache": {"JH_RP2040_FREERTOS": True},
    },
    {"dir": "30_bh1750_light", "targets": ["rp2040", "stm32g474"]},
    {"dir": "31_hd44780", "targets": ["rp2040", "stm32g474"]},
    {"dir": "32_tsc2007_touch", "targets": ["rp2040", "stm32g474"]},
    {"dir": "33_stmpe610_touch", "targets": ["rp2040", "stm32g474"]},
    {"dir": "34_irsmall_decoder", "targets": ["rp2040", "stm32g474"]},
    {"dir": "35_cJSON", "targets": ["rp2040", "stm32g474"]},
    {"dir": "36_lodePNG", "targets": ["rp2040", "stm32g474"]},
    {"dir": "37_lodePNG_ili9341_base64", "targets": ["rp2040", "stm32g474"]},
    {"dir": "38_stm32g474_fdcan_native", "targets": ["stm32g474"], "target": "stm32g474", "board": "nucleo-g474re"},
    {"dir": "39_sdlogger", "targets": ["rp2040", "stm32g474"], "extraDefines": ["HAL_ENABLE_SDLOGGER"]},
    {"dir": "40_jpeg", "targets": ["rp2040", "stm32g474"]},
    {"dir": "41_jpeg_ili931_base64", "targets": ["rp2040", "stm32g474"]},
    {
        "dir": "42_bsd_sockets_tcp_udp",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
        "module": "42_bsd_sockets_tcp_server",
        "sources": ["tcp_server.c", "bsd_socket_example_common.h"],
        "variants": [
            {
                "id": "tcp_client",
                "module": "42_bsd_sockets_tcp_client",
                "sources": ["tcp_client.c", "bsd_socket_example_common.h"],
                "targets": ["rp2040", "stm32g474"],
            },
            {
                "id": "udp_server",
                "module": "42_bsd_sockets_udp_server",
                "sources": ["udp_server.c", "bsd_socket_example_common.h"],
                "targets": ["rp2040", "stm32g474"],
            },
            {
                "id": "udp_client",
                "module": "42_bsd_sockets_udp_client",
                "sources": ["udp_client.c", "bsd_socket_example_common.h"],
                "targets": ["rp2040", "stm32g474"],
            },
        ],
    },
    {"dir": "43_dht_temperature_humidity", "targets": ["rp2040", "stm32g474"]},
    {
        "dir": "44_dacless_audio",
        "targets": ["rp2040", "stm32g474"],
        "variants": [
            {
                "id": "polling",
                "module": "44_dacless_audio_polling",
                "extraDefines": ["HAL_ENABLE_DACLESS", "DACLESS_EXAMPLE_USE_DMA=0"],
                "targets": ["rp2040", "stm32g474"],
            }
        ],
    },
    {"dir": "45_swserial_loopback", "targets": ["rp2040", "stm32g474"]},
    {"dir": "46_mfrc522_rfid", "targets": ["rp2040", "stm32g474"]},
    {"dir": "47_pn532_nfc", "targets": ["rp2040", "stm32g474"]},
    {
        "dir": "48_http_server",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {
        "dir": "49_websocket",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {
        "dir": "50_net_console",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {
        "dir": "51_net_commands",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {
        "dir": "52_http_files",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {"dir": "53_simple_io_chips", "targets": ["rp2040", "stm32g474"]},
    {"dir": "54_adp5360_pmic", "targets": ["rp2040", "stm32g474"]},
    {"dir": "55_epd_display", "targets": ["rp2040", "stm32g474"]},
    {
        "dir": "56_http_https_client",
        "targets": ["rp2040", "stm32g474"],
        "board": "picow",
        "stm32Board": "nucleo-g474re-pim730",
    },
    {
        "dir": "57_ota",
        "targets": ["rp2040"],
        "board": "picow",
        "extraDefines": ["HAL_ENABLE_OTA"],
        "cache": {"JH_OTA_GENERATION": 1, "JH_OTA_VERSION": "example"},
        "ota": {
            "hostname": "jaszczurhal-ota",
            "port": 8266,
            "listenPort": 8266,
            "password": "change-this-ota-password",
        },
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
    if "rp2040" not in declared:
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
    }
    if entry.get("variants"):
        variants = []
        for item in entry["variants"]:
            variant = dict(item)
            variant["targets"] = example_targets(
                entry,
                [str(target) for target in item.get("targets", entry["targets"])],
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
    variants: list[str],
    verbose: bool,
) -> tuple[bool, str, Path]:
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
    commands: list[list[str]] = [base_command]
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
                env={**os.environ, "JH_VSCODE_MEMORY_OVERVIEW": "0"},
                check=False,
            )
            if result.returncode != 0:
                failed_variant = cmd[cmd.index("--variant") + 1] if "--variant" in cmd else None
                return False, command_label(example_dir, target, failed_variant), log_path
    return True, label, log_path


def build(args: argparse.Namespace) -> int:
    examples = selected_example_dirs(args.example or [])
    groups: list[tuple[Path, str | None, list[str]]] = []
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
        variants: list[str] = []
        for variant in example_meta.get("variants", []) if isinstance(example_meta.get("variants"), list) else []:
            if not isinstance(variant, dict) or not variant.get("id"):
                continue
            variant_targets = [str(item) for item in variant.get("targets", targets)]
            if args.target in variant_targets:
                variants.append(str(variant["id"]))
        boards = example_meta.get("boards")
        board = str(boards.get(args.target)) if isinstance(boards, dict) and boards.get(args.target) else None
        groups.append((example_dir, board, variants))

    if skipped:
        for item in skipped:
            print(f"skip {item} (unsupported target)", flush=True)
    if not groups:
        print(f"no examples to build for target {args.target}")
        return 0

    workers = max(1, int(args.jobs or 1))
    failures: list[tuple[str, Path]] = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {
            pool.submit(
                run_one_example,
                example_dir,
                args.target,
                board,
                variants,
                args.verbose,
            ): example_dir
            for example_dir, board, variants in groups
        }
        for future in as_completed(futures):
            ok, label, log_path = future.result()
            if ok:
                print(f"pass {label}", flush=True)
            else:
                print(f"fail {label} (log: {log_path})", flush=True)
                failures.append((label, log_path))

    if failures:
        label, log_path = failures[0]
        print(f"\nfirst failure: {label}", file=sys.stderr)
        print(tail(log_path), file=sys.stderr)
        return 1
    print(f"built {len(groups)} example project(s) for {args.target}", flush=True)
    return 0


def list_examples() -> int:
    for entry in EXAMPLES:
        print(f"{entry['dir']}: {', '.join(example_targets(entry))}")
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
    build_parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

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
