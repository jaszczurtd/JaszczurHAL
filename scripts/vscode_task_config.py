#!/usr/bin/env python3
"""Shared VS Code task fragments derived from the board registry."""

from __future__ import annotations

from typing import Any


BOARD_SELECTION_INPUT_ID = "boardSelection"
SYNC_BOARD_PICKER_LABEL = "Project: Sync board picker"

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


def extensions_recommendations() -> dict[str, Any]:
    return {"recommendations": list(VSCODE_EXTENSION_RECOMMENDATIONS)}


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
    return {
        "label": SYNC_BOARD_PICKER_LABEL,
        "detail": "Refresh target/board options from the JaszczurHAL registry",
        "type": "shell",
        "command": "${config:jaszczurhal.vscodeEntry}",
        "args": [
            "sync-board-picker",
            "--project",
            "${workspaceFolder}",
        ],
        "runOptions": {
            "runOn": "folderOpen",
            "instanceLimit": 1,
        },
        "presentation": {
            "reveal": "never",
            "panel": "shared",
            "showReuseMessage": False,
        },
        "problemMatcher": [],
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
