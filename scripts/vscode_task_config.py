#!/usr/bin/env python3
"""Shared VS Code task fragments derived from the board registry."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any

import project_config


BOARD_SELECTION_INPUT_ID = "boardSelection"
SYNC_BOARD_PICKER_LABEL = "Project: Sync board picker"
CHECK_DEVICE_TREE_LABEL = "Project: Check device tree"
DEBUG_BUILD_LABEL = "Project: Build (Debug)"
# Every Cortex-Debug profile JaszczurHAL writes starts with this prefix; the
# synchronization replaces them and keeps profiles with other names.
DEBUG_PROFILE_PREFIX = "Project: Debug "
# Probe settings of each target a profile debugs: label, Cortex-Debug device,
# OpenOCD configuration files and launch commands. ESP32-S3 and RP2350
# RISC-V have no profile.
DEBUG_TARGETS = {
    "rp2040": (
        "RP2040",
        "RP2040",
        ("interface/cmsis-dap.cfg", "target/rp2040.cfg"),
        ("adapter speed 5000",),
    ),
    "rp2350-arm": (
        "RP2350 ARM",
        "RP2350",
        ("interface/cmsis-dap.cfg", "target/rp2350.cfg"),
        ("adapter speed 2000",),
    ),
    "stm32g474": (
        "STM32G474 / ST-Link",
        "STM32G474RE",
        ("board/st_nucleo_g4.cfg",),
        ("reset_config srst_only srst_nogate connect_assert_srst",),
    ),
}
BOARD_DEBUG_PROFILE_NAMES = {
    "rp2040": "Project: Debug Firmware",
    "rp2350-arm": "Project: Debug Firmware (RP2350 ARM)",
    "stm32g474": "Project: Debug Firmware (STM32G474 / ST-Link)",
}
MANAGED_DEBUG_PROFILE_NAMES = {
    *BOARD_DEBUG_PROFILE_NAMES.values(),
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


def cortex_debug_configuration(
    name: str,
    target: str,
    executable: str,
    pre_launch_task: str,
    *,
    run_to_entry_point: str = "main",
) -> dict[str, Any]:
    """One Cortex-Debug profile with the probe settings of ``target``."""
    _, device, config_files, openocd_launch_commands = DEBUG_TARGETS[target]
    configuration = {
        "name": name,
        "type": "cortex-debug",
        "request": "launch",
        "cwd": "${workspaceFolder}",
        "executable": executable,
        "servertype": "openocd",
        "device": device,
        "configFiles": list(config_files),
        "runToEntryPoint": run_to_entry_point,
        "preLaunchTask": pre_launch_task,
    }
    if openocd_launch_commands:
        configuration["openOCDLaunchCommands"] = list(openocd_launch_commands)
    return configuration


def cortex_debug_launch_document(
    executable: str,
    *,
    run_to_entry_point: str = "main",
) -> dict[str, Any]:
    """The profiles of a project without device_tree.json: one per debugged
    target, all for the ELF of the selected board."""
    configurations = [
        cortex_debug_configuration(
            name,
            target,
            executable,
            DEBUG_BUILD_LABEL,
            run_to_entry_point=run_to_entry_point,
        )
        for target, name in BOARD_DEBUG_PROFILE_NAMES.items()
    ]
    return {"version": "0.2.0", "configurations": configurations}


def debug_build_label(assembly: str, variant: str | None) -> str:
    suffix = f" variant: {variant}" if variant else ""
    return f"{DEBUG_BUILD_LABEL} {assembly}{suffix}"


def debug_profile_name(target: str, assembly: str, variant: str | None) -> str:
    suffix = f" variant: {variant}" if variant else ""
    return f"{DEBUG_PROFILE_PREFIX}{assembly}{suffix} ({DEBUG_TARGETS[target][0]})"


def assembly_debug_builds(project_dir: Path) -> list[tuple[str, str, str | None]]:
    """(target, assembly, variant) of every build of a device tree project
    that a probe debugs: each assembly of a debugged target with the base and
    every variant its configuration accepts."""
    import device_tree_project

    return [
        (build.target.id, build.assembly, build.variant.id if build.variant else None)
        for build in device_tree_project.builds(project_dir, project_config.load_targets())
        if build.target.id in DEBUG_TARGETS
    ]


def assembly_launch_document(
    manifest: dict[str, Any],
    debug_builds: list[tuple[str, str, str | None]],
    *,
    workspace_dir: Path | None = None,
    jh_root: Path | None = None,
    default_assembly: str | None = None,
) -> dict[str, Any]:
    """The profiles of a device tree project: one per debugged assembly and
    variant, each for that build's own ELF and built first by its own task,
    so a profile never loads an image of another assembly or MCU. The
    ``default_assembly`` comes first, as VS Code preselects the first."""
    ordered = sorted(debug_builds, key=lambda build: build[1] != default_assembly)
    configurations = [
        cortex_debug_configuration(
            debug_profile_name(target, assembly, variant),
            target,
            vscode_launch_executable(
                manifest,
                workspace_dir=workspace_dir,
                jh_root=jh_root,
                assembly=(target, assembly),
                variant=variant,
            ),
            debug_build_label(assembly, variant),
        )
        for target, assembly, variant in ordered
    ]
    return {"version": "0.2.0", "configurations": configurations}


def is_managed_debug_profile(configuration: Any) -> bool:
    if not isinstance(configuration, dict):
        return False
    name = configuration.get("name")
    return isinstance(name, str) and (
        name in MANAGED_DEBUG_PROFILE_NAMES or name.startswith(DEBUG_PROFILE_PREFIX)
    )


def vscode_workspace_path(path: Path, workspace_dir: Path) -> str:
    try:
        relative = os.path.relpath(path, workspace_dir)
    except ValueError:
        return path.as_posix()
    relative_text = Path(relative).as_posix()
    if relative_text == ".":
        return "${workspaceFolder}"
    return f"${{workspaceFolder}}/{relative_text}"


def cmake_tools_cache_value(
    value: Any,
    *,
    jh_root_ref: str,
    project_ref: str,
    build_ref: str,
) -> Any:
    """Translate project-manifest variables for VS Code CMake Tools."""
    if not isinstance(value, str):
        return value
    return (
        value.replace("${jhRoot}", jh_root_ref)
        .replace("${project}", project_ref)
        .replace("${projectDir}", project_ref)
        .replace("${workspaceFolder}", project_ref)
        .replace("${buildDir}", build_ref)
    )


def target_board_cache(
    registry: dict[str, dict[str, Any]],
    target: str,
    board: str,
) -> dict[str, Any]:
    """Return the merged target and board cache defaults from the registry."""
    target_desc = registry.get(target) or {}
    cache: dict[str, Any] = dict(target_desc.get("cache") or {})
    for board_desc in target_desc.get("boards") or []:
        if isinstance(board_desc, dict) and board_desc.get("id") == board:
            cache.update(board_desc.get("cache") or {})
            break
    return cache


def cmake_tools_configure_settings(
    registry: dict[str, dict[str, Any]],
    *,
    target: str,
    board: str,
    module: str,
    jh_root_ref: str,
    project_ref: str = "${workspaceFolder}",
    build_ref: str = "${workspaceFolder}/.build",
    project_cache: dict[str, Any] | None = None,
    assembly: str | None = None,
) -> dict[str, Any]:
    """Build the initial dispatcher cache consumed directly by CMake Tools.
    A device tree project passes its ``assembly``: CMake resolves the board
    from it and refuses JH_BOARD, and like jh-vscode the build publishes into
    the assembly's own <build_ref>/<target>/<assembly>, so two assemblies never
    share artifacts."""
    artifacts = (project_cache or {}).get("JH_ARTIFACT_DIR")
    if assembly:
        build_ref = f"{build_ref}/{target}/{assembly}"

    def translate(cache: dict[str, Any]) -> dict[str, Any]:
        return {
            key: cmake_tools_cache_value(
                value,
                jh_root_ref=jh_root_ref,
                project_ref=project_ref,
                build_ref=build_ref,
            )
            for key, value in cache.items()
        }

    settings = translate(target_board_cache(registry, target, board))
    settings.update(translate(project_cache or {}))
    settings.update(
        {
            "JH_ROOT": jh_root_ref,
            "JH_PROJECT_DIR": project_ref,
            "JH_MODULE_NAME": module,
            "JH_TARGET": target,
        }
    )
    if assembly:
        settings.pop("JH_BOARD", None)
        settings["JH_ASSEMBLY"] = assembly
        # Without its own value CMake publishes into <project>/.build, which
        # every assembly shares; a directory outside buildDir gets the suffix.
        if not artifacts:
            settings["JH_ARTIFACT_DIR"] = build_ref
        elif "${buildDir}" not in str(artifacts):
            settings["JH_ARTIFACT_DIR"] = f"{settings['JH_ARTIFACT_DIR']}/{target}/{assembly}"
    else:
        settings["JH_BOARD"] = board
    return settings


def assembly_build_dir(manifest: dict[str, Any], target: str, assembly: str) -> str:
    """The unexpanded buildDir of one assembly of a device tree project:
    <base>/<target>/<assembly>, where the base is the target's targetProfiles
    buildDir, else the manifest's, else ${project}/.build. Each assembly then
    keeps its artifacts and everything else derived from ${buildDir}."""
    base = manifest.get("buildDir") or "${project}/.build"
    profiles = manifest.get("targetProfiles")
    overlay = profiles.get(target) if isinstance(profiles, dict) else None
    if isinstance(overlay, dict) and overlay.get("buildDir"):
        base = overlay["buildDir"]
    if not isinstance(base, str):
        raise ValueError("project manifest field 'buildDir' must be a string")
    return os.path.join(base, target, assembly)


def vscode_launch_executable(
    manifest: dict[str, Any],
    *,
    workspace_dir: Path | None = None,
    jh_root: Path | None = None,
    assembly: tuple[str, str] | None = None,
    variant: str | None = None,
) -> str:
    """Translate the tracked manifest artifact path into VS Code variables;
    ``assembly`` is the (target, key) whose own buildDir holds the ELF, and a
    ``variant`` builds into its variants/<id> below it."""
    build_dir = manifest.get("buildDir") or "${project}/.build"
    if assembly is not None:
        build_dir = assembly_build_dir(manifest, *assembly)
    if not isinstance(build_dir, str):
        raise ValueError("project manifest field 'buildDir' must be a string")
    if variant:
        build_dir = f"{build_dir}/variants/{variant}"
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
    desired: dict[str, Any],
) -> bool:
    """Replace JaszczurHAL-owned profiles with the ``desired`` launch
    document's while preserving consumer profiles."""
    configurations = document.get("configurations")
    if configurations is None:
        configurations = []
    elif not isinstance(configurations, list):
        raise ValueError("launch.json field 'configurations' must be an array")

    custom_configurations = [
        configuration
        for configuration in configurations
        if not is_managed_debug_profile(configuration)
    ]
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
        "description": "Select a target architecture and board",
        "type": "pickString",
        "options": options,
        "default": default,
    }


