#!/usr/bin/env python3
"""Repository-root VS Code workflow for JaszczurHAL static libraries."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from typing import Any, Sequence


DEFAULT_TARGET = "rp2040"
LOCAL_STATE = Path(".vscode/jaszczurhal.library.local.json")
CPP_PROPERTIES = Path(".vscode/c_cpp_properties.json")
MANAGED_BUILD_ROOT = Path(".build/vscode/library")
MANAGED_INSTALL_ROOT = Path(".build/install")
SELECTION_INPUT_ID = "libraryBoardSelection"
CONFIG_DIR_INPUT_ID = "libraryConfigDir"


class WorkspaceError(RuntimeError):
    """Expected configuration or build failure with a stable exit code."""

    def __init__(self, message: str, exit_code: int = 3):
        super().__init__(message)
        self.exit_code = exit_code


@dataclass(frozen=True)
class LibraryProfile:
    target: str
    board: str
    provider: str
    target_display_name: str
    board_display_name: str
    # A configuration directory with device_tree.json, its assembly and the
    # program variant; the assembly fixes target and board.
    config_dir: Path | None = None
    assembly: str | None = None
    variant: str | None = None

    @property
    def selection(self) -> str:
        if self.config_dir is not None:
            variant = f" variant {self.variant}" if self.variant else ""
            return f"{self.config_dir} assembly {self.assembly}{variant}"
        return f"{self.target}:{self.board}"


@dataclass(frozen=True)
class ProfilePaths:
    build_dir: Path
    install_dir: Path
    compile_commands: Path
    archive: Path


def add_scripts_to_path(repo_root: Path) -> None:
    scripts_dir = str(repo_root / "scripts")
    if scripts_dir not in sys.path:
        sys.path.insert(0, scripts_dir)


def load_registry(repo_root: Path) -> dict[str, dict[str, Any]]:
    add_scripts_to_path(repo_root)
    from board_registry import library_target_registry

    registry = library_target_registry(repo_root)
    if DEFAULT_TARGET not in registry:
        raise WorkspaceError(
            f"board registry does not contain default target '{DEFAULT_TARGET}'"
        )
    return registry


def validate_repo_root(repo_root: Path) -> Path:
    resolved = repo_root.resolve()
    required = (resolved / "CMakeLists.txt", resolved / "boards")
    if not all(path.exists() for path in required):
        raise WorkspaceError(f"not a JaszczurHAL repository root: {resolved}")
    return resolved


def profile_from_selection(
    registry: dict[str, dict[str, Any]],
    target: str,
    board: str,
) -> LibraryProfile:
    target_desc = registry.get(target)
    if target_desc is None:
        choices = ", ".join(sorted(registry))
        raise WorkspaceError(f"unknown library target '{target}'; available: {choices}")

    board_desc = next(
        (item for item in target_desc["boards"] if item["id"] == board),
        None,
    )
    if board_desc is None:
        choices = ", ".join(item["id"] for item in target_desc["boards"])
        raise WorkspaceError(
            f"board '{board}' is not compatible with target '{target}'; "
            f"available: {choices}"
        )

    return LibraryProfile(
        target=target,
        board=board,
        provider=str(target_desc["provider"]),
        target_display_name=str(target_desc["displayName"]),
        board_display_name=str(board_desc["displayName"]),
    )


def device_tree_profile(
    registry: dict[str, dict[str, Any]],
    config_dir: Path,
    assembly: str | None,
    variant: str | None,
) -> LibraryProfile:
    """The profile of a configuration directory with device_tree.json: one
    of its assemblies (the only one may be implied) and a declared variant."""
    import device_tree_project
    import project_config

    config_dir = config_dir.resolve()
    if device_tree_project.device_tree_path(config_dir) is None:
        raise WorkspaceError(f"{config_dir} has no device_tree.json")
    try:
        item, _ = device_tree_project.choose(config_dir, None, assembly)
        variants = project_config.read_project_config(
            config_dir / project_config.HEADER_NAME
        ).variants
    except ValueError as exc:
        raise WorkspaceError(str(exc)) from exc
    if variant and variant not in {entry.id for entry in variants}:
        known = ", ".join(entry.id for entry in variants) or "none"
        raise WorkspaceError(f"unknown variant '{variant}' in {config_dir}; declared: {known}")
    profile = profile_from_selection(registry, item["target"], item["board"])
    return LibraryProfile(
        target=profile.target,
        board=profile.board,
        provider=profile.provider,
        target_display_name=profile.target_display_name,
        board_display_name=profile.board_display_name,
        config_dir=config_dir,
        assembly=item["name"],
        variant=variant or None,
    )


def default_profile(registry: dict[str, dict[str, Any]]) -> LibraryProfile:
    target_desc = registry[DEFAULT_TARGET]
    return profile_from_selection(
        registry,
        DEFAULT_TARGET,
        str(target_desc["defaultBoard"]),
    )


def load_active_profile(
    repo_root: Path,
    registry: dict[str, dict[str, Any]],
) -> LibraryProfile:
    state_path = repo_root / LOCAL_STATE
    if not state_path.is_file():
        return default_profile(registry)
    try:
        state = json.loads(state_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise WorkspaceError(f"cannot read {state_path}: {exc}") from exc
    if not isinstance(state, dict) or state.get("schemaVersion") != 1:
        raise WorkspaceError(f"unsupported library workspace state in {state_path}")
    if "configDir" in state:
        # A device tree profile stores only what selects it; target and
        # board come from the assembly every time.
        return device_tree_profile(
            registry, Path(str(state["configDir"])), state.get("assembly"), state.get("variant")
        )
    target = state.get("target")
    board = state.get("board")
    if not isinstance(target, str) or not isinstance(board, str):
        raise WorkspaceError(f"invalid target/board in {state_path}")
    return profile_from_selection(registry, target, board)


def save_active_profile(repo_root: Path, profile: LibraryProfile) -> None:
    state_path = repo_root / LOCAL_STATE
    state_path.parent.mkdir(parents=True, exist_ok=True)
    state: dict[str, Any] = {"schemaVersion": 1}
    if profile.config_dir is not None:
        state.update(configDir=str(profile.config_dir), assembly=profile.assembly)
        if profile.variant:
            state["variant"] = profile.variant
    else:
        state.update(target=profile.target, board=profile.board)
    state_path.write_text(json.dumps(state, indent=4) + "\n", encoding="utf-8")


def profile_paths(repo_root: Path, profile: LibraryProfile) -> ProfilePaths:
    key = profile.board
    if profile.config_dir is not None:
        import device_tree_project

        # <assembly>/<digest of the configuration directory and variant>:
        # two projects or variants never share a build or an installation.
        key = device_tree_project.build_key(
            profile.config_dir, str(profile.assembly), [f"variant={profile.variant or ''}"]
        )
    build_dir = repo_root / MANAGED_BUILD_ROOT / profile.target / key
    install_dir = repo_root / MANAGED_INSTALL_ROOT / profile.target / key
    archive_name = "libhal_mock.a" if profile.provider == "host" else "libJaszczurHAL.a"
    return ProfilePaths(
        build_dir=build_dir,
        install_dir=install_dir,
        compile_commands=build_dir / "compile_commands.json",
        archive=build_dir / archive_name,
    )


def selection_options(registry: dict[str, dict[str, Any]]) -> list[str]:
    options = []
    for target, target_desc in sorted(registry.items()):
        for board_desc in target_desc["boards"]:
            label = f"{target}:{board_desc['id']} - {board_desc['displayName']}"
            if board_desc.get("status") == "experimental":
                label += " (experimental)"
            options.append(label)
    return options


def parse_selection(
    registry: dict[str, dict[str, Any]],
    raw_selection: str,
) -> LibraryProfile:
    selection = raw_selection.split(" - ", 1)[0].strip()
    if ":" not in selection:
        raise WorkspaceError(
            f"invalid selection '{raw_selection}'; expected <target>:<board>"
        )
    target, board = (part.strip() for part in selection.split(":", 1))
    return profile_from_selection(registry, target, board)


def select_interactively(
    registry: dict[str, dict[str, Any]],
    current: LibraryProfile,
) -> LibraryProfile:
    options = selection_options(registry)
    print(f"Active library profile: {current.selection}")
    for index, option in enumerate(options, start=1):
        marker = " *" if option.startswith(f"{current.selection} - ") else ""
        print(f"{index:2d}. {option}{marker}")
    try:
        answer = input("Select target/board number: ").strip()
        selected_index = int(answer)
    except (EOFError, ValueError) as exc:
        raise WorkspaceError("selection cancelled or invalid") from exc
    if selected_index < 1 or selected_index > len(options):
        raise WorkspaceError(f"selection must be between 1 and {len(options)}")
    return parse_selection(registry, options[selected_index - 1])


def command_text(command: Sequence[str]) -> str:
    return shlex.join(str(item) for item in command)


def run_command(command: Sequence[str], repo_root: Path) -> int:
    normalized = [str(item) for item in command]
    print(f"+ {command_text(normalized)}", flush=True)
    try:
        return subprocess.run(normalized, cwd=repo_root, check=False).returncode
    except OSError as exc:
        raise WorkspaceError(f"cannot execute '{normalized[0]}': {exc}", 5) from exc


def build_commands(
    repo_root: Path,
    profile: LibraryProfile,
    paths: ProfilePaths,
) -> list[list[str]]:
    if profile.provider == "host":
        return [
            [
                "cmake",
                "-S",
                str(repo_root),
                "-B",
                str(paths.build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            ],
            [
                "cmake",
                "--build",
                str(paths.build_dir),
                "--target",
                "hal_mock",
                "--parallel",
            ],
        ]
    add_scripts_to_path(repo_root)
    from board_registry import LINK_LIBRARY_PROVIDERS

    if profile.provider in LINK_LIBRARY_PROVIDERS:
        selector = ["--board", profile.board]
        if profile.config_dir is not None:
            selector = ["--assembly", str(profile.assembly), "-p", str(profile.config_dir)]
            if profile.variant:
                selector += ["--variant", profile.variant]
        return [
            [
                str(repo_root / "scripts/build_link_library.sh"),
                "--target",
                profile.target,
                *selector,
                "--library-only",
                "--output",
                str(paths.build_dir),
            ]
        ]
    raise WorkspaceError(
        f"target '{profile.target}' uses unsupported provider '{profile.provider}'",
        4,
    )


def build_profile(repo_root: Path, profile: LibraryProfile) -> ProfilePaths:
    paths = profile_paths(repo_root, profile)
    print(f"Building JaszczurHAL library profile {profile.selection}")
    for command in build_commands(repo_root, profile, paths):
        if run_command(command, repo_root) != 0:
            raise WorkspaceError(
                f"library build failed for {profile.selection}",
                5,
            )
    if not paths.archive.is_file():
        raise WorkspaceError(f"library archive was not generated: {paths.archive}", 5)
    if not paths.compile_commands.is_file():
        raise WorkspaceError(
            f"compile database was not generated: {paths.compile_commands}",
            5,
        )
    return paths


def workspace_relative(repo_root: Path, path: Path) -> str:
    relative = path.resolve().relative_to(repo_root.resolve()).as_posix()
    return f"${{workspaceFolder}}/{relative}"


def cpp_properties_document(
    repo_root: Path,
    profile: LibraryProfile,
    paths: ProfilePaths,
) -> dict[str, Any]:
    return {
        "configurations": [
            {
                "name": f"JaszczurHAL: {profile.selection}",
                "compileCommands": workspace_relative(
                    repo_root,
                    paths.compile_commands,
                ),
                "cStandard": (
                    "c17" if profile.provider in {"pico-sdk", "esp-idf"} else "c11"
                ),
                "cppStandard": "gnu++17",
            }
        ],
        "version": 4,
    }


def write_cpp_properties(
    repo_root: Path,
    profile: LibraryProfile,
    paths: ProfilePaths,
) -> Path:
    output = repo_root / CPP_PROPERTIES
    output.parent.mkdir(parents=True, exist_ok=True)
    document = cpp_properties_document(repo_root, profile, paths)
    output.write_text(json.dumps(document, indent=4) + "\n", encoding="utf-8")
    print(f"IntelliSense now uses {paths.compile_commands}")
    return output


def managed_cpp_properties_matches(
    repo_root: Path,
    profile: LibraryProfile,
    paths: ProfilePaths,
) -> bool:
    properties_path = repo_root / CPP_PROPERTIES
    if not properties_path.is_file():
        return False
    try:
        current = json.loads(properties_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return current == cpp_properties_document(repo_root, profile, paths)


def clear_managed_cpp_properties(
    repo_root: Path,
    profile: LibraryProfile,
    paths: ProfilePaths,
) -> None:
    properties_path = repo_root / CPP_PROPERTIES
    if managed_cpp_properties_matches(repo_root, profile, paths):
        properties_path.unlink()
        print(f"Removed {properties_path}")


def ensure_managed_path(repo_root: Path, path: Path, managed_root: Path) -> Path:
    resolved_repo = repo_root.resolve()
    resolved_managed_root = (repo_root / managed_root).resolve()
    resolved = path.resolve()
    if resolved == resolved_managed_root or resolved_managed_root not in resolved.parents:
        raise WorkspaceError(f"refusing unsafe managed path: {resolved}")
    if resolved_repo not in resolved.parents:
        raise WorkspaceError(f"managed path escaped repository: {resolved}")
    return resolved


def remove_managed_tree(repo_root: Path, path: Path, managed_root: Path) -> None:
    resolved = ensure_managed_path(repo_root, path, managed_root)
    if resolved.is_dir():
        shutil.rmtree(resolved)
        print(f"Removed {resolved}")


def clean_profile(repo_root: Path, profile: LibraryProfile) -> None:
    paths = profile_paths(repo_root, profile)
    targets = (
        (paths.build_dir, MANAGED_BUILD_ROOT),
        (paths.install_dir, MANAGED_INSTALL_ROOT),
    )
    for path, managed_root in targets:
        remove_managed_tree(repo_root, path, managed_root)
    clear_managed_cpp_properties(repo_root, profile, paths)
    print(f"Cleaned library profile {profile.selection}")


def install_profile(repo_root: Path, profile: LibraryProfile) -> ProfilePaths:
    if profile.provider == "host":
        raise WorkspaceError(
            "the mock profile produces libhal_mock.a but has no install contract",
            4,
        )
    if profile.provider == "esp-idf":
        raise WorkspaceError(
            "the ESP-IDF profile publishes libJaszczurHAL.a and include/generated "
            "in its build directory but has no install contract",
            4,
        )
    paths = build_profile(repo_root, profile)
    remove_managed_tree(
        repo_root,
        paths.install_dir,
        MANAGED_INSTALL_ROOT,
    )
    command = [
        "cmake",
        "--install",
        str(paths.build_dir),
        "--prefix",
        str(paths.install_dir),
    ]
    if run_command(command, repo_root) != 0:
        raise WorkspaceError(f"library install failed for {profile.selection}", 5)
    installed_archive = paths.install_dir / "lib/libJaszczurHAL.a"
    if not installed_archive.is_file():
        raise WorkspaceError(
            f"installed library archive was not generated: {installed_archive}",
            5,
        )
    write_cpp_properties(repo_root, profile, paths)
    print(f"Installed JaszczurHAL profile to {paths.install_dir}")
    return paths


def root_keybindings_reference() -> list[dict[str, str]]:
    bindings = [
        ("ctrl+shift+1", "Project: Build"),
        ("ctrl+shift+6", "Project: Refresh IntelliSense"),
        ("ctrl+shift+7", "Project: Clean"),
        ("ctrl+shift+0", "Project: Install library"),
        ("ctrl+shift+alt+1", "Project: Select board (GUI)"),
        ("ctrl+shift+alt+2", "Project: Select board"),
    ]
    return [
        {
            "key": key,
            "command": "workbench.action.tasks.runTask",
            "args": label,
        }
        for key, label in bindings
    ]


def task(
    label: str,
    detail: str,
    action_args: list[str],
    **extra: Any,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "label": label,
        "detail": detail,
        "type": "shell",
        "command": "python3",
        "args": [
            "${workspaceFolder}/scripts/vscode_library_workspace.py",
            *action_args,
        ],
        "presentation": {
            "echo": True,
            "reveal": "always",
            "focus": False,
            "panel": "shared",
            "showReuseMessage": False,
            "clear": True,
        },
        "problemMatcher": [],
    }
    result.update(extra)
    return result


def root_tasks_document(
    registry: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    default = default_profile(registry)
    tasks = [
        task(
            "Project: Build",
            "Ctrl+Shift+1 - build the active linkable JaszczurHAL library",
            ["build"],
            group={"kind": "build", "isDefault": True},
            problemMatcher="$gcc",
        ),
        task(
            "Project: Refresh IntelliSense",
            "Ctrl+Shift+6 - build the active profile and select its compile database",
            ["refresh-intellisense"],
            problemMatcher="$gcc",
        ),
        task(
            "Project: Clean",
            "Ctrl+Shift+7 - remove only the active library build and install trees",
            ["clean"],
        ),
        task(
            "Project: Install library",
            "Ctrl+Shift+0 - install the active archive, headers, and link contract",
            ["install"],
            problemMatcher="$gcc",
        ),
        task(
            "Project: Select board (GUI)",
            "Ctrl+Shift+Alt+1 - select the active library target and board",
            ["select", "--selection", f"${{input:{SELECTION_INPUT_ID}}}"],
        ),
        task(
            "Project: Select board",
            "Ctrl+Shift+Alt+2 - select the active library target and board in the terminal",
            ["select", "--interactive"],
        ),
        task(
            "Library: Select device tree configuration",
            "Select an assembly and variant of a configuration directory with device_tree.json",
            [
                "select",
                "--config-dir",
                f"${{input:{CONFIG_DIR_INPUT_ID}}}",
                "--interactive",
            ],
        ),
        task(
            "Library: Show active profile",
            "Print the active target, board, build paths, and install paths",
            ["config-dump"],
        ),
        {
            "label": "Install git hooks",
            "type": "shell",
            "command": (
                "chmod +x .githooks/pre-commit .githooks/commit-msg "
                "&& git config core.hooksPath .githooks"
            ),
            "presentation": {
                "reveal": "silent",
                "panel": "shared",
                "close": True,
            },
            "runOptions": {"runOn": "folderOpen"},
            "problemMatcher": [],
        },
    ]
    return {
        "version": "2.0.0",
        "inputs": [
            {
                "id": SELECTION_INPUT_ID,
                "description": "JaszczurHAL library target/board",
                "type": "pickString",
                "options": selection_options(registry),
                "default": next(
                    option
                    for option in selection_options(registry)
                    if option.startswith(f"{default.selection} - ")
                ),
            },
            {
                "id": CONFIG_DIR_INPUT_ID,
                "description": "Configuration directory with device_tree.json and hal_project_config.h",
                "type": "promptString",
            },
        ],
        "tasks": tasks,
    }


def root_settings_document() -> dict[str, Any]:
    return {
        "C_Cpp.default.configurationProvider": "",
        "C_Cpp.errorSquiggles": "enabled",
        "cmake.configureOnOpen": False,
        # CMake Tools would otherwise configure into ${workspaceFolder}/build.
        "cmake.buildDirectory": "${workspaceFolder}/.build/vscode",
        "files.exclude": {"**/.build": True},
        "search.exclude": {"**/.build": True},
    }


def root_extensions_document() -> dict[str, list[str]]:
    return {
        "recommendations": [
            "ms-vscode.cpptools",
            "ms-vscode.cmake-tools",
        ]
    }


def root_vscode_documents(
    repo_root: Path,
) -> dict[Path, Any]:
    registry = load_registry(repo_root)
    return {
        Path(".vscode/tasks.json"): root_tasks_document(registry),
        Path(".vscode/settings.json"): root_settings_document(),
        Path(".vscode/extensions.json"): root_extensions_document(),
        Path(".vscode/keybindings.reference.json"): root_keybindings_reference(),
    }


def sync_vscode_documents(repo_root: Path, check: bool) -> None:
    stale = []
    for relative, document in root_vscode_documents(repo_root).items():
        path = repo_root / relative
        desired = json.dumps(document, indent=4) + "\n"
        current = path.read_text(encoding="utf-8") if path.is_file() else ""
        if current == desired:
            continue
        stale.append(relative.as_posix())
        if not check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(desired, encoding="utf-8")
            print(f"Wrote {path}")
    if stale and check:
        raise WorkspaceError(
            "stale root VS Code files: "
            + ", ".join(stale)
            + "; run 'python3 scripts/vscode_library_workspace.py "
            "sync-vscode' to refresh them",
            6,
        )


def select_device_tree_interactively(
    registry: dict[str, dict[str, Any]], config_dir: Path
) -> LibraryProfile:
    """Choose one build of the directory's matrix: assembly and variant."""
    import device_tree_project
    import project_config

    try:
        builds = device_tree_project.builds(config_dir, project_config.load_targets())
    except ValueError as exc:
        raise WorkspaceError(str(exc)) from exc
    for index, build in enumerate(builds, start=1):
        variant = build.variant.id if build.variant else "base"
        print(f"{index:2d}. {build.assembly} ({build.target.id}) variant {variant}")
    try:
        answer = input("Select assembly/variant number: ").strip()
        selected = builds[int(answer) - 1] if int(answer) >= 1 else None
    except (EOFError, ValueError, IndexError) as exc:
        raise WorkspaceError("selection cancelled or invalid") from exc
    if selected is None:
        raise WorkspaceError(f"selection must be between 1 and {len(builds)}")
    return device_tree_profile(
        registry, config_dir, selected.assembly,
        selected.variant.id if selected.variant else None,
    )


