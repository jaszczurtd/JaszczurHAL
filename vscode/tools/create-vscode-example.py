#!/usr/bin/env python3
"""Generate a standalone JaszczurHAL VS Code example firmware project."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any


DEFAULT_PROJECT_NAME = "jaszczurhal-vscode-example"
DEFAULT_MODULE = "example"
DEFAULT_TARGET = "rp2040"
DEFAULT_USB_MANUFACTURER = "Jaszczur"
DEFAULT_USB_PRODUCT = "Example Project"


GENERATED_FILES = (
    ".gitignore",
    "README.md",
    "app.cpp",
    "hal_project_config.h",
    ".vscode/jaszczurhal.project.json",
    ".vscode/settings.json",
    ".vscode/tasks.json",
    ".vscode/launch.json",
    ".vscode/extensions.json",
    ".vscode/keybindings.reference.json",
)


def relpath(from_dir: Path, to_path: Path) -> str:
    destination = to_path.resolve()
    try:
        reference = os.path.relpath(destination, start=from_dir.resolve())
    except ValueError:
        # Windows cannot express a relative path between different volumes.
        reference = str(destination)
    return reference.replace(os.sep, "/")


def variable_path(variable: str, reference: str) -> str:
    if Path(reference).is_absolute():
        return reference
    return f"${{{variable}}}/{reference}"


def cmake_tools_cache_value(
    value: Any,
    *,
    jh_root_ref: str,
    project_ref: str,
    build_ref: str,
) -> Any:
    if not isinstance(value, str):
        return value
    return (
        value.replace("${jhRoot}", jh_root_ref)
        .replace("${project}", project_ref)
        .replace("${projectDir}", project_ref)
        .replace("${workspaceFolder}", project_ref)
        .replace("${buildDir}", build_ref)
    )


def target_board_cache(registry: dict[str, dict], target: str, board: str) -> dict[str, Any]:
    target_desc = registry.get(target) or {}
    cache: dict[str, Any] = dict(target_desc.get("cache") or {})
    for board_desc in target_desc.get("boards") or []:
        if isinstance(board_desc, dict) and board_desc.get("id") == board:
            cache.update(board_desc.get("cache") or {})
            break
    return cache


def cmake_tools_configure_settings(
    registry: dict[str, dict],
    *,
    target: str,
    board: str,
    module: str,
    jh_root_ref: str,
    project_ref: str = "${workspaceFolder}",
    build_ref: str = "${workspaceFolder}/.build",
) -> dict[str, Any]:
    settings = {
        key: cmake_tools_cache_value(
            value,
            jh_root_ref=jh_root_ref,
            project_ref=project_ref,
            build_ref=build_ref,
        )
        for key, value in target_board_cache(registry, target, board).items()
    }
    settings.update(
        {
            "JH_ROOT": jh_root_ref,
            "JH_PROJECT_DIR": project_ref,
            "JH_MODULE_NAME": module,
            "JH_TARGET": target,
            "JH_BOARD": board,
        }
    )
    return settings


def schema_reference(from_dir: Path, to_path: Path) -> str:
    reference = relpath(from_dir, to_path)
    if Path(reference).is_absolute():
        return to_path.resolve().as_uri()
    return reference


def sanitize_module(value: str) -> str:
    module = re.sub(r"[^A-Za-z0-9_]+", "_", value.strip()).strip("_")
    if not module:
        module = DEFAULT_MODULE
    if module[0].isdigit():
        module = f"_{module}"
    return module


def identity_hint(value: str) -> str:
    hint = re.sub(r"[^A-Za-z0-9]+", "_", value.strip()).strip("_")
    return hint or DEFAULT_USB_PRODUCT


def render_template(template: str, values: dict[str, str]) -> str:
    for key, value in values.items():
        template = template.replace(f"@@{key}@@", value)
    return template


def json_text(data: dict) -> str:
    return json.dumps(data, indent=4, sort_keys=False) + "\n"


def add_scripts_to_path(jh_root: Path) -> None:
    scripts_dir = jh_root / "scripts"
    if str(scripts_dir) not in sys.path:
        sys.path.insert(0, str(scripts_dir))


def load_target_registry(jh_root: Path) -> dict[str, dict]:
    add_scripts_to_path(jh_root)
    from board_registry import tooling_target_registry

    return tooling_target_registry(jh_root)


def load_extension_recommendations(jh_root: Path) -> dict:
    add_scripts_to_path(jh_root)
    from vscode_task_config import extensions_recommendations

    return extensions_recommendations()


def load_keybindings(jh_root: Path) -> list[dict[str, str]]:
    add_scripts_to_path(jh_root)
    from vscode_task_config import keybindings_reference

    return keybindings_reference()


def resolve_target_board(registry: dict[str, dict], target: str, board: str | None) -> tuple[str, str]:
    if target not in registry:
        known = ", ".join(sorted(registry)) or "(none)"
        raise RuntimeError(f"unknown target '{target}'. Known targets: {known}")

    desc = registry[target]
    boards = [item for item in (desc.get("boards") or []) if isinstance(item, dict)]
    board_ids = [str(item.get("id")) for item in boards if item.get("id")]
    selected = board or desc.get("defaultBoard")
    if not selected:
        raise RuntimeError(f"target '{target}' does not define a default board; pass --board")
    selected = str(selected)
    if board_ids and selected not in board_ids:
        raise RuntimeError(
            f"unknown board '{selected}' for target '{target}'. Known boards: {', '.join(board_ids)}"
        )
    return target, selected


def ensure_writable_output(output_dir: Path, force: bool) -> None:
    if not output_dir.exists():
        return
    if not output_dir.is_dir():
        raise RuntimeError(f"output path exists and is not a directory: {output_dir}")
    if force:
        return
    try:
        next(output_dir.iterdir())
    except StopIteration:
        return
    raise RuntimeError(
        f"output directory is not empty: {output_dir}\n"
        "Use --force to overwrite files generated by this tool."
    )


def build_files(
    *,
    output_dir: Path,
    project_name: str,
    module: str,
    target: str,
    board: str | None,
    usb_manufacturer: str,
    usb_product: str,
    by_id_hint: str,
) -> dict[str, str]:
    script_path = Path(__file__).resolve()
    jh_root = script_path.parents[2]
    vscode_dir = output_dir / ".vscode"
    registry = load_target_registry(jh_root)
    target, board = resolve_target_board(registry, target, board)
    from vscode_task_config import (
        cortex_debug_launch_document,
        project_tasks_document,
        vscode_entry_settings,
    )

    try:
        tasks_document = project_tasks_document(
            registry,
            target,
            board,
            module=module,
            usb_product=usb_product,
        )
    except ValueError as exc:
        raise RuntimeError(str(exc)) from exc
    values = {
        "PROJECT_NAME": project_name,
        "MODULE": module,
        "TARGET": target,
        "BOARD": board,
        "USB_MANUFACTURER": usb_manufacturer,
        "USB_PRODUCT": usb_product,
        "BY_ID_HINT": by_id_hint,
        "JH_ROOT_REL": relpath(output_dir, jh_root),
        "JH_DISPATCHER_REL": relpath(output_dir, jh_root / "cmake" / "jh_firmware_project"),
        "JH_ENTRY_REL": relpath(output_dir, jh_root / "vscode" / "entry" / "jh-vscode"),
        "SCHEMA_REL": schema_reference(
            vscode_dir,
            jh_root / "vscode" / "schema" / "jh_vscode_project.schema.json",
        ),
    }
    jh_root_ref = variable_path("workspaceFolder", values["JH_ROOT_REL"])

    files: dict[str, str] = {
        ".gitignore": GITIGNORE_TEMPLATE,
        "README.md": render_template(README_TEMPLATE, values),
        "app.cpp": render_template(APP_CPP_TEMPLATE, values),
        "hal_project_config.h": HAL_PROJECT_CONFIG_TEMPLATE,
        ".vscode/jaszczurhal.project.json": json_text(
            {
                "$schema": values["SCHEMA_REL"],
                "project": project_name,
                "module": module,
                "toolchain": "cmake",
                "target": target,
                "board": board,
                "buildDir": "${project}/.build",
                "cmakeBuildDir": "${project}/.build/cmake",
                "cmake": {
                    "sourceDir": variable_path(
                        "project", values["JH_DISPATCHER_REL"]
                    ),
                    "cache": {
                        "JH_PROJECT_DIR": "${project}",
                        "JH_MODULE_NAME": module,
                    },
                },
                "identity": {
                    "enabled": True,
                    "usbManufacturer": usb_manufacturer,
                    "usbProduct": usb_product,
                    "byIdHint": by_id_hint,
                },
                "artifacts": {
                    "elf": "${buildDir}/firmware.elf",
                    "uf2": "${buildDir}/firmware.uf2",
                    "compileCommands": "${buildDir}/compile_commands_patched.json",
                },
            }
        ),
        ".vscode/settings.json": json_text(
            {
                "jaszczurhal.buildDir": "${workspaceFolder}/.build",
                "jaszczurhal.verbose": False,
                "jaszczurhal.root": values["JH_ROOT_REL"],
                **vscode_entry_settings(values["JH_ENTRY_REL"]),
                "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
                "C_Cpp.default.compileCommands": "${workspaceFolder}/.build/compile_commands_patched.json",
                "cmake.sourceDirectory": variable_path(
                    "workspaceFolder", values["JH_DISPATCHER_REL"]
                ),
                "cmake.buildDirectory": (
                    f"${{workspaceFolder}}/.build/cmake-tools/{target}-{board}"
                ),
                "cmake.generator": "Ninja",
                "cmake.configureSettings": cmake_tools_configure_settings(
                    registry,
                    target=target,
                    board=board,
                    module=module,
                    jh_root_ref=jh_root_ref,
                ),
            }
        ),
        ".vscode/tasks.json": json_text(tasks_document),
        ".vscode/launch.json": json_text(
            cortex_debug_launch_document("${workspaceFolder}/.build/firmware.elf")
        ),
        ".vscode/extensions.json": json_text(load_extension_recommendations(jh_root)),
        ".vscode/keybindings.reference.json": json.dumps(
            load_keybindings(jh_root),
            indent=4,
        )
        + "\n",
    }
    return files


def write_files(output_dir: Path, files: dict[str, str], dry_run: bool) -> None:
    add_scripts_to_path(Path(__file__).resolve().parents[2])
    from vscode_task_config import write_text_lf

    for rel, content in files.items():
        path = output_dir / rel
        if dry_run:
            print(path)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        write_text_lf(path, content)


def configure_console_streams() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if callable(reconfigure):
            reconfigure(errors="backslashreplace")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a standalone JaszczurHAL VS Code example firmware project."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path.cwd() / DEFAULT_PROJECT_NAME,
        help=f"Target project directory, default: ./{DEFAULT_PROJECT_NAME}",
    )
    parser.add_argument("--name", default=DEFAULT_PROJECT_NAME, help="Project name stored in the manifest.")
    parser.add_argument("--module", default=DEFAULT_MODULE, help="Firmware module/sketch name, default: example.")
    parser.add_argument("--target", default=DEFAULT_TARGET, help=f"Initial target id, default: {DEFAULT_TARGET}.")
    parser.add_argument("--board", default="", help="Initial board id; defaults to the selected target's default board.")
    parser.add_argument(
        "--usb-manufacturer",
        default=DEFAULT_USB_MANUFACTURER,
        help=f"USB manufacturer descriptor, default: {DEFAULT_USB_MANUFACTURER}.",
    )
    parser.add_argument(
        "--usb-product",
        default=DEFAULT_USB_PRODUCT,
        help=f"USB product descriptor, default: {DEFAULT_USB_PRODUCT}.",
    )
    parser.add_argument(
        "--by-id-hint",
        default="",
        help="Substring expected in /dev/serial/by-id; defaults to the USB product with spaces as underscores.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite files generated by this tool in an existing directory.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print generated file paths without writing them.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    configure_console_streams()
    args = parse_args(argv)
    output_dir = args.output.expanduser().resolve()
    module = sanitize_module(args.module)
    by_id_hint = args.by_id_hint.strip() or identity_hint(args.usb_product)

    try:
        ensure_writable_output(output_dir, args.force)
        files = build_files(
            output_dir=output_dir,
            project_name=args.name.strip() or DEFAULT_PROJECT_NAME,
            module=module,
            target=args.target.strip() or DEFAULT_TARGET,
            board=args.board.strip() or None,
            usb_manufacturer=args.usb_manufacturer.strip() or DEFAULT_USB_MANUFACTURER,
            usb_product=args.usb_product.strip() or DEFAULT_USB_PRODUCT,
            by_id_hint=by_id_hint,
        )
        write_files(output_dir, files, args.dry_run)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not args.dry_run:
        print(f"Generated JaszczurHAL VS Code example: {output_dir}")
        print(f"Run: {relpath(output_dir, Path(__file__).resolve().parents[2] / 'vscode' / 'entry' / 'jh-vscode')} build --project {output_dir}")
    return 0


GITIGNORE_TEMPLATE = """.build/
build/
.vscode/.jh-upload-in-progress
.vscode/jaszczurhal.local.json
.vscode/c_cpp_properties.json
compile_commands.json
compile_commands_patched.json
*.elf
*.bin
*.uf2
*.map
"""


README_TEMPLATE = """# @@PROJECT_NAME@@

