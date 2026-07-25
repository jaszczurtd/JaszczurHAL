#!/usr/bin/env python3
"""JaszczurHAL VS Code firmware workflow entrypoint."""

from __future__ import annotations

import argparse
import binascii
import copy
from contextlib import contextmanager
import errno
import fcntl
import hashlib
import hmac
import json
import os
import re
import signal
import shutil
import socket
import struct
import subprocess
import tempfile
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
    "upload-ota",
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
    "upload-ota",
    "monitor",
    "monitor-probe",
    "monitor-any",
    "refresh-intellisense",
    "clean",
    "select-board",
    "sync-board-picker",
    "list-ports",
    "ota-discover",
    "change-port",
    "clear-identity",
    "config-dump",
}

BOOTSEL_LABELS = {"RPI-RP2", "RP2350", "RPI-RP2350"}
NATIVE_RP_TARGETS = {
    "rp2040",
    "rp2350-arm",
    "rp2350-riscv",
}
UF2_TARGETS = {
    *NATIVE_RP_TARGETS,
}
CMAKE_CACHE_KEYS_FILE = ".jh-vscode-cache-keys.json"
DEFAULT_OTA_PORT = 8266
DEFAULT_OTA_LISTEN_PORT = 8266
CMAKE_TRANSIENT_CACHE_KEYS = {
    "JH_ARTIFACT_DIR",
    "JH_EXTRA_DEFINES",
    "JH_EXTRA_INCLUDES",
    "JH_EXTRA_LIBRARIES",
    "JH_EXTRA_SOURCES",
    "JH_LINK_LIBRARIES",
    "JH_PROJECT_RECIPE",
    "JH_PROJECT_SOURCES",
    "JH_OTA_GENERATION",
    "JH_OTA_VERSION",
    "JH_RP2040_EXTRA_LINK_FLAGS",
    "JH_RP2040_FREERTOS",
    "JH_USB_MANUFACTURER",
    "JH_USB_PRODUCT",
}

SECTION_HEADER_RE = re.compile(
    r"^\s*\d+\s+"
    r"(?P<name>\S+)\s+"
    r"(?P<size>[0-9a-fA-F]+)\s+"
    r"(?P<vma>[0-9a-fA-F]+)\s+"
    r"(?P<lma>[0-9a-fA-F]+)\s+"
    r"(?P<fileoff>[0-9a-fA-F]+)\s+"
    r"2\*\*(?P<align>\d+)"
)
HAL_ENABLE_RE = re.compile(r"^\s*#\s*define\s+(HAL_ENABLE_[A-Z0-9_]+)\b")
HAL_DEFINE_TOKEN_RE = re.compile(r"(?:^|(?<=[;\s]))(?:-D)?(HAL_ENABLE_[A-Z0-9_]+)(?=[=;\s]|$)")
REGION_OVERFLOW_RE = re.compile(r"region [`'](?P<region>[^`']+)[`'] overflowed by (?P<bytes>\d+) bytes")
SECTION_WILL_NOT_FIT_RE = re.compile(
    r"section [`'](?P<section>[^`']+)[`'] will not fit in region [`'](?P<region>[^`']+)[`']"
)

STM32G474_NETWORK_MODULES = {
    "HAL_ENABLE_WIFI",
    "HAL_ENABLE_TIME",
    "HAL_ENABLE_MQTT",
    "HAL_ENABLE_UDP",
    "HAL_ENABLE_TCP",
    "HAL_ENABLE_HTTP_SERVER",
    "HAL_ENABLE_HTTP_FILES",
    "HAL_ENABLE_WEBSOCKET",
    "HAL_ENABLE_NET_CONSOLE",
    "HAL_ENABLE_NET_COMMANDS",
    "HAL_ENABLE_BSD_SOCKETS",
    "HAL_ENABLE_OTA",
    "HAL_ENABLE_WIREGUARD",
}

ANSI_YELLOW = "\033[33m"
ANSI_RESET = "\033[0m"
YELLOW_OUTPUT_PREFIXES = (
    "Using verified serial port:",
    "released own serial monitor PID ",
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
        "${jhRoot}": str(jaszczurhal_root()),
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
    for section_name in ("artifacts", "upload", "ota", "hooks"):
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
    # value. cmake.cache from all three layers deep-merges key-by-key. The active
    # dispatcher target itself is pinned after the merge so legacy manifests with
    # a hard-coded JH_TARGET cannot silently select a different backend.
    base = dict(config)
    base.pop("targetProfiles", None)
    merged = deep_merge(deep_merge(registry_layer, base), manifest_overlay)
    config.clear()
    config.update(merged)

    # Expand tokens the overlay may have introduced (idempotent on the base).
    expand_config_sections(config, project_dir)

    cmake = config.get("cmake")
    if isinstance(cmake, dict):
        cache = cmake.get("cache")
        if not isinstance(cache, dict):
            cache = {}
            cmake["cache"] = cache
        cache["JH_TARGET"] = active

    target_switched = active != (manifest_target or "rp2040")
    registry_upload = target_desc.get("upload") if isinstance(target_desc, dict) else None
    overlay_upload = manifest_overlay.get("upload") if isinstance(manifest_overlay, dict) else None
    if (
        target_switched
        and isinstance(registry_upload, dict)
        and registry_upload.get("strategy")
        and not (isinstance(overlay_upload, dict) and overlay_upload.get("strategy"))
    ):
        upload = config.get("upload")
        if not isinstance(upload, dict):
            upload = {}
            config["upload"] = upload
        upload["strategy"] = registry_upload["strategy"]
        sources["upload"] = f"registry:{active}.upload.strategy"

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
    return jaszczurhal_root() / "boards"


def load_target_registry() -> dict[str, dict[str, Any]]:
    """Load the tooling view generated in memory from authoritative ``boards/``."""
    scripts_dir = jaszczurhal_root() / "scripts"
    if str(scripts_dir) not in sys.path:
        sys.path.insert(0, str(scripts_dir))
    from board_registry import tooling_target_registry

    return tooling_target_registry(jaszczurhal_root())


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
        "example",
        "toolchain",
        "target",
        "board",
        "targetProfiles",
        "buildDir",
        "cmakeBuildDir",
        "cmake",
        "identity",
        "artifacts",
        "upload",
        "ota",
        "hooks",
    ):
        if key in data:
            config[key] = data[key]
    return config


def example_variants(config: dict[str, Any]) -> list[dict[str, Any]]:
    example = config.get("example")
    if not isinstance(example, dict):
        return []
    variants = example.get("variants")
    if not isinstance(variants, list):
        return []
    return [variant for variant in variants if isinstance(variant, dict)]


def apply_example_variant(config: dict[str, Any], variant_id: str | None) -> None:
    if not variant_id:
        return

    variant: dict[str, Any] | None = None
    for candidate in example_variants(config):
        if str(candidate.get("id") or "") == variant_id:
            variant = candidate
            break
    if variant is None:
        known = ", ".join(str(item.get("id")) for item in example_variants(config) if item.get("id"))
        raise ValueError(f"unknown example variant '{variant_id}'" + (f"; known variants: {known}" if known else ""))

    module = str(variant.get("module") or f"{config.get('module', 'firmware')}_{variant_id}")
    config["module"] = module
    if config.get("cmakeBuildDir"):
        config["cmakeBuildDir"] = f"{config['cmakeBuildDir']}/variants/{variant_id}"

    cmake = dict(config.get("cmake") or {})
    cache = dict(cmake.get("cache") or {})
    cache["JH_MODULE_NAME"] = module
    sources = variant.get("sources")
    if isinstance(sources, list) and sources:
        cache["JH_PROJECT_SOURCES"] = ";".join(str(source) for source in sources)
    extra_defines = variant.get("extraDefines")
    if isinstance(extra_defines, list) and extra_defines:
        cache["JH_EXTRA_DEFINES"] = ";".join(str(item) for item in extra_defines)
    if isinstance(variant.get("cmake"), dict):
        variant_cmake = variant["cmake"]
        if isinstance(variant_cmake.get("cache"), dict):
            cache = deep_merge(cache, {str(k): v for k, v in variant_cmake["cache"].items()})
        cmake = deep_merge(cmake, {k: v for k, v in variant_cmake.items() if k != "cache"})
    cmake["cache"] = cache
    config["cmake"] = cmake

    example = dict(config.get("example") or {})
    example["activeVariant"] = variant_id
    if isinstance(variant.get("targets"), list):
        example["activeTargets"] = [str(target) for target in variant["targets"]]
    config["example"] = example
    config.setdefault("_sources", {})["example.activeVariant"] = "cli"