def config_dump(repo_root: Path, profile: LibraryProfile) -> None:
    paths = profile_paths(repo_root, profile)
    document = {
        "target": profile.target,
        "board": profile.board,
        **(
            {
                "configDir": str(profile.config_dir),
                "assembly": profile.assembly,
                "variant": profile.variant,
            }
            if profile.config_dir is not None
            else {}
        ),
        "provider": profile.provider,
        "state": str(repo_root / LOCAL_STATE),
        "buildDir": str(paths.build_dir),
        "compileCommands": str(paths.compile_commands),
        "archive": str(paths.archive),
        "installDir": str(paths.install_dir),
    }
    print(json.dumps(document, indent=4))


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Manage the JaszczurHAL repository VS Code library profile."
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="JaszczurHAL repository root",
    )
    subparsers = parser.add_subparsers(dest="action", required=True)
    # Explicit inputs of one action win over the stored profile.
    explicit = argparse.ArgumentParser(add_help=False)
    explicit.add_argument(
        "--config-dir", type=Path,
        help="Configuration directory with device_tree.json and hal_project_config.h",
    )
    explicit.add_argument("--assembly", help="Assembly key of device_tree.json")
    explicit.add_argument("--variant", help="Program variant of hal_project_config.h")
    subparsers.add_parser(
        "build", parents=[explicit], help="Build the active linkable static library"
    )
    subparsers.add_parser(
        "refresh-intellisense",
        parents=[explicit],
        help="Build the active profile and write c_cpp_properties.json",
    )
    subparsers.add_parser(
        "clean", parents=[explicit], help="Clean only the active managed profile"
    )
    subparsers.add_parser(
        "install", parents=[explicit], help="Install the active production library"
    )
    subparsers.add_parser(
        "config-dump", parents=[explicit], help="Print the resolved active profile"
    )

    select_parser = subparsers.add_parser(
        "select",
        parents=[explicit],
        help="Select target and board, or a device tree assembly and variant",
    )
    select_group = select_parser.add_mutually_exclusive_group()
    select_group.add_argument("--selection", help="Selection as <target>:<board>")
    select_group.add_argument("--interactive", action="store_true")

    sync_parser = subparsers.add_parser(
        "sync-vscode",
        help="Regenerate tracked root .vscode files from the board registry",
    )
    sync_parser.add_argument("--check", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        repo_root = validate_repo_root(args.repo_root)
        if args.action == "sync-vscode":
            sync_vscode_documents(repo_root, args.check)
            return 0

        registry = load_registry(repo_root)
        config_dir = getattr(args, "config_dir", None)
        if config_dir is None and (getattr(args, "assembly", None) or getattr(args, "variant", None)):
            raise WorkspaceError("--assembly and --variant need --config-dir with device_tree.json")
        explicit = (
            device_tree_profile(registry, config_dir, args.assembly, args.variant)
            if config_dir is not None and not (args.action == "select" and args.interactive)
            else None
        )
        if args.action == "select":
            # A stored profile that no longer resolves (its configuration
            # directory is gone) never blocks choosing a new one.
            try:
                previous = load_active_profile(repo_root, registry)
            except WorkspaceError:
                previous = None
            if config_dir is not None:
                selected = explicit or select_device_tree_interactively(registry, config_dir)
            elif args.selection is not None:
                selected = parse_selection(registry, args.selection)
            elif args.interactive:
                selected = select_interactively(registry, previous or default_profile(registry))
            else:
                raise WorkspaceError("select needs --selection, --interactive or --config-dir")
            if previous is not None:
                clear_managed_cpp_properties(
                    repo_root,
                    previous,
                    profile_paths(repo_root, previous),
                )
            save_active_profile(repo_root, selected)
            print(f"Selected JaszczurHAL library profile {selected.selection}")
            print("Run Project: Refresh IntelliSense for the new profile.")
            return 0
        # Explicit inputs replace the stored profile, which is then not read.
        profile = explicit or load_active_profile(repo_root, registry)
        if args.action == "build":
            paths = build_profile(repo_root, profile)
            write_cpp_properties(repo_root, profile, paths)
            return 0
        if args.action == "refresh-intellisense":
            paths = build_profile(repo_root, profile)
            write_cpp_properties(repo_root, profile, paths)
            return 0
        if args.action == "clean":
            clean_profile(repo_root, profile)
            return 0
        if args.action == "install":
            install_profile(repo_root, profile)
            return 0
        if args.action == "config-dump":
            config_dump(repo_root, profile)
            return 0
        raise WorkspaceError(f"unsupported action: {args.action}")
    except WorkspaceError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