def assembly_selection_input(project_dir: Path, selected_assembly: str | None) -> dict[str, Any]:
    """The picker of a project with device_tree.json: its assemblies, each
    with the target and board it fixes. Without a selected assembly the first
    one is the default."""
    import device_tree_project

    items = device_tree_project.assemblies(project_dir)
    names = [item["name"] for item in items]
    if selected_assembly is not None and selected_assembly not in names:
        raise ValueError(f"assembly '{selected_assembly}' is not in {project_dir}/device_tree.json")
    options = [f"{item['name']} - {item['target']}/{item['board']}" for item in items]
    return {
        "id": BOARD_SELECTION_INPUT_ID,
        "description": "Select an assembly of device_tree.json",
        "type": "pickString",
        "options": options,
        "default": options[names.index(selected_assembly or names[0])],
    }


def selection_input(
    registry: dict[str, dict[str, Any]],
    selected_target: str,
    selected_board: str,
    project_dir: Path | None,
    selected_assembly: str | None,
) -> dict[str, Any]:
    """The assembly picker with device_tree.json, the board picker without."""
    import device_tree_project

    if device_tree_project.device_tree_path(project_dir) is not None:
        return assembly_selection_input(Path(project_dir), selected_assembly)
    return board_selection_input(registry, selected_target, selected_board)