def settings_value(settings: dict[str, Any], semantic_key: str) -> Any:
    jh_key = f"jaszczurhal.{semantic_key}"

    aliases = {
        "uploadPort": ("jaszczurhal.uploadPort",),
        "buildDir": ("jaszczurhal.buildDir",),
        "verbose": ("jaszczurhal.verbose",),
        "projectName": ("jaszczurhal.projectName",),
        "moduleName": ("jaszczurhal.moduleName",),
        "usbManufacturer": ("jaszczurhal.usbManufacturer",),
        "usbProduct": ("jaszczurhal.usbProduct",),
        "identityEnabled": ("jaszczurhal.identityEnabled",),
        "vscodeEntry": ("jaszczurhal.vscodeEntry",),
        "root": ("jaszczurhal.root",),
    }

    for key in aliases.get(semantic_key, (jh_key,)):
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

    config = normalize_manifest(manifest)
    sources: dict[str, str] = {}
    for key in config:
        sources[key] = ".vscode/jaszczurhal.project.json"

    setting_map = {
        "uploadPort": "uploadPort",
        "buildDir": "buildDir",
        "verbose": "verbose",
        "projectName": "project",
        "moduleName": "module",
        "vscodeEntry": "vscodeEntry",
        "root": "root",
    }
    for semantic, target in setting_map.items():
        if target in config and target in {"project", "module", "buildDir"}:
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

    if "project" not in config:
        config["project"] = project_dir.parent.name
        sources["project"] = "directory-fallback"
    if "module" not in config:
        config["module"] = project_dir.name
        sources["module"] = "directory-fallback"
    if "toolchain" not in config:
        config["toolchain"] = "custom"
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
    local_port = local_state.get("uploadPort") if isinstance(local_state, dict) else None
    if local_port:
        config["uploadPort"] = str(local_port)
        upload = dict(config.get("upload") or {})
        upload["port"] = str(local_port)
        config["upload"] = upload
        sources["uploadPort"] = ".vscode/jaszczurhal.local.json"
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
    try:
        apply_example_variant(config, getattr(args, "variant", None))
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


def git_repo_root(path: Path) -> Path | None:
    try:
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "--show-toplevel"],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    root = result.stdout.strip()
    return Path(root).resolve() if root else None


def git_path_is_ignored(path: Path) -> bool:
    root = git_repo_root(path.parent)
    if root is None:
        return False
    try:
        rel = path.resolve().relative_to(root)
    except ValueError:
        return False
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "check-ignore", "-q", "--", rel.as_posix()],
            check=False,
        )
    except OSError:
        return False
    return result.returncode == 0


def gitignore_has_entry(gitignore: Path, entry: str) -> bool:
    if not gitignore.exists():
        return False
    try:
        text = gitignore.read_text(encoding="utf-8")
    except OSError:
        return False
    return entry in [line.strip() for line in text.splitlines()]


def local_state_gitignore_target(project_dir: Path) -> tuple[Path, str]:
    project_gitignore = project_dir / ".gitignore"
    entry = f".vscode/{LOCAL_STATE_FILENAME}"
    if project_gitignore.exists():
        return project_gitignore, entry

    root = git_repo_root(project_dir)
    if root is not None:
        root_gitignore = root / ".gitignore"
        if root_gitignore.exists():
            local_state = project_dir / ".vscode" / LOCAL_STATE_FILENAME
            try:
                return root_gitignore, local_state.resolve().relative_to(root).as_posix()
            except ValueError:
                pass

    return project_gitignore, entry


def ensure_local_state_gitignored(project_dir: Path) -> None:
    """Make sure the gitignored user-local selection file is actually ignored.
    Appends a missing rule only; existing .gitignore contents are never
    rewritten or regenerated."""
    local_state = project_dir / ".vscode" / LOCAL_STATE_FILENAME
    if git_path_is_ignored(local_state):
        return

    gitignore, entry = local_state_gitignore_target(project_dir)
    if gitignore_has_entry(gitignore, entry):
        return

    text = ""
    if gitignore.exists():
        try:
            text = gitignore.read_text(encoding="utf-8")
        except OSError:
            return
    prefix = "" if (not text or text.endswith("\n")) else "\n"
    try:
        with gitignore.open("a", encoding="utf-8") as handle:
            handle.write(f"{prefix}# jh-vscode user-local active board selection\n{entry}\n")
    except OSError:
        pass


def collect_hal_enables(config: dict[str, Any], project_dir: Path) -> dict[str, str]:
    enabled: dict[str, str] = {}
    hal_project_config = project_dir / "hal_project_config.h"
    if hal_project_config.is_file():
        try:
            text = hal_project_config.read_text(encoding="utf-8", errors="replace")
        except OSError:
            text = ""
        for line in text.splitlines():
            match = HAL_ENABLE_RE.match(line)
            if match:
                enabled.setdefault(match.group(1), "hal_project_config.h")

    cmake = config.get("cmake")
    cache = cmake.get("cache") if isinstance(cmake, dict) else None
    if isinstance(cache, dict):
        for key in ("JH_EXTRA_DEFINES", "EXTRA_HAL_DEFINES"):
            value = cache.get(key)
            if not value:
                continue
            for match in HAL_DEFINE_TOKEN_RE.finditer(str(value)):
                enabled.setdefault(match.group(1), f"cmake.cache.{key}")
    return enabled


def target_descriptor(config: dict[str, Any]) -> dict[str, Any] | None:
    target = config.get("target")
    if not target:
        return None
    return load_target_registry().get(str(target))


def target_display_name(config: dict[str, Any]) -> str:
    target = str(config.get("target") or "default")
    board = config.get("board")
    if board:
        return f"{target}/{board}"
    return target


def build_preflight_diagnostics(config: dict[str, Any], project_dir: Path) -> list[str]:
    messages: list[str] = []
    target = str(config.get("target") or "")
    example = config.get("example")
    if isinstance(example, dict):
        supported_targets = example.get("activeTargets") or example.get("targets")
        if isinstance(supported_targets, list) and target and target not in [str(item) for item in supported_targets]:
            variant = example.get("activeVariant")
            suffix = f" variant '{variant}'" if variant else ""
            messages.append(
                f"axis-2: example {config.get('module', project_dir.name)}{suffix} "
                f"does not declare support for target {target_display_name(config)}; "
                f"supported targets: {', '.join(str(item) for item in supported_targets)}."
            )
            return messages

    desc = target_descriptor(config)
    if isinstance(desc, dict) and desc.get("status") == "skeleton":
        messages.append(
            f"axis-2: target {target_display_name(config)} is a skeleton; "
            "the HAL backend/recipe is not implemented yet."
        )
        return messages

    enabled = collect_hal_enables(config, project_dir)
    if target == "stm32g474":
        network = sorted(module for module in enabled if module in STM32G474_NETWORK_MODULES)
        project_config = project_dir / "hal_project_config.h"
        try:
            project_defines = project_config.read_text(encoding="utf-8", errors="replace")
        except OSError:
            project_defines = ""
        cmake = config.get("cmake")
        cache = cmake.get("cache") if isinstance(cmake, dict) else {}
        cache_defines = str(cache.get("JH_EXTRA_DEFINES", "")) if isinstance(cache, dict) else ""
        configured_cyw43 = all(
            symbol in project_defines or symbol in cache_defines
            for symbol in (
                "HAL_NETWORK_BACKEND_CYW43",
                "HAL_CYW43_BUS_STM32_GSPI",
                "HAL_CYW43_STACK_LWIP",
            )
        )
        if network and not configured_cyw43:
            messages.append(
                "axis-2: stm32g474 network modules require the explicit "
                "CYW43 gSPI/lwIP backend profile; "
                f"enabled modules: {', '.join(network)}. "
                "Define HAL_NETWORK_BACKEND_CYW43, HAL_CYW43_BUS_STM32_GSPI "
                "and HAL_CYW43_STACK_LWIP, or disable those modules."
            )
    return messages


