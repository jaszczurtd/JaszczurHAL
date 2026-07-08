#!/usr/bin/env python3
"""JaszczurHAL VS Code firmware workflow entrypoint."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import fcntl
import json
import os
import re
import signal
import shutil
import subprocess
import time
import sys
from pathlib import Path
from typing import Any


VERSION = "0.1.0"

EXIT_GENERIC = 1
EXIT_USAGE = 2
EXIT_CONFIG = 3
EXIT_UNSAFE_DEVICE = 4
EXIT_BUILD = 5
EXIT_UPLOAD = 6
EXIT_MONITOR = 7
EXIT_UNSUPPORTED = 8

MODULE_ACTIONS = {
    "build",
    "build-debug",
    "debug",
    "upload",
    "upload-uf2",
    "refresh-intellisense",
    "clean",
    "clear-identity",
    "config-dump",
}

SUPPORTED_ACTIONS = {
    "build",
    "build-debug",
    "debug",
    "upload",
    "upload-uf2",
    "monitor",
    "monitor-probe",
    "monitor-any",
    "refresh-intellisense",
    "clean",
    "select-board",
    "list-ports",
    "change-port",
    "clear-identity",
    "config-dump",
}

BOOTSEL_LABELS = {"RPI-RP2", "RP2350", "RPI-RP2350"}

SECTION_HEADER_RE = re.compile(
    r"^\s*\d+\s+"
    r"(?P<name>\S+)\s+"
    r"(?P<size>[0-9a-fA-F]+)\s+"
    r"(?P<vma>[0-9a-fA-F]+)\s+"
    r"(?P<lma>[0-9a-fA-F]+)\s+"
    r"(?P<fileoff>[0-9a-fA-F]+)\s+"
    r"2\*\*(?P<align>\d+)"
)

ANSI_YELLOW = "\033[33m"
ANSI_RESET = "\033[0m"
YELLOW_OUTPUT_PREFIXES = (
    "Using verified serial port:",
    "released own serial monitor PID ",
    "-- Arduino FQBN:",
    "-- Generated sketch:",
    "-- Arduino build dir:",
)


def color_enabled() -> bool:
    if os.environ.get("FORCE_COLOR"):
        return True
    if os.environ.get("NO_COLOR"):
        return False
    return sys.stdout.isatty()


def yellow_text(text: str) -> str:
    if not color_enabled():
        return text
    return f"{ANSI_YELLOW}{text}{ANSI_RESET}"


def maybe_yellow_output(line: str) -> str:
    bare = line.rstrip("\r\n")
    newline = line[len(bare):]
    if any(bare.startswith(prefix) for prefix in YELLOW_OUTPUT_PREFIXES):
        return yellow_text(bare) + newline
    return line


def strip_json_comments(text: str) -> str:
    """Remove JSONC comments while preserving string contents."""
    result: list[str] = []
    in_string = False
    escaped = False
    i = 0
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""

        if in_string:
            result.append(ch)
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_string = False
            i += 1
            continue

        if ch == '"':
            in_string = True
            result.append(ch)
            i += 1
            continue

        if ch == "/" and nxt == "/":
            i += 2
            while i < len(text) and text[i] not in "\r\n":
                i += 1
            continue

        if ch == "/" and nxt == "*":
            i += 2
            while i + 1 < len(text) and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue

        result.append(ch)
        i += 1

    return "".join(result)


def load_json_file(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        text = strip_json_comments(path.read_text(encoding="utf-8"))
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON in {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return data


def expand_project_vars(value: Any, project_dir: Path, config: dict[str, Any]) -> Any:
    if not isinstance(value, str):
        return value
    build_dir = str(config.get("buildDir", project_dir / ".build"))
    replacements = {
        "${project}": str(project_dir),
        "${projectDir}": str(project_dir),
        "${workspaceFolder}": str(project_dir),
        "${module}": str(config.get("module", project_dir.name)),
        "${projectName}": str(config.get("project", project_dir.parent.name)),
        "${buildDir}": build_dir,
    }
    for key, replacement in replacements.items():
        value = value.replace(key, replacement)
    return value


def expand_config_sections(config: dict[str, Any], project_dir: Path) -> None:
    """Expand ${...} tokens in the path-bearing sections of config, in place.

    Idempotent: once tokens are substituted, re-running is a no-op. Called for the
    base manifest and again after a target overlay is merged in.
    """
    if "buildDir" in config:
        config["buildDir"] = expand_project_vars(config["buildDir"], project_dir, config)
    if "cmakeBuildDir" in config:
        config["cmakeBuildDir"] = expand_project_vars(config["cmakeBuildDir"], project_dir, config)
    for section_name in ("artifacts", "upload", "hooks"):
        section = config.get(section_name)
        if isinstance(section, dict):
            config[section_name] = {
                key: expand_project_vars(value, project_dir, config)
                for key, value in section.items()
            }
    cmake = config.get("cmake")
    if isinstance(cmake, dict):
        cmake = dict(cmake)
        if "sourceDir" in cmake:
            cmake["sourceDir"] = expand_project_vars(cmake["sourceDir"], project_dir, config)
        if isinstance(cmake.get("cache"), dict):
            cmake["cache"] = {
                str(key): expand_project_vars(value, project_dir, config)
                for key, value in cmake["cache"].items()
            }
        config["cmake"] = cmake


def deep_merge(base: dict[str, Any], overlay: dict[str, Any]) -> dict[str, Any]:
    """Return base with overlay applied: dicts merge recursively, everything else
    (scalars, lists) is replaced by the overlay value."""
    result = dict(base)
    for key, value in overlay.items():
        if key in result and isinstance(result[key], dict) and isinstance(value, dict):
            result[key] = deep_merge(result[key], value)
        else:
            result[key] = value
    return result


def resolve_target_profile(
    config: dict[str, Any],
    project_dir: Path,
    sources: dict[str, str],
    target_override: str | None = None,
    board_override: str | None = None,
    override_source: str = "cli",
) -> None:
    """Resolve the active target and merge its targetProfiles overlay in place.

    Precedence for the active target/board: override (CLI or the gitignored
    .vscode/jaszczurhal.local.json, whichever the caller passed) > manifest
    target/board > default 'rp2040'. `override_source` labels the override's
    origin for config-dump only.

    Parity contract: a manifest with no target / board / targetProfiles AND no
    override is left byte-for-byte untouched, so config-dump on existing pico
    manifests is unchanged. When target machinery IS present, the active target's
    overlay is deep-merged over the base, the input-only targetProfiles map is
    dropped, and ${...} tokens introduced by the overlay are expanded.
    """
    profiles = config.get("targetProfiles")
    manifest_target = config.get("target")
    manifest_board = config.get("board")
    has_any = (
        target_override is not None
        or board_override is not None
        or manifest_target is not None
        or manifest_board is not None
        or isinstance(profiles, dict)
    )
    if not has_any:
        return

    # Registry is loaded ONLY on the active-target path, so plain pico manifests
    # (no target machinery) never touch it -> parity + no cost.
    registry = load_target_registry()
    active = target_override or manifest_target or "rp2040"
    target_desc = registry.get(active)

    # Board precedence: override > manifest > registry defaultBoard. The board is
    # scoped to the ACTIVE target: a board carried over from a different target
    # (e.g. local.json held an STM32 board but the target was switched to rp2040)
    # is invalid here and falls back to this target's default.
    board = board_override or manifest_board
    if isinstance(target_desc, dict):
        valid_boards = [b.get("id") for b in (target_desc.get("boards") or []) if isinstance(b, dict)]
        if board is None or (valid_boards and board not in valid_boards):
            board = target_desc.get("defaultBoard")

    # Effective overlay = registry family+board layer (build defaults) with the
    # manifest's own targetProfiles.<active> entry merged on top (project wins).
    registry_layer: dict[str, Any] = {}
    if isinstance(target_desc, dict):
        if target_desc.get("toolchain"):
            registry_layer["toolchain"] = target_desc["toolchain"]
        reg_cache = resolve_registry_board_cache(target_desc, board)
        if reg_cache:
            registry_layer["cmake"] = {"cache": reg_cache}
        if isinstance(target_desc.get("upload"), dict):
            registry_layer["upload"] = dict(target_desc["upload"])

    manifest_overlay: dict[str, Any] = {}
    if isinstance(profiles, dict) and isinstance(profiles.get(active), dict):
        manifest_overlay = profiles[active]

    # Precedence (low -> high): registry target/board defaults are the FLOOR, the
    # base manifest overrides them, and the active target's targetProfiles overlay
    # overrides the base. (CLI --target/--board is already folded into
    # active/board.) So a project's explicit setting (e.g. upload.strategy) always
    # beats a registry default, and a per-target profile beats the project-wide
    # value. cmake.cache from all three layers deep-merges key-by-key.
    base = dict(config)
    base.pop("targetProfiles", None)
    merged = deep_merge(deep_merge(registry_layer, base), manifest_overlay)
    config.clear()
    config.update(merged)

    # Expand tokens the overlay may have introduced (idempotent on the base).
    expand_config_sections(config, project_dir)

    config["target"] = active
    sources["target"] = override_source if target_override else (
        ".vscode/jaszczurhal.project.json" if manifest_target is not None else "default"
    )
    # Display-only: mark keys the registry/targetProfiles contributed, without
    # clobbering a base-manifest source already recorded for the same key.
    overlay_source = f"registry/targetProfiles:{active}"
    for key in set(registry_layer) | set(manifest_overlay):
        if key not in ("targetProfiles", "target", "board"):
            sources.setdefault(key, overlay_source)

    if board is not None:
        config["board"] = board
        if board_override:
            sources["board"] = override_source
        elif manifest_board is not None:
            sources["board"] = ".vscode/jaszczurhal.project.json"
        else:
            sources["board"] = f"registry:{active}.defaultBoard"


def jaszczurhal_root() -> Path:
    """Absolute JaszczurHAL repo root, located relative to this script:
    <root>/vscode/linux/runtime/jh_vscode.py."""
    return Path(__file__).resolve().parents[3]


def target_registry_dir() -> Path:
    return jaszczurhal_root() / "vscode" / "targets"


def load_target_registry() -> dict[str, dict[str, Any]]:
    """Load every target descriptor in vscode/targets/*.json, keyed by target id.
    Malformed / unreadable files are skipped so one bad descriptor cannot break
    the CLI."""
    registry: dict[str, dict[str, Any]] = {}
    directory = target_registry_dir()
    if not directory.is_dir():
        return registry
    for path in sorted(directory.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (ValueError, OSError):
            continue
        if not isinstance(data, dict):
            continue
        target_id = str(data.get("id") or path.stem)
        registry[target_id] = data
    return registry


def resolve_registry_board_cache(target_desc: dict[str, Any], board_id: str | None) -> dict[str, Any]:
    """Effective CMake cache for a board: family cache overlaid by the board's
    own cache, with ${jhRoot} resolved to the absolute repo root."""
    cache: dict[str, Any] = dict(target_desc.get("cache") or {})
    boards = target_desc.get("boards") or []
    selected = board_id or target_desc.get("defaultBoard")
    for board in boards:
        if isinstance(board, dict) and board.get("id") == selected:
            cache.update(board.get("cache") or {})
            break
    root = str(jaszczurhal_root())
    return {
        key: (value.replace("${jhRoot}", root) if isinstance(value, str) else value)
        for key, value in cache.items()
    }


def normalize_manifest(data: dict[str, Any]) -> dict[str, Any]:
    config: dict[str, Any] = {}
    for key in (
        "project",
        "module",
        "toolchain",
        "target",
        "board",
        "targetProfiles",
        "fqbn",
        "buildDir",
        "cmakeBuildDir",
        "cmake",
        "identity",
        "artifacts",
        "upload",
        "hooks",
    ):
        if key in data:
            config[key] = data[key]
    return config


def settings_value(settings: dict[str, Any], semantic_key: str) -> Any:
    jh_key = f"jaszczurhal.{semantic_key}"
    arduino_key = f"arduino.{semantic_key}"

    aliases = {
        "fqbn": ("jaszczurhal.fqbn", "arduino.fqbn"),
        "uploadPort": ("jaszczurhal.uploadPort", "arduino.uploadPort"),
        "sketchbookPath": ("jaszczurhal.sketchbookPath", "arduino.sketchbookPath"),
        "cliPath": ("jaszczurhal.cliPath", "arduino.cliPath"),
        "buildDir": ("jaszczurhal.buildDir", "arduino.buildDir"),
        "verbose": ("jaszczurhal.verbose", "arduino.verbose"),
        "projectName": ("jaszczurhal.projectName",),
        "moduleName": ("jaszczurhal.moduleName",),
        "usbManufacturer": ("jaszczurhal.usbManufacturer",),
        "usbProduct": ("jaszczurhal.usbProduct",),
        "identityEnabled": ("jaszczurhal.identityEnabled",),
        "vscodeEntry": ("jaszczurhal.vscodeEntry",),
        "root": ("jaszczurhal.root",),
    }

    for key in aliases.get(semantic_key, (jh_key, arduino_key)):
        if key in settings:
            return settings[key]
    return None


def load_project_config(
    project_dir: Path,
    target_override: str | None = None,
    board_override: str | None = None,
) -> dict[str, Any]:
    vscode_dir = project_dir / ".vscode"
    manifest = load_json_file(vscode_dir / "jaszczurhal.project.json")
    settings = load_json_file(vscode_dir / "settings.json")
    legacy_arduino = load_json_file(vscode_dir / "arduino.json")

    config = normalize_manifest(manifest)
    sources: dict[str, str] = {}
    for key in config:
        sources[key] = ".vscode/jaszczurhal.project.json"

    setting_map = {
        "fqbn": "fqbn",
        "uploadPort": "uploadPort",
        "sketchbookPath": "sketchbookPath",
        "cliPath": "cliPath",
        "buildDir": "buildDir",
        "verbose": "verbose",
        "projectName": "project",
        "moduleName": "module",
        "vscodeEntry": "vscodeEntry",
        "root": "root",
    }
    for semantic, target in setting_map.items():
        if target in config and target in {"project", "module", "fqbn", "buildDir"}:
            continue
        value = settings_value(settings, semantic)
        if value is not None:
            config[target] = value
            sources[target] = ".vscode/settings.json"

    identity = dict(config.get("identity") or {})
    identity_map = {
        "identityEnabled": "enabled",
        "usbManufacturer": "usbManufacturer",
        "usbProduct": "usbProduct",
    }
    for semantic, target in identity_map.items():
        if target in identity:
            continue
        value = settings_value(settings, semantic)
        if value is not None:
            identity[target] = value
            sources[f"identity.{target}"] = ".vscode/settings.json"
    if identity:
        config["identity"] = identity

    if "fqbn" not in config and "fqbn" in legacy_arduino:
        config["fqbn"] = legacy_arduino["fqbn"]
        sources["fqbn"] = ".vscode/arduino.json"
    if "uploadPort" not in config and "port" in legacy_arduino:
        config["uploadPort"] = legacy_arduino["port"]
        sources["uploadPort"] = ".vscode/arduino.json"

    if "project" not in config:
        config["project"] = project_dir.parent.name
        sources["project"] = "directory-fallback"
    if "module" not in config:
        config["module"] = project_dir.name
        sources["module"] = "directory-fallback"
    if "toolchain" not in config:
        config["toolchain"] = "arduino-cli" if (project_dir / f"{project_dir.name}.ino").exists() else "custom"
        sources["toolchain"] = "directory-fallback"
    if "buildDir" not in config:
        config["buildDir"] = str(project_dir / ".build")
        sources["buildDir"] = "default"

    expand_config_sections(config, project_dir)
    # User-local active board selection (gitignored). CLI overrides win over it;
    # it wins over the manifest default. Absent -> no effect (parity preserved).
    local_state = load_json_file(vscode_dir / "jaszczurhal.local.json")
    local_target = local_state.get("target") if isinstance(local_state, dict) else None
    local_board = local_state.get("board") if isinstance(local_state, dict) else None
    eff_target = target_override or local_target
    eff_board = board_override or local_board
    if target_override or board_override:
        override_source = "cli"
    else:
        override_source = ".vscode/jaszczurhal.local.json"
    resolve_target_profile(
        config, project_dir, sources,
        target_override=eff_target,
        board_override=eff_board,
        override_source=override_source,
    )
    config["_projectDir"] = str(project_dir)
    config["_sources"] = sources
    return config


def print_human_config(config: dict[str, Any]) -> None:
    sources = config.get("_sources", {})
    for key in sorted(k for k in config if not k.startswith("_")):
        value = config[key]
        source = sources.get(key, "")
        suffix = f"  ({source})" if source else ""
        if isinstance(value, (dict, list)):
            encoded = json.dumps(value, ensure_ascii=False, sort_keys=True)
            print(f"{key}: {encoded}{suffix}")
        else:
            print(f"{key}: {value}{suffix}")


def resolve_project(args: argparse.Namespace) -> Path | None:
    if not args.project:
        return None
    return Path(args.project).expanduser().resolve()


def apply_cli_overrides(config: dict[str, Any], args: argparse.Namespace) -> None:
    sources = config.setdefault("_sources", {})
    if args.fqbn:
        config["fqbn"] = args.fqbn
        sources["fqbn"] = "cli"
    if args.port:
        config["uploadPort"] = args.port
        upload = dict(config.get("upload") or {})
        upload["port"] = args.port
        config["upload"] = upload
        sources["uploadPort"] = "cli"
    if args.verbose:
        config["verbose"] = True
        sources["verbose"] = "cli"


def command_config_dump(args: argparse.Namespace) -> int:
    project_dir = resolve_project(args)
    if project_dir is None:
        print("error: config-dump requires --project <path>", file=sys.stderr)
        return EXIT_USAGE
    if not project_dir.exists() or not project_dir.is_dir():
        print(f"error: project directory does not exist: {project_dir}", file=sys.stderr)
        return EXIT_CONFIG
    try:
        config = load_project_config(
            project_dir,
            target_override=getattr(args, "target", None),
            board_override=getattr(args, "board", None),
        )
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_CONFIG
    apply_cli_overrides(config, args)
    if args.json:
        print(json.dumps(config, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        print_human_config(config)
    return 0


LOCAL_STATE_FILENAME = "jaszczurhal.local.json"


def ensure_local_state_gitignored(project_dir: Path) -> None:
    """Make sure the gitignored user-local selection file is actually ignored.
    Adds the rule to <project>/.gitignore if missing (the rule is tracked; the
    state file it names is not)."""
    gitignore = project_dir / ".gitignore"
    entry = f".vscode/{LOCAL_STATE_FILENAME}"
    text = ""
    if gitignore.exists():
        try:
            text = gitignore.read_text(encoding="utf-8")
        except OSError:
            return
        if entry in [line.strip() for line in text.splitlines()]:
            return
    prefix = "" if (not text or text.endswith("\n")) else "\n"
    try:
        with gitignore.open("a", encoding="utf-8") as handle:
            handle.write(f"{prefix}# jh-vscode user-local active board selection\n{entry}\n")
    except OSError:
        pass


def command_select_board(args: argparse.Namespace) -> int:
    project_dir = resolve_project(args)
    if project_dir is None:
        print("error: select-board requires --project <path>", file=sys.stderr)
        return EXIT_USAGE
    if not project_dir.is_dir():
        print(f"error: project directory does not exist: {project_dir}", file=sys.stderr)
        return EXIT_CONFIG

    registry = load_target_registry()
    vscode_dir = project_dir / ".vscode"
    local_state = load_json_file(vscode_dir / LOCAL_STATE_FILENAME)
    manifest = load_json_file(vscode_dir / "jaszczurhal.project.json")
    current_target = local_state.get("target") or manifest.get("target")
    current_board = local_state.get("board") or manifest.get("board")

    # List mode: no --target given.
    if not args.target:
        if args.json:
            listing = {
                "current": {"target": current_target, "board": current_board},
                "targets": [
                    {
                        "id": desc.get("id", tid),
                        "displayName": desc.get("displayName"),
                        "status": desc.get("status"),
                        "defaultBoard": desc.get("defaultBoard"),
                        "boards": [
                            {"id": b.get("id"), "displayName": b.get("displayName")}
                            for b in (desc.get("boards") or []) if isinstance(b, dict)
                        ],
                    }
                    for tid, desc in sorted(registry.items())
                ],
            }
            print(json.dumps(listing, ensure_ascii=False, indent=2, sort_keys=True))
            return 0
        shown_t = current_target or "(default rp2040)"
        shown_b = current_board or "(target default)"
        print(f"Current selection: target={shown_t} board={shown_b}")
        print("Available targets:")
        for tid, desc in sorted(registry.items()):
            flag = " [skeleton]" if desc.get("status") == "skeleton" else ""
            print(f"  {tid}{flag} - {desc.get('displayName', '')}")
            for board in desc.get("boards") or []:
                if not isinstance(board, dict):
                    continue
                mark = "*" if board.get("id") == desc.get("defaultBoard") else " "
                print(f"     {mark} {board.get('id')} - {board.get('displayName', '')}")
        print("Select: jh-vscode select-board --project <p> --target <id> [--board <id>]")
        return 0

    # Set mode.
    target = args.target
    desc = registry.get(target)
    if desc is None:
        known = ", ".join(sorted(registry)) or "(registry empty)"
        print(f"error: unknown target '{target}'. Known targets: {known}", file=sys.stderr)
        return EXIT_CONFIG
    board_ids = [b.get("id") for b in (desc.get("boards") or []) if isinstance(b, dict)]
    board = args.board or desc.get("defaultBoard")
    if board_ids and board not in board_ids:
        print(f"error: unknown board '{board}' for target '{target}'. Known boards: {', '.join(board_ids)}", file=sys.stderr)
        return EXIT_CONFIG
    if desc.get("status") == "skeleton":
        print(f"warning: target '{target}' is a skeleton (no working HAL backend yet); selecting anyway.", file=sys.stderr)

    new_state = dict(local_state) if isinstance(local_state, dict) else {}
    new_state["target"] = target
    if board is not None:
        new_state["board"] = board
    vscode_dir.mkdir(parents=True, exist_ok=True)
    (vscode_dir / LOCAL_STATE_FILENAME).write_text(
        json.dumps(new_state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    ensure_local_state_gitignored(project_dir)
    print(f"Selected target={target} board={board or '(target default)'}")
    print(f"Persisted to .vscode/{LOCAL_STATE_FILENAME} (gitignored; project default stays in the manifest).")
    return 0


def as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    return bool(value)


def resolve_cli(cli: str | None) -> str:
    if cli:
        expanded = os.path.expanduser(cli)
        if os.path.isabs(expanded) or "/" in expanded:
            return expanded
        found = shutil.which(expanded)
        if found:
            return found
        return expanded
    found = shutil.which("arduino-cli")
    return found or "arduino-cli"


def require_arduino_sketch(project_dir: Path, module: str) -> Path:
    native = project_dir / f"{module}.ino"
    if native.is_file():
        return project_dir
    raise ValueError(
        f"arduino-cli toolchain requires {native}; "
        "use toolchain 'cmake' for projects that generate the Arduino sketch via CMake"
    )


def build_arduino_compile_command(
    config: dict[str, Any],
    project_dir: Path,
    *,
    debug: bool = False,
    upload: bool = False,
    only_compilation_database: bool = False,
) -> list[str]:
    cli = resolve_cli(config.get("cliPath"))
    fqbn = config.get("fqbn")
    if not fqbn:
        raise ValueError("missing fqbn; set jaszczurhal.fqbn or arduino.fqbn")

    build_dir = get_build_dir(config, project_dir)
    sketch_dir = require_arduino_sketch(project_dir, str(config.get("module") or project_dir.name))

    cmd = [
        cli,
        "compile",
        "--fqbn",
        str(fqbn),
        "--build-path",
        str(build_dir),
    ]

    sketchbook = str(config.get("sketchbookPath") or "")
    if sketchbook:
        libraries_dir = Path(os.path.expanduser(sketchbook)) / "libraries"
        if libraries_dir.is_dir():
            cmd.extend(["--libraries", str(libraries_dir)])

    cmd.extend(
        [
            "--build-property",
            f"compiler.cpp.extra_flags=-I '{project_dir}'",
            "--build-property",
            f"compiler.c.extra_flags=-I '{project_dir}'",
        ]
    )

    identity = config.get("identity")
    if isinstance(identity, dict) and as_bool(identity.get("enabled")):
        manufacturer = identity.get("usbManufacturer")
        product = identity.get("usbProduct")
        if manufacturer and product:
            cmd.extend(["--build-property", f'build.usb_manufacturer="{manufacturer}"'])
            cmd.extend(["--build-property", f'build.usb_product="{product}"'])

    if debug:
        cmd.append("--optimize-for-debug")
    if upload:
        cmd.append("--upload")
        port = config.get("uploadPort") or (config.get("upload") or {}).get("port")
        if port:
            cmd.extend(["--port", str(port)])
    if only_compilation_database:
        cmd.append("--only-compilation-database")

    cmd.extend(["--warnings", "all"])
    if as_bool(config.get("verbose")):
        cmd.append("-v")
    cmd.append(str(sketch_dir))
    return cmd


def normalize_identity_text(value: str) -> str:
    return "".join(ch.lower() if ch.isalnum() else "_" for ch in value).strip("_")


def identity_enabled(config: dict[str, Any]) -> bool:
    identity = config.get("identity")
    return isinstance(identity, dict) and as_bool(identity.get("enabled"))


def expected_identity_tokens(config: dict[str, Any]) -> list[str]:
    identity = config.get("identity")
    if not isinstance(identity, dict):
        return []
    tokens = []
    by_id_hint = identity.get("byIdHint")
    if isinstance(by_id_hint, str) and by_id_hint.strip():
        tokens.append(normalize_identity_text(by_id_hint))
    manufacturer = identity.get("usbManufacturer")
    product = identity.get("usbProduct")
    if isinstance(manufacturer, str) and isinstance(product, str) and manufacturer.strip() and product.strip():
        tokens.append(normalize_identity_text(f"{manufacturer}_{product}"))
        tokens.append(normalize_identity_text(f"{manufacturer} {product}"))
    return [token for token in tokens if token]


def by_id_links_for_port(port: str) -> list[Path]:
    port_path = Path(port)
    try:
        resolved_port = port_path.resolve()
    except OSError:
        return []

    by_id_dir = Path("/dev/serial/by-id")
    if not by_id_dir.is_dir():
        return []

    matches = []
    for link in sorted(by_id_dir.iterdir()):
        try:
            if link.resolve() == resolved_port:
                matches.append(link)
        except OSError:
            continue
    return matches


def read_optional_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        return ""


def tty_usb_identity_text(tty: Path) -> str:
    sys_tty = Path("/sys/class/tty") / tty.name / "device"
    try:
        device = sys_tty.resolve(strict=True)
    except OSError:
        return ""

    parts: list[str] = []
    current = device
    sys_root = Path("/sys")
    for _ in range(8):
        for name in ("manufacturer", "product", "interface", "serial"):
            value = read_optional_text(current / name)
            if value:
                parts.append(value)
        if current == sys_root or current.parent == current:
            break
        current = current.parent
    return " ".join(parts)


def verified_identity_ports(config: dict[str, Any]) -> list[tuple[Path, Path | None]]:
    expected = expected_identity_tokens(config)
    if not expected:
        return []

    matches: list[tuple[Path, Path | None]] = []
    seen: set[Path] = set()

    by_id_dir = Path("/dev/serial/by-id")
    if by_id_dir.is_dir():
        for link in sorted(by_id_dir.iterdir()):
            normalized = normalize_identity_text(link.name)
            if not any(token in normalized for token in expected):
                continue
            try:
                resolved = link.resolve(strict=True)
            except OSError:
                continue
            if resolved in seen:
                continue
            seen.add(resolved)
            matches.append((resolved, link))

    for pattern in ("ttyACM*", "ttyUSB*"):
        for tty in sorted(Path("/dev").glob(pattern)):
            try:
                resolved = tty.resolve(strict=True)
            except OSError:
                continue
            if resolved in seen:
                continue
            normalized = normalize_identity_text(tty_usb_identity_text(tty))
            if normalized and any(token in normalized for token in expected):
                seen.add(resolved)
                matches.append((resolved, None))
    return matches


def serial_candidate_paths() -> list[Path]:
    candidates: set[Path] = set()
    for pattern in ("/dev/ttyACM*", "/dev/ttyUSB*"):
        for path in sorted(Path("/dev").glob(Path(pattern).name)):
            if path.exists():
                candidates.add(path)

    by_id_dir = Path("/dev/serial/by-id")
    if by_id_dir.is_dir():
        for link in by_id_dir.iterdir():
            try:
                resolved = link.resolve(strict=True)
            except OSError:
                continue
            if resolved.name.startswith(("ttyACM", "ttyUSB")):
                candidates.add(resolved)
    return sorted(candidates, key=lambda path: path.name)


def serial_port_records(config: dict[str, Any] | None = None) -> list[dict[str, Any]]:
    expected = expected_identity_tokens(config or {})
    records: list[dict[str, Any]] = []
    for port in serial_candidate_paths():
        links = by_id_links_for_port(str(port))
        link_names = [link.name for link in links]
        sysfs_identity = tty_usb_identity_text(port)
        haystacks = [normalize_identity_text(name) for name in link_names]
        if sysfs_identity:
            haystacks.append(normalize_identity_text(sysfs_identity))
        verified = bool(expected and any(token in text for token in expected for text in haystacks))
        records.append(
            {
                "port": str(port),
                "byId": link_names,
                "sysfsIdentity": sysfs_identity,
                "verifiedForProject": verified,
            }
        )
    return records


def bootsel_candidates_without_mount() -> list[str]:
    mounts = find_bootsel_mounts()
    blocks = find_bootsel_blocks()
    block_mounts = [mount for mount in (bootsel_mountpoint(block) for block in blocks) if mount]
    for mount in block_mounts:
        if mount not in mounts:
            mounts.append(mount)

    unique_mounts = sorted(set(mounts))
    unmounted = [block for block in blocks if bootsel_mountpoint(block) is None]
    return [str(mount) for mount in unique_mounts] + [
        str(block.get("path")) for block in unmounted if block.get("path")
    ]


def command_list_ports(args: argparse.Namespace) -> int:
    project_dir = resolve_project(args)
    config: dict[str, Any] = {}
    if project_dir is not None:
        if not project_dir.exists() or not project_dir.is_dir():
            print(f"error: project directory does not exist: {project_dir}", file=sys.stderr)
            return EXIT_CONFIG
        try:
            config = load_project_config(
            project_dir,
            target_override=getattr(args, "target", None),
            board_override=getattr(args, "board", None),
        )
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return EXIT_CONFIG
        apply_cli_overrides(config, args)

    records = serial_port_records(config if config else None)
    bootsel = bootsel_candidates_without_mount()

    if args.json:
        print(
            json.dumps(
                {
                    "project": str(project_dir) if project_dir else None,
                    "expectedIdentity": identity_display_text(config) if identity_enabled(config) else None,
                    "serial": records,
                    "bootsel": bootsel,
                },
                indent=2,
                sort_keys=True,
            )
        )
        return 0

    if identity_enabled(config):
        print(f"Expected identity: {identity_display_text(config)}")
    if records:
        print("Serial ports:")
        for record in records:
            label = "verified" if record["verifiedForProject"] else "unverified"
            print(f"  {record['port']} [{label}]")
            for link in record["byId"]:
                print(f"    by-id: {link}")
            if record["sysfsIdentity"]:
                print(f"    sysfs: {record['sysfsIdentity']}")
    else:
        print("Serial ports: none")

    if bootsel:
        print("BOOTSEL candidates:")
        for candidate in bootsel:
            print(f"  {candidate}")
    else:
        print("BOOTSEL candidates: none")
    return 0


def select_verified_identity_port(config: dict[str, Any]) -> tuple[str | None, int]:
    matches = verified_identity_ports(config)
    if len(matches) == 1:
        port, link = matches[0]
        if link is not None:
            print(yellow_text(f"Using verified serial port: {port} ({link.name})"))
        else:
            print(yellow_text(f"Using verified serial port: {port} (matched USB identity from sysfs)"))
        return str(port), 0
    if len(matches) > 1:
        print("error: multiple verified serial ports match this project identity:", file=sys.stderr)
        for port, link in matches:
            suffix = f" ({link.name})" if link is not None else " (matched USB identity from sysfs)"
            print(f"  {port}{suffix}", file=sys.stderr)
        print("error: pass --port explicitly to choose one", file=sys.stderr)
        return None, EXIT_UNSAFE_DEVICE
    return None, 0


def identity_display_text(config: dict[str, Any]) -> str:
    identity = config.get("identity") or {}
    manufacturer = identity.get("usbManufacturer") or "?"
    product = identity.get("usbProduct") or "?"
    by_id_hint = identity.get("byIdHint")
    label = f"{manufacturer} {product}"
    if by_id_hint:
        label = f"{label} (by-id hint: {by_id_hint})"
    return label


def print_identity_upload_requirements(config: dict[str, Any]) -> None:
    print("error: identity-enabled upload has no verified target", file=sys.stderr)
    print(f"error: expected USB identity: {identity_display_text(config)}", file=sys.stderr)
    print("error: requirements for the default 'upload' flow:", file=sys.stderr)
    print("  1. For normal reflashing, the running firmware must expose a USB serial port", file=sys.stderr)
    print("     whose /dev/serial/by-id link or sysfs USB descriptors match the expected identity.", file=sys.stderr)
    print("  2. If no serial port is configured, exactly one BOOTSEL UF2 drive may be visible", file=sys.stderr)
    print("     so the tool can safely fall back to UF2 upload.", file=sys.stderr)
    print("  3. For the first flash of a clean board, either use BOOTSEL/UF2 or pass", file=sys.stderr)
    print("     both an explicit --port and --allow-unverified-port intentionally.", file=sys.stderr)
    print("  4. If more than one matching board is connected, pass --port explicitly.", file=sys.stderr)
    print("error: currently no verified serial port was selected and no usable BOOTSEL fallback was chosen", file=sys.stderr)


def resolve_upload_port_for_tool(port: str) -> str:
    if not port:
        return port
    try:
        resolved = Path(port).resolve(strict=True)
    except OSError:
        return port
    if resolved.parent == Path("/dev") and resolved.name.startswith("tty"):
        return str(resolved)
    return port


def verify_upload_port(config: dict[str, Any], port: str, *, allow_unverified: bool = False) -> int:
    if not identity_enabled(config):
        return 0
    if allow_unverified:
        print("warning: unverified serial upload allowed by --allow-unverified-port", file=sys.stderr)
        return 0
    if not port:
        print_identity_upload_requirements(config)
        return EXIT_UNSAFE_DEVICE

    expected = expected_identity_tokens(config)
    links = by_id_links_for_port(port)
    normalized_names = [normalize_identity_text(link.name) for link in links]

    if expected and any(token in name for token in expected for name in normalized_names):
        return 0

    try:
        sysfs_port = Path(port).resolve(strict=True)
    except OSError:
        sysfs_port = Path(port)
    normalized_sysfs = normalize_identity_text(tty_usb_identity_text(sysfs_port))
    if expected and normalized_sysfs and any(token in normalized_sysfs for token in expected):
        return 0

    print(f"error: refusing upload to unverified port: {port}", file=sys.stderr)
    print(f"error: expected USB identity: {identity_display_text(config)}", file=sys.stderr)
    if links:
        print("error: matching /dev/serial/by-id links for this port:", file=sys.stderr)
        for link in links:
            print(f"  {link.name}", file=sys.stderr)
    else:
        print("error: no /dev/serial/by-id link resolves to this port", file=sys.stderr)
    print("error: for first flash of a clean board, pass --allow-unverified-port with an explicit --port", file=sys.stderr)
    return EXIT_UNSAFE_DEVICE


def process_cmdline(pid: int) -> str:
    try:
        raw = Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ").strip()
        return raw.decode("utf-8", errors="replace")
    except Exception:
        return ""


def port_owner_pids(port: str) -> list[int]:
    pids: set[int] = set()
    for cmd in (["fuser", port], ["lsof", "-t", "--", port]):
        try:
            result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, check=False)
        except Exception:
            continue
        for item in result.stdout.split():
            try:
                pids.add(int(item))
            except ValueError:
                pass
    return sorted(pids)


def owns_jh_monitor(pid: int, project_dir: Path) -> bool:
    cmdline = process_cmdline(pid)
    if "serial_persistent.py" not in cmdline and "serial-persistent.py" not in cmdline:
        return False
    return str(project_dir) in cmdline


def upload_marker(project_dir: Path) -> Path:
    return project_dir / ".vscode" / ".jh-upload-in-progress"


def begin_upload_release(project_dir: Path) -> None:
    marker = upload_marker(project_dir)
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(str(os.getpid()), encoding="utf-8")


def end_upload_release(project_dir: Path) -> None:
    marker = upload_marker(project_dir)
    try:
        marker.unlink()
    except FileNotFoundError:
        pass
    except OSError as exc:
        print(f"warning: could not remove upload marker {marker}: {exc}", file=sys.stderr)


def release_port_for_upload(port: str, project_dir: Path) -> int:
    owners = [pid for pid in port_owner_pids(port) if pid != os.getpid()]
    if not owners:
        return 0

    own_monitors = [pid for pid in owners if owns_jh_monitor(pid, project_dir)]
    foreign = [pid for pid in owners if pid not in own_monitors]
    if foreign:
        print(f"error: serial port is busy and not owned by this project monitor: {port}", file=sys.stderr)
        for pid in foreign:
            print(f"  PID {pid}: {process_cmdline(pid) or '?'}", file=sys.stderr)
        return EXIT_UNSAFE_DEVICE

    begin_upload_release(project_dir)
    for pid in own_monitors:
        try:
            release_signal = signal.SIGUSR1 if hasattr(signal, "SIGUSR1") else signal.SIGTERM
            os.kill(pid, release_signal)
            print(yellow_text(f"released own serial monitor PID {pid}"))
        except ProcessLookupError:
            pass
        except PermissionError:
            print(f"error: cannot stop own serial monitor PID {pid}: permission denied", file=sys.stderr)
            end_upload_release(project_dir)
            return EXIT_UNSAFE_DEVICE

    deadline = time.time() + 3.0
    while time.time() < deadline:
        remaining = [pid for pid in port_owner_pids(port) if pid != os.getpid()]
        if not remaining:
            return 0
        time.sleep(0.1)

    print(f"error: serial port stayed busy after stopping own monitor: {port}", file=sys.stderr)
    for pid in port_owner_pids(port):
        if pid != os.getpid():
            print(f"  PID {pid}: {process_cmdline(pid) or '?'}", file=sys.stderr)
    end_upload_release(project_dir)
    return EXIT_UNSAFE_DEVICE


def run_command(cmd: list[str], *, verbose: bool = False) -> int:
    if verbose:
        print("+ " + " ".join(cmd), flush=True)
    if Path(cmd[0]).name == "cmake":
        try:
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
        except FileNotFoundError:
            print(f"error: command not found: {cmd[0]}", file=sys.stderr)
            return EXIT_CONFIG
        except OSError as exc:
            print(f"error: failed to run {cmd[0]}: {exc}", file=sys.stderr)
            return EXIT_GENERIC

        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(maybe_yellow_output(line))
            sys.stdout.flush()
        return int(process.wait())

    try:
        completed = subprocess.run(cmd, check=False)
    except FileNotFoundError:
        print(f"error: command not found: {cmd[0]}", file=sys.stderr)
        return EXIT_CONFIG
    except OSError as exc:
        print(f"error: failed to run {cmd[0]}: {exc}", file=sys.stderr)
        return EXIT_GENERIC
    return int(completed.returncode)


def memory_overview_enabled() -> bool:
    value = os.environ.get("JH_VSCODE_MEMORY_OVERVIEW", "1").strip().lower()
    return value not in {"0", "false", "no", "off"}


def find_configured_elf(config: dict[str, Any]) -> Path | None:
    artifacts = config.get("artifacts")
    if not isinstance(artifacts, dict):
        return None
    elf = artifacts.get("elf")
    if not elf:
        return None
    path = Path(str(elf)).expanduser()
    if path.is_file():
        return path
    return None


def find_elf(build_dir: Path) -> Path | None:
    direct = build_dir / "firmware.elf"
    if direct.is_file():
        return direct

    matches = sorted(build_dir.rglob("*.elf"))
    if not matches:
        return None
    if len(matches) == 1:
        return matches[0]

    exact = [path for path in matches if path.parent == build_dir]
    if len(exact) == 1:
        return exact[0]
    return None


def resolve_elf_artifact(config: dict[str, Any], project_dir: Path) -> Path | None:
    configured = find_configured_elf(config)
    if configured is not None:
        return configured
    return find_elf(get_build_dir(config, project_dir))


def parse_objdump_sections(elf: Path, objdump: str) -> list[dict[str, Any]]:
    try:
        result = subprocess.run([objdump, "-h", str(elf)], check=False, capture_output=True, text=True)
    except OSError as exc:
        print(f"warning: memory map overview skipped; failed to run {objdump}: {exc}", file=sys.stderr)
        return []
    if result.returncode != 0:
        message = (result.stderr or result.stdout).strip()
        if message:
            print(f"warning: memory map overview skipped; objdump failed: {message}", file=sys.stderr)
        return []

    sections: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    for line in result.stdout.splitlines():
        match = SECTION_HEADER_RE.match(line)
        if match:
            current = {
                "name": match.group("name"),
                "size": int(match.group("size"), 16),
                "vma": int(match.group("vma"), 16),
                "lma": int(match.group("lma"), 16),
                "flags": [],
            }
            sections.append(current)
            continue
        if current is not None:
            stripped = line.strip()
            if stripped:
                current["flags"] = [part.strip() for part in stripped.split(",") if part.strip()]
                current = None

    return [
        section
        for section in sections
        if section["size"] > 0 and section["vma"] != 0 and "ALLOC" in set(section.get("flags") or [])
    ]


def is_flash_address(address: int) -> bool:
    return 0x10000000 <= address < 0x11000000


def is_psram_address(address: int) -> bool:
    return 0x11000000 <= address < 0x12000000


def is_sram_address(address: int) -> bool:
    return 0x20000000 <= address < 0x30000000


def memory_region(address: int) -> str:
    if is_flash_address(address):
        return "FLASH/XIP"
    if is_psram_address(address):
        return "PSRAM/XIP"
    if is_sram_address(address):
        return "SRAM"
    return "OTHER"


def format_size(size: int) -> str:
    if size < 1024:
        return f"{size} B"
    kib = size / 1024
    if kib < 1024:
        return f"{kib:.1f} KiB"
    return f"{kib / 1024:.2f} MiB"


def format_range(start: int, size: int) -> str:
    return f"0x{start:08x}-0x{start + size - 1:08x}"


def memory_section_note(section: dict[str, Any]) -> str:
    name = str(section["name"])
    flags = set(section.get("flags") or [])
    vma = int(section["vma"])
    lma = int(section["lma"])

    if name == ".boot2":
        return "boot block"
    if name == ".ota":
        return "OTA metadata"
    if name == ".partition":
        return "partition table"
    if name == ".flash_end":
        return "flash marker"
    if name == ".data" and is_sram_address(vma) and is_flash_address(lma):
        return "RAM runtime, FLASH init"
    if name in {".bss", ".tbss"}:
        return "zeroed RAM"
    if name == ".noinit":
        return "retained/noinit RAM"
    if "heap" in name:
        return "reserved heap"
    if "stack" in name:
        return "reserved stack"
    if name == ".ram_vector_table":
        return "runtime vector table"
    if "CODE" in flags:
        return "code"
    if "READONLY" in flags:
        return "read-only data"
    if "LOAD" not in flags:
        return "reserved"
    return "data"


def print_memory_map_overview(config: dict[str, Any], project_dir: Path) -> None:
    if not memory_overview_enabled():
        return

    elf = resolve_elf_artifact(config, project_dir)
    if elf is None:
        return

    objdump = shutil.which("arm-none-eabi-objdump") or shutil.which("objdump")
    if objdump is None:
        print("warning: memory map overview skipped; arm-none-eabi-objdump not found", file=sys.stderr)
        return

    sections = parse_objdump_sections(elf, objdump)
    if not sections:
        return

    regions: dict[str, list[dict[str, Any]]] = {}
    for section in sections:
        regions.setdefault(memory_region(int(section["vma"])), []).append(section)

    print("")
    print(f"Memory map overview: {elf}")
    for region in ("FLASH/XIP", "SRAM", "PSRAM/XIP", "OTHER"):
        region_sections = regions.get(region)
        if not region_sections:
            continue
        total = sum(int(section["size"]) for section in region_sections)
        print(f"{region} ({format_size(total)} by VMA):")
        print("  section              VMA range              LMA          size       note")
        for section in region_sections:
            lma = int(section["lma"])
            vma = int(section["vma"])
            flags = set(section.get("flags") or [])
            if "LOAD" not in flags:
                lma_text = "no-load"
            elif lma == vma:
                lma_text = "same"
            else:
                lma_text = f"0x{lma:08x}"
            print(
                f"  {str(section['name']):<20} "
                f"{format_range(vma, int(section['size'])):<22} "
                f"{lma_text:<12} "
                f"{format_size(int(section['size'])):>9}  "
                f"{memory_section_note(section)}"
            )

    flash_load_sections = [
        section
        for section in sections
        if is_flash_address(int(section["lma"])) and "LOAD" in set(section.get("flags") or [])
    ]
    flash_load_size = sum(int(section["size"]) for section in flash_load_sections)
    sram_static = sum(
        int(section["size"])
        for section in sections
        if memory_region(int(section["vma"])) == "SRAM"
        and "heap" not in str(section["name"])
        and "stack" not in str(section["name"])
    )
    sram_reserved = sum(
        int(section["size"])
        for section in sections
        if memory_region(int(section["vma"])) == "SRAM"
        and ("heap" in str(section["name"]) or "stack" in str(section["name"]))
    )
    print(
        "Totals: "
        f"FLASH load sections {format_size(flash_load_size)}, "
        f"SRAM static {format_size(sram_static)}, "
        f"SRAM reserved heap/stack {format_size(sram_reserved)}"
    )


def load_config_for_action(args: argparse.Namespace) -> tuple[Path, dict[str, Any], int]:
    project_dir = resolve_project(args)
    if project_dir is None:
        print(f"error: {args.action} requires --project <path>", file=sys.stderr)
        return Path(), {}, EXIT_USAGE
    if not project_dir.exists() or not project_dir.is_dir():
        print(f"error: project directory does not exist: {project_dir}", file=sys.stderr)
        return project_dir, {}, EXIT_CONFIG
    try:
        config = load_project_config(
            project_dir,
            target_override=getattr(args, "target", None),
            board_override=getattr(args, "board", None),
        )
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return project_dir, {}, EXIT_CONFIG
    apply_cli_overrides(config, args)
    return project_dir, config, 0


def get_cmake_build_dir(config: dict[str, Any], project_dir: Path) -> Path:
    build_dir = Path(str(config.get("cmakeBuildDir") or project_dir / ".build" / "cmake")).expanduser()
    if not build_dir.is_absolute():
        build_dir = project_dir / build_dir
    # When a target is active, isolate the CMake cache per target/board: RP2040
    # (project NONE) and STM32 (cross toolchain) must never share one cache dir.
    # Manifests without a resolved target keep the flat path -> pico parity.
    target = config.get("target")
    if target:
        build_dir = build_dir / str(target)
        board = config.get("board")
        if board:
            build_dir = build_dir / str(board)
    return build_dir


def get_cmake_source_dir(config: dict[str, Any], project_dir: Path) -> Path:
    cmake = config.get("cmake")
    source_dir = None
    if isinstance(cmake, dict):
        source_dir = cmake.get("sourceDir")
    source_path = Path(str(source_dir or project_dir)).expanduser()
    if not source_path.is_absolute():
        source_path = project_dir / source_path
    return source_path


def cmake_targets(config: dict[str, Any]) -> dict[str, str]:
    defaults = {
        "build": "firmware",
        "buildDebug": "firmware_debug",
        "upload": "firmware_upload",
        "compileDb": "firmware_compile_db",
    }
    cmake = config.get("cmake")
    if isinstance(cmake, dict) and isinstance(cmake.get("targets"), dict):
        defaults.update({str(key): str(value) for key, value in cmake["targets"].items()})
    return defaults


def cmake_cache_args(config: dict[str, Any], project_dir: Path) -> list[str]:
    args = [
        f"-DARDUINO_CLI={resolve_cli(config.get('cliPath'))}",
        f"-DARDUINO_FQBN={config.get('fqbn') or ''}",
        f"-DARDUINO_SKETCHBOOK={config.get('sketchbookPath') or ''}",
        f"-DARDUINO_UPLOAD_PORT={config.get('uploadPort') or (config.get('upload') or {}).get('port') or ''}",
        f"-DARDUINO_VERBOSE={'ON' if as_bool(config.get('verbose')) else 'OFF'}",
    ]

    root = config.get("root")
    if isinstance(root, str) and root:
        root_path = Path(os.path.expanduser(root))
        if not root_path.is_absolute():
            root_path = project_dir / root_path
        args.append(f"-DJH_ROOT={root_path.resolve()}")

    identity = config.get("identity")
    if isinstance(identity, dict) and as_bool(identity.get("enabled")):
        manufacturer = identity.get("usbManufacturer")
        product = identity.get("usbProduct")
        if manufacturer and product:
            args.append(f"-DJH_USB_MANUFACTURER={manufacturer}")
            args.append(f"-DJH_USB_PRODUCT={product}")

    cmake = config.get("cmake")
    if isinstance(cmake, dict) and isinstance(cmake.get("cache"), dict):
        for key, value in cmake["cache"].items():
            if value is None:
                continue
            if isinstance(value, bool):
                value = "ON" if value else "OFF"
            args.append(f"-D{key}={value}")

    return args


def configure_cmake_project(config: dict[str, Any], project_dir: Path) -> int:
    source_dir = get_cmake_source_dir(config, project_dir)
    build_dir = get_cmake_build_dir(config, project_dir)
    cmd = ["cmake", "-S", str(source_dir), "-B", str(build_dir)]
    cmd.extend(cmake_cache_args(config, project_dir))
    return run_command(cmd, verbose=as_bool(config.get("verbose")))


def run_cmake_target(config: dict[str, Any], project_dir: Path, target: str) -> int:
    configure_status = configure_cmake_project(config, project_dir)
    if configure_status != 0:
        return configure_status
    cmd = ["cmake", "--build", str(get_cmake_build_dir(config, project_dir)), "--target", target]
    return run_command(cmd, verbose=as_bool(config.get("verbose")))


def command_build(args: argparse.Namespace, *, debug: bool = False, show_memory_overview: bool = True) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if config.get("toolchain") == "cmake":
        target = cmake_targets(config)["buildDebug" if debug else "build"]
        rc = run_cmake_target(config, project_dir, target)
        if rc == 0:
            if show_memory_overview:
                print_memory_map_overview(config, project_dir)
            return 0
        return EXIT_BUILD
    if config.get("toolchain") not in {"arduino-cli", "custom"}:
        print(f"error: build for toolchain '{config.get('toolchain')}' is not implemented yet", file=sys.stderr)
        return EXIT_UNSUPPORTED
    with build_lock(config, project_dir):
        try:
            cmd = build_arduino_compile_command(config, project_dir, debug=debug)
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return EXIT_CONFIG
        rc = run_command(cmd, verbose=as_bool(config.get("verbose")))
    if rc == 0:
        if show_memory_overview:
            print_memory_map_overview(config, project_dir)
        return 0
    return EXIT_BUILD


def command_upload(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if config.get("toolchain") not in {"arduino-cli", "custom", "cmake"}:
        print(f"error: upload for toolchain '{config.get('toolchain')}' is not implemented yet", file=sys.stderr)
        return EXIT_UNSUPPORTED
    port = args.port or config.get("uploadPort") or (config.get("upload") or {}).get("port")
    upload_config = config.get("upload") or {}
    upload_strategy = upload_config.get("strategy") if isinstance(upload_config, dict) else None
    if config.get("toolchain") == "cmake" and upload_strategy == "openocd":
        # SWD/JTAG flashers (e.g. STM32 via OpenOCD): no serial port, BOOTSEL,
        # by-id identity guard, port release, or monitor handoff applies. Delegate
        # straight to the CMake firmware_upload target after config resolution.
        target = cmake_targets(config)["upload"]
        rc = run_cmake_target(config, project_dir, target)
        if rc == 0:
            print_memory_map_overview(config, project_dir)
            return 0
        return EXIT_UPLOAD
    if config.get("toolchain") == "cmake" and not port:
        if upload_strategy == "uf2":
            return command_upload_uf2(args)
        _, bootsel_candidates = find_single_bootsel_mount()
        if bootsel_candidates:
            print("No serial upload port configured; BOOTSEL device detected, using UF2 upload.")
            return command_upload_uf2(args)
        if identity_enabled(config):
            port, detect_status = select_verified_identity_port(config)
            if detect_status != 0:
                return detect_status

    verify_status = verify_upload_port(config, str(port or ""), allow_unverified=args.allow_unverified_port)
    if verify_status != 0:
        return verify_status
    if port:
        upload_port = resolve_upload_port_for_tool(str(port))
        config["uploadPort"] = upload_port
        upload = dict(config.get("upload") or {})
        upload["port"] = upload_port
        config["upload"] = upload
        release_status = release_port_for_upload(upload_port, project_dir)
        if release_status != 0:
            return release_status
    if config.get("toolchain") == "cmake":
        try:
            target = cmake_targets(config)["upload"]
            rc = run_cmake_target(config, project_dir, target)
        finally:
            end_upload_release(project_dir)
        if rc == 0:
            print_memory_map_overview(config, project_dir)
            return 0
        return EXIT_UPLOAD
    try:
        with build_lock(config, project_dir):
            try:
                cmd = build_arduino_compile_command(config, project_dir, upload=True)
            except ValueError as exc:
                print(f"error: {exc}", file=sys.stderr)
                return EXIT_CONFIG
            rc = run_command(cmd, verbose=as_bool(config.get("verbose")))
    finally:
        end_upload_release(project_dir)
    if rc == 0:
        print_memory_map_overview(config, project_dir)
        return 0
    return EXIT_UPLOAD


def find_configured_uf2(config: dict[str, Any]) -> Path | None:
    artifacts = config.get("artifacts")
    if not isinstance(artifacts, dict):
        return None
    uf2 = artifacts.get("uf2")
    if not uf2:
        return None
    path = Path(str(uf2)).expanduser()
    if path.is_file():
        return path
    return None


def find_uf2(build_dir: Path) -> Path | None:
    matches = sorted(build_dir.rglob("*.uf2"))
    if not matches:
        return None
    if len(matches) == 1:
        return matches[0]
    exact = [path for path in matches if path.parent == build_dir]
    if len(exact) == 1:
        return exact[0]
    print("error: multiple UF2 artifacts found:", file=sys.stderr)
    for path in matches:
        print(f"  {path}", file=sys.stderr)
    return None


def find_bootsel_mounts() -> list[Path]:
    user = os.environ.get("USER", "")
    roots = []
    if user:
        roots.extend([Path("/media") / user, Path("/run/media") / user])
    mounts: list[Path] = []
    for root in roots:
        if not root.is_dir():
            continue
        for child in root.iterdir():
            if child.is_dir() and child.name in BOOTSEL_LABELS:
                mounts.append(child)
    return sorted(mounts)


def iter_lsblk_devices(devices: list[dict[str, Any]]) -> list[dict[str, Any]]:
    flat: list[dict[str, Any]] = []
    for device in devices:
        flat.append(device)
        children = device.get("children")
        if isinstance(children, list):
            flat.extend(iter_lsblk_devices(children))
    return flat


def find_bootsel_blocks() -> list[dict[str, Any]]:
    cmd = ["lsblk", "--json", "-o", "PATH,LABEL,FSTYPE,MOUNTPOINTS"]
    try:
        result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    except OSError:
        return []
    if result.returncode != 0:
        return []
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError:
        return []

    matches: list[dict[str, Any]] = []
    for device in iter_lsblk_devices(payload.get("blockdevices") or []):
        if device.get("label") not in BOOTSEL_LABELS:
            continue
        if device.get("fstype") not in {None, "vfat", "fat", "msdos"}:
            continue
        path = device.get("path")
        if not path:
            continue
        matches.append(device)
    return matches


def bootsel_mountpoint(block: dict[str, Any]) -> Path | None:
    mountpoints = block.get("mountpoints")
    if isinstance(mountpoints, list):
        for mountpoint in mountpoints:
            if mountpoint:
                return Path(str(mountpoint))
    mountpoint = block.get("mountpoint")
    if mountpoint:
        return Path(str(mountpoint))
    return None


def mount_bootsel_block(block: dict[str, Any]) -> Path | None:
    existing = bootsel_mountpoint(block)
    if existing:
        return existing

    device = str(block.get("path") or "")
    if not device:
        return None
    if shutil.which("udisksctl") is None:
        return None

    cmd = ["udisksctl", "mount", "-b", device]
    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        message = (result.stderr or result.stdout).strip()
        if message:
            print(f"warning: could not mount {device}: {message}", file=sys.stderr)
        return None

    blocks = find_bootsel_blocks()
    for candidate in blocks:
        if candidate.get("path") == device:
            return bootsel_mountpoint(candidate)
    mounts = find_bootsel_mounts()
    if len(mounts) == 1:
        return mounts[0]
    return None


def find_single_bootsel_mount() -> tuple[Path | None, list[str]]:
    mounts = find_bootsel_mounts()
    blocks = find_bootsel_blocks()
    block_mounts = [mount for mount in (bootsel_mountpoint(block) for block in blocks) if mount]
    for mount in block_mounts:
        if mount not in mounts:
            mounts.append(mount)

    unique_mounts = sorted(set(mounts))
    unmounted = [block for block in blocks if bootsel_mountpoint(block) is None]
    candidates = [str(mount) for mount in unique_mounts] + [
        str(block.get("path")) for block in unmounted if block.get("path")
    ]
    if len(candidates) != 1:
        return None, candidates
    if unique_mounts:
        return unique_mounts[0], candidates
    return mount_bootsel_block(unmounted[0]), candidates


def command_upload_uf2(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status

    build_status = command_build(args, debug=False, show_memory_overview=False)
    if build_status != 0:
        return build_status

    build_dir = Path(str(config.get("buildDir") or project_dir / ".build")).expanduser()
    if not build_dir.is_absolute():
        build_dir = project_dir / build_dir
    uf2 = find_configured_uf2(config) or find_uf2(build_dir)
    if uf2 is None:
        print(f"error: no unique UF2 artifact found in {build_dir}", file=sys.stderr)
        return EXIT_UPLOAD

    mount, bootsel_candidates = find_single_bootsel_mount()
    if not bootsel_candidates:
        print("error: BOOTSEL drive not found", file=sys.stderr)
        return EXIT_UNSAFE_DEVICE
    if len(bootsel_candidates) > 1:
        print("error: multiple BOOTSEL drives found; refusing to guess", file=sys.stderr)
        for candidate in bootsel_candidates:
            print(f"  {candidate}", file=sys.stderr)
        return EXIT_UNSAFE_DEVICE
    if mount is None:
        print("error: BOOTSEL drive found but could not be mounted", file=sys.stderr)
        return EXIT_UNSAFE_DEVICE

    destination = mount / uf2.name
    print(f"Copying {uf2} -> {destination}")
    try:
        shutil.copy2(uf2, destination)
        os.sync()
    except OSError as exc:
        print(f"error: UF2 copy failed: {exc}", file=sys.stderr)
        return EXIT_UPLOAD
    print_memory_map_overview(config, project_dir)
    time.sleep(0.2)
    return 0


def neutral_firmware_source_dir() -> Path:
    return Path(__file__).resolve().parents[2] / "neutral_fw" / "rp2040_arduino_pico"


def prepare_neutral_sketch(build_dir: Path) -> Path:
    source = neutral_firmware_source_dir() / "neutral_identity.ino"
    if not source.is_file():
        raise FileNotFoundError(f"neutral firmware source not found: {source}")

    sketch_dir = build_dir / "_jh_neutral_identity" / "neutral_identity"
    sketch_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, sketch_dir / "neutral_identity.ino")
    return sketch_dir


def build_neutral_compile_command(config: dict[str, Any], sketch_dir: Path, build_dir: Path) -> list[str]:
    cli = resolve_cli(config.get("cliPath"))
    fqbn = config.get("fqbn")
    if not fqbn:
        raise ValueError("missing fqbn; set jaszczurhal.fqbn or arduino.fqbn")

    cmd = [
        cli,
        "compile",
        "--fqbn",
        str(fqbn),
        "--build-path",
        str(build_dir),
        "--upload",
    ]
    port = config.get("uploadPort") or (config.get("upload") or {}).get("port")
    if port:
        cmd.extend(["--port", str(port)])
    cmd.extend(["--warnings", "all"])
    if as_bool(config.get("verbose")):
        cmd.append("-v")
    cmd.append(str(sketch_dir))
    return cmd


def command_clear_identity(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if config.get("toolchain") not in {"arduino-cli", "custom", "cmake"}:
        print(f"error: clear-identity for toolchain '{config.get('toolchain')}' is not implemented yet", file=sys.stderr)
        return EXIT_UNSUPPORTED

    port = args.port or config.get("uploadPort") or (config.get("upload") or {}).get("port")
    verify_status = verify_upload_port(config, str(port or ""), allow_unverified=args.allow_unverified_port)
    if verify_status != 0:
        return verify_status
    if port:
        upload_port = resolve_upload_port_for_tool(str(port))
        config["uploadPort"] = upload_port
        upload = dict(config.get("upload") or {})
        upload["port"] = upload_port
        config["upload"] = upload
        release_status = release_port_for_upload(upload_port, project_dir)
        if release_status != 0:
            return release_status

    neutral_build_dir = get_build_dir(config, project_dir) / "neutral_identity"
    try:
        with build_lock(config, project_dir):
            try:
                sketch_dir = prepare_neutral_sketch(get_build_dir(config, project_dir))
                cmd = build_neutral_compile_command(config, sketch_dir, neutral_build_dir)
            except (OSError, ValueError) as exc:
                print(f"error: {exc}", file=sys.stderr)
                return EXIT_CONFIG
            rc = run_command(cmd, verbose=as_bool(config.get("verbose")))
    finally:
        end_upload_release(project_dir)
    return 0 if rc == 0 else EXIT_UPLOAD


def command_monitor(args: argparse.Namespace, mode: str) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status

    runtime = Path(__file__).resolve().parent / "serial_persistent.py"
    cmd = [
        sys.executable,
        str(runtime),
        "--project",
        str(project_dir),
        "--baud",
        str(args.baud),
        "--mode",
        mode,
        "--lock-policy",
        args.lock_policy,
    ]
    port = args.port or config.get("uploadPort") or (config.get("upload") or {}).get("port")
    if port:
        cmd.append(str(port))
    rc = run_command(cmd, verbose=as_bool(config.get("verbose")))
    return 0 if rc == 0 else EXIT_MONITOR


def get_build_dir(config: dict[str, Any], project_dir: Path) -> Path:
    build_dir = Path(str(config.get("buildDir") or project_dir / ".build")).expanduser()
    if not build_dir.is_absolute():
        build_dir = project_dir / build_dir
    return build_dir


@contextmanager
def build_lock(config: dict[str, Any], project_dir: Path):
    build_dir = get_build_dir(config, project_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    lock_path = build_dir / ".jh-build.lock"
    with lock_path.open("w", encoding="utf-8") as lock_file:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)


def command_refresh_intellisense(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if config.get("toolchain") == "cmake":
        target = cmake_targets(config)["compileDb"]
        rc = run_cmake_target(config, project_dir, target)
        if rc != 0:
            return EXIT_BUILD

        build_dir = get_build_dir(config, project_dir)
        raw_compile_db = build_dir / "arduino" / "compile_commands.json"
        if not raw_compile_db.is_file():
            raw_compile_db = build_dir / "compile_commands.json"
        patched_compile_db = build_dir / "compile_commands_patched.json"
        if not raw_compile_db.is_file():
            print(f"error: compile database was not generated: {raw_compile_db}", file=sys.stderr)
            return EXIT_BUILD

        try:
            compile_db = json.loads(raw_compile_db.read_text(encoding="utf-8"))
            patched_compile_db.write_text(json.dumps(compile_db, indent=2) + "\n", encoding="utf-8")
        except (OSError, json.JSONDecodeError) as exc:
            print(f"error: failed to write patched compile database: {exc}", file=sys.stderr)
            return EXIT_GENERIC

        cpp_props = {
            "configurations": [
                {
                    "name": str(config.get("module") or project_dir.name),
                    "compileCommands": str(patched_compile_db),
                    "intelliSenseMode": "gcc-arm",
                    "compilerPath": "",
                    "cStandard": "c11",
                    "cppStandard": "gnu++17",
                }
            ],
            "version": 4,
        }
        cpp_props_path = project_dir / ".vscode" / "c_cpp_properties.json"
        try:
            cpp_props_path.write_text(json.dumps(cpp_props, indent=4) + "\n", encoding="utf-8")
        except OSError as exc:
            print(f"error: failed to write {cpp_props_path}: {exc}", file=sys.stderr)
            return EXIT_GENERIC

        print(f"Wrote {patched_compile_db}")
        print(f"Wrote {cpp_props_path}")
        return 0
    if config.get("toolchain") not in {"arduino-cli", "custom"}:
        print(f"error: refresh-intellisense for toolchain '{config.get('toolchain')}' is not implemented yet", file=sys.stderr)
        return EXIT_UNSUPPORTED

    with build_lock(config, project_dir):
        try:
            cmd = build_arduino_compile_command(config, project_dir, only_compilation_database=True)
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return EXIT_CONFIG

        rc = run_command(cmd, verbose=as_bool(config.get("verbose")))
    if rc != 0:
        return EXIT_BUILD

    build_dir = get_build_dir(config, project_dir)
    raw_compile_db = build_dir / "compile_commands.json"
    patched_compile_db = build_dir / "compile_commands_patched.json"
    if not raw_compile_db.is_file():
        print(f"error: compile database was not generated: {raw_compile_db}", file=sys.stderr)
        return EXIT_BUILD

    try:
        compile_db = json.loads(raw_compile_db.read_text(encoding="utf-8"))
        patched_compile_db.write_text(json.dumps(compile_db, indent=2) + "\n", encoding="utf-8")
    except (OSError, json.JSONDecodeError) as exc:
        print(f"error: failed to write patched compile database: {exc}", file=sys.stderr)
        return EXIT_GENERIC

    cpp_props = {
        "configurations": [
            {
                "name": str(config.get("module") or project_dir.name),
                "compileCommands": str(patched_compile_db),
                "intelliSenseMode": "gcc-arm",
                "compilerPath": "",
                "cStandard": "c11",
                "cppStandard": "gnu++17",
            }
        ],
        "version": 4,
    }
    cpp_props_path = project_dir / ".vscode" / "c_cpp_properties.json"
    try:
        cpp_props_path.write_text(json.dumps(cpp_props, indent=4) + "\n", encoding="utf-8")
    except OSError as exc:
        print(f"error: failed to write {cpp_props_path}: {exc}", file=sys.stderr)
        return EXIT_GENERIC

    print(f"Wrote {patched_compile_db}")
    print(f"Wrote {cpp_props_path}")
    return 0


def command_clean(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    build_dir = get_build_dir(config, project_dir)
    if not build_dir.exists():
        print(f"Nothing to clean: {build_dir}")
        return 0
    if project_dir not in build_dir.resolve().parents and build_dir.resolve() != project_dir:
        print(f"error: refusing to clean outside project directory: {build_dir}", file=sys.stderr)
        return EXIT_UNSAFE_DEVICE
    try:
        shutil.rmtree(build_dir)
    except OSError as exc:
        print(f"error: failed to remove {build_dir}: {exc}", file=sys.stderr)
        return EXIT_GENERIC
    print(f"Removed {build_dir}")
    return 0


def command_stub(args: argparse.Namespace) -> int:
    action = "build-debug" if args.action == "debug" else args.action
    if args.action in MODULE_ACTIONS:
        project_dir = resolve_project(args)
        if project_dir is None:
            print(f"error: {args.action} requires --project <path>", file=sys.stderr)
            return EXIT_USAGE
        if not project_dir.exists() or not project_dir.is_dir():
            print(f"error: project directory does not exist: {project_dir}", file=sys.stderr)
            return EXIT_CONFIG

    print(f"error: action '{action}' is part of the CLI contract but is not implemented yet", file=sys.stderr)
    return EXIT_UNSUPPORTED


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="jh-vscode",
        description="Shared JaszczurHAL VS Code firmware workflow entrypoint.",
    )
    parser.add_argument("action", nargs="?", help="Action to run.")
    parser.add_argument("--project", help="Firmware module directory.")
    parser.add_argument("--fqbn", help="Override board FQBN.")
    parser.add_argument("--target", help="Override active target family (e.g. rp2040, stm32g474).")
    parser.add_argument("--board", help="Override active board/variant within the target.")
    parser.add_argument("--port", help="Override serial upload/monitor port.")
    parser.add_argument(
        "--allow-unverified-port",
        action="store_true",
        help="Allow upload to a port that does not match configured USB identity.",
    )
    parser.add_argument("--baud", type=int, default=115200, help="Serial monitor baud rate.")
    parser.add_argument(
        "--lock-policy",
        choices=["wait", "replace-own", "replace-any"],
        default="wait",
        help="Serial monitor port lock policy.",
    )
    parser.add_argument("--verbose", action="store_true", help="Enable verbose output.")
    parser.add_argument("--json", action="store_true", help="Emit JSON where supported.")
    parser.add_argument("--version", action="store_true", help="Show version and exit.")
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.version:
        print(f"jh-vscode {VERSION}")
        return 0
    if not args.action:
        parser.print_help()
        return 0

    if args.action not in SUPPORTED_ACTIONS:
        print(f"error: unsupported action '{args.action}'", file=sys.stderr)
        return EXIT_UNSUPPORTED

    if args.action == "config-dump":
        return command_config_dump(args)
    if args.action == "build":
        return command_build(args, debug=False)
    if args.action in {"build-debug", "debug"}:
        return command_build(args, debug=True)
    if args.action == "upload":
        return command_upload(args)
    if args.action == "upload-uf2":
        return command_upload_uf2(args)
    if args.action == "monitor":
        return command_monitor(args, "pico")
    if args.action == "monitor-probe":
        return command_monitor(args, "probe")
    if args.action == "monitor-any":
        return command_monitor(args, "any")
    if args.action == "list-ports":
        return command_list_ports(args)
    if args.action == "refresh-intellisense":
        return command_refresh_intellisense(args)
    if args.action == "clean":
        return command_clean(args)
    if args.action == "clear-identity":
        return command_clear_identity(args)
    if args.action == "select-board":
        return command_select_board(args)
    return command_stub(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        raise SystemExit(EXIT_GENERIC)