Standalone JaszczurHAL VS Code example project generated by
`libraries/JaszczurHAL/vscode/tools/create-vscode-example.py`.

The project demonstrates the current firmware workflow:

- The manifest points CMake at JaszczurHAL's shared firmware dispatcher.
- Target/board selection starts at `@@TARGET@@:@@BOARD@@` and can be changed
  without editing tracked files.
- `jh-vscode` owns build, debug build, upload, UF2/OTA upload, discovery, monitor, clean, and IntelliSense refresh.
- The firmware identity is `@@USB_MANUFACTURER@@ @@USB_PRODUCT@@`.
- Application code lives in `app.cpp` and uses `app_start` / `app_task0`.

## Build

```bash
@@JH_ENTRY_REL@@ build --project "$PWD"
@@JH_ENTRY_REL@@ build-debug --project "$PWD"
@@JH_ENTRY_REL@@ refresh-intellisense --project "$PWD"
```

## Target / Board

```bash
@@JH_ENTRY_REL@@ select-board --project "$PWD" --interactive
@@JH_ENTRY_REL@@ select-board --project "$PWD" --target stm32g474 --board nucleo-g474re
@@JH_ENTRY_REL@@ config-dump --project "$PWD"
```

The selection is stored in `.vscode/jaszczurhal.local.json`, which is ignored by
the generated `.gitignore`. `Project: Sync board picker` refreshes the tracked
GUI options and repairs the managed RP2040, RP2350 ARM, and STM32G474/ST-Link
debugger profiles when a trusted workspace opens. Consumer-owned debugger
profiles are preserved. VS Code may request one-time approval for automatic
tasks.