def sync_board_picker_task() -> dict[str, Any]:
    return vscode_entry_task(
        label=SYNC_BOARD_PICKER_LABEL,
        detail="Update the board-selection list and managed debug configurations.",
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


# Problems of check-hardware: "<file>:<line>:<column>: error: <message>",
# replaced after every check the task reports between its two markers.
DEVICE_TREE_PROBLEM_MATCHER = {
    "owner": "jaszczurhal-device-tree",
    "source": "device_tree.json",
    "fileLocation": ["autoDetect", "${workspaceFolder}"],
    "pattern": {
        "regexp": r"^(.+?):(\d+):(\d+): (error|warning): (.*)$",
        "file": 1,
        "line": 2,
        "column": 3,
        "severity": 4,
        "message": 5,
    },
    "background": {
        "activeOnStart": True,
        "beginsPattern": r"^jh-vscode check-hardware: checking ",
        "endsPattern": r"^jh-vscode check-hardware: (no problems|\d+ problems?)$",
    },
}


def check_device_tree_task() -> dict[str, Any]:
    return vscode_entry_task(
        label=CHECK_DEVICE_TREE_LABEL,
        detail=(
            "Check every assembly of device_tree.json after each save and list "
            "the problems at their place in the file."
        ),
        args=["check-hardware", "--project", "${workspaceFolder}", "--watch"],
        isBackground=True,
        runOptions={
            "runOn": "folderOpen",
            "instanceLimit": 1,
        },
        presentation={
            "reveal": "never",
            "panel": "dedicated",
            "showReuseMessage": False,
        },
        problemMatcher=DEVICE_TREE_PROBLEM_MATCHER,
    )


def debug_build_task(assembly: str, variant: str | None) -> dict[str, Any]:
    """The hidden pre-launch task of one assembly's debug profile."""
    subject = f"the {assembly} assembly" + (f", variant {variant}," if variant else "")
    return vscode_entry_task(
        label=debug_build_label(assembly, variant),
        detail=f"Build {subject} with debug settings for its debug profile.",
        args=[
            "build-debug",
            "--project",
            "${workspaceFolder}",
            "--assembly",
            assembly,
            *(["--variant", variant] if variant else []),
        ],
        hide=True,
        problemMatcher="$gcc",
    )


def assembly_tasks(debug_builds: list[tuple[str, str, str | None]]) -> list[dict[str, Any]]:
    """Tasks only a device tree project has: the device tree check and the
    debug build of each debugged assembly and variant."""
    return [
        check_device_tree_task(),
        *(debug_build_task(assembly, variant) for _, assembly, variant in debug_builds),
    ]


def is_assembly_task(task: Any) -> bool:
    if not isinstance(task, dict):
        return False
    label = task.get("label")
    return isinstance(label, str) and (
        label == CHECK_DEVICE_TREE_LABEL or label.startswith(f"{DEBUG_BUILD_LABEL} ")
    )


def project_tasks_document(
    registry: dict[str, dict[str, Any]],
    selected_target: str,
    selected_board: str,
    *,
    module: str = "",
    usb_product: str = "",
    project_dir: Path | None = None,
    selected_assembly: str | None = None,
) -> dict[str, Any]:
    """Tasks of one project; ``project_dir`` adds a build and an upload task
    for each variant its hal_project_config.h declares. With device_tree.json
    in ``project_dir`` the selector offers its assemblies, defaulting to
    ``selected_assembly``."""
    import device_tree_project

    subject = f" the {module}" if module else ""
    choice = (
        "an assembly of device_tree.json (it fixes target and board)"
        if device_tree_project.device_tree_path(project_dir) is not None
        else "a target architecture and board"
    )
    tasks = [
        vscode_entry_task(
            label="Project: Build",
            detail=f"Build{subject} firmware for the selected board.",
            args=["build", "--project", "${workspaceFolder}"],
            group={"kind": "build", "isDefault": True},
            problemMatcher="$gcc",
        ),
        vscode_entry_task(
            label=DEBUG_BUILD_LABEL,
            detail=f"Build{subject} firmware with debug settings.",
            args=["build-debug", "--project", "${workspaceFolder}"],
            group="build",
            problemMatcher="$gcc",
        ),
        vscode_entry_task(
            label="Project: Upload",
            detail=f"Upload{subject} firmware to the selected board.",
            args=["upload", "--project", "${workspaceFolder}"],
            problemMatcher="$gcc",
        ),
        vscode_entry_task(
            label="Project: Upload (UF2 / BOOTSEL)",
            detail=(
                f"RP2040 only: build{subject} firmware and copy its UF2 file to "
                "the only detected BOOTSEL drive."
            ),
            args=["upload-uf2", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Upload (OTA)",
            detail=(
                f"Build{subject} firmware, authenticate, and upload it to one RP "
                "device discovered on the network."
            ),
            args=["upload-ota", "--project", "${workspaceFolder}", "--interactive"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Discover OTA devices",
            detail="List JaszczurHAL devices available for native OTA updates.",
            args=["ota-discover", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: List ports",
            detail=(
                "List serial ports, matching device identities, and possible "
                "BOOTSEL drives."
            ),
            args=["list-ports", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Change port",
            detail=(
                "Select and save the serial port used for uploads and monitoring."
            ),
            args=["change-port", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Serial Monitor",
            detail="Open the project's persistent serial monitor.",
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
            detail="Monitor serial output through the Debug Probe.",
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
            detail="Monitor output from any serial port.",
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
            detail="Update IntelliSense data for this project's sources.",
            args=["refresh-intellisense", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Clean",
            detail="Remove the project's build files.",
            args=["clean", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Clear USB Identity",
            detail=(
                "Replace the installed application with neutral firmware after "
                f"verifying the {usb_product} USB device identity."
                if usb_product
                else (
                    "Replace the installed application with neutral firmware after "
                    "verifying the USB device identity."
                )
            ),
            args=["clear-identity", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Config Dump",
            detail="Show the effective project configuration.",
            args=["config-dump", "--project", "${workspaceFolder}"],
            problemMatcher=[],
        ),
        vscode_entry_task(
            label="Project: Select board",
            detail=f"Choose {choice} in the interactive selector.",
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
            detail=f"Choose {choice} from the VS Code menu.",
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
    variants = (
        project_config.read_project_config(project_dir / project_config.HEADER_NAME).variants
        if project_dir is not None
        else ()
    )
    for variant in variants:
        variant_id = variant.id
        variant_subject = f"the {variant_id} variant: {variant.description}"
        variant_args = ["--project", "${workspaceFolder}", "--variant", variant_id]
        tasks.append(
            vscode_entry_task(
                label=f"Project: Build variant: {variant_id}",
                detail=f"Build {variant_subject}.",
                args=["build", *variant_args],
                group="build",
                problemMatcher="$gcc",
            )
        )
        tasks.append(
            vscode_entry_task(
                label=f"Project: Upload variant: {variant_id}",
                detail=f"Build and upload {variant_subject}.",
                args=["upload", *variant_args],
                problemMatcher="$gcc",
            )
        )
    if device_tree_project.device_tree_path(project_dir) is not None:
        tasks.extend(assembly_tasks(assembly_debug_builds(Path(project_dir))))
    return {
        "version": "2.0.0",
        "inputs": [
            selection_input(
                registry, selected_target, selected_board, project_dir, selected_assembly
            )
        ],
        "tasks": tasks,
    }


def sync_board_picker_document(
    document: dict[str, Any],
    registry: dict[str, dict[str, Any]],
    selected_target: str,
    selected_board: str,
    project_dir: Path | None = None,
    selected_assembly: str | None = None,
    managed_tasks: list[dict[str, Any]] | None = None,
) -> bool:
    """Update the picker input, the synchronization task and the
    ``managed_tasks`` of a device tree project (none without one) in place;
    other tasks stay as they are."""
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

    desired_input = selection_input(
        registry, selected_target, selected_board, project_dir, selected_assembly
    )
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

    # Device tree tasks follow the assemblies: when they differ, the current
    # ones go and the desired ones are appended, as the full document lists
    # them last.
    desired_managed = list(managed_tasks or [])
    current_managed = [task for task in tasks if is_assembly_task(task)]
    if current_managed != desired_managed:
        tasks[:] = [task for task in tasks if not is_assembly_task(task)]
        tasks.extend(desired_managed)
        changed = True
    return changed
