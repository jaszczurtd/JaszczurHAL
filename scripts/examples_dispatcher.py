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
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
EXAMPLES_DIR = REPO_ROOT / "examples"
JH_VSCODE = REPO_ROOT / "vscode" / "entry" / "jh-vscode"


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
    {"dir": "10_mqtt", "targets": ["rp2040"], "board": "picow"},
    {"dir": "11_wireguard", "targets": ["rp2040"], "board": "picow"},
    {"dir": "12_kv_store", "targets": ["rp2040", "stm32g474"]},
    {"dir": "13_i2c_slave", "targets": ["rp2040", "stm32g474"]},
    {"dir": "14_uart", "targets": ["rp2040", "stm32g474"]},
    {"dir": "15_wifi", "targets": ["rp2040"], "board": "picow"},
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
        "targets": ["rp2040"],
        "board": "picow",
        "module": "42_bsd_sockets_tcp_server",
        "sources": ["tcp_server.c", "bsd_socket_example_common.h"],
        "variants": [
            {
                "id": "tcp_client",
                "module": "42_bsd_sockets_tcp_client",
                "sources": ["tcp_client.c", "bsd_socket_example_common.h"],
                "targets": ["rp2040"],
            },
            {
                "id": "udp_server",
                "module": "42_bsd_sockets_udp_server",
                "sources": ["udp_server.c", "bsd_socket_example_common.h"],
                "targets": ["rp2040"],
            },
            {
                "id": "udp_client",
                "module": "42_bsd_sockets_udp_client",
                "sources": ["udp_client.c", "bsd_socket_example_common.h"],
                "targets": ["rp2040"],
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
    {"dir": "48_http_server", "targets": ["rp2040"], "board": "picow"},
    {"dir": "49_websocket", "targets": ["rp2040"], "board": "picow"},
    {"dir": "50_net_console", "targets": ["rp2040"], "board": "picow"},
    {"dir": "51_net_commands", "targets": ["rp2040"], "board": "picow"},
    {"dir": "52_http_files", "targets": ["rp2040"], "board": "picow"},
    {"dir": "53_simple_io_chips", "targets": ["rp2040", "stm32g474"]},
]


def json_text(data: Any) -> str:
    return json.dumps(data, indent=4, ensure_ascii=False) + "\n"


def target_registry() -> dict[str, dict[str, Any]]:
    registry: dict[str, dict[str, Any]] = {}
    for path in sorted((REPO_ROOT / "vscode" / "targets").glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict) and data.get("id"):
            registry[str(data["id"])] = data
    return registry


def board_selection_values(default_target: str, default_board: str) -> tuple[list[str], str]:
    registry = target_registry()
    options: list[str] = []
    default = ""
    for target, desc in sorted(registry.items(), key=lambda item: (item[1].get("status") == "skeleton", item[0])):
        for board_desc in desc.get("boards") or []:
            board = str(board_desc.get("id") or "")
            if not board:
                continue
            label = f"{target}:{board} - {board_desc.get('displayName', board)}"
            if desc.get("status") == "skeleton":
                label += " (skeleton)"
            options.append(label)
            if target == default_target and board == default_board:
                default = label
    return options, default or (options[0] if options else "")


def example_cache(entry: dict[str, Any], module: str) -> dict[str, Any]:
    cache: dict[str, Any] = {
        "JH_PROJECT_DIR": "${project}",
        "JH_MODULE_NAME": module,
    }
    if entry.get("sources"):
        cache["JH_PROJECT_SOURCES"] = ";".join(str(item) for item in entry["sources"])
    if entry.get("extraDefines"):
        cache["JH_EXTRA_DEFINES"] = ";".join(str(item) for item in entry["extraDefines"])
    cache.update(entry.get("cache") or {})
    return cache


def manifest_for(entry: dict[str, Any]) -> dict[str, Any]:
    name = str(entry["dir"])
    module = str(entry.get("module") or name)
    target = str(entry.get("target") or "rp2040")
    board = str(entry.get("board") or ("nucleo-g474re" if target == "stm32g474" else "pico"))
    example = {
        "targets": entry["targets"],
    }
    if entry.get("variants"):
        example["variants"] = entry["variants"]
    return {
        "$schema": "../../../vscode/schema/jh_vscode_project.schema.json",
        "project": "JaszczurHAL examples",
        "module": module,
        "example": example,
        "toolchain": "cmake",
        "target": target,
        "board": board,
        "buildDir": "${project}/.build",
        "cmakeBuildDir": "${project}/.build/cmake",
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


def settings_for() -> dict[str, Any]:
    return {
        "jaszczurhal.cliPath": "arduino-cli",
        "jaszczurhal.sketchbookPath": "",
        "jaszczurhal.buildDir": "${workspaceFolder}/.build",
        "jaszczurhal.verbose": False,
        "jaszczurhal.root": "../..",
        "jaszczurhal.vscodeEntry": "../../vscode/entry/jh-vscode",
        "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
        "C_Cpp.default.compileCommands": "${workspaceFolder}/.build/compile_commands_patched.json",
        "C_Cpp.errorSquiggles": "enabled",
        "cmake.sourceDirectory": "${workspaceFolder}/../../cmake/jh_firmware_project",
        "cmake.buildDirectory": "${workspaceFolder}/.build/cmake",
        "files.associations": {"*.ino": "cpp"},
        "files.exclude": {"**/.build": True},
        "search.exclude": {"**/.build": True},
    }


def base_tasks(default_target: str, default_board: str, variants: list[dict[str, Any]]) -> dict[str, Any]:
    options, default = board_selection_values(default_target, default_board)
    tasks: list[dict[str, Any]] = [
        {
            "label": "Project: Build",
            "detail": "Compile through JaszczurHAL VS Code entry",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["build", "--project", "${workspaceFolder}"],
            "group": {"kind": "build", "isDefault": True},
            "problemMatcher": "$gcc",
        },
        {
            "label": "Project: Build (Debug)",
            "detail": "Debug build through JaszczurHAL VS Code entry",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["build-debug", "--project", "${workspaceFolder}"],
            "group": "build",
            "problemMatcher": "$gcc",
        },
        {
            "label": "Project: Upload",
            "detail": "Upload through the active target backend",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["upload", "--project", "${workspaceFolder}"],
            "problemMatcher": "$gcc",
        },
        {
            "label": "Project: Upload (UF2 / BOOTSEL)",
            "detail": "RP2040 only: build and copy UF2 to the single visible BOOTSEL drive",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["upload-uf2", "--project", "${workspaceFolder}"],
            "problemMatcher": [],
        },
        {
            "label": "Project: List ports",
            "detail": "Show serial ports, identity matches, and BOOTSEL candidates",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["list-ports", "--project", "${workspaceFolder}"],
            "problemMatcher": [],
        },
        {
            "label": "Project: Serial Monitor",
            "detail": "Persistent serial monitor",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["monitor", "--project", "${workspaceFolder}", "--lock-policy", "replace-own"],
            "isBackground": True,
            "problemMatcher": [],
        },
        {
            "label": "Project: Debug Probe Monitor",
            "detail": "Debug Probe monitor through JaszczurHAL VS Code entry",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["monitor-probe", "--project", "${workspaceFolder}", "--lock-policy", "replace-own"],
            "isBackground": True,
            "problemMatcher": [],
        },
        {
            "label": "Project: Serial Monitor (Any)",
            "detail": "Any serial monitor through JaszczurHAL VS Code entry",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["monitor-any", "--project", "${workspaceFolder}", "--lock-policy", "wait"],
            "isBackground": True,
            "problemMatcher": [],
        },
        {
            "label": "Project: Refresh IntelliSense",
            "detail": "Refresh IntelliSense through JaszczurHAL VS Code entry",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["refresh-intellisense", "--project", "${workspaceFolder}"],
            "problemMatcher": [],
        },
        {
            "label": "Project: Clean",
            "detail": "Clean build directory",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["clean", "--project", "${workspaceFolder}"],
            "problemMatcher": [],
        },
        {
            "label": "Project: Config Dump",
            "detail": "Show resolved JaszczurHAL VS Code project configuration",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["config-dump", "--project", "${workspaceFolder}"],
            "problemMatcher": [],
        },
        {
            "label": "Project: Select board",
            "detail": "Interactive target/board selection",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["select-board", "--project", "${workspaceFolder}", "--interactive"],
            "presentation": {
                "echo": True,
                "reveal": "always",
                "focus": True,
                "panel": "shared",
                "showReuseMessage": False,
                "clear": True,
            },
            "problemMatcher": [],
        },
        {
            "label": "Project: Select board (GUI)",
            "detail": "Pick target/board from the VS Code input menu",
            "type": "shell",
            "command": "${config:jaszczurhal.vscodeEntry}",
            "args": ["select-board", "--project", "${workspaceFolder}", "--selection", "${input:boardSelection}"],
            "problemMatcher": [],
        },
    ]
    for variant in variants:
        variant_id = str(variant.get("id") or "")
        if not variant_id:
            continue
        tasks.append(
            {
                "label": f"Project: Build variant: {variant_id}",
                "detail": f"Compile example variant {variant_id}",
                "type": "shell",
                "command": "${config:jaszczurhal.vscodeEntry}",
                "args": ["build", "--project", "${workspaceFolder}", "--variant", variant_id],
                "group": "build",
                "problemMatcher": "$gcc",
            }
        )
    return {
        "version": "2.0.0",
        "inputs": [
            {
                "id": "boardSelection",
                "description": "Target/board",
                "type": "pickString",
                "options": options,
                "default": default,
            }
        ],
        "tasks": tasks,
    }


def launch_for() -> dict[str, Any]:
    return {
        "version": "0.2.0",
        "configurations": [
            {
                "name": "Project: Debug Firmware",
                "type": "cortex-debug",
                "request": "launch",
                "cwd": "${workspaceFolder}",
                "executable": "${workspaceFolder}/.build/firmware.elf",
                "servertype": "openocd",
                "device": "RP2040",
                "runToEntryPoint": "setup",
                "preLaunchTask": "Project: Build (Debug)",
            }
        ],
    }


def keybindings_for() -> list[dict[str, str]]:
    return [
        {"key": "ctrl+shift+1", "command": "workbench.action.tasks.runTask", "args": "Project: Build"},
        {"key": "ctrl+shift+2", "command": "workbench.action.tasks.runTask", "args": "Project: Upload"},
        {"key": "ctrl+shift+3", "command": "workbench.action.tasks.runTask", "args": "Project: Serial Monitor"},
        {"key": "ctrl+shift+4", "command": "workbench.action.tasks.runTask", "args": "Project: Upload (UF2 / BOOTSEL)"},
        {"key": "ctrl+shift+5", "command": "workbench.action.tasks.runTask", "args": "Project: Debug Probe Monitor"},
        {"key": "ctrl+shift+6", "command": "workbench.action.tasks.runTask", "args": "Project: Refresh IntelliSense"},
        {"key": "ctrl+shift+7", "command": "workbench.action.tasks.runTask", "args": "Project: Clean"},
        {"key": "ctrl+shift+8", "command": "workbench.action.tasks.runTask", "args": "Project: Config Dump"},
        {"key": "ctrl+shift+alt+1", "command": "workbench.action.tasks.runTask", "args": "Project: Select board (GUI)"},
        {"key": "ctrl+shift+alt+2", "command": "workbench.action.tasks.runTask", "args": "Project: Select board"},
    ]


def generate() -> int:
    for entry in EXAMPLES:
        example_dir = EXAMPLES_DIR / str(entry["dir"])
        if not example_dir.is_dir():
            print(f"error: missing example directory: {example_dir}", file=sys.stderr)
            return 1
        vscode_dir = example_dir / ".vscode"
        vscode_dir.mkdir(exist_ok=True)
        target = str(entry.get("target") or "rp2040")
        board = str(entry.get("board") or ("nucleo-g474re" if target == "stm32g474" else "pico"))
        variants = entry.get("variants") if isinstance(entry.get("variants"), list) else []
        files = {
            "jaszczurhal.project.json": manifest_for(entry),
            "settings.json": settings_for(),
            "tasks.json": base_tasks(target, board, variants),
            "launch.json": launch_for(),
            "keybindings.reference.json": keybindings_for(),
        }
        for name, data in files.items():
            (vscode_dir / name).write_text(json_text(data), encoding="utf-8")
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


def run_one_example(example_dir: Path, target: str, variants: list[str], verbose: bool) -> tuple[bool, str, Path]:
    log_path = Path("/tmp") / f"jh_examples_dispatcher_{target}_{example_dir.name}.log"
    label = f"{example_dir.name}@{target}"
    commands: list[list[str]] = [[str(JH_VSCODE), "build", "--project", str(example_dir), "--target", target]]
    for variant in variants:
        commands.append([str(JH_VSCODE), "build", "--project", str(example_dir), "--target", target, "--variant", variant])

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
    groups: list[tuple[Path, list[str]]] = []
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
        groups.append((example_dir, variants))

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
            pool.submit(run_one_example, example_dir, args.target, variants, args.verbose): example_dir
            for example_dir, variants in groups
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
        print(f"{entry['dir']}: {', '.join(entry['targets'])}")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("generate", help="Generate .vscode project files for all examples.")
    sub.add_parser("list", help="List known examples and supported targets.")
    build_parser = sub.add_parser("build", help="Build examples through jh-vscode and the dispatcher.")
    build_parser.add_argument("--target", required=True, choices=["rp2040", "stm32g474"])
    build_parser.add_argument("--example", action="append", help="Build only this example directory name.")
    build_parser.add_argument("--jobs", type=int, default=1)
    build_parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    if args.command == "generate":
        return generate()
    if args.command == "build":
        return build(args)
    if args.command == "list":
        return list_examples()
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