def print_build_diagnostics(messages: list[str]) -> None:
    if not messages:
        return
    print("", file=sys.stderr)
    print(yellow_text("JaszczurHAL diagnostics:"), file=sys.stderr)
    for message in messages:
        print(f"  - {message}", file=sys.stderr)


def diagnose_build_output(output: str, config: dict[str, Any]) -> list[str]:
    messages: list[str] = []
    seen: set[tuple[str, str]] = set()
    overflow_regions: set[str] = set()
    for match in REGION_OVERFLOW_RE.finditer(output):
        region = match.group("region")
        overflow_bytes = int(match.group("bytes"))
        key = ("overflow", region)
        if key in seen:
            continue
        seen.add(key)
        overflow_regions.add(region)
        messages.append(
            f"axis-3: firmware does not fit {target_display_name(config)} region {region}; "
            f"linker reports overflow by {format_size(overflow_bytes)} ({overflow_bytes} bytes)."
        )

    for match in SECTION_WILL_NOT_FIT_RE.finditer(output):
        section = match.group("section")
        region = match.group("region")
        if region in overflow_regions:
            continue
        key = ("section", region)
        if key in seen:
            continue
        seen.add(key)
        messages.append(
            f"axis-3: section {section} will not fit {target_display_name(config)} region {region}."
        )
    return messages


def select_board_options(registry: dict[str, dict[str, Any]]) -> list[tuple[str, str, dict[str, Any], dict[str, Any]]]:
    options: list[tuple[str, str, dict[str, Any], dict[str, Any]]] = []
    for target_id, desc in sorted_target_items(registry):
        boards = desc.get("boards") or []
        for board in boards:
            if not isinstance(board, dict):
                continue
            board_id = board.get("id")
            if board_id:
                options.append((str(target_id), str(board_id), desc, board))
    return options


def sorted_target_items(registry: dict[str, dict[str, Any]]) -> list[tuple[str, dict[str, Any]]]:
    return sorted(registry.items(), key=lambda item: (item[1].get("status") == "skeleton", item[0]))


def parse_board_selection(selection: str) -> tuple[str, str | None]:
    value = selection.strip()
    if " - " in value:
        value = value.split(" - ", 1)[0].strip()
    if not value:
        raise ValueError("empty board selection")
    value = value.split()[0]
    if ":" in value:
        target, board = value.split(":", 1)
    elif "/" in value:
        target, board = value.split("/", 1)
    else:
        target, board = value, None
    target = target.strip()
    board = board.strip() if board is not None else None
    if not target:
        raise ValueError(f"invalid board selection: {selection!r}")
    if board == "":
        board = None
    return target, board


def current_board_selection(
    registry: dict[str, dict[str, Any]],
    local_state: dict[str, Any],
    manifest: dict[str, Any],
) -> tuple[str, str | None, str]:
    target = local_state.get("target") or manifest.get("target") or "rp2040"
    source = ".vscode/jaszczurhal.local.json" if local_state.get("target") else (
        ".vscode/jaszczurhal.project.json" if manifest.get("target") else "default"
    )
    board = local_state.get("board") or manifest.get("board")
    desc = registry.get(str(target))
    if isinstance(desc, dict):
        board_ids = [b.get("id") for b in (desc.get("boards") or []) if isinstance(b, dict)]
        if board is None or (board_ids and board not in board_ids):
            board = desc.get("defaultBoard")
    return str(target), str(board) if board is not None else None, source


def persist_board_selection(
    project_dir: Path,
    local_state: dict[str, Any],
    target: str,
    board: str | None,
) -> int:
    vscode_dir = project_dir / ".vscode"
    new_state = dict(local_state) if isinstance(local_state, dict) else {}
    new_state["target"] = target
    if board is not None:
        new_state["board"] = board
    else:
        new_state.pop("board", None)
    vscode_dir.mkdir(parents=True, exist_ok=True)
    (vscode_dir / LOCAL_STATE_FILENAME).write_text(
        json.dumps(new_state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    ensure_local_state_gitignored(project_dir)
    print(f"Selected target={target} board={board or '(target default)'}")
    print(f"Persisted to .vscode/{LOCAL_STATE_FILENAME} (gitignored; project default stays in the manifest).")
    return 0


def validate_board_selection(
    registry: dict[str, dict[str, Any]],
    target: str,
    board: str | None,
) -> tuple[dict[str, Any] | None, str | None, int]:
    desc = registry.get(target)
    if desc is None:
        known = ", ".join(sorted(registry)) or "(registry empty)"
        print(f"error: unknown target '{target}'. Known targets: {known}", file=sys.stderr)
        return None, None, EXIT_CONFIG
    board_ids = [b.get("id") for b in (desc.get("boards") or []) if isinstance(b, dict)]
    selected_board = board or desc.get("defaultBoard")
    if board_ids and selected_board not in board_ids:
        print(f"error: unknown board '{selected_board}' for target '{target}'. Known boards: {', '.join(board_ids)}", file=sys.stderr)
        return None, None, EXIT_CONFIG
    if desc.get("status") == "skeleton":
        print(f"warning: target '{target}' is a skeleton (no working HAL backend yet); selecting anyway.", file=sys.stderr)
    return desc, str(selected_board) if selected_board is not None else None, 0


def interactive_board_selection(
    registry: dict[str, dict[str, Any]],
    current_target: str,
    current_board: str | None,
) -> tuple[str | None, str | None, int]:
    options = select_board_options(registry)
    if not options:
        print("error: target registry has no selectable boards", file=sys.stderr)
        return None, None, EXIT_CONFIG

    print(f"Current selection: target={current_target} board={current_board or '(target default)'}")
    print("Select target/board:")
    for index, (target, board, desc, board_desc) in enumerate(options, start=1):
        marker = "*" if target == current_target and board == current_board else " "
        skeleton = " [skeleton]" if desc.get("status") == "skeleton" else ""
        print(
            f"  {index:2d}. {marker} {target}:{board}{skeleton} - "
            f"{desc.get('displayName', target)} / {board_desc.get('displayName', board)}"
        )
    print("  q. cancel")
    try:
        answer = input("Board selection> ").strip()
    except EOFError:
        print("error: no interactive input available", file=sys.stderr)
        return None, None, EXIT_USAGE
    if answer.lower() in {"", "q", "quit", "cancel"}:
        print("Selection cancelled.")
        return None, None, 0
    try:
        selected = int(answer)
    except ValueError:
        try:
            return (*parse_board_selection(answer), 0)
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return None, None, EXIT_USAGE
    if selected < 1 or selected > len(options):
        print(f"error: selection out of range: {selected}", file=sys.stderr)
        return None, None, EXIT_USAGE
    target, board, _, _ = options[selected - 1]
    return target, board, 0


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
    current_target, current_board, current_source = current_board_selection(registry, local_state, manifest)

    target = args.target
    board = args.board
    if args.selection:
        try:
            target, board = parse_board_selection(args.selection)
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return EXIT_USAGE
    elif args.interactive and not target:
        target, board, status = interactive_board_selection(registry, current_target, current_board)
        if status != 0:
            return status
        if target is None:
            return 0

    # List mode: no selection requested.
    if not target:
        if args.json:
            listing = {
                "current": {"target": current_target, "board": current_board, "source": current_source},
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
                    for tid, desc in sorted_target_items(registry)
                ],
            }
            print(json.dumps(listing, ensure_ascii=False, indent=2, sort_keys=True))
            return 0
        print(f"Current selection: target={current_target} board={current_board or '(target default)'} ({current_source})")
        print("Available targets:")
        for tid, desc in sorted_target_items(registry):
            flag = " [skeleton]" if desc.get("status") == "skeleton" else ""
            print(f"  {tid}{flag} - {desc.get('displayName', '')}")
            for board in desc.get("boards") or []:
                if not isinstance(board, dict):
                    continue
                mark = "*" if board.get("id") == desc.get("defaultBoard") else " "
                print(f"     {mark} {board.get('id')} - {board.get('displayName', '')}")
        print("Select: jh-vscode select-board --project <p> --target <id> [--board <id>]")
        print("        jh-vscode select-board --project <p> --interactive")
        return 0

    # Set mode.
    _, selected_board, status = validate_board_selection(registry, str(target), board)
    if status != 0:
        return status
    return persist_board_selection(project_dir, local_state, str(target), selected_board)


def json_indent_width(text: str) -> int:
    for line in text.splitlines()[1:]:
        stripped = line.lstrip()
        if stripped.startswith('"'):
            width = len(line) - len(stripped)
            if width > 0:
                return width
    return 4


def write_text_atomic(path: Path, content: str) -> None:
    mode = path.stat().st_mode & 0o777
    temp_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as handle:
            temp_path = Path(handle.name)
            handle.write(content)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temp_path, mode)
        os.replace(temp_path, path)
    finally:
        if temp_path is not None and temp_path.exists():
            temp_path.unlink()


