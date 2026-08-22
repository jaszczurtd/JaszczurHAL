#!/usr/bin/env python3
"""JaszczurHAL VS Code firmware workflow entrypoint."""

from __future__ import annotations

import argparse
import binascii
import copy
from contextlib import contextmanager
import errno
import hashlib
import hmac
import importlib
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import sys
from pathlib import Path
from typing import Any, Iterable

from vscode.runtime.exit_codes import (
    EXIT_BUILD,
    EXIT_CONFIG,
    EXIT_GENERIC,
    EXIT_MONITOR,
    EXIT_UNSAFE_DEVICE,
    EXIT_UNSUPPORTED,
    EXIT_UPLOAD,
    EXIT_USAGE,
)
from vscode.runtime.platform_api import (
    PlatformOperationUnsupported,
    get_platform_adapter,
)
from vscode.runtime.serial_identity import (
    IDENTITY_MISSING_METADATA,
    SerialIdentityExpectation,
    match_serial_identity,
    normalize_identity_text as normalize_serial_identity_text,
    verified_serial_records,
)
from vscode.runtime.monitor.ownership import (
    RELEASE_UPLOAD,
    load_monitor_ownership,
    request_monitor_release,
)


VERSION = "0.1.0"

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
    "debug-tools",
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
    "debug-tools",
}

