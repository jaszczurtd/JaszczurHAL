#!/usr/bin/env python3
"""Shared VS Code task fragments derived from the board registry."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any


BOARD_SELECTION_INPUT_ID = "boardSelection"
SYNC_BOARD_PICKER_LABEL = "Project: Sync board picker"
MANAGED_DEBUG_PROFILE_NAMES = {
    "Project: Debug Firmware",
    "Project: Debug Firmware (RP2350 ARM)",
    "Project: Debug Firmware (STM32G474 / ST-Link)",
    "Debug: RP2040 (Pico/Pico W/Zero/Plus)",
    "Debug: RP2350 (Pico 2/Pico 2W)",
    "Debug: Attach RP2040 (no upload)",
    "Debug: Attach RP2350 (no upload)",
    "Debug: Attach RP2040 (bez uploadu)",
    "Debug: Attach RP2350 (bez uploadu)",
}
VSCODE_ENTRY_CONFIG = "${config:jaszczurhal.vscodeEntry}"
VSCODE_ENTRY_WINDOWS_CONFIG = "${config:jaszczurhal.vscodeEntryWindows}"

# Single source of truth for .vscode/extensions.json across every generator.
# cpptools drives IntelliSense over the patched compile database, cmake-tools is
# the C_Cpp.default.configurationProvider written into settings.json,
# cortex-debug owns the launch.json configuration and serial-monitor sits beside
# the jh-vscode monitor actions.
VSCODE_EXTENSION_RECOMMENDATIONS = [
    "ms-vscode.cpptools",
    "ms-vscode.cmake-tools",
    "marus25.cortex-debug",
    "ms-vscode.vscode-serial-monitor",
]


def write_text_lf(path: Path, content: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(content)


def extensions_recommendations() -> dict[str, Any]:
    return {"recommendations": list(VSCODE_EXTENSION_RECOMMENDATIONS)}


def cortex_debug_launch_document(
    executable: str,
    *,
    run_to_entry_point: str = "main",
) -> dict[str, Any]:
    profiles = (
        (
            "Project: Debug Firmware",
            "RP2040",
            ["interface/cmsis-dap.cfg", "target/rp2040.cfg"],
            ["adapter speed 5000"],
        ),
        (
            "Project: Debug Firmware (RP2350 ARM)",
            "RP2350",
            ["interface/cmsis-dap.cfg", "target/rp2350.cfg"],
            ["adapter speed 2000"],
        ),
        (
            "Project: Debug Firmware (STM32G474 / ST-Link)",
            "STM32G474RE",
            ["board/st_nucleo_g4.cfg"],
            ["reset_config srst_only srst_nogate connect_assert_srst"],
        ),
    )
    configurations = []
    for name, device, config_files, openocd_launch_commands in profiles:
        configuration = {
            "name": name,
            "type": "cortex-debug",
            "request": "launch",
            "cwd": "${workspaceFolder}",
            "executable": executable,
            "servertype": "openocd",
            "device": device,
            "configFiles": config_files,
            "runToEntryPoint": run_to_entry_point,
            "preLaunchTask": "Project: Build (Debug)",
        }
        if openocd_launch_commands:
            configuration["openOCDLaunchCommands"] = openocd_launch_commands
        configurations.append(configuration)
    return {"version": "0.2.0", "configurations": configurations}


def vscode_workspace_path(path: Path, workspace_dir: Path) -> str:
    try:
        relative = os.path.relpath(path, workspace_dir)
    except ValueError:
        return path.as_posix()
    relative_text = Path(relative).as_posix()
    if relative_text == ".":
        return "${workspaceFolder}"
    return f"${{workspaceFolder}}/{relative_text}"


def vscode_launch_executable(
    manifest: dict[str, Any],
    *,
    workspace_dir: Path | None = None,
    jh_root: Path | None = None,
) -> str:
    """Translate the tracked manifest artifact path into VS Code variables."""
    build_dir = manifest.get("buildDir") or "${project}/.build"
    if not isinstance(build_dir, str):
        raise ValueError("project manifest field 'buildDir' must be a string")
    artifacts = manifest.get("artifacts") or {}
    if not isinstance(artifacts, dict):
        raise ValueError("project manifest field 'artifacts' must be an object")
    executable = artifacts.get("elf") or "${buildDir}/firmware.elf"
    if not isinstance(executable, str):
        raise ValueError("project manifest field 'artifacts.elf' must be a string")
    executable = executable.replace("${buildDir}", build_dir)
    if "${jhRoot}" in executable:
        if workspace_dir is None or jh_root is None:
            raise ValueError(
                "manifest ELF path uses '${jhRoot}' without workspace context"
            )
        executable = executable.replace(
            "${jhRoot}",
            vscode_workspace_path(jh_root, workspace_dir),
        )
    return (
        executable.replace("${projectDir}", "${workspaceFolder}")
        .replace("${project}", "${workspaceFolder}")
        .replace("\\", "/")
    )


def sync_cortex_debug_launch_document(
    document: dict[str, Any],
    executable: str,
) -> bool:
    """Replace JaszczurHAL-owned profiles while preserving consumer profiles."""
    configurations = document.get("configurations")
    if configurations is None:
        configurations = []
    elif not isinstance(configurations, list):
        raise ValueError("launch.json field 'configurations' must be an array")

    custom_configurations = [
        configuration
        for configuration in configurations
        if not (
            isinstance(configuration, dict)
            and configuration.get("name") in MANAGED_DEBUG_PROFILE_NAMES
        )
    ]
    desired = cortex_debug_launch_document(executable)
    desired_configurations = desired["configurations"] + custom_configurations
    changed = False
    if document.get("version") != desired["version"]:
        document["version"] = desired["version"]
        changed = True
    if configurations != desired_configurations:
        document["configurations"] = desired_configurations
        changed = True
    return changed


def keybindings_reference() -> list[dict[str, str]]:
    bindings = [
        ("ctrl+shift+1", "Project: Build"),
        ("ctrl+shift+2", "Project: Upload"),
        ("ctrl+shift+3", "Project: Serial Monitor"),
        ("ctrl+shift+4", "Project: Upload (UF2 / BOOTSEL)"),
        ("ctrl+shift+5", "Project: Debug Probe Monitor"),
        ("ctrl+shift+6", "Project: Refresh IntelliSense"),
        ("ctrl+shift+7", "Project: Clean"),
        ("ctrl+shift+8", "Project: Upload (OTA)"),
        ("ctrl+shift+9", "Project: Config Dump"),
        ("ctrl+shift+alt+1", "Project: Select board (GUI)"),
        ("ctrl+shift+alt+2", "Project: Select board"),
        ("ctrl+shift+alt+3", "Project: Discover OTA devices"),
    ]
    return [
        {
            "key": key,
            "command": "workbench.action.tasks.runTask",
            "args": label,
        }
        for key, label in bindings
    ]


def vscode_entry_settings(unix_entry: str) -> dict[str, str]:
    return {
        "jaszczurhal.vscodeEntry": unix_entry,
        "jaszczurhal.vscodeEntryWindows": f"{unix_entry}.cmd",
        "cortex-debug.gdbPath.linux": "gdb-multiarch",
    }


def vscode_entry_task(
    *,
    label: str,
    detail: str,
    args: list[str],
    **extra: Any,
) -> dict[str, Any]:
    task: dict[str, Any] = {
        "label": label,
        "detail": detail,
        "type": "shell",
        "command": VSCODE_ENTRY_CONFIG,
        "windows": {"command": VSCODE_ENTRY_WINDOWS_CONFIG},
        "args": args,
    }
    task.update(extra)
    return task


def board_selection_values(
    registry: dict[str, dict[str, Any]],
    selected_target: str,
    selected_board: str,
) -> tuple[list[str], str]:
    options: list[str] = []
    default = ""
    sorted_targets = sorted(
        registry.items(),
        key=lambda item: (item[1].get("status") == "skeleton", item[0]),
    )
    for target, desc in sorted_targets:
        boards = [item for item in (desc.get("boards") or []) if isinstance(item, dict)]
        for board_desc in boards:
            board = str(board_desc.get("id") or "")
            if not board:
                continue
            label = f"{target}:{board} - {board_desc.get('displayName', board)}"
            if desc.get("status") == "skeleton":
                label += " (skeleton)"
            options.append(label)
            if target == selected_target and board == selected_board:
                default = label
    if not options:
        raise ValueError("target registry has no selectable boards")
    if not default:
        raise ValueError(
            f"target/board '{selected_target}:{selected_board}' is not selectable"
        )
    return options, default


def board_selection_input(
    registry: dict[str, dict[str, Any]],
    selected_target: str,
    selected_board: str,
) -> dict[str, Any]:
    options, default = board_selection_values(registry, selected_target, selected_board)
    return {
        "id": BOARD_SELECTION_INPUT_ID,
        "description": "Target/board",
        "type": "pickString",
        "options": options,
        "default": default,
    }


def sync_board_picker_task() -> dict[str, Any]:
    return vscode_entry_task(
        label=SYNC_BOARD_PICKER_LABEL,
        detail="Refresh target/board options and managed debug profiles",
        args=[
            "sync-board-picker",
            "--project",
            "${workspaceFolder}",
        ],
        runOptions={
            "runOn": "folderOpen",
            "instanceLimit": 1,
        },
        presentation={
            "reveal": "never",
            "panel": "shared",
            "showReuseMessage": False,
        },
        problemMatcher=[],
    )


def project_tasks_document(
    registry: dict[str, dict[str, Any]],
    selected_target: str,
    selected_board: str,
    *,
    module: str = "",
    usb_product: str = "",
    variants: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    subject = f" {module}" if module else ""
    tasks = [
        vscode_entry_task(
            label="Project: Build",
            detail=f"Compile{subject} through JaszczurHAL VS Code entry",
            args=["build", "--project", "${workspaceFolder}"],
            group={"kind": "build", "isDefault": True},
            problemMatcher="$gcc",
        ),
        vscode_entry_task(
            label="Project: Build (Debug)",
            detail=f"Debug build{subject} through JaszczurHAL VS Code entry",
            args=["build-debug", "--project", "${workspaceFolder}"],
            group="build",
            problemMatcher="$gcc",
        ),
        vscode_entry_task(
            label="Project: Upload",
            detail=(
                f"Upload{subject} through the active target backend"
                if module
                else "Upload through the active target backend"
            ),
            args=["upload", "--project", "${workspaceFolder}"],
            problemMatcher="$gcc",
        ),
        vscode_entry_task(
            label="Project: Upload (UF2 / BOOTSEL)",
            detail=(
                f"RP2040 only: build{subject} and copy UF2 to the single visible "
                "BOOTSEL drive"
            ),
            args=["upload-uf2", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Upload (OTA)",
            detail=(
                f"Build, authenticate, and upload{subject} to a discovered native RP device"
                if module
                else "Build, authenticate, and upload to one discovered native RP device"
            ),
            args=["upload-ota", "--project", "${workspaceFolder}", "--interactive"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Discover OTA devices",
            detail="List JaszczurHAL devices advertising native OTA",
            args=["ota-discover", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: List ports",
            detail="Show serial ports, identity matches, and BOOTSEL candidates",
            args=["list-ports", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Change port",
            detail="Interactively persist the upload/monitor serial port",
            args=["change-port", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Serial Monitor",
            detail=f"Persistent{subject} serial monitor" if module else "Persistent serial monitor",
            args=[
                "monitor",
                "--project",
                "${workspaceFolder}",
                "--lock-policy",
                "replace-own",
            ],
            isBackground=True,
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Debug Probe Monitor",
            detail="Debug Probe monitor through JaszczurHAL VS Code entry",
            args=[
                "monitor-probe",
                "--project",
                "${workspaceFolder}",
                "--lock-policy",
                "replace-own",
            ],
            isBackground=True,
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Serial Monitor (Any)",
            detail="Any serial monitor through JaszczurHAL VS Code entry",
            args=[
                "monitor-any",
                "--project",
                "${workspaceFolder}",
                "--lock-policy",
                "wait",
            ],
            isBackground=True,
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Refresh IntelliSense",
            detail=(
                f"Refresh{subject} IntelliSense through JaszczurHAL VS Code entry"
                if module
                else "Refresh IntelliSense through JaszczurHAL VS Code entry"
            ),
            args=["refresh-intellisense", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Clean",
            detail=f"Clean{subject} build directory" if module else "Clean build directory",
            args=["clean", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Clear USB Identity",
            detail=(
                f"Flash neutral firmware after verifying current {usb_product} identity"
                if usb_product
                else "Flash neutral firmware after verifying current USB identity"
            ),
            args=["clear-identity", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Config Dump",
            detail="Show resolved JaszczurHAL VS Code project configuration",
            args=["config-dump", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Select board",
            detail="Interactive target/board selection",
            args=["select-board", "--project", "${workspaceFolder}", "--interactive"],
            presentation={
                "echo": True,
                "reveal": "always",
                "focus": True,
                "panel": "shared",
                "showReuseMessage": False,
                "clear": True,
            },
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Select board (GUI)",
            detail="Pick target/board from the VS Code input menu",
            args=[
                "select-board",
                "--project",
                "${workspaceFolder}",
                "--selection",
                "${input:boardSelection}",
            ],
            problemMatcher=[],
        ),
        sync_board_picker_task(),
    ]
    for variant in variants or []:
        variant_id = str(variant.get("id") or "")
        if not variant_id:
            continue
        tasks.append(
            vscode_entry_task(
                label=f"Project: Build variant: {variant_id}",
                detail=f"Compile example variant {variant_id}",
                args=[
                    "build",
                    "--project",
                    "${workspaceFolder}",
                    "--variant",
                    variant_id,
                ],
                group="build",
                problemMatcher="$gcc",
            )
        )
    return {
        "version": "2.0.0",
        "inputs": [board_selection_input(registry, selected_target, selected_board)],
        "tasks": tasks,
    }


def sync_board_picker_document(
    document: dict[str, Any],
    registry: dict[str, dict[str, Any]],
    selected_target: str,
    selected_board: str,
) -> bool:
    inputs = document.get("inputs")
    changed = False
    if inputs is None:
        inputs = []
        document["inputs"] = inputs
        changed = True
    elif not isinstance(inputs, list):
        raise ValueError("tasks.json field 'inputs' must be an array")
    tasks = document.get("tasks")
    if not isinstance(tasks, list):
        raise ValueError("tasks.json field 'tasks' must be an array")

    desired_input = board_selection_input(registry, selected_target, selected_board)
    matching_inputs = [
        item
        for item in inputs
        if isinstance(item, dict) and item.get("id") == BOARD_SELECTION_INPUT_ID
    ]
    if len(matching_inputs) > 1:
        raise ValueError(f"tasks.json contains duplicate '{BOARD_SELECTION_INPUT_ID}' inputs")

    if matching_inputs:
        current_input = matching_inputs[0]
        for key, value in desired_input.items():
            if current_input.get(key) != value:
                current_input[key] = value
                changed = True
    else:
        inputs.append(desired_input)
        changed = True

    desired_task = sync_board_picker_task()
    matching_tasks = [
        item
        for item in tasks
        if isinstance(item, dict) and item.get("label") == SYNC_BOARD_PICKER_LABEL
    ]
    if len(matching_tasks) > 1:
        raise ValueError(f"tasks.json contains duplicate '{SYNC_BOARD_PICKER_LABEL}' tasks")

    if matching_tasks:
        current_task = matching_tasks[0]
        for key, value in desired_task.items():
            if current_task.get(key) != value:
                current_task[key] = value
                changed = True
    else:
        tasks.append(desired_task)
        changed = True
    return changed