def command_sync_board_picker(args: argparse.Namespace) -> int:
    project_dir = resolve_project(args)
    if project_dir is None:
        print("error: sync-board-picker requires --project <path>", file=sys.stderr)
        return EXIT_USAGE
    if not project_dir.is_dir():
        print(f"error: project directory does not exist: {project_dir}", file=sys.stderr)
        return EXIT_CONFIG

    tasks_path = project_dir / ".vscode" / "tasks.json"
    manifest_path = project_dir / ".vscode" / "jaszczurhal.project.json"
    if not tasks_path.is_file():
        print(f"error: VS Code tasks file does not exist: {tasks_path}", file=sys.stderr)
        return EXIT_CONFIG

    try:
        tasks_text = tasks_path.read_text(encoding="utf-8")
        tasks_document = load_json_file(tasks_path)
        manifest = load_json_file(manifest_path)
        registry = load_target_registry()
        selected_target, selected_board, _ = current_board_selection(
            registry,
            {},
            manifest,
        )
        if selected_board is None:
            raise ValueError(
                f"target '{selected_target}' does not define a selectable board"
            )

        from vscode_task_config import sync_board_picker_document

        changed = sync_board_picker_document(
            tasks_document,
            registry,
            selected_target,
            selected_board,
        )
    except (OSError, ValueError) as exc:
        print(f"error: cannot synchronize {tasks_path}: {exc}", file=sys.stderr)
        return EXIT_CONFIG

    if not changed:
        print(f"Board picker is current: {tasks_path}")
        return 0

    content = (
        json.dumps(
            tasks_document,
            indent=json_indent_width(tasks_text),
            ensure_ascii=False,
        )
        + "\n"
    )
    try:
        write_text_atomic(tasks_path, content)
    except OSError as exc:
        print(f"error: cannot update {tasks_path}: {exc}", file=sys.stderr)
        return EXIT_CONFIG
    print(f"Updated board picker from the JaszczurHAL registry: {tasks_path}")
    return 0


def as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    return bool(value)



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
    block_mounts = [
        mount
        for mount in (bootsel_mountpoint(block) for block in blocks)
        if mount
    ]
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