## Flashing

Check visible serial ports and BOOTSEL candidates before flashing:

```bash
@@JH_ENTRY_REL@@ list-ports --project "$PWD"
```

Default serial upload is identity-guarded. After the board already runs this
example firmware, use:

```bash
@@JH_ENTRY_REL@@ upload --project "$PWD"
```

For RP2040 first flash of a blank board, either use BOOTSEL:

```bash
@@JH_ENTRY_REL@@ upload-uf2 --project "$PWD"
```

or make an explicit serial decision:

```bash
@@JH_ENTRY_REL@@ upload --project "$PWD" --port /dev/ttyACM0 --allow-unverified-port
```

The explicit-port path is intentionally manual because it can overwrite any
board connected on that port.

The generated task set also contains `Project: Upload (OTA)` and
`Project: Discover OTA devices`. Configure `HAL_ENABLE_OTA`, OTA artifact
metadata, and credentials according to
[`OTAWorkflow.md`](@@JH_ROOT_REL@@/doc/en/OTAWorkflow.md) before using them.
"""


APP_CPP_TEMPLATE = """#include <JaszczurHAL.h>
#include <hal/core/hal_app.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

extern "C" void app_start(void) {
    hal_debug_init_default();
    hal_debug_set_module_prefix("@@MODULE@@");
    hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
    hal_deb("JaszczurHAL VS Code example started");
}

extern "C" void app_task0(void) {
    static bool led_on = false;

    led_on = !led_on;
    hal_gpio_write(HAL_LED_BUILTIN, led_on);
    hal_deb("blink");
    hal_delay_ms(500u);
}
"""


HAL_PROJECT_CONFIG_TEMPLATE = """#pragma once

#ifndef HAL_PROVIDE_APP_ENTRY
#define HAL_PROVIDE_APP_ENTRY
#endif
"""


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