BOOTSEL_LABELS = {"RPI-RP2", "RP2350", "RPI-RP2350"}
UF2_BLOCK_SIZE = 512
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_MAX_PAYLOAD_SIZE = 476
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
UF2_FLAG_EXTENSION_FLAGS_PRESENT = 0x00008000
UF2_ABSOLUTE_FAMILY_ID = 0xE48BFF57
UF2_EXTENSION_RP2_IGNORE_BLOCK = 0x9957E304
NATIVE_RP_TARGETS = {
    "rp2040",
    "rp2350-arm",
    "rp2350-riscv",
}
UF2_TARGETS = {
    *NATIVE_RP_TARGETS,
}
STABLE_FIRMWARE_ARTIFACTS = (
    "firmware.elf",
    "firmware.bin",
    "firmware.hex",
    "firmware.map",
    "firmware.uf2",
    "firmware.ota",
)
CMAKE_CACHE_KEYS_FILE = ".jh-vscode-cache-keys.json"
WINDOWS_HOST_ENVIRONMENT_FILE = "host-environment.json"
DEFAULT_OTA_PORT = 8266
DEFAULT_OTA_LISTEN_PORT = 8266
MONITOR_RELEASE_TIMEOUT_S = 3.0
MONITOR_TERMINATE_TIMEOUT_S = 1.0
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
HAL_FEATURE_RE = re.compile(
    r"^\s*#\s*define\s+(HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)\b(?P<tail>.*)$"
)
HAL_DEFINE_TOKEN_RE = re.compile(
    r"(?:^|(?<=[;\s]))(?:-D)?"
    r"(?P<symbol>HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)"
    r"(?P<assignment>=(?P<value>[^;\s]*))?"
    r"(?=[;\s]|$)"
)
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
    for section_name in ("artifacts", "upload", "ota", "hooks", "espIdf"):
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
        if isinstance(target_desc.get("espIdf"), dict):
            registry_layer["espIdf"] = dict(target_desc["espIdf"])
        board_desc = resolve_registry_board(target_desc, board)
        if isinstance(board_desc, dict) and isinstance(
            board_desc.get("identity"), dict
        ):
            registry_layer["identity"] = dict(board_desc["identity"])

    manifest_overlay: dict[str, Any] = {}
    if isinstance(profiles, dict) and isinstance(profiles.get(active), dict):
        manifest_overlay = profiles[active]

    # Precedence (low -> high): registry target/board defaults are the floor, the
    # base manifest overrides them, and the active target's targetProfiles overlay
    # overrides the base. Provider/toolchain selection and its managed runner stay
    # registry-owned. cmake.cache from all three layers deep-merges key-by-key.
    # The active CMake dispatcher target itself is pinned after the merge so legacy
    # manifests with a hard-coded JH_TARGET cannot select a different backend.
    base = dict(config)
    base.pop("targetProfiles", None)
    merged = deep_merge(deep_merge(registry_layer, base), manifest_overlay)
    if registry_layer.get("toolchain"):
        merged["toolchain"] = registry_layer["toolchain"]
        sources["toolchain"] = f"registry:{active}.build.provider"
    if isinstance(registry_layer.get("espIdf"), dict):
        merged["espIdf"] = dict(registry_layer["espIdf"])
        sources["espIdf"] = f"registry:{active}.build.provider"
    registry_identity = registry_layer.get("identity")
    if isinstance(registry_identity, dict):
        effective_identity = dict(merged.get("identity") or {})
        effective_identity.update(registry_identity)
        SerialIdentityExpectation.from_config(effective_identity)
        merged["identity"] = effective_identity
        sources["identity"] = (
            f"registry:{active}.boards.{board}.programming.usb"
        )
    config.clear()
    config.update(merged)

    # Expand tokens the overlay may have introduced (idempotent on the base).
    expand_config_sections(config, project_dir)

    cmake = config.get("cmake")
    if config.get("toolchain") == "cmake" and isinstance(cmake, dict):
        cache = cmake.get("cache")
        if not isinstance(cache, dict):
            cache = {}
            cmake["cache"] = cache
        cache["JH_TARGET"] = active

    target_switched = active != (manifest_target or "rp2040")
    registry_upload = target_desc.get("upload") if isinstance(target_desc, dict) else None
    overlay_upload = manifest_overlay.get("upload") if isinstance(manifest_overlay, dict) else None
    if (
        config.get("toolchain") == "esp-idf"
        and isinstance(registry_upload, dict)
        and registry_upload.get("strategy")
    ):
        upload = config.get("upload")
        if not isinstance(upload, dict):
            upload = {}
            config["upload"] = upload
        upload["strategy"] = registry_upload["strategy"]
        sources["upload"] = f"registry:{active}.upload.strategy"
    if (
        target_switched
        and config.get("toolchain") != "esp-idf"
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
    <root>/vscode/runtime/jh_vscode.py."""
    return Path(__file__).resolve().parents[2]


def windows_host_environment_path() -> Path:
    override = os.environ.get("JH_WINDOWS_HOST_ENVIRONMENT")
    if override:
        return Path(override).expanduser()
    return (
        jaszczurhal_root()
        / ".build"
        / "windows"
        / WINDOWS_HOST_ENVIRONMENT_FILE
    )


def load_windows_host_environment() -> dict[str, Any]:
    """Load the bootstrap-produced native Windows build environment."""
    if sys.platform != "win32" and os.environ.get("JH_TEST_WINDOWS_HOST") != "1":
        return {}
    return load_json_file(windows_host_environment_path())


def windows_project_build_key(project_dir: Path) -> str:
    normalized = os.path.normcase(str(project_dir.resolve()))
    digest = hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:12]
    stem = re.sub(r"[^A-Za-z0-9_.-]+", "-", project_dir.name).strip("-.")
    return f"{(stem or 'firmware')[:24]}-{digest}"


def resolved_windows_tools() -> dict[str, str]:
    environment = load_windows_host_environment()
    tools = environment.get("tools")
    if not isinstance(tools, dict):
        return {}
    return {
        str(name): str(path)
        for name, path in tools.items()
        if isinstance(path, str) and path
    }


def objdump_program(config: dict[str, Any]) -> str | None:
    """Resolve objdump from the same verified toolchain used by the build."""
    tools = resolved_windows_tools()
    if config.get("target") == "rp2350-riscv":
        riscv = tools.get("riscv")
        if riscv:
            candidate = Path(riscv).with_name("riscv32-unknown-elf-objdump.exe")
            if candidate.is_file():
                return str(candidate)
        return shutil.which("riscv32-unknown-elf-objdump") or shutil.which("objdump")

    gnu_arm = tools.get("gnu-arm")
    if gnu_arm:
        candidate = Path(gnu_arm).with_name("arm-none-eabi-objdump.exe")
        if candidate.is_file():
            return str(candidate)
    return shutil.which("arm-none-eabi-objdump") or shutil.which("objdump")


def openocd_scripts_root(
    executable: Path,
    required: tuple[str, ...],
) -> Path | None:
    candidates = (
        executable.parent / "scripts",
        executable.parent.parent / "share" / "openocd" / "scripts",
        executable.parent.parent / "scripts",
    )
    return next(
        (
            root
            for root in candidates
            if all((root / relative).is_file() for relative in required)
        ),
        None,
    )


def debug_tool_paths(config: dict[str, Any]) -> dict[str, str] | None:
    """Resolve one verified OpenOCD/GDB/scripts set for Cortex-Debug."""
    target = str(config.get("target", ""))
    target_configs = {
        "rp2040": "target/rp2040.cfg",
        "rp2350-arm": "target/rp2350.cfg",
        "stm32g474": "target/stm32g4x.cfg",
    }
    interface_configs = {
        "rp2040": "interface/cmsis-dap.cfg",
        "rp2350-arm": "interface/cmsis-dap.cfg",
        "stm32g474": "interface/stlink.cfg",
    }
    required_configs = {
        "rp2040": ("interface/cmsis-dap.cfg", "target/rp2040.cfg"),
        "rp2350-arm": ("interface/cmsis-dap.cfg", "target/rp2350.cfg"),
        "stm32g474": (
            "board/st_nucleo_g4.cfg",
            "interface/stlink.cfg",
            "target/stm32g4x.cfg",
        ),
    }
    if target not in target_configs:
        return None

    tools = resolved_windows_tools()
    openocd_text = tools.get("openocd") or shutil.which("openocd")
    gnu_arm = tools.get("gnu-arm")
    gdb = (
        Path(gnu_arm).with_name("arm-none-eabi-gdb.exe")
        if gnu_arm
        else None
    )
    if gdb is None or not gdb.is_file():
        located_gdb = shutil.which("arm-none-eabi-gdb") or shutil.which(
            "gdb-multiarch"
        )
        gdb = Path(located_gdb) if located_gdb else None
    if not openocd_text or gdb is None or not gdb.is_file():
        return None
    openocd = Path(openocd_text)
    if not openocd.is_file():
        return None
    scripts = openocd_scripts_root(openocd, required_configs[target])
    if scripts is None:
        return None
    return {
        "openocd": str(openocd),
        "gdb": str(gdb),
        "armToolchainPath": str(gdb.parent),
        "scripts": str(scripts),
        "interfaceConfig": interface_configs[target],
        "targetConfig": target_configs[target],
    }


def cmake_program(config: dict[str, Any]) -> str:
    cmake = config.get("cmake")
    if isinstance(cmake, dict) and cmake.get("executable"):
        return str(cmake["executable"])
    return resolved_windows_tools().get("cmake", "cmake")


def cmake_generator(config: dict[str, Any]) -> str:
    cmake = config.get("cmake")
    if isinstance(cmake, dict) and cmake.get("generator"):
        return str(cmake["generator"])
    return "Ninja"


def cmake_process_environment() -> dict[str, str] | None:
    tools = resolved_windows_tools()
    if not tools:
        return None
    host_environment = load_windows_host_environment()
    environment = os.environ.copy()
    directories = [str(Path(path).parent) for path in tools.values()]
    environment["PATH"] = os.pathsep.join(
        [*dict.fromkeys(directories), environment.get("PATH", "")]
    )
    build_root = host_environment.get("buildRoot")
    if isinstance(build_root, str) and build_root:
        environment["JH_MANAGED_BUILD_ROOT"] = str(Path(build_root).expanduser())
    return environment


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
    board = resolve_registry_board(target_desc, board_id)
    if board is not None:
        cache.update(board.get("cache") or {})
    root = str(jaszczurhal_root())
    return {
        key: (value.replace("${jhRoot}", root) if isinstance(value, str) else value)
        for key, value in cache.items()
    }


def resolve_registry_board(
    target_desc: dict[str, Any], board_id: str | None
) -> dict[str, Any] | None:
    selected = board_id or target_desc.get("defaultBoard")
    return next(
        (
            board
            for board in (target_desc.get("boards") or [])
            if isinstance(board, dict) and board.get("id") == selected
        ),
        None,
    )


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
        "espIdf",
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
    base_build_dir = str(config.get("buildDir") or "")
    if base_build_dir:
        variant_build_dir = str(Path(base_build_dir) / "variants" / variant_id)
        config["buildDir"] = variant_build_dir

        artifacts = config.get("artifacts")
        if isinstance(artifacts, dict):
            rebased_artifacts: dict[str, Any] = {}
            for name, value in artifacts.items():
                if isinstance(value, str):
                    try:
                        relative = Path(value).relative_to(Path(base_build_dir))
                    except ValueError:
                        pass
                    else:
                        value = str(Path(variant_build_dir) / relative)
                rebased_artifacts[name] = value
            config["artifacts"] = rebased_artifacts

    if config.get("cmakeBuildDir"):
        config["cmakeBuildDir"] = f"{config['cmakeBuildDir']}/variants/{variant_id}"

    cmake = dict(config.get("cmake") or {})
    cache = dict(cmake.get("cache") or {})
    if base_build_dir and cache.get("JH_ARTIFACT_DIR") == base_build_dir:
        cache["JH_ARTIFACT_DIR"] = config["buildDir"]
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
    use_local_state: bool = True,
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
        try:
            SerialIdentityExpectation.from_config(identity)
        except ValueError as exc:
            raise ValueError(f"invalid USB identity configuration: {exc}") from exc
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
    local_state = (
        load_json_file(vscode_dir / "jaszczurhal.local.json")
        if use_local_state
        else {}
    )
    local_target = local_state.get("target") if isinstance(local_state, dict) else None
    local_board = local_state.get("board") if isinstance(local_state, dict) else None
    local_port = local_state.get("uploadPort") if isinstance(local_state, dict) else None
    local_bootsel_volume = (
        local_state.get("bootselVolume") if isinstance(local_state, dict) else None
    )
    if local_port:
        config["uploadPort"] = str(local_port)
        upload = dict(config.get("upload") or {})
        upload["port"] = str(local_port)
        config["upload"] = upload
        sources["uploadPort"] = ".vscode/jaszczurhal.local.json"
    if local_bootsel_volume:
        upload = dict(config.get("upload") or {})
        upload["bootselVolume"] = str(local_bootsel_volume)
        config["upload"] = upload
        sources["upload.bootselVolume"] = ".vscode/jaszczurhal.local.json"
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
    port = getattr(args, "port", None)
    if port:
        config["uploadPort"] = port
        upload = dict(config.get("upload") or {})
        upload["port"] = port
        config["upload"] = upload
        sources["uploadPort"] = "cli"
    bootsel_volume = getattr(args, "bootsel_volume", None)
    if bootsel_volume:
        upload = dict(config.get("upload") or {})
        upload["bootselVolume"] = bootsel_volume
        config["upload"] = upload
        sources["upload.bootselVolume"] = "cli"
    if getattr(args, "verbose", False):
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
    try:
        attach_hal_feature_resolution(config, project_dir)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_CONFIG
    if args.json:
        print(json.dumps(config, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        print_human_config(config)
    return 0


def command_debug_tools(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    paths = debug_tool_paths(config)
    if paths is None:
        print(
            "error: a verified OpenOCD, Arm-capable GDB, and matching scripts "
            f"were not found for target {config.get('target')}",
            file=sys.stderr,
        )
        return EXIT_UNSUPPORTED
    if args.json:
        print(json.dumps(paths, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        print(f"OpenOCD:           {paths['openocd']}")
        print(f"GNU Arm GDB:       {paths['gdb']}")
        print(f"OpenOCD scripts:   {paths['scripts']}")
        print(f"Probe config:      {paths['interfaceConfig']}")
        print(f"Target config:     {paths['targetConfig']}")
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


def preprocessor_logical_lines(text: str) -> Iterable[tuple[int, str]]:
    """Yield comment-free preprocessing lines and their source line."""
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    spliced: list[str] = []
    source_lines: list[int] = []
    source_line = 1
    source_offset = 0
    while source_offset < len(text):
        if text.startswith("\\\n", source_offset):
            source_line += 1
            source_offset += 2
            continue
        character = text[source_offset]
        spliced.append(character)
        source_lines.append(source_line)
        if character == "\n":
            source_line += 1
        source_offset += 1
    text = "".join(spliced)

    buffer: list[str] = []
    origin_line: int | None = None
    offset = 0
    quote: str | None = None

    def append(character: str) -> None:
        nonlocal origin_line
        if origin_line is None and not character.isspace():
            origin_line = source_lines[offset]
        buffer.append(character)

    while offset < len(text):
        character = text[offset]
        if quote is not None:
            append(character)
            if character == "\\" and offset + 1 < len(text):
                offset += 1
                append(text[offset])
            elif character == quote:
                quote = None
            elif character == "\n":
                yield origin_line or source_lines[offset], "".join(buffer)
                buffer.clear()
                origin_line = None
                quote = None
            offset += 1
            continue

        if text.startswith("//", offset):
            append(" ")
            newline = text.find("\n", offset + 2)
            offset = len(text) if newline < 0 else newline
            continue
        if text.startswith("/*", offset):
            append(" ")
            block_end = text.find("*/", offset + 2)
            if block_end < 0:
                offset = len(text)
                continue
            offset = block_end + 2
            continue
        if character in {'"', "'"}:
            quote = character
            append(character)
            offset += 1
            continue
        if character == "\n":
            yield origin_line or source_lines[offset], "".join(buffer)
            buffer.clear()
            origin_line = None
            offset += 1
            continue
        append(character)
        offset += 1

    if buffer:
        final_line = source_lines[-1] if source_lines else 1
        yield origin_line or final_line, "".join(buffer)


def header_hal_feature_definitions(
    project_dir: Path,
) -> list[tuple[str, str | None, str]]:
    definitions: list[tuple[str, str | None, str]] = []
    hal_project_config = project_dir / "hal_project_config.h"
    if not hal_project_config.is_file():
        return definitions
    try:
        text = hal_project_config.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return definitions
    for line_number, line in preprocessor_logical_lines(text):
        match = HAL_FEATURE_RE.match(line)
        if not match:
            continue
        raw_value = match.group("tail").strip()
        definitions.append(
            (
                match.group(1),
                raw_value if raw_value else None,
                f"hal_project_config.h:{line_number}",
            )
        )
    return definitions


def cache_hal_feature_definitions(
    config: dict[str, Any],
) -> list[tuple[str, str | None, str]]:
    definitions: list[tuple[str, str | None, str]] = []
    cmake = config.get("cmake")
    cache = cmake.get("cache") if isinstance(cmake, dict) else None
    if not isinstance(cache, dict):
        return definitions
    for key, value in cache.items():
        if re.fullmatch(
            r"HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+", str(key)
        ) is None:
            continue
        definitions.append(
            (
                str(key),
                str(value) if value is not None else "",
                f"cmake.cache.{key}",
            )
        )
    for key in ("JH_EXTRA_DEFINES", "EXTRA_HAL_DEFINES"):
        value = cache.get(key)
        if value is None or value == "":
            continue
        raw_value = str(value)
        for raw_token in raw_value.split(";"):
            token = raw_token.strip()
            if not token:
                continue
            if "$<" in token:
                raise ValueError(
                    f"cmake.cache.{key}: [JH-CFG-VALUE] compile definition "
                    f"{token!r} uses an unsupported generator expression"
                )
            match = HAL_DEFINE_TOKEN_RE.fullmatch(token)
            if (
                "HAL_ENABLE_" in token or "HAL_DISABLE_" in token
            ) and match is None:
                raise ValueError(
                    f"cmake.cache.{key}: [JH-CFG-VALUE] compile definition "
                    f"{token!r} embeds a HAL feature in an unsupported "
                    "expression"
                )
            if match is None:
                continue
            definitions.append(
                (
                    match.group("symbol"),
                    match.group("value")
                    if match.group("assignment") is not None
                    else None,
                    f"cmake.cache.{key}",
                )
            )
    return definitions


def validate_hal_enable_values(config: dict[str, Any], project_dir: Path) -> None:
    definitions = [
        *header_hal_feature_definitions(project_dir),
        *cache_hal_feature_definitions(config),
    ]
    for symbol, value, source in definitions:
        if value in {None, "1"}:
            continue
        if value == "0":
            raise ValueError(
                f"{source}: [JH-CFG-VALUE] {symbol}=0 is unsupported; "
                "omit the symbol to disable it"
            )
        raise ValueError(
            f"{source}: [JH-CFG-VALUE] {symbol} has unsupported value "
            f"{value!r}; use the bare symbol or {symbol}=1"
        )


def collect_hal_features(
    config: dict[str, Any], project_dir: Path
) -> dict[str, str]:
    features: dict[str, str] = {}
    for symbol, value, source in header_hal_feature_definitions(project_dir):
        if value in {None, "1"}:
            features.setdefault(symbol, source.split(":", 1)[0])
    for symbol, value, source in cache_hal_feature_definitions(config):
        if value in {None, "1"}:
            features.setdefault(symbol, source)
    return features


def collect_hal_enables(
    config: dict[str, Any], project_dir: Path
) -> dict[str, str]:
    return {
        symbol: source
        for symbol, source in collect_hal_features(config, project_dir).items()
        if symbol.startswith("HAL_ENABLE_")
    }


_HAL_FEATURE_SUPPORT: tuple[Any, Any] | None = None


def hal_feature_support() -> tuple[Any, Any]:
    global _HAL_FEATURE_SUPPORT
    if _HAL_FEATURE_SUPPORT is None:
        scripts_dir = jaszczurhal_root() / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))
        module = importlib.import_module("generate_hal_features")
        model = module.load_registry(jaszczurhal_root() / "config")
        _HAL_FEATURE_SUPPORT = module, model
    return _HAL_FEATURE_SUPPORT


def resolve_hal_features(
    config: dict[str, Any], project_dir: Path
) -> dict[str, Any]:
    validate_hal_enable_values(config, project_dir)
    module, model = hal_feature_support()
    definitions: list[tuple[str, str | None, str]] = [
        *header_hal_feature_definitions(project_dir),
        *cache_hal_feature_definitions(config),
    ]
    requested_symbols = {symbol for symbol, _, _ in definitions}
    target = str(config.get("target") or "")
    descriptor = target_descriptor(config)
    required_features = (
        descriptor.get("requiredFeatures", [])
        if isinstance(descriptor, dict)
        else []
    )
    for index, raw_feature in enumerate(required_features):
        symbol = str(raw_feature).removesuffix("=1")
        disabled = symbol.replace("HAL_ENABLE_", "HAL_DISABLE_", 1)
        if disabled in requested_symbols:
            raise ValueError(
                f"[JH-CFG-TARGET-REQUIRED] {target} requires {symbol}; "
                f"{disabled} cannot be requested"
            )
        if symbol not in requested_symbols:
            definitions.append(
                (
                    symbol,
                    "1",
                    f"target:{target}:requiredFeatures[{index}]",
                )
            )
    requests = [
        module.FeatureRequest(symbol, value, source)
        for symbol, value, source in definitions
    ]
    resolution, findings = module.resolve_feature_requests(
        requests, model, str(project_dir)
    )
    if findings:
        raise ValueError("\n".join(sorted(set(findings))))
    requested = [
        symbol
        for symbol in resolution.requested
        if symbol in requested_symbols
    ]
    provenance = {
        symbol: list(sources)
        for symbol, sources in resolution.provenance.items()
    }
    for index, raw_feature in enumerate(required_features):
        symbol = str(raw_feature).removesuffix("=1")
        source = f"target:{target}:requiredFeatures[{index}]"
        provenance[symbol] = sorted({*provenance.get(symbol, []), source})
    return {
        "registryDigest": model.digest,
        "requestedFeatures": requested,
        "resolvedFeatures": list(resolution.resolved),
        "resolvedFeaturesDigest": module.resolved_features_digest(
            resolution.resolved
        ),
        "provenance": provenance,
    }


def attach_hal_feature_resolution(
    config: dict[str, Any], project_dir: Path
) -> None:
    config["featureResolution"] = resolve_hal_features(config, project_dir)


def resolved_hal_feature_names(
    config: dict[str, Any], project_dir: Path
) -> set[str]:
    resolution = config.get("featureResolution")
    if not isinstance(resolution, dict) or not isinstance(
        resolution.get("resolvedFeatures"), list
    ):
        resolution = resolve_hal_features(config, project_dir)
    return {str(symbol) for symbol in resolution["resolvedFeatures"]}


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

    enabled = resolved_hal_feature_names(config, project_dir)
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
        selected_board = str(config.get("board") or "")
        board_components: set[str] = set()
        if isinstance(desc, dict):
            for board_desc in desc.get("boards") or []:
                if isinstance(board_desc, dict) and board_desc.get("id") == selected_board:
                    board_components = {
                        str(component)
                        for component in board_desc.get("components") or []
                    }
                    break
        configured_cyw43 = all(
            symbol in project_defines or symbol in cache_defines
            for symbol in (
                "HAL_NETWORK_BACKEND_CYW43",
                "HAL_CYW43_BUS_STM32_GSPI",
                "HAL_CYW43_STACK_LWIP",
            )
        ) or {
            "cyw43-stm32-gspi",
            "cyw43-lwip",
        }.issubset(board_components)
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


def temporary_directory() -> Path:
    return get_platform_adapter().temporary_directory()


def write_text_atomic(path: Path, content: str) -> None:
    mode = path.stat().st_mode & 0o777 if path.exists() else 0o644
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
    launch_path = project_dir / ".vscode" / "launch.json"
    manifest_path = project_dir / ".vscode" / "jaszczurhal.project.json"
    if not tasks_path.is_file():
        print(f"error: VS Code tasks file does not exist: {tasks_path}", file=sys.stderr)
        return EXIT_CONFIG

    try:
        tasks_text = tasks_path.read_text(encoding="utf-8")
        tasks_document = load_json_file(tasks_path)
        launch_exists = launch_path.is_file()
        launch_text = launch_path.read_text(encoding="utf-8") if launch_exists else ""
        launch_document = load_json_file(launch_path)
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

        from vscode_task_config import (
            sync_board_picker_document,
            sync_cortex_debug_launch_document,
            vscode_launch_executable,
        )

        tasks_changed = sync_board_picker_document(
            tasks_document,
            registry,
            selected_target,
            selected_board,
        )
        launch_changed = sync_cortex_debug_launch_document(
            launch_document,
            vscode_launch_executable(
                manifest,
                workspace_dir=project_dir,
                jh_root=jaszczurhal_root(),
            ),
        )
    except (OSError, ValueError) as exc:
        print(f"error: cannot synchronize VS Code project files: {exc}", file=sys.stderr)
        return EXIT_CONFIG

    if not tasks_changed and not launch_changed:
        print(f"VS Code target picker and debug profiles are current: {project_dir}")
        return 0

    try:
        if tasks_changed:
            tasks_content = (
                json.dumps(
                    tasks_document,
                    indent=json_indent_width(tasks_text),
                    ensure_ascii=False,
                )
                + "\n"
            )
            write_text_atomic(tasks_path, tasks_content)
        if launch_changed:
            launch_content = (
                json.dumps(
                    launch_document,
                    indent=json_indent_width(launch_text) if launch_exists else 4,
                    ensure_ascii=False,
                )
                + "\n"
            )
            write_text_atomic(launch_path, launch_content)
    except OSError as exc:
        print(f"error: cannot update VS Code project files: {exc}", file=sys.stderr)
        return EXIT_CONFIG

    updated = []
    if tasks_changed:
        updated.append(str(tasks_path))
    if launch_changed:
        updated.append(str(launch_path))
    print(
        "Updated VS Code target picker and debug profiles from JaszczurHAL: "
        + ", ".join(updated)
    )
    return 0


def as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    return bool(value)



def normalize_identity_text(value: str) -> str:
    return normalize_serial_identity_text(value)


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
    for key in ("usbSerialNumber", "usbInterface", "usbLocation"):
        value = identity.get(key)
        if isinstance(value, str) and value.strip():
            tokens.append(normalize_identity_text(value))
    return [token for token in tokens if token]


def expected_serial_identity(config: dict[str, Any]) -> SerialIdentityExpectation:
    identity = config.get("identity")
    return SerialIdentityExpectation.from_config(
        identity if isinstance(identity, dict) else None
    )


def verified_identity_ports(config: dict[str, Any]) -> list[tuple[Path, Path | None]]:
    expected = expected_serial_identity(config)
    matches = verified_serial_records(
        get_platform_adapter().list_serial_ports(),
        expected,
    )
    return [
        (
            Path(record.device),
            Path(record.aliases[0]) if record.aliases else None,
        )
        for record, _match in matches
    ]


def serial_candidate_paths() -> list[Path]:
    return get_platform_adapter().serial_candidate_paths()


def serial_port_records(config: dict[str, Any] | None = None) -> list[dict[str, Any]]:
    expected = expected_serial_identity(config or {})
    records: list[dict[str, Any]] = []
    for port in get_platform_adapter().list_serial_ports():
        match = match_serial_identity(port, expected)
        records.append(
            {
                "port": port.device,
                "platform": port.platform,
                "aliases": list(port.aliases),
                "platformIdentity": port.platform_identity or None,
                "vid": port.vid,
                "pid": port.pid,
                "serialNumber": port.serial_number or None,
                "manufacturer": port.manufacturer or None,
                "product": port.product or None,
                "interface": port.interface or None,
                "location": port.location or None,
                "hwid": port.hwid or None,
                "description": port.description or None,
                "identityStatus": match.status,
                "identityScore": match.score,
                "identityReason": match.reason(),
                "verifiedForProject": match.verified,
            }
        )
    return records


def bootsel_inventory() -> tuple[list[str], list[dict[str, str | None]]]:
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
    candidates = [str(mount) for mount in unique_mounts] + [
        str(block.get("path")) for block in unmounted if block.get("path")
    ]
    records = [
        {
            "path": str(block.get("path")) if block.get("path") else None,
            "mount": str(mount) if mount else None,
            "volumeGuid": (
                str(block.get("volumeGuid")) if block.get("volumeGuid") else None
            ),
            "label": str(block.get("label")) if block.get("label") else None,
            "filesystem": str(block.get("fstype")) if block.get("fstype") else None,
        }
        for block in blocks
        for mount in [bootsel_mountpoint(block)]
    ]
    known_mounts = {
        mount
        for block in blocks
        for mount in [bootsel_mountpoint(block)]
        if mount is not None
    }
    records.extend(
        {
            "path": None,
            "mount": str(mount),
            "volumeGuid": None,
            "label": None,
            "filesystem": None,
        }
        for mount in unique_mounts
        if mount not in known_mounts
    )
    records.sort(
        key=lambda record: str(
            record["mount"] or record["volumeGuid"] or record["path"] or ""
        ).casefold()
    )
    return candidates, records


def bootsel_candidates_without_mount() -> list[str]:
    candidates, _records = bootsel_inventory()
    return candidates


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
    bootsel_supported = True
    try:
        bootsel, bootsel_records = bootsel_inventory()
    except PlatformOperationUnsupported:
        bootsel = []
        bootsel_records = []
        bootsel_supported = False

    if args.json:
        print(
            json.dumps(
                {
                    "project": str(project_dir) if project_dir else None,
                    "expectedIdentity": identity_display_text(config) if identity_enabled(config) else None,
                    "serial": records,
                    "bootsel": bootsel,
                    "bootselRecords": bootsel_records,
                    "bootselSupported": bootsel_supported,
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
            label = record["identityStatus"] if config else "detected"
            print(f"  {record['port']} [{label}]")
            for alias in record["aliases"]:
                print(f"    alias: {alias}")
            if record["platformIdentity"]:
                print(f"    platform identity: {record['platformIdentity']}")
            usb_id = (
                f"{record['vid']:04x}:{record['pid']:04x}"
                if record["vid"] is not None and record["pid"] is not None
                else "?:????"
            )
            metadata = [
                f"USB {usb_id}",
                *(
                    f"{name}={record[key]}"
                    for name, key in (
                        ("serial", "serialNumber"),
                        ("manufacturer", "manufacturer"),
                        ("product", "product"),
                        ("interface", "interface"),
                        ("location", "location"),
                    )
                    if record[key]
                ),
            ]
            print(f"    {', '.join(metadata)}")
            if config and not record["verifiedForProject"]:
                print(f"    identity: {record['identityReason']}")
    else:
        print("Serial ports: none")

    if not bootsel_supported:
        print("BOOTSEL candidates: unavailable on this host")
    elif bootsel:
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
            suffix = f" ({', '.join(record['aliases'])})" if record["aliases"] else ""
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

    selected_value = get_platform_adapter().resolve_serial_port(str(selected))
    if not get_platform_adapter().serial_port_exists(selected_value):
        print(f"error: serial port does not exist: {selected_value}", file=sys.stderr)
        return EXIT_CONFIG
    selected_value = get_platform_adapter().resolve_serial_port(selected_value)

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
            if link.is_absolute():
                return str(link), 0
            if get_platform_adapter().platform_name == "linux":
                return str(Path("/dev/serial/by-id") / link.name), 0
        else:
            print(yellow_text(f"Using verified serial port: {port} (matched USB metadata)"))
        return str(port), 0
    if len(matches) > 1:
        print("error: multiple verified serial ports match this project identity:", file=sys.stderr)
        for port, link in matches:
            suffix = f" ({link.name})" if link is not None else " (matched USB metadata)"
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
        hint_name = (
            "identity hint"
            if get_platform_adapter().platform_name == "windows"
            else "by-id hint"
        )
        label = f"{label} ({hint_name}: {by_id_hint})"
    serial_number = identity.get("usbSerialNumber")
    if serial_number:
        label = f"{label}, serial: {serial_number}"
    vid = identity.get("usbVid")
    pid = identity.get("usbPid")
    if vid is not None or pid is not None:
        label = f"{label}, VID:PID {vid if vid is not None else '?'}:{pid if pid is not None else '?'}"
    return label


def print_identity_upload_requirements(config: dict[str, Any]) -> None:
    print("error: identity-enabled upload has no verified target", file=sys.stderr)
    print(f"error: expected USB identity: {identity_display_text(config)}", file=sys.stderr)
    print("error: requirements for the default 'upload' flow:", file=sys.stderr)
    print("  1. For normal reflashing, the running firmware must expose a USB serial port", file=sys.stderr)
    if get_platform_adapter().platform_name == "windows":
        print("     whose COM metadata matches the expected USB descriptors.", file=sys.stderr)
    else:
        print("     whose by-id link or sysfs USB descriptors match the expected identity.", file=sys.stderr)
    print("  2. If no serial port is configured, exactly one BOOTSEL UF2 drive may be visible", file=sys.stderr)
    print("     so the tool can safely fall back to UF2 upload.", file=sys.stderr)
    print("  3. For the first flash of a clean board, either use BOOTSEL/UF2 or pass", file=sys.stderr)
    print("     both an explicit --port and --allow-unverified-port intentionally.", file=sys.stderr)
    print("  4. If more than one matching board is connected, pass --port explicitly.", file=sys.stderr)
    print("error: currently no verified serial port was selected and no usable BOOTSEL fallback was chosen", file=sys.stderr)


def resolve_upload_port_for_tool(port: str) -> str:
    return get_platform_adapter().resolve_serial_port(port)


def upload_port_path_exists(port: str) -> bool:
    return get_platform_adapter().serial_port_exists(port)


def _resolve_checked_upload_port(
    config: dict[str, Any],
    port: str,
    *,
    allow_unverified: bool = False,
    require_available: bool = False,
    warn_unverified: bool = True,
) -> tuple[str | None, int]:
    identity_required = identity_enabled(config)
    if identity_required and allow_unverified and warn_unverified:
        print("warning: unverified serial upload allowed by --allow-unverified-port", file=sys.stderr)
    if identity_required and not allow_unverified and not port:
        print_identity_upload_requirements(config)
        return None, EXIT_UNSAFE_DEVICE

    if not port:
        return None, 0

    adapter = get_platform_adapter()
    resolved = adapter.resolve_serial_port(port)
    if not identity_required or allow_unverified:
        if require_available and not adapter.serial_port_exists(resolved):
            print(
                f"error: selected upload port is stale or unavailable: {resolved}",
                file=sys.stderr,
            )
            return None, EXIT_UNSAFE_DEVICE
        return resolved, 0

    record = adapter.serial_port_record(resolved)
    if record is None:
        print(f"error: selected serial port is stale or unavailable: {resolved}", file=sys.stderr)
        return None, EXIT_UNSAFE_DEVICE

    match = match_serial_identity(record, expected_serial_identity(config))
    if match.verified:
        if require_available and not adapter.serial_port_exists(resolved):
            print(
                f"error: selected upload port is stale or unavailable: {resolved}",
                file=sys.stderr,
            )
            return None, EXIT_UNSAFE_DEVICE
        return resolved, 0

    print(f"error: refusing upload to unverified port: {resolved}", file=sys.stderr)
    print(f"error: expected USB identity: {identity_display_text(config)}", file=sys.stderr)
    if match.status == IDENTITY_MISSING_METADATA:
        print(f"error: port metadata is incomplete: {match.reason()}", file=sys.stderr)
    else:
        print(f"error: port identity {match.reason()}", file=sys.stderr)
    if record.aliases:
        print("error: serial aliases for this port:", file=sys.stderr)
        for alias in record.aliases:
            print(f"  {alias}", file=sys.stderr)
    print("error: for first flash of a clean board, pass --allow-unverified-port with an explicit --port", file=sys.stderr)
    return None, EXIT_UNSAFE_DEVICE


def verify_upload_port(config: dict[str, Any], port: str, *, allow_unverified: bool = False) -> int:
    _, status = _resolve_checked_upload_port(
        config,
        port,
        allow_unverified=allow_unverified,
    )
    return status


def resolve_upload_port_for_flash(
    config: dict[str, Any],
    port: str,
    *,
    allow_unverified: bool = False,
) -> tuple[str | None, int]:
    """Recheck identity and resolve the exact device immediately before flash."""

    return _resolve_checked_upload_port(
        config,
        port,
        allow_unverified=allow_unverified,
        require_available=True,
        warn_unverified=False,
    )


def process_cmdline(pid: int) -> str:
    return get_platform_adapter().process_cmdline(pid)


def port_owner_pids(port: str) -> list[int]:
    return get_platform_adapter().port_owner_pids(port)


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
    adapter = get_platform_adapter()
    ownership = load_monitor_ownership(adapter, project_dir, port)
    owners = {
        pid for pid in port_owner_pids(port) if pid != os.getpid()
    }
    if ownership is not None:
        owners.add(ownership.pid)
    foreign = sorted(
        pid
        for pid in owners
        if ownership is None or pid != ownership.pid
    )
    if foreign:
        print(f"error: serial port is busy and not owned by this project monitor: {port}", file=sys.stderr)
        for pid in foreign:
            print(f"  PID {pid}: {process_cmdline(pid) or '?'}", file=sys.stderr)
        return EXIT_UNSAFE_DEVICE
    if ownership is None:
        return 0

    begin_upload_release(project_dir)
    try:
        request_monitor_release(adapter, ownership, RELEASE_UPLOAD)
        print(yellow_text(f"requested release from own serial monitor PID {ownership.pid}"))
    except ProcessLookupError:
        print(
            "error: monitor ownership changed before the release request; "
            "refusing automatic handoff",
            file=sys.stderr,
        )
        end_upload_release(project_dir)
        return EXIT_UNSAFE_DEVICE
    except PermissionError:
        print(
            f"error: cannot signal own serial monitor PID {ownership.pid}: permission denied",
            file=sys.stderr,
        )
        end_upload_release(project_dir)
        return EXIT_UNSAFE_DEVICE

    deadline = time.monotonic() + MONITOR_RELEASE_TIMEOUT_S
    while time.monotonic() < deadline:
        current = load_monitor_ownership(adapter, project_dir, port)
        remaining = {
            pid for pid in port_owner_pids(port) if pid != os.getpid()
        }
        if current is None and not remaining:
            return 0
        time.sleep(0.1)

    current = load_monitor_ownership(adapter, project_dir, port)
    if current is not None and (
        current.pid == ownership.pid
        and current.process_start == ownership.process_start
        and adapter.process_start_identity(current.pid) == current.process_start
    ):
        try:
            adapter.terminate_process(current.pid)
            print(yellow_text(f"stopped unresponsive own serial monitor PID {current.pid}"))
        except ProcessLookupError:
            pass
        except PermissionError:
            print(
                f"error: cannot stop own serial monitor PID {current.pid}: permission denied",
                file=sys.stderr,
            )
            end_upload_release(project_dir)
            return EXIT_UNSAFE_DEVICE
        fallback_deadline = time.monotonic() + MONITOR_TERMINATE_TIMEOUT_S
        while time.monotonic() < fallback_deadline:
            current_after_stop = load_monitor_ownership(adapter, project_dir, port)
            remaining = {
                pid for pid in port_owner_pids(port) if pid != os.getpid()
            }
            if current_after_stop is None and not remaining:
                return 0
            time.sleep(0.1)

    print(f"error: serial port stayed busy after requesting release from own monitor: {port}", file=sys.stderr)
    for pid in port_owner_pids(port):
        if pid != os.getpid():
            print(f"  PID {pid}: {process_cmdline(pid) or '?'}", file=sys.stderr)
    end_upload_release(project_dir)
    return EXIT_UNSAFE_DEVICE


def run_command(
    cmd: list[str],
    *,
    verbose: bool = False,
    environment: dict[str, str] | None = None,
) -> int:
    if verbose:
        print("+ " + " ".join(cmd), flush=True)
    if Path(cmd[0]).name.lower() in {"cmake", "cmake.exe"}:
        try:
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                env=environment,
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
        completed = subprocess.run(cmd, check=False, env=environment)
    except FileNotFoundError:
        print(f"error: command not found: {cmd[0]}", file=sys.stderr)
        return EXIT_CONFIG
    except OSError as exc:
        print(f"error: failed to run {cmd[0]}: {exc}", file=sys.stderr)
        return EXIT_GENERIC
    return int(completed.returncode)


def run_command_capture(
    cmd: list[str],
    *,
    verbose: bool = False,
    capture_limit: int = 250_000,
    environment: dict[str, str] | None = None,
) -> tuple[int, str]:
    if verbose:
        print("+ " + " ".join(cmd), flush=True)
    try:
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=environment,
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


def esp_idf_configuration(config: dict[str, Any]) -> dict[str, Any]:
    value = config.get("espIdf")
    if not isinstance(value, dict):
        raise ValueError("ESP-IDF target is missing its tooling configuration")
    return value


def esp_idf_runner(config: dict[str, Any], project_dir: Path) -> Path:
    value = esp_idf_configuration(config).get("runner")
    if not isinstance(value, str) or not value.strip():
        raise ValueError("ESP-IDF target is missing its runner path")
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = project_dir / path
    return path.resolve()


def esp_idf_manifest_path(config: dict[str, Any], project_dir: Path) -> Path:
    value = esp_idf_configuration(config).get("artifactManifest")
    if not isinstance(value, str) or not value.strip():
        raise ValueError("ESP-IDF target is missing its artifact manifest path")
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = project_dir / path
    return path.resolve()


def esp_idf_extra_arguments(config: dict[str, Any]) -> list[str]:
    """Translate manifest-only build definitions to the ESP-IDF runner CLI.

    ``hal_project_config.h`` remains owned by the project and is read by the
    runner directly. Only definitions introduced through the VS Code manifest
    are forwarded here.
    """
    cmake = config.get("cmake")
    cache = cmake.get("cache") if isinstance(cmake, dict) else None
    if not isinstance(cache, dict):
        return []

    features: list[str] = []
    defines: list[str] = []

    def add_unique(values: list[str], value: str) -> None:
        if value not in values:
            values.append(value)

    for key, value in cache.items():
        symbol = str(key)
        if re.fullmatch(r"HAL_ENABLE_[A-Z0-9_]+", symbol):
            if value is None or str(value) in {"", "1"}:
                add_unique(features, symbol)
        elif re.fullmatch(r"HAL_DISABLE_[A-Z0-9_]+", symbol):
            suffix = "" if value is None or str(value) == "" else f"={value}"
            add_unique(defines, f"{symbol}{suffix}")

    for key in ("JH_EXTRA_DEFINES", "EXTRA_HAL_DEFINES"):
        raw = cache.get(key)
        if raw is None or raw == "":
            continue
        for item in str(raw).split(";"):
            token = item.strip()
            if not token:
                continue
            match = HAL_DEFINE_TOKEN_RE.fullmatch(token)
            if match is not None and match.group("symbol").startswith(
                "HAL_ENABLE_"
            ):
                add_unique(features, match.group("symbol"))
            else:
                add_unique(defines, token)

    arguments: list[str] = []
    for feature in features:
        arguments.extend(["--feature", feature])
    for define in defines:
        arguments.extend(["--define", define])
    return arguments


def esp_idf_runner_command(
    config: dict[str, Any],
    project_dir: Path,
    action: str,
    *,
    port: str | None = None,
) -> list[str]:
    target = str(config.get("target") or "").strip()
    board = str(config.get("board") or "").strip()
    if not target or not board:
        raise ValueError("ESP-IDF runner requires a resolved target and board")
    command = [
        sys.executable,
        str(esp_idf_runner(config, project_dir)),
        action,
        "--project",
        str(project_dir),
        "--target",
        target,
        "--board",
        board,
        "--output",
        str(get_build_dir(config, project_dir)),
    ]
    if port is not None:
        command.extend(["--port", port])
    command.extend(esp_idf_extra_arguments(config))
    return command


def run_esp_idf_action(
    config: dict[str, Any],
    project_dir: Path,
    action: str,
    *,
    port: str | None = None,
) -> int:
    build_dir = get_build_dir(config, project_dir)
    if not managed_build_dir_allowed(build_dir, project_dir):
        print(
            f"error: refusing ESP-IDF output outside managed artifact roots: "
            f"{build_dir}",
            file=sys.stderr,
        )
        return EXIT_UNSAFE_DEVICE
    try:
        command = esp_idf_runner_command(
            config, project_dir, action, port=port
        )
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_CONFIG
    return run_command(command, verbose=as_bool(config.get("verbose")))


def _esp_idf_artifact_path(
    build_dir: Path,
    raw_path: Any,
    field: str,
) -> Path:
    if not isinstance(raw_path, str) or not raw_path.strip():
        raise ValueError(f"ESP-IDF artifact manifest field {field} is invalid")
    relative = Path(raw_path)
    if relative.is_absolute():
        raise ValueError(
            f"ESP-IDF artifact manifest field {field} must be relative"
        )
    resolved = (build_dir / relative).resolve()
    if not path_within(resolved, build_dir):
        raise ValueError(
            f"ESP-IDF artifact manifest field {field} escapes the build directory"
        )
    if not resolved.is_file():
        raise ValueError(f"ESP-IDF artifact is missing: {resolved}")
    return resolved


def validate_esp_idf_artifact_manifest(
    config: dict[str, Any], project_dir: Path
) -> tuple[dict[str, Any], dict[str, Path], list[Path]]:
    manifest_path = esp_idf_manifest_path(config, project_dir)
    try:
        manifest = load_json_file(manifest_path)
    except (OSError, ValueError) as exc:
        raise ValueError(
            f"cannot load ESP-IDF artifact manifest {manifest_path}: {exc}"
        ) from exc
    if manifest.get("schemaVersion") != 1:
        raise ValueError("ESP-IDF artifact manifest has an unsupported schema")
    if manifest.get("target") != config.get("target"):
        raise ValueError("ESP-IDF artifact manifest target does not match the build")
    if manifest.get("board") != config.get("board"):
        raise ValueError("ESP-IDF artifact manifest board does not match the build")

    build_dir = get_build_dir(config, project_dir).resolve()
    artifacts_raw = manifest.get("artifacts")
    if not isinstance(artifacts_raw, dict):
        raise ValueError("ESP-IDF artifact manifest is missing artifacts")
    artifacts = {
        str(name): _esp_idf_artifact_path(
            build_dir, value, f"artifacts.{name}"
        )
        for name, value in artifacts_raw.items()
    }

    flash_images_raw = manifest.get("flashImages")
    if not isinstance(flash_images_raw, list) or not flash_images_raw:
        raise ValueError("ESP-IDF artifact manifest is missing flash images")
    flash_images: list[Path] = []
    seen_paths: set[Path] = set()
    seen_offsets: set[str] = set()
    for index, item in enumerate(flash_images_raw):
        if not isinstance(item, dict):
            raise ValueError(
                f"ESP-IDF artifact manifest flashImages[{index}] is invalid"
            )
        offset = item.get("offset")
        if not isinstance(offset, str) or not offset.strip():
            raise ValueError(
                f"ESP-IDF artifact manifest flashImages[{index}].offset is invalid"
            )
        path = _esp_idf_artifact_path(
            build_dir, item.get("path"), f"flashImages[{index}].path"
        )
        if offset in seen_offsets or path in seen_paths:
            raise ValueError("ESP-IDF artifact manifest has duplicate flash images")
        seen_offsets.add(offset)
        seen_paths.add(path)
        flash_images.append(path)
    return manifest, artifacts, flash_images


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

    objdump = objdump_program(config)
    if objdump is None:
        print("warning: memory map overview skipped; target objdump not found", file=sys.stderr)
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
    try:
        attach_hal_feature_resolution(config, project_dir)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return project_dir, {}, EXIT_CONFIG
    return project_dir, config, 0


def get_cmake_build_dir(config: dict[str, Any], project_dir: Path) -> Path:
    host_environment = load_windows_host_environment()
    managed_root = host_environment.get("buildRoot")
    if isinstance(managed_root, str) and managed_root:
        build_dir = (
            Path(managed_root).expanduser()
            / windows_project_build_key(project_dir)
            / "cmake"
        )
    else:
        build_dir = Path(
            str(config.get("cmakeBuildDir") or project_dir / ".build" / "cmake")
        ).expanduser()
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
    configuration = config.get("_cmakeBuildConfiguration")
    if configuration:
        build_dir = build_dir / str(configuration)
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
    if path_within(build_dir, project_dir) or path_within(
        build_dir, jaszczurhal_root() / ".build"
    ):
        return True
    host_environment = load_windows_host_environment()
    managed_root = host_environment.get("buildRoot")
    return isinstance(managed_root, str) and bool(managed_root) and path_within(
        build_dir, Path(managed_root).expanduser()
    )


def cmake_cache_value(cache_path: Path, key: str) -> str | None:
    try:
        text = cache_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    for line in text.splitlines():
        if line.startswith(f"{key}:") and "=" in line:
            value = line.split("=", 1)[1].strip()
            return value or None
    return None


def cmake_cache_home_directory(cache_path: Path) -> Path | None:
    value = cmake_cache_value(cache_path, "CMAKE_HOME_DIRECTORY")
    return Path(value).expanduser() if value else None


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


def reset_stale_cmake_cache_if_needed(
    source_dir: Path,
    build_dir: Path,
    project_dir: Path,
    generator: str,
) -> int:
    cache_path = build_dir / "CMakeCache.txt"
    if not cache_path.is_file():
        return 0

    cached_source = cmake_cache_home_directory(cache_path)
    if cached_source is None:
        return 0
    cached_generator = cmake_cache_value(cache_path, "CMAKE_GENERATOR")
    source_matches = cached_source.resolve() == source_dir.resolve()
    generator_matches = cached_generator in {None, generator}
    if source_matches and generator_matches:
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
            "warning: stale CMake cache contract changed; resetting "
            f"{build_dir} (source {cached_source.resolve()} -> {source_dir.resolve()}, "
            f"generator {cached_generator or '?'} -> {generator})"
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


def cmake_build_config(config: dict[str, Any], *, debug: bool) -> dict[str, Any]:
    if not debug:
        return config
    configured = copy.deepcopy(config)
    configured["_cmakeBuildConfiguration"] = "debug"
    cmake = dict(configured.get("cmake") or {})
    cache = dict(cmake.get("cache") or {})
    cache["CMAKE_BUILD_TYPE"] = "Debug"
    cmake["cache"] = cache
    configured["cmake"] = cmake
    return configured


def normalize_cmake_cli_value(value: str) -> str:
    if re.search(r"(?:^|;)[A-Za-z]:[\\/]", value) or value.startswith("\\\\"):
        return value.replace("\\", "/")
    return value


def cmake_cache_args(config: dict[str, Any], project_dir: Path) -> list[str]:
    args: list[str] = []
    root = config.get("root")
    if isinstance(root, str) and root:
        root_path = Path(os.path.expanduser(root))
        if not root_path.is_absolute():
            root_path = project_dir / root_path
        args.append(f"-DJH_ROOT={normalize_cmake_cli_value(str(root_path.resolve()))}")

    identity = config.get("identity")
    if (
        config.get("target") in NATIVE_RP_TARGETS
        and isinstance(identity, dict)
        and as_bool(identity.get("enabled"))
    ):
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
            args.append(f"-D{key}={normalize_cmake_cli_value(str(value))}")

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
                args.append(
                    f"-D{cache_key}={normalize_cmake_cli_value(str(value))}"
                )

    return args


def platform_cmake_cache_args(config: dict[str, Any]) -> list[str]:
    args = [
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        f"-DPython3_EXECUTABLE={Path(sys.executable).resolve()}",
    ]
    tools = resolved_windows_tools()
    ninja = tools.get("ninja")
    picotool = os.environ.get("JH_PICOTOOL_EXECUTABLE") or tools.get("picotool")
    if not picotool and config.get("target") in NATIVE_RP_TARGETS:
        executable = "picotool.exe" if sys.platform == "win32" else "picotool"
        picotool = str(jaszczurhal_root() / ".build" / "tools" / "picotool" / executable)
    riscv = tools.get("riscv")
    openocd = tools.get("openocd")
    if ninja and cmake_generator(config).lower() == "ninja":
        args.append(f"-DCMAKE_MAKE_PROGRAM={ninja}")
    if picotool and config.get("target") in NATIVE_RP_TARGETS:
        args.append(f"-DJH_PICOTOOL_EXECUTABLE={picotool}")
    if riscv and config.get("target") == "rp2350-riscv":
        args.append(f"-DPICO_TOOLCHAIN_PATH={Path(riscv).parent.parent}")
    if openocd and config.get("target") == "stm32g474":
        args.append(f"-DOPENOCD_BIN={openocd}")
    return args


def configure_cmake_project(config: dict[str, Any], project_dir: Path) -> int:
    source_dir = get_cmake_source_dir(config, project_dir)
    build_dir = get_cmake_build_dir(config, project_dir)
    generator = cmake_generator(config)
    reset_status = reset_stale_cmake_cache_if_needed(
        source_dir, build_dir, project_dir, generator
    )
    if reset_status != 0:
        return reset_status
    cmd = [
        cmake_program(config),
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        "-G",
        generator,
    ]
    cmd.extend(removed_cmake_cache_args(config, build_dir))
    cmd.extend(cmake_cache_args(config, project_dir))
    cmd.extend(platform_cmake_cache_args(config))
    status = run_command(
        cmd,
        verbose=as_bool(config.get("verbose")),
        environment=cmake_process_environment(),
    )
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
    cmd = [
        cmake_program(config),
        "--build",
        str(get_cmake_build_dir(config, project_dir)),
        "--target",
        target,
    ]
    rc, output = run_command_capture(
        cmd,
        verbose=as_bool(config.get("verbose")),
        environment=cmake_process_environment(),
    )
    if rc != 0:
        print_build_diagnostics(diagnose_build_output(output, config))
    return rc


def synchronize_cmake_firmware_artifacts(
    config: dict[str, Any], project_dir: Path
) -> int:
    """Refresh stable artifacts even when the selected CMake tree is a no-op."""
    target = str(config.get("target") or "")
    cmake_build_dir = get_cmake_build_dir(config, project_dir)
    artifact_dir = get_build_dir(config, project_dir)
    sources: dict[str, tuple[str, ...]] = {
        "firmware.elf": ("firmware.elf",),
        "firmware.bin": ("firmware.bin",),
        "firmware.hex": ("firmware.hex",),
        "firmware.map": ("firmware.map", "firmware.elf.map"),
    }
    if target in UF2_TARGETS:
        sources["firmware.uf2"] = ("firmware.uf2",)
        if "HAL_ENABLE_OTA" in resolved_hal_feature_names(config, project_dir):
            sources["firmware.ota"] = ("firmware.ota",)

    resolved: dict[str, Path] = {}
    for artifact, candidates in sources.items():
        source = next(
            (
                cmake_build_dir / candidate
                for candidate in candidates
                if (cmake_build_dir / candidate).is_file()
            ),
            None,
        )
        if source is None:
            print(
                f"error: successful {target} build omitted {artifact} in "
                f"{cmake_build_dir}",
                file=sys.stderr,
            )
            return EXIT_BUILD
        resolved[artifact] = source

    try:
        artifact_dir.mkdir(parents=True, exist_ok=True)
        for artifact, source in resolved.items():
            destination = artifact_dir / artifact
            if source.resolve() != destination.resolve():
                shutil.copy2(source, destination)
        for stale in {"firmware.uf2", "firmware.ota"} - set(resolved):
            stale_path = artifact_dir / stale
            if stale_path.is_file():
                stale_path.unlink()
    except OSError as exc:
        print(f"error: failed to synchronize firmware artifacts: {exc}", file=sys.stderr)
        return EXIT_BUILD
    return 0


def invalidate_stable_firmware_artifacts(
    config: dict[str, Any], project_dir: Path
) -> int:
    """Remove uploadable output before attempting the selected target build."""
    artifact_dir = get_build_dir(config, project_dir)
    try:
        for artifact in STABLE_FIRMWARE_ARTIFACTS:
            path = artifact_dir / artifact
            if path.is_file():
                path.unlink()
    except OSError as exc:
        print(f"error: failed to invalidate firmware artifacts: {exc}", file=sys.stderr)
        return EXIT_BUILD
    return 0


def command_build(args: argparse.Namespace, *, debug: bool = False, show_memory_overview: bool = True) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if config.get("toolchain") == "esp-idf":
        if debug:
            print(
                "error: ESP-IDF debug builds are not implemented by jh-vscode",
                file=sys.stderr,
            )
            return EXIT_UNSUPPORTED
        diagnostics = build_preflight_diagnostics(config, project_dir)
        if diagnostics:
            print_build_diagnostics(diagnostics)
            return EXIT_BUILD
        rc = run_esp_idf_action(config, project_dir, "build")
        if rc != 0:
            return EXIT_BUILD
        rc = run_esp_idf_action(config, project_dir, "artifacts")
        if rc != 0:
            return EXIT_BUILD
        try:
            _, _, flash_images = validate_esp_idf_artifact_manifest(
                config, project_dir
            )
            manifest_path = esp_idf_manifest_path(config, project_dir)
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return EXIT_BUILD
        print(
            f"ESP-IDF artifacts: {manifest_path} "
            f"({len(flash_images)} flash images)"
        )
        return 0
    if config.get("toolchain") == "cmake":
        invalidate_status = invalidate_stable_firmware_artifacts(config, project_dir)
        if invalidate_status != 0:
            return invalidate_status
        diagnostics = build_preflight_diagnostics(config, project_dir)
        if diagnostics:
            print_build_diagnostics(diagnostics)
            return EXIT_BUILD
        build_config = cmake_build_config(config, debug=debug)
        target = cmake_targets(build_config)["buildDebug" if debug else "build"]
        rc = run_cmake_target(build_config, project_dir, target)
        if rc == 0:
            sync_status = synchronize_cmake_firmware_artifacts(build_config, project_dir)
            if sync_status != 0:
                return sync_status
            if show_memory_overview:
                print_memory_map_overview(build_config, project_dir)
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


def configured_bootsel_volume(config: dict[str, Any]) -> str | None:
    upload = config.get("upload")
    if not isinstance(upload, dict):
        return None
    value = str(upload.get("bootselVolume") or "").strip()
    return value or None


def command_upload(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if config.get("toolchain") == "esp-idf":
        diagnostics = build_preflight_diagnostics(config, project_dir)
        if diagnostics:
            print_build_diagnostics(diagnostics)
            return EXIT_UPLOAD
        port = (
            args.port
            or config.get("uploadPort")
            or (config.get("upload") or {}).get("port")
        )
        if not port and identity_enabled(config):
            port, detect_status = select_verified_identity_port(config)
            if detect_status != 0:
                return detect_status
        if not port:
            print(
                "error: ESP-IDF upload requires --port or a port selected "
                "with change-port",
                file=sys.stderr,
            )
            return EXIT_USAGE
        verify_status = verify_upload_port(
            config,
            str(port),
            allow_unverified=args.allow_unverified_port,
        )
        if verify_status != 0:
            return verify_status
        requested_port = str(port)
        build_status = command_build(
            args, debug=False, show_memory_overview=False
        )
        if build_status != 0:
            return build_status
        upload_port, recheck_status = resolve_upload_port_for_flash(
            config,
            requested_port,
            allow_unverified=args.allow_unverified_port,
        )
        if recheck_status != 0:
            return recheck_status
        if upload_port is None:
            print(
                "error: ESP-IDF upload port disappeared after the build",
                file=sys.stderr,
            )
            return EXIT_UNSAFE_DEVICE
        release_status = release_port_for_upload(upload_port, project_dir)
        if release_status != 0:
            return release_status
        try:
            upload_port, final_status = resolve_upload_port_for_flash(
                config,
                requested_port,
                allow_unverified=args.allow_unverified_port,
            )
            if final_status != 0:
                return final_status
            if upload_port is None:
                print(
                    "error: ESP-IDF upload port disappeared during monitor release",
                    file=sys.stderr,
                )
                return EXIT_UNSAFE_DEVICE
            rc = run_esp_idf_action(
                config, project_dir, "flash", port=upload_port
            )
        finally:
            end_upload_release(project_dir)
        return 0 if rc == 0 else EXIT_UPLOAD
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
    bootsel_volume = configured_bootsel_volume(config)
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
    if native_rp_serial_upload and bootsel_volume:
        _, selected_candidates = find_single_bootsel_mount(
            selected_id=bootsel_volume
        )
        if selected_candidates:
            print(
                "Explicit BOOTSEL volume detected, using UF2 upload: "
                f"{bootsel_volume}"
            )
            return command_upload_uf2(args)
    if native_rp_serial_upload and not args.port and port:
        if not upload_port_path_exists(str(port)):
            _, bootsel_candidates = find_single_bootsel_mount(
                selected_id=bootsel_volume
            )
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
        _, bootsel_candidates = find_single_bootsel_mount(
            selected_id=bootsel_volume
        )
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
    return get_platform_adapter().find_bootsel_mounts(BOOTSEL_LABELS)


def find_bootsel_blocks() -> list[dict[str, Any]]:
    return get_platform_adapter().find_bootsel_blocks(BOOTSEL_LABELS)


def bootsel_mountpoint(block: dict[str, Any]) -> Path | None:
    mountpoints = block.get("mountpoints")
    if isinstance(mountpoints, list):
        for mountpoint in mountpoints:
            if mountpoint:
                return Path(str(mountpoint))
    mountpoint = block.get("mountpoint")
    return Path(str(mountpoint)) if mountpoint else None


def mount_bootsel_block(block: dict[str, Any]) -> Path | None:
    return get_platform_adapter().mount_bootsel_block(
        block,
        BOOTSEL_LABELS,
        bootsel_mountpoint,
    )


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


def _normalized_bootsel_selection(value: str | Path) -> str:
    return str(value).strip().rstrip("\\/").casefold()


def bootsel_selection_matches(block: dict[str, Any], selected_id: str) -> bool:
    selected = _normalized_bootsel_selection(selected_id)
    if not selected:
        return False
    identities = [
        block.get("path"),
        block.get("volumeGuid"),
        bootsel_mountpoint(block),
    ]
    return any(
        identity is not None
        and _normalized_bootsel_selection(identity) == selected
        for identity in identities
    )


def find_single_bootsel_mount(
    excluded_ids: set[str] | None = None,
    selected_id: str | None = None,
) -> tuple[Path | None, list[str]]:
    excluded = excluded_ids or set()
    all_blocks = find_bootsel_blocks()
    if selected_id:
        blocks = [
            block
            for block in all_blocks
            if bootsel_selection_matches(block, selected_id)
        ]
        excluded_mounts: set[Path] = set()
    else:
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
        if (
            (
                not selected_id
                and f"mount:{mount}" not in excluded
                and mount not in excluded_mounts
            )
            or (
                selected_id
                and _normalized_bootsel_selection(mount)
                == _normalized_bootsel_selection(selected_id)
            )
        )
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
    selected_id: str | None = None,
) -> tuple[Path | None, list[str]]:
    deadline = time.monotonic() + timeout_s
    last_candidates: list[str] = []
    while True:
        mount, candidates = find_single_bootsel_mount(
            excluded_ids,
            selected_id,
        )
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
    opened = False
    try:
        with serial.Serial(
            port=port,
            baudrate=1200,
            timeout=0.25,
            write_timeout=0.25,
        ) as touch:
            opened = True
            touch.dtr = True
            time.sleep(0.05)
            touch.dtr = False
            time.sleep(0.05)
    except (OSError, serial.SerialException) as exc:
        expected_disconnect = getattr(exc, "errno", None) in {
            errno.EIO,
            errno.ENODEV,
        }
        if opened and get_platform_adapter().platform_name == "windows":
            expected_disconnect = True
        if expected_disconnect:
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
        selected_bootsel_id=configured_bootsel_volume(config),
    )


def validate_uf2_artifact(uf2: Path) -> str | None:
    try:
        file_size = uf2.stat().st_size
    except OSError as exc:
        return f"cannot read artifact metadata: {exc}"
    if file_size == 0:
        return "artifact is empty"
    if file_size % UF2_BLOCK_SIZE != 0:
        return f"size {file_size} is not a multiple of {UF2_BLOCK_SIZE} bytes"

    block_count = file_size // UF2_BLOCK_SIZE
    group_counts: dict[int, int] = {}
    group_blocks: dict[int, set[int]] = {}
    try:
        with uf2.open("rb") as artifact:
            for index in range(block_count):
                block = artifact.read(UF2_BLOCK_SIZE)
                if len(block) != UF2_BLOCK_SIZE:
                    return f"block {index} is truncated"
                (
                    magic0,
                    magic1,
                    flags,
                    _target_address,
                    payload_size,
                    block_number,
                    declared_count,
                    family_id,
                ) = struct.unpack_from("<IIIIIIII", block, 0)
                magic_end = struct.unpack_from("<I", block, 508)[0]
                if (
                    magic0 != UF2_MAGIC_START0
                    or magic1 != UF2_MAGIC_START1
                    or magic_end != UF2_MAGIC_END
                ):
                    return f"block {index} has invalid UF2 magic"
                if payload_size == 0 or payload_size > UF2_MAX_PAYLOAD_SIZE:
                    return f"block {index} has invalid payload size {payload_size}"
                if declared_count == 0 or block_number >= declared_count:
                    return f"block {index} has invalid sequence number {block_number}"
                is_rp2350_absolute_ignore = (
                    family_id == UF2_ABSOLUTE_FAMILY_ID
                    and flags
                    == UF2_FLAG_FAMILY_ID_PRESENT
                    | UF2_FLAG_EXTENSION_FLAGS_PRESENT
                    and payload_size == 256
                    and block_number == 0
                    and declared_count == 2
                    and all(value == 0xEF for value in block[32:288])
                    and struct.unpack_from("<I", block, 32 + payload_size)[0]
                    == UF2_EXTENSION_RP2_IGNORE_BLOCK
                )
                if is_rp2350_absolute_ignore:
                    continue

                group_key = (
                    family_id
                    if flags & UF2_FLAG_FAMILY_ID_PRESENT
                    else -1
                )
                previous_count = group_counts.setdefault(group_key, declared_count)
                if previous_count != declared_count:
                    return f"block {index} changes its UF2 group block count"
                seen = group_blocks.setdefault(group_key, set())
                if block_number in seen:
                    return f"block {index} duplicates sequence number {block_number}"
                seen.add(block_number)
            if artifact.read(1):
                return "artifact changed while it was being validated"
    except OSError as exc:
        return f"cannot read artifact: {exc}"

    if not group_counts:
        return "artifact has no programmable UF2 blocks"

    grouped_block_total = sum(len(blocks) for blocks in group_blocks.values())
    if (
        grouped_block_total == block_count
        and all(count == block_count for count in group_counts.values())
    ):
        global_blocks: set[int] = set()
        for blocks in group_blocks.values():
            overlap = global_blocks.intersection(blocks)
            if overlap:
                duplicate = min(overlap)
                return f"merged UF2 duplicates global sequence number {duplicate}"
            global_blocks.update(blocks)
        if len(global_blocks) != block_count:
            return (
                f"merged UF2 declares {block_count} blocks but contains "
                f"{len(global_blocks)} unique sequence numbers"
            )
        return None

    for group_key, declared_count in group_counts.items():
        seen = group_blocks.get(group_key, set())
        if len(seen) != declared_count:
            return (
                f"UF2 group 0x{group_key & 0xFFFFFFFF:08x} declares "
                f"{declared_count} blocks but contains {len(seen)}"
            )
    return None


def upload_uf2_artifact(
    uf2: Path,
    config: dict[str, Any],
    project_dir: Path,
    *,
    bootsel_wait_s: float = 0.0,
    excluded_bootsel_ids: set[str] | None = None,
    selected_bootsel_id: str | None = None,
) -> int:
    validation_error = validate_uf2_artifact(uf2)
    if validation_error:
        print(
            f"error: invalid or incomplete UF2 artifact {uf2}: {validation_error}",
            file=sys.stderr,
        )
        return EXIT_UPLOAD
    if bootsel_wait_s > 0.0:
        mount, bootsel_candidates = wait_for_single_bootsel_mount(
            bootsel_wait_s,
            excluded_bootsel_ids,
            selected_bootsel_id,
        )
    else:
        mount, bootsel_candidates = find_single_bootsel_mount(
            selected_id=selected_bootsel_id
        )
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
        get_platform_adapter().durable_copy(uf2, destination)
    except PermissionError as exc:
        print(
            f"error: BOOTSEL drive is read-only or access was denied: {exc}",
            file=sys.stderr,
        )
        return EXIT_UPLOAD
    except FileNotFoundError as exc:
        print(f"error: BOOTSEL drive disappeared during UF2 copy: {exc}", file=sys.stderr)
        return EXIT_UPLOAD
    except OSError as exc:
        print(f"error: UF2 copy failed: {exc}", file=sys.stderr)
        return EXIT_UPLOAD
    print_memory_map_overview(config, project_dir)
    time.sleep(0.2)
    return 0


def parse_ota_discovery_response(payload: bytes, address: tuple[str, int]) -> dict[str, Any] | None:
    try:
        fields = ota_control_line(payload).split(" ")
    except RuntimeError:
        return None
    if len(fields) != 8 or fields[:2] != ["JHOTA", "1"]:
        return None
    decimal_fields = fields[4:8]
    if any(
        re.fullmatch(r"0|[1-9][0-9]*", field) is None
        for field in decimal_fields
    ):
        return None
    try:
        port = int(fields[4])
        slot_size = int(fields[5])
        generation = int(fields[6])
        mode = int(fields[7])
    except ValueError:
        return None
    if (
        not (1 <= port <= 65535)
        or not (1 <= slot_size <= 0xFFFFFFFF)
        or not (0 <= generation <= 0xFFFFFFFF)
        or not (0 <= mode <= 4)
    ):
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


def ota_auth2_tag(
    password: str,
    command: int,
    tcp_port: int,
    image_size: int,
    image_md5: str,
    device_nonce: str,
    client_nonce: str,
) -> str:
    """Bind an OTA password proof to the exact invitation and both nonces."""
    if (
        command not in (0, 100)
        or not 1 <= tcp_port <= 65535
        or not 1 <= image_size <= 0xFFFFFFFF
    ):
        raise ValueError("invalid OTA AUTH2 invitation")
    for name, value, length in (
        ("image MD5", image_md5, 32),
        ("device nonce", device_nonce, 32),
        ("client nonce", client_nonce, 32),
    ):
        if len(value) != length or any(
            character not in "0123456789abcdefABCDEF" for character in value
        ):
            raise ValueError(f"invalid OTA AUTH2 {name}")
    password_md5 = hashlib.md5(password.encode("utf-8")).hexdigest()
    transcript = (
        f"JHOTA-AUTH-2:{command}:{tcp_port}:{image_size}:"
        f"{image_md5.lower()}:{device_nonce.lower()}:{client_nonce.lower()}"
    ).encode("ascii")
    return hmac.new(
        password_md5.encode("ascii"), transcript, hashlib.sha256
    ).hexdigest()


def ota_control_line(payload: bytes) -> str:
    """Decode one exact OTA UDP control line."""
    try:
        text = payload.decode("ascii", errors="strict")
    except UnicodeDecodeError as exc:
        raise RuntimeError("device returned a non-ASCII OTA response") from exc
    if text.endswith("\r\n"):
        text = text[:-2]
    elif text.endswith("\n"):
        text = text[:-1]
    if (
        not text
        or text != text.strip()
        or "\r" in text
        or "\n" in text
        or "\0" in text
        or any(
            ord(character) < 0x20 or ord(character) > 0x7E
            for character in text
        )
    ):
        raise RuntimeError("device returned an invalid OTA control line")
    return text


def ota_acknowledged_bytes(payload: bytes, remaining: int) -> int:
    """Parse one positive, bounded decimal OTA TCP acknowledgement."""
    if remaining <= 0 or re.fullmatch(rb"[1-9][0-9]*\n", payload) is None:
        raise RuntimeError("invalid OTA byte acknowledgement")
    acknowledged = int(payload[:-1])
    if acknowledged > remaining:
        raise RuntimeError("invalid OTA byte acknowledgement")
    return acknowledged


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


def esp_idf_ota_image(
    config: dict[str, Any], project_dir: Path
) -> tuple[Path, bytes]:
    """Return the validated raw ESP-IDF application image for OTA."""
    manifest, artifacts, flash_images = validate_esp_idf_artifact_manifest(
        config, project_dir
    )
    integration = manifest.get("integration")
    resolved = (
        integration.get("resolvedFeatures")
        if isinstance(integration, dict)
        else None
    )
    if not isinstance(resolved, list) or "HAL_ENABLE_OTA" not in resolved:
        raise ValueError("ESP-IDF artifact was not built with HAL_ENABLE_OTA")

    application = artifacts.get("applicationBinary")
    if application is None or application not in flash_images:
        raise ValueError(
            "ESP-IDF artifact manifest does not identify its application flash image"
        )
    flash_records = manifest.get("flashImages")
    application_index = flash_images.index(application)
    if (
        not isinstance(flash_records, list)
        or application_index >= len(flash_records)
        or not isinstance(flash_records[application_index], dict)
    ):
        raise ValueError("ESP-IDF application image has no flash manifest record")
    # The validator returns paths in flashImages order. Reuse that mapping:
    # resolving a validated path again is redundant and may rewrite a Windows
    # junction/short-path alias into a different lexical Path.
    record = flash_records[application_index]

    image = application.read_bytes()
    if not image:
        raise ValueError("ESP-IDF OTA application image is empty")
    expected_size = record.get("size")
    expected_sha256 = record.get("sha256")
    if (
        not isinstance(expected_size, int)
        or expected_size != len(image)
        or not isinstance(expected_sha256, str)
        or hashlib.sha256(image).hexdigest() != expected_sha256
    ):
        raise ValueError("ESP-IDF OTA application image differs from its manifest")
    return application, image


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
    if not container or len(container) > 0xFFFFFFFF:
        raise ValueError("OTA image size must be in range 1..4294967295")
    device_port = int(device["port"])
    if not 1 <= device_port <= 65535:
        raise ValueError("OTA device UDP port must be in range 1..65535")
    udp_address = (str(device["address"]), device_port)
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
            udp.connect(udp_address)
            expected_peer = str(udp.getpeername()[0])
            udp.send(invitation)
            try:
                response = udp.recv(256)
            except socket.timeout as exc:
                raise TimeoutError(
                    "timed out waiting for the OTA invitation response"
                ) from exc
            text = ota_control_line(response)
            challenge = re.fullmatch(r"AUTH2 ([0-9a-fA-F]{32})", text)
            if challenge is not None:
                nonce = challenge.group(1)
                client_nonce = os.urandom(16).hex()
                image_md5 = hashlib.md5(container).hexdigest()
                digest = ota_auth2_tag(
                    password,
                    0,
                    tcp_port,
                    len(container),
                    image_md5,
                    nonce,
                    client_nonce,
                )
                udp.send(f"201 {client_nonce} {digest}\n".encode("ascii"))
                try:
                    response = udp.recv(256)
                except socket.timeout as exc:
                    raise TimeoutError(
                        "timed out waiting for the OTA authentication response"
                    ) from exc
                text = ota_control_line(response)
                if text != "OK":
                    raise RuntimeError(
                        f"device rejected OTA authentication: {text}"
                    )
            elif text.startswith("AUTH2"):
                raise RuntimeError("device returned an invalid OTA AUTH2 challenge")
            elif text.startswith("AUTH"):
                raise RuntimeError(
                    "device requested obsolete OTA authentication; AUTH2 is required"
                )
            elif password:
                if text == "OK":
                    raise RuntimeError(
                        "device skipped AUTH2 while an OTA password is configured"
                    )
                raise RuntimeError(
                    f"device did not complete required OTA AUTH2: {text}"
                )
            elif text != "OK":
                raise RuntimeError(f"device rejected OTA invitation: {text}")

        try:
            connection, peer = server.accept()
        except socket.timeout as exc:
            raise TimeoutError(
                "timed out waiting for the device's OTA TCP connection"
            ) from exc
        if peer[0] != expected_peer:
            connection.close()
            raise RuntimeError(
                "OTA TCP peer differs from the authenticated UDP endpoint"
            )
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
                    acknowledged += ota_acknowledged_bytes(
                        line, len(chunk) - acknowledged
                    )
                sent += len(chunk)
                print(f"\rOTA {sent}/{len(container)} bytes", end="", flush=True)
            final = reader.read(3)
            if final != b"OK":
                raise RuntimeError(f"device did not confirm OTA completion: {final!r}")
        print(f"\nOTA image accepted by {peer[0]}; device is rebooting.")


def command_upload_ota(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    target = str(config.get("target") or "")
    native_rp = target in NATIVE_RP_TARGETS
    esp_idf = config.get("toolchain") == "esp-idf"
    if not native_rp and not esp_idf:
        print(
            "error: upload-ota requires a native RP or ESP-IDF target",
            file=sys.stderr,
        )
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
    try:
        build_dir = get_build_dir(config, project_dir)
        if esp_idf:
            source, container = esp_idf_ota_image(config, project_dir)
        else:
            source = find_configured_ota(config) or find_ota(build_dir)
            if source is None:
                raise ValueError(
                    f"no unique OTA artifact found in {build_dir}"
                )
            signed = build_dir / f"{source.stem}.signed.ota"
            container = sign_ota_container(source, password, signed)
        slot_size = device.get("slotSize")
        if isinstance(slot_size, int) and len(container) > slot_size:
            raise ValueError(
                f"OTA image is {len(container)} bytes but the device slot is "
                f"only {slot_size} bytes"
            )
        listen_port = ota_listen_port(ota)
        upload_ota_container(device, container, password, listen_port)
    except (OSError, RuntimeError, ValueError, socket.timeout) as exc:
        print(f"error: OTA upload failed: {exc}", file=sys.stderr)
        return EXIT_UPLOAD
    print_memory_map_overview(config, project_dir)
    return 0


def neutral_firmware_source_dir() -> Path:
    return jaszczurhal_root() / "vscode" / "neutral_fw" / "rp_native"


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
    bootsel_volume = configured_bootsel_volume(config)
    bootsel_candidates: list[str] = []
    if bootsel_volume:
        _, bootsel_candidates = find_single_bootsel_mount(
            selected_id=bootsel_volume
        )
        if bootsel_candidates:
            port = None
    if not port and not bootsel_candidates:
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
                selected_bootsel_id=bootsel_volume,
            )
        return upload_uf2_artifact(
            uf2,
            neutral,
            project_dir,
            selected_bootsel_id=bootsel_volume,
        )
    finally:
        if port_released:
            end_upload_release(project_dir)


def command_monitor(args: argparse.Namespace, mode: str) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status

    if mode == "pico" and config.get("toolchain") == "esp-idf":
        mode = "esp"

    port = (
        args.port
        or config.get("uploadPort")
        or (config.get("upload") or {}).get("port")
    )
    identity_tokens: list[str] = []
    follow_identity = (
        mode in {"pico", "esp"}
        and not args.port
        and identity_enabled(config)
    )
    if follow_identity:
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

    runtime = get_platform_adapter().persistent_monitor_path()
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
    if follow_identity:
        cmd.append("--follow-identity")
        cmd.extend(
            [
                "--identity-json",
                json.dumps(
                    expected_serial_identity(config).as_dict(),
                    ensure_ascii=False,
                    separators=(",", ":"),
                ),
            ]
        )
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
    with get_platform_adapter().build_lock(lock_path):
        yield


def write_intellisense_configuration(
    config: dict[str, Any],
    project_dir: Path,
    raw_compile_db: Path,
    *,
    intellisense_mode: str | None,
) -> int:
    build_dir = get_build_dir(config, project_dir)
    patched_compile_db = build_dir / "compile_commands_patched.json"
    try:
        compile_db = json.loads(raw_compile_db.read_text(encoding="utf-8"))
        if not isinstance(compile_db, list):
            raise ValueError("compile database root must be an array")
        patched_compile_db.write_text(
            json.dumps(compile_db, indent=2) + "\n", encoding="utf-8"
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(
            f"error: failed to write patched compile database: {exc}",
            file=sys.stderr,
        )
        return EXIT_GENERIC

    cpp_configuration: dict[str, Any] = {
        "name": str(config.get("module") or project_dir.name),
        "compileCommands": str(patched_compile_db),
        "compilerPath": "",
        "cStandard": "c11",
        "cppStandard": "gnu++17",
    }
    if intellisense_mode is not None:
        cpp_configuration["intelliSenseMode"] = intellisense_mode
    cpp_props = {"configurations": [cpp_configuration], "version": 4}
    cpp_props_path = project_dir / ".vscode" / "c_cpp_properties.json"
    try:
        cpp_props_path.write_text(
            json.dumps(cpp_props, indent=4) + "\n", encoding="utf-8"
        )
    except OSError as exc:
        print(f"error: failed to write {cpp_props_path}: {exc}", file=sys.stderr)
        return EXIT_GENERIC

    print(f"Wrote {patched_compile_db}")
    print(f"Wrote {cpp_props_path}")
    return 0


def command_refresh_intellisense(args: argparse.Namespace) -> int:
    project_dir, config, status = load_config_for_action(args)
    if status != 0:
        return status
    if config.get("toolchain") == "esp-idf":
        build_status = command_build(
            args, debug=False, show_memory_overview=False
        )
        if build_status != 0:
            return build_status
        try:
            _, artifacts, _ = validate_esp_idf_artifact_manifest(
                config, project_dir
            )
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return EXIT_BUILD
        raw_compile_db = artifacts.get("compileCommands")
        if raw_compile_db is None:
            print(
                "error: ESP-IDF artifact manifest omitted compileCommands",
                file=sys.stderr,
            )
            return EXIT_BUILD
        # cpptools consumes the Xtensa compiler and flags directly from the
        # ESP-IDF compile database; its fixed architecture modes do not model
        # Xtensa accurately.
        return write_intellisense_configuration(
            config,
            project_dir,
            raw_compile_db,
            intellisense_mode=None,
        )
    if config.get("toolchain") == "cmake":
        target = cmake_targets(config)["compileDb"]
        rc = run_cmake_target(config, project_dir, target)
        if rc != 0:
            return EXIT_BUILD

        build_dir = get_build_dir(config, project_dir)
        cmake_build_dir = get_cmake_build_dir(config, project_dir)
        raw_compile_db = find_compile_database(cmake_build_dir, build_dir)
        if raw_compile_db is None:
            print(
                "error: compile database was not generated; checked: "
                f"{cmake_build_dir / 'compile_commands.json'}, "
                f"{build_dir / 'compile_commands.json'}",
                file=sys.stderr,
            )
            return EXIT_BUILD

        return write_intellisense_configuration(
            config,
            project_dir,
            raw_compile_db,
            intellisense_mode="gcc-arm",
        )
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
    cmake_build_dir = get_cmake_build_dir(config, project_dir)
    candidates = [path for path in (build_dir, cmake_build_dir) if path.exists()]
    existing = [
        path
        for path in candidates
        if not any(path != other and path_within(path, other) for other in candidates)
    ]
    if not existing:
        print(f"Nothing to clean: {build_dir}")
        return 0
    for path in dict.fromkeys(existing):
        if not managed_build_dir_allowed(path, project_dir):
            print(
                f"error: refusing to clean outside managed artifact roots: {path}",
                file=sys.stderr,
            )
            return EXIT_UNSAFE_DEVICE
    for path in dict.fromkeys(existing):
        try:
            shutil.rmtree(path)
        except OSError as exc:
            print(f"error: failed to remove {path}: {exc}", file=sys.stderr)
            return EXIT_GENERIC
        print(f"Removed {path}")
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
    parser.add_argument(
        "--bootsel-volume",
        help="Select one BOOTSEL drive root or volume GUID for UF2 upload.",
    )
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


def dispatch(args: argparse.Namespace) -> int:
    if args.action == "config-dump":
        return command_config_dump(args)
    if args.action == "debug-tools":
        return command_debug_tools(args)
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

    try:
        return dispatch(args)
    except PlatformOperationUnsupported as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_UNSUPPORTED


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        raise SystemExit(EXIT_GENERIC)