def command_change_port(args: argparse.Namespace) -> int:
    project_dir = resolve_project(args)
    if project_dir is None:
        print("error: change-port requires --project <path>", file=sys.stderr)
        return EXIT_USAGE
    if not project_dir.is_dir():
        print(f"error: project directory does not exist: {project_dir}", file=sys.stderr)
        return EXIT_CONFIG

    selected = args.port
    records = serial_port_records()
    if not selected:
        if not records:
            print("error: no serial ports found; connect a device or pass --port <path>", file=sys.stderr)
            return EXIT_CONFIG
        print("Select serial port:")
        for index, record in enumerate(records, start=1):
            suffix = f" ({', '.join(record['byId'])})" if record["byId"] else ""
            print(f"  {index:2d}. {record['port']}{suffix}")
        print("  q. cancel")
        try:
            answer = input("Port selection> ").strip()
        except EOFError:
            print("error: no interactive input available; pass --port <path>", file=sys.stderr)
            return EXIT_USAGE
        if answer.lower() in {"", "q", "quit", "cancel"}:
            print("Selection cancelled.")
            return 0
        try:
            index = int(answer)
        except ValueError:
            selected = answer
        else:
            if index < 1 or index > len(records):
                print(f"error: selection out of range: {index}", file=sys.stderr)
                return EXIT_USAGE
            selected = records[index - 1]["port"]

    selected_path = Path(str(selected)).expanduser()
    if not selected_path.exists():
        print(f"error: serial port does not exist: {selected_path}", file=sys.stderr)
        return EXIT_CONFIG
    try:
        selected_value = str(selected_path.resolve(strict=True))
    except OSError as exc:
        print(f"error: cannot resolve serial port {selected_path}: {exc}", file=sys.stderr)
        return EXIT_CONFIG

    vscode_dir = project_dir / ".vscode"
    local_path = vscode_dir / LOCAL_STATE_FILENAME
    local_state = load_json_file(local_path)
    local_state["uploadPort"] = selected_value
    vscode_dir.mkdir(parents=True, exist_ok=True)
    local_path.write_text(json.dumps(local_state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    ensure_local_state_gitignored(project_dir)
    print(f"Selected upload/monitor port: {selected_value}")
    print(f"Persisted to .vscode/{LOCAL_STATE_FILENAME} (user-local).")
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


def upload_port_path_exists(port: str) -> bool:
    if not port:
        return False
    return Path(port).expanduser().exists()


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


def run_command_capture(cmd: list[str], *, verbose: bool = False, capture_limit: int = 250_000) -> tuple[int, str]:
    if verbose:
        print("+ " + " ".join(cmd), flush=True)
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
        return EXIT_CONFIG, ""
    except OSError as exc:
        print(f"error: failed to run {cmd[0]}: {exc}", file=sys.stderr)
        return EXIT_GENERIC, ""

    captured: list[str] = []
    captured_size = 0
    assert process.stdout is not None
    for line in process.stdout:
        sys.stdout.write(maybe_yellow_output(line))
        sys.stdout.flush()
        captured.append(line)
        captured_size += len(line)
        while captured_size > capture_limit and captured:
            captured_size -= len(captured.pop(0))
    return int(process.wait()), "".join(captured)


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
    if config.get("toolchain") == "cmake":
        cmake_elf = find_elf(get_cmake_build_dir(config, project_dir))
        if cmake_elf is not None:
            return cmake_elf
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
    return (
        0x08000000 <= address < 0x09000000
        or 0x10000000 <= address < 0x11000000
    )


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


def section_memory_region(section: dict[str, Any]) -> str:
    if str(section["name"]).startswith(".ccmram"):
        return "SRAM"
    return memory_region(int(section["vma"]))


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
    if name.startswith(".ccmram"):
        return "CPU-only SRAM"
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
        regions.setdefault(section_memory_region(section), []).append(section)

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
        if section_memory_region(section) == "SRAM"
        and "heap" not in str(section["name"])
        and "stack" not in str(section["name"])
    )
    sram_reserved = sum(
        int(section["size"])
        for section in sections
        if section_memory_region(section) == "SRAM"
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
    try:
        apply_example_variant(config, getattr(args, "variant", None))
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


def path_within(child: Path, parent: Path) -> bool:
    child_resolved = child.resolve()
    parent_resolved = parent.resolve()
    return child_resolved == parent_resolved or parent_resolved in child_resolved.parents


def managed_build_dir_allowed(build_dir: Path, project_dir: Path) -> bool:
    return path_within(build_dir, project_dir) or path_within(
        build_dir, jaszczurhal_root() / ".build"
    )


def cmake_cache_home_directory(cache_path: Path) -> Path | None:
    try:
        text = cache_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    for line in text.splitlines():
        if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
            value = line.split("=", 1)[1].strip()
            if value:
                return Path(value).expanduser()
            return None
    return None


def configured_cmake_cache_keys(config: dict[str, Any]) -> set[str]:
    cmake = config.get("cmake")
    if not isinstance(cmake, dict) or not isinstance(cmake.get("cache"), dict):
        return set()
    return {
        str(key)
        for key, value in cmake["cache"].items()
        if value is not None
    }


def recorded_cmake_cache_keys(build_dir: Path) -> set[str]:
    path = build_dir / CMAKE_CACHE_KEYS_FILE
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return set(CMAKE_TRANSIENT_CACHE_KEYS)
    if not isinstance(value, list):
        return set(CMAKE_TRANSIENT_CACHE_KEYS)
    return {str(key) for key in value}


def removed_cmake_cache_args(
    config: dict[str, Any], build_dir: Path
) -> list[str]:
    current = configured_cmake_cache_keys(config)
    removed = recorded_cmake_cache_keys(build_dir) - current
    return [f"-U{key}" for key in sorted(removed)]


def record_cmake_cache_keys(config: dict[str, Any], build_dir: Path) -> None:
    path = build_dir / CMAKE_CACHE_KEYS_FILE
    temporary = path.with_name(f"{path.name}.tmp")
    keys = sorted(configured_cmake_cache_keys(config))
    temporary.write_text(json.dumps(keys, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def reset_stale_cmake_cache_if_needed(source_dir: Path, build_dir: Path, project_dir: Path) -> int:
    cache_path = build_dir / "CMakeCache.txt"
    if not cache_path.is_file():
        return 0

    cached_source = cmake_cache_home_directory(cache_path)
    if cached_source is None:
        return 0
    if cached_source.resolve() == source_dir.resolve():
        return 0

    if not managed_build_dir_allowed(build_dir, project_dir):
        print(
            f"error: refusing to reset stale CMake cache outside managed "
            f"artifact roots: {build_dir}",
            file=sys.stderr,
        )
        print(
            f"       cached source: {cached_source.resolve()}",
            file=sys.stderr,
        )
        print(
            f"       requested source: {source_dir.resolve()}",
            file=sys.stderr,
        )
        return EXIT_UNSAFE_DEVICE

    print(
        yellow_text(
            "warning: stale CMake cache source changed; resetting "
            f"{build_dir} ({cached_source.resolve()} -> {source_dir.resolve()})"
        ),
        file=sys.stderr,
    )
    try:
        cache_path.unlink()
        shutil.rmtree(build_dir / "CMakeFiles", ignore_errors=True)
    except OSError as exc:
        print(f"error: failed to reset stale CMake cache in {build_dir}: {exc}", file=sys.stderr)
        return EXIT_GENERIC
    return 0


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
    args: list[str] = []
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

    upload = config.get("upload")
    openocd = upload.get("openocd") if isinstance(upload, dict) else None
    if isinstance(openocd, dict):
        openocd_cache_keys = {
            "bin": "OPENOCD_BIN",
            "interface": "OPENOCD_INTERFACE",
            "target": "OPENOCD_TARGET",
        }
        for semantic, cache_key in openocd_cache_keys.items():
            value = openocd.get(semantic)
            if value:
                args.append(f"-D{cache_key}={value}")

    return args


def configure_cmake_project(config: dict[str, Any], project_dir: Path) -> int:
    source_dir = get_cmake_source_dir(config, project_dir)
    build_dir = get_cmake_build_dir(config, project_dir)
    reset_status = reset_stale_cmake_cache_if_needed(source_dir, build_dir, project_dir)
    if reset_status != 0:
        return reset_status
    cmd = ["cmake", "-S", str(source_dir), "-B", str(build_dir)]
    cmd.extend(removed_cmake_cache_args(config, build_dir))
    cmd.extend(cmake_cache_args(config, project_dir))
    status = run_command(cmd, verbose=as_bool(config.get("verbose")))
    if status != 0:
        return status
    try:
        record_cmake_cache_keys(config, build_dir)
    except OSError as exc:
        print(
            f"error: failed to record managed CMake cache keys in "
            f"{build_dir}: {exc}",
            file=sys.stderr,
        )
        return EXIT_GENERIC
    return 0


def run_cmake_target(config: dict[str, Any], project_dir: Path, target: str) -> int:
    configure_status = configure_cmake_project(config, project_dir)
    if configure_status != 0:
        return configure_status
    cmd = ["cmake", "--build", str(get_cmake_build_dir(config, project_dir)), "--target", target]
    rc, output = run_command_capture(cmd, verbose=as_bool(config.get("verbose")))
    if rc != 0:
        print_build_diagnostics(diagnose_build_output(output, config))
    return rc


def command_build(args: argparse.Namespace, *, debug: bool = False, show_memory_overview: bool = True) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if config.get("toolchain") == "cmake":
        diagnostics = build_preflight_diagnostics(config, project_dir)
        if diagnostics:
            print_build_diagnostics(diagnostics)
            return EXIT_BUILD
        target = cmake_targets(config)["buildDebug" if debug else "build"]
        rc = run_cmake_target(config, project_dir, target)
        if rc == 0:
            if show_memory_overview:
                print_memory_map_overview(config, project_dir)
            return 0
        return EXIT_BUILD
    print(
        f"error: build for toolchain '{config.get('toolchain')}' is not implemented",
        file=sys.stderr,
    )
    return EXIT_UNSUPPORTED


def native_rp_upload_uses_serial_bootsel(
    config: dict[str, Any], upload_strategy: str | None
) -> bool:
    return (
        config.get("target") in NATIVE_RP_TARGETS
        and upload_strategy in {None, "serial", "uf2"}
    )


def command_upload(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if config.get("toolchain") != "cmake":
        print(f"error: upload for toolchain '{config.get('toolchain')}' is not implemented yet", file=sys.stderr)
        return EXIT_UNSUPPORTED
    diagnostics = build_preflight_diagnostics(config, project_dir)
    if diagnostics:
        print_build_diagnostics(diagnostics)
        return EXIT_UPLOAD
    port = args.port or config.get("uploadPort") or (config.get("upload") or {}).get("port")
    upload_config = config.get("upload") or {}
    upload_strategy = upload_config.get("strategy") if isinstance(upload_config, dict) else None
    native_rp_serial_upload = native_rp_upload_uses_serial_bootsel(
        config, upload_strategy
    )
    if upload_strategy == "openocd":
        # SWD/JTAG flashers (e.g. STM32 via OpenOCD): no serial port, BOOTSEL,
        # by-id identity guard, port release, or monitor handoff applies. Delegate
        # straight to the CMake firmware_upload target after config resolution.
        target = cmake_targets(config)["upload"]
        rc = run_cmake_target(config, project_dir, target)
        if rc == 0:
            print_memory_map_overview(config, project_dir)
            return 0
        return EXIT_UPLOAD
    if native_rp_serial_upload and not args.port and port:
        if not upload_port_path_exists(str(port)):
            _, bootsel_candidates = find_single_bootsel_mount()
            if bootsel_candidates:
                print(
                    "Configured serial upload port is unavailable; "
                    "BOOTSEL device detected, using UF2 upload."
                )
                return command_upload_uf2(args)
            if identity_enabled(config):
                detected_port, detect_status = select_verified_identity_port(config)
                if detect_status != 0:
                    return detect_status
                if detected_port:
                    print(
                        "Configured serial upload port is unavailable; "
                        "using the single verified CDC device."
                    )
                    port = detected_port
    if not port:
        _, bootsel_candidates = find_single_bootsel_mount()
        if bootsel_candidates:
            print("No serial upload port configured; BOOTSEL device detected, using UF2 upload.")
            return command_upload_uf2(args)
        if identity_enabled(config):
            port, detect_status = select_verified_identity_port(config)
            if detect_status != 0:
                return detect_status
        if native_rp_serial_upload and not port:
            return command_upload_uf2(args)

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
    try:
        if native_rp_serial_upload:
            build_status = command_build(
                args, debug=False, show_memory_overview=False
            )
            if build_status != 0:
                return build_status
            existing_bootsel_ids = bootsel_candidate_ids()
            touch_status = touch_rp_bootloader_port(str(config["uploadPort"]))
            if touch_status != 0:
                return touch_status
            return command_upload_uf2(
                args,
                build_first=False,
                bootsel_wait_s=8.0,
                excluded_bootsel_ids=existing_bootsel_ids,
            )
        target = cmake_targets(config)["upload"]
        rc = run_cmake_target(config, project_dir, target)
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


def bootsel_candidate_ids() -> set[str]:
    blocks = find_bootsel_blocks()
    ids = {
        f"block:{block.get('path')}"
        for block in blocks
        if block.get("path")
    }
    block_mounts = {
        mount
        for mount in (bootsel_mountpoint(block) for block in blocks)
        if mount is not None
    }
    ids.update(
        f"mount:{mount}"
        for mount in find_bootsel_mounts()
        if mount not in block_mounts
    )
    return ids


def find_single_bootsel_mount(
    excluded_ids: set[str] | None = None,
) -> tuple[Path | None, list[str]]:
    excluded = excluded_ids or set()
    all_blocks = find_bootsel_blocks()
    blocks = [
        block
        for block in all_blocks
        if f"block:{block.get('path')}" not in excluded
    ]
    excluded_mounts = {
        mount
        for block in all_blocks
        if f"block:{block.get('path')}" in excluded
        for mount in [bootsel_mountpoint(block)]
        if mount is not None
    }
    mounts = [
        mount
        for mount in find_bootsel_mounts()
        if f"mount:{mount}" not in excluded and mount not in excluded_mounts
    ]
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


def wait_for_single_bootsel_mount(
    timeout_s: float,
    excluded_ids: set[str] | None = None,
) -> tuple[Path | None, list[str]]:
    deadline = time.monotonic() + timeout_s
    last_candidates: list[str] = []
    while True:
        mount, candidates = find_single_bootsel_mount(excluded_ids)
        last_candidates = candidates
        if mount is not None or len(candidates) > 1:
            return mount, candidates
        if time.monotonic() >= deadline:
            return None, last_candidates
        time.sleep(0.1)


def touch_rp_bootloader_port(port: str) -> int:
    try:
        import serial
    except ImportError:
        print(
            "error: native RP auto-upload requires pyserial for the 1200-bps bootloader touch",
            file=sys.stderr,
        )
        return EXIT_UPLOAD

    print(f"Requesting BOOTSEL via 1200-bps touch on {port}", flush=True)
    try:
        with serial.Serial(
            port=port,
            baudrate=1200,
            timeout=0.25,
            write_timeout=0.25,
        ) as touch:
            touch.dtr = True
            time.sleep(0.05)
            touch.dtr = False
            time.sleep(0.05)
    except (OSError, serial.SerialException) as exc:
        if getattr(exc, "errno", None) in {errno.EIO, errno.ENODEV}:
            print(
                "Serial port disconnected during the bootloader touch; "
                "waiting for BOOTSEL."
            )
            return 0
        print(f"error: 1200-bps bootloader touch failed on {port}: {exc}", file=sys.stderr)
        return EXIT_UPLOAD
    return 0


def command_upload_uf2(
    args: argparse.Namespace,
    *,
    build_first: bool = True,
    bootsel_wait_s: float = 0.0,
    excluded_bootsel_ids: set[str] | None = None,
) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status

    target = config.get("target")
    if target and str(target) not in UF2_TARGETS:
        print(
            f"error: upload-uf2 is RP-family/UF2 only; active target is {target_display_name(config)}",
            file=sys.stderr,
        )
        print(
            "       Select an RP target or use the target-neutral 'upload' action.",
            file=sys.stderr,
        )
        return EXIT_UNSUPPORTED

    if build_first:
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

    return upload_uf2_artifact(
        uf2,
        config,
        project_dir,
        bootsel_wait_s=bootsel_wait_s,
        excluded_bootsel_ids=excluded_bootsel_ids,
    )


def upload_uf2_artifact(
    uf2: Path,
    config: dict[str, Any],
    project_dir: Path,
    *,
    bootsel_wait_s: float = 0.0,
    excluded_bootsel_ids: set[str] | None = None,
) -> int:
    if bootsel_wait_s > 0.0:
        mount, bootsel_candidates = wait_for_single_bootsel_mount(
            bootsel_wait_s,
            excluded_bootsel_ids,
        )
    else:
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


def parse_ota_discovery_response(payload: bytes, address: tuple[str, int]) -> dict[str, Any] | None:
    try:
        fields = payload.decode("utf-8").strip().split()
    except UnicodeDecodeError:
        return None
    if len(fields) != 8 or fields[:2] != ["JHOTA", "1"]:
        return None
    try:
        port = int(fields[4])
        slot_size = int(fields[5])
        generation = int(fields[6])
        mode = int(fields[7])
    except ValueError:
        return None
    if not (1 <= port <= 65535) or slot_size <= 0:
        return None
    return {
        "address": address[0],
        "hostname": fields[2],
        "target": fields[3],
        "port": port,
        "slotSize": slot_size,
        "generation": generation,
        "bootMode": mode,
    }


def discover_ota_devices(port: int, broadcast: str, timeout_s: float = 1.0) -> list[dict[str, Any]]:
    devices: dict[tuple[str, int], dict[str, Any]] = {}
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp.bind(("", 0))
        udp.settimeout(0.1)
        udp.sendto(b"JHOTA DISCOVER 1", (broadcast, port))
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                payload, address = udp.recvfrom(512)
            except socket.timeout:
                continue
            device = parse_ota_discovery_response(payload, address)
            if device is not None:
                devices[(device["address"], device["port"])] = device
    return sorted(
        devices.values(),
        key=lambda device: (device["hostname"], device["address"], device["port"]),
    )


def resolved_ota_config(config: dict[str, Any]) -> dict[str, Any]:
    ota = config.get("ota")
    return dict(ota) if isinstance(ota, dict) else {}


def ota_listen_port(ota: dict[str, Any]) -> int:
    configured = ota.get("listenPort")
    return DEFAULT_OTA_LISTEN_PORT if configured is None else int(configured)


def ota_password(ota: dict[str, Any]) -> str:
    env_name = ota.get("passwordEnv")
    if env_name:
        value = os.environ.get(str(env_name))
        if value is None:
            raise ValueError(f"OTA password environment variable is not set: {env_name}")
        return value
    return str(ota.get("password") or "")


def print_ota_devices(devices: list[dict[str, Any]], *, as_json: bool) -> None:
    if as_json:
        print(json.dumps(devices, indent=2))
        return
    if not devices:
        print("No JaszczurHAL OTA devices found.")
        return
    print("JaszczurHAL OTA devices:")
    for index, device in enumerate(devices, start=1):
        print(
            f"  {index}. {device['hostname']}  {device['address']}:{device['port']}  "
            f"{device['target']}  generation={device['generation']}  "
            f"slot={device['slotSize']} bytes  bootMode={device['bootMode']}"
        )


def command_ota_discover(args: argparse.Namespace) -> int:
    _, config, status = load_config_for_action(args)
    if status != 0:
        return status
    ota = resolved_ota_config(config)
    port = int(ota.get("port") or DEFAULT_OTA_PORT)
    destination = str(
        args.host
        or ota.get("host")
        or ota.get("broadcast")
        or "255.255.255.255"
    )
    try:
        devices = discover_ota_devices(port, destination)
    except OSError as exc:
        print(f"error: OTA discovery failed: {exc}", file=sys.stderr)
        return EXIT_UPLOAD
    print_ota_devices(devices, as_json=args.json)
    return 0


def find_configured_ota(config: dict[str, Any]) -> Path | None:
    artifacts = config.get("artifacts")
    if not isinstance(artifacts, dict) or not artifacts.get("ota"):
        return None
    path = Path(str(artifacts["ota"])).expanduser()
    return path if path.is_file() else None


def find_ota(build_dir: Path) -> Path | None:
    matches = sorted(
        path for path in build_dir.rglob("*.ota") if not path.name.endswith(".signed.ota")
    )
    if len(matches) == 1:
        return matches[0]
    exact = [path for path in matches if path.parent == build_dir]
    return exact[0] if len(exact) == 1 else None


def sign_ota_container(source: Path, password: str, output: Path) -> bytes:
    container = bytearray(source.read_bytes())
    if len(container) <= 160 or container[:8] != b"JHOTA1\r\n":
        raise ValueError(f"{source}: invalid JaszczurHAL OTA container")
    version, header_size = struct.unpack_from("<HH", container, 8)
    payload_size = struct.unpack_from("<I", container, 20)[0]
    if version != 1 or header_size != 160 or len(container) != header_size + payload_size:
        raise ValueError(f"{source}: unsupported OTA container shape")
    if hashlib.sha256(container[header_size:]).digest() != bytes(container[32:64]):
        raise ValueError(f"{source}: payload SHA-256 mismatch")
    key = hashlib.md5(password.encode("utf-8")).hexdigest().encode("ascii") if password else b""
    container[96:128] = hmac.new(key, container[:96], hashlib.sha256).digest()
    struct.pack_into("<I", container, header_size - 4, binascii.crc32(container[: header_size - 4]) & 0xFFFFFFFF)
    output.write_bytes(container)
    return bytes(container)


def choose_ota_device(
    devices: list[dict[str, Any]],
    config: dict[str, Any],
    ota: dict[str, Any],
    args: argparse.Namespace,
) -> dict[str, Any] | None:
    target = str(config.get("target") or "rp2040")
    hostname = str(ota.get("hostname") or "")
    configured_host = str(args.host or ota.get("host") or "")
    candidates = [
        device
        for device in devices
        if device["target"] == target
        and (not hostname or device["hostname"] == hostname)
    ]
    if configured_host:
        return {
            "address": configured_host,
            "hostname": hostname or configured_host,
            "target": target,
            "port": int(ota.get("port") or DEFAULT_OTA_PORT),
        }
    if len(candidates) == 1:
        return candidates[0]
    print_ota_devices(candidates, as_json=False)
    if len(candidates) > 1 and args.interactive and sys.stdin.isatty():
        try:
            selected = int(input("Select OTA device: ").strip())
        except (EOFError, ValueError):
            return None
        if 1 <= selected <= len(candidates):
            return candidates[selected - 1]
    return None


def upload_ota_container(
    device: dict[str, Any],
    container: bytes,
    password: str,
    listen_port: int = DEFAULT_OTA_LISTEN_PORT,
) -> None:
    if not 0 <= listen_port <= 65535:
        raise ValueError("OTA TCP listen port must be in range 0..65535")
    udp_address = (str(device["address"]), int(device["port"]))
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("", listen_port))
        server.listen(1)
        server.settimeout(8.0)
        tcp_port = server.getsockname()[1]
        invitation = (
            f"0 {tcp_port} {len(container)} {hashlib.md5(container).hexdigest()}\n"
        ).encode("ascii")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("", 0))
            udp.settimeout(5.0)
            udp.sendto(invitation, udp_address)
            try:
                response, _ = udp.recvfrom(256)
            except socket.timeout as exc:
                raise TimeoutError(
                    "timed out waiting for the OTA invitation response"
                ) from exc
            text = response.decode("ascii", errors="strict").strip()
            if text.startswith("AUTH "):
                nonce = text.split(maxsplit=1)[1]
                password_md5 = hashlib.md5(password.encode("utf-8")).hexdigest()
                client_nonce = os.urandom(16).hex()
                challenge = f"{password_md5}:{nonce}:{client_nonce}".encode("ascii")
                digest = hashlib.md5(challenge).hexdigest()
                udp.sendto(f"200 {client_nonce} {digest}\n".encode("ascii"), udp_address)
                try:
                    response, _ = udp.recvfrom(256)
                except socket.timeout as exc:
                    raise TimeoutError(
                        "timed out waiting for the OTA authentication response"
                    ) from exc
                text = response.decode("ascii", errors="strict").strip()
            if text != "OK":
                raise RuntimeError(f"device rejected OTA invitation: {text}")

        try:
            connection, peer = server.accept()
        except socket.timeout as exc:
            raise TimeoutError(
                "timed out waiting for the device's OTA TCP connection"
            ) from exc
        with connection:
            connection.settimeout(8.0)
            reader = connection.makefile("rb")
            sent = 0
            while sent < len(container):
                chunk = container[sent : sent + 1024]
                connection.sendall(chunk)
                acknowledged = 0
                while acknowledged < len(chunk):
                    line = reader.readline(32)
                    if not line:
                        raise RuntimeError("device closed OTA connection during transfer")
                    acknowledged += int(line.strip())
                if acknowledged != len(chunk):
                    raise RuntimeError("invalid OTA byte acknowledgement")
                sent += len(chunk)
                print(f"\rOTA {sent}/{len(container)} bytes", end="", flush=True)
            final = reader.read(2)
            if final != b"OK":
                raise RuntimeError(f"device did not confirm OTA completion: {final!r}")
        print(f"\nOTA image accepted by {peer[0]}; device is rebooting.")


def command_upload_ota(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if str(config.get("target") or "") not in NATIVE_RP_TARGETS:
        print("error: upload-ota requires a native RP target", file=sys.stderr)
        return EXIT_UNSUPPORTED
    build_status = command_build(args, debug=False, show_memory_overview=False)
    if build_status != 0:
        return build_status
    ota = resolved_ota_config(config)
    try:
        password = ota_password(ota)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_CONFIG
    if not password and not as_bool(ota.get("allowEmptyPassword")):
        print(
            "error: OTA password is empty; configure ota.passwordEnv "
            "or explicitly set ota.allowEmptyPassword=true",
            file=sys.stderr,
        )
        return EXIT_CONFIG
    port = int(ota.get("port") or DEFAULT_OTA_PORT)
    broadcast = str(ota.get("broadcast") or "255.255.255.255")
    configured_host = str(args.host or ota.get("host") or "")
    try:
        devices = (
            [] if configured_host else discover_ota_devices(port, broadcast)
        )
        device = choose_ota_device(devices, config, ota, args)
    except OSError as exc:
        print(f"error: OTA discovery failed: {exc}", file=sys.stderr)
        return EXIT_UPLOAD
    if device is None:
        print(
            "error: no unique matching OTA device; use --host or --interactive",
            file=sys.stderr,
        )
        return EXIT_UNSAFE_DEVICE
    build_dir = get_build_dir(config, project_dir)
    source = find_configured_ota(config) or find_ota(build_dir)
    if source is None:
        print(f"error: no unique OTA artifact found in {build_dir}", file=sys.stderr)
        return EXIT_UPLOAD
    signed = build_dir / f"{source.stem}.signed.ota"
    try:
        container = sign_ota_container(source, password, signed)
        listen_port = ota_listen_port(ota)
        upload_ota_container(device, container, password, listen_port)
    except (OSError, RuntimeError, ValueError, socket.timeout) as exc:
        print(f"error: OTA upload failed: {exc}", file=sys.stderr)
        return EXIT_UPLOAD
    print_memory_map_overview(config, project_dir)
    return 0


def neutral_firmware_source_dir() -> Path:
    return Path(__file__).resolve().parents[2] / "neutral_fw" / "rp_native"


def neutral_firmware_config(
    config: dict[str, Any], project_dir: Path
) -> dict[str, Any]:
    neutral = copy.deepcopy(config)
    source_dir = neutral_firmware_source_dir()
    build_dir = get_build_dir(config, project_dir) / "neutral_identity"

    neutral["module"] = "neutral_identity"
    neutral["root"] = str(jaszczurhal_root())
    neutral["buildDir"] = str(build_dir)
    neutral["cmakeBuildDir"] = str(build_dir / "cmake")
    neutral["identity"] = {"enabled": False}
    neutral["artifacts"] = {
        "elf": str(build_dir / "firmware.elf"),
        "uf2": str(build_dir / "firmware.uf2"),
    }

    cmake = dict(neutral.get("cmake") or {})
    cmake["sourceDir"] = str(
        jaszczurhal_root() / "cmake" / "jh_firmware_project"
    )
    cmake.pop("targets", None)
    cache = dict(cmake.get("cache") or {})
    for key in CMAKE_TRANSIENT_CACHE_KEYS:
        cache.pop(key, None)
    cache.update(
        {
            "JH_ARTIFACT_DIR": str(build_dir),
            "JH_MODULE_NAME": "neutral_identity",
            "JH_PROJECT_DIR": str(source_dir),
        }
    )
    cmake["cache"] = cache
    neutral["cmake"] = cmake
    return neutral


def command_clear_identity(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    upload = config.get("upload") or {}
    if (
        config.get("toolchain") != "cmake"
        or config.get("target") not in NATIVE_RP_TARGETS
        or upload.get("strategy") != "uf2"
    ):
        print(
            "error: clear-identity requires a native RP target with UF2 upload",
            file=sys.stderr,
        )
        return EXIT_UNSUPPORTED

    source_dir = neutral_firmware_source_dir()
    if not (source_dir / "app.c").is_file():
        print(
            f"error: neutral firmware source not found: {source_dir}",
            file=sys.stderr,
        )
        return EXIT_CONFIG

    port = args.port or config.get("uploadPort") or upload.get("port")
    bootsel_candidates: list[str] = []
    if not port:
        _, bootsel_candidates = find_single_bootsel_mount()
        if not bootsel_candidates and identity_enabled(config):
            port, detect_status = select_verified_identity_port(config)
            if detect_status != 0:
                return detect_status

    if port or not bootsel_candidates:
        verify_status = verify_upload_port(
            config,
            str(port or ""),
            allow_unverified=args.allow_unverified_port,
        )
        if verify_status != 0:
            return verify_status

    upload_port = ""
    port_released = False
    if port:
        upload_port = resolve_upload_port_for_tool(str(port))
        release_status = release_port_for_upload(upload_port, project_dir)
        if release_status != 0:
            return release_status
        port_released = True

    neutral = neutral_firmware_config(config, project_dir)
    uf2 = Path(neutral["artifacts"]["uf2"])
    try:
        with build_lock(neutral, project_dir):
            rc = run_cmake_target(
                neutral,
                project_dir,
                cmake_targets(neutral)["build"],
            )
        if rc != 0:
            return EXIT_BUILD
        if not uf2.is_file():
            print(f"error: neutral UF2 artifact not found: {uf2}", file=sys.stderr)
            return EXIT_BUILD

        if upload_port:
            existing_bootsel_ids = bootsel_candidate_ids()
            touch_status = touch_rp_bootloader_port(upload_port)
            if touch_status != 0:
                return touch_status
            return upload_uf2_artifact(
                uf2,
                neutral,
                project_dir,
                bootsel_wait_s=8.0,
                excluded_bootsel_ids=existing_bootsel_ids,
            )
        return upload_uf2_artifact(uf2, neutral, project_dir)
    finally:
        if port_released:
            end_upload_release(project_dir)


def command_monitor(args: argparse.Namespace, mode: str) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status

    port = (
        args.port
        or config.get("uploadPort")
        or (config.get("upload") or {}).get("port")
    )
    identity_tokens: list[str] = []
    if mode == "pico" and not args.port and identity_enabled(config):
        identity_tokens = list(dict.fromkeys(expected_identity_tokens(config)))
        if not port or not upload_port_path_exists(str(port)):
            detected_port, detect_status = select_verified_identity_port(config)
            if detect_status != 0:
                return detect_status
            if detected_port:
                if port:
                    print(
                        "Configured serial monitor port is unavailable; "
                        "using the single verified CDC device."
                    )
                port = detected_port

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
    if identity_tokens:
        cmd.append("--follow-identity")
        for token in identity_tokens:
            cmd.extend(["--identity-token", token])
    if port:
        cmd.append(str(port))
    rc = run_command(cmd, verbose=as_bool(config.get("verbose")))
    return 0 if rc == 0 else EXIT_MONITOR


def get_build_dir(config: dict[str, Any], project_dir: Path) -> Path:
    build_dir = Path(str(config.get("buildDir") or project_dir / ".build")).expanduser()
    if not build_dir.is_absolute():
        build_dir = project_dir / build_dir
    return build_dir


def find_compile_database(*base_dirs: Path) -> Path | None:
    seen: set[Path] = set()
    for base_dir in base_dirs:
        for candidate in (base_dir / "compile_commands.json",):
            key = candidate.resolve(strict=False)
            if key in seen:
                continue
            seen.add(key)
            if candidate.is_file():
                return candidate
    return None


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
        cmake_build_dir = get_cmake_build_dir(config, project_dir)
        raw_compile_db = find_compile_database(cmake_build_dir, build_dir)
        patched_compile_db = build_dir / "compile_commands_patched.json"
        if raw_compile_db is None:
            print(
                "error: compile database was not generated; checked: "
                f"{cmake_build_dir / 'compile_commands.json'}, "
                f"{build_dir / 'compile_commands.json'}",
                file=sys.stderr,
            )
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
    print(
        f"error: refresh-intellisense for toolchain "
        f"'{config.get('toolchain')}' is not implemented",
        file=sys.stderr,
    )
    return EXIT_UNSUPPORTED


def command_clean(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    build_dir = get_build_dir(config, project_dir)
    if not build_dir.exists():
        print(f"Nothing to clean: {build_dir}")
        return 0
    if not managed_build_dir_allowed(build_dir, project_dir):
        print(
            f"error: refusing to clean outside managed artifact roots: {build_dir}",
            file=sys.stderr,
        )
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
    parser.add_argument(
        "--target",
        help="Override active target family (e.g. rp2040, rp2350-arm, stm32g474).",
    )
    parser.add_argument("--board", help="Override active board/variant within the target.")
    parser.add_argument("--variant", help="Example variant id from the manifest's example.variants list.")
    parser.add_argument("--selection", help="Board selection in '<target>:<board>' form (for VS Code pickers).")
    parser.add_argument("--interactive", action="store_true", help="Prompt for a target/board selection in the terminal.")
    parser.add_argument("--port", help="Override serial upload/monitor port.")
    parser.add_argument("--host", help="Override OTA device IPv4 address or hostname.")
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
    if args.action == "upload-ota":
        return command_upload_ota(args)
    if args.action == "monitor":
        return command_monitor(args, "pico")
    if args.action == "monitor-probe":
        return command_monitor(args, "probe")
    if args.action == "monitor-any":
        return command_monitor(args, "any")
    if args.action == "list-ports":
        return command_list_ports(args)
    if args.action == "ota-discover":
        return command_ota_discover(args)
    if args.action == "change-port":
        return command_change_port(args)
    if args.action == "refresh-intellisense":
        return command_refresh_intellisense(args)
    if args.action == "clean":
        return command_clean(args)
    if args.action == "clear-identity":
        return command_clear_identity(args)
    if args.action == "select-board":
        return command_select_board(args)
    if args.action == "sync-board-picker":
        return command_sync_board_picker(args)
    return command_stub(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        raise SystemExit(EXIT_GENERIC)
