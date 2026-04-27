#!/usr/bin/env python3
"""
VS Code helpers for Arduino RP2040 projects.

This script keeps VS Code tasks portable between Linux and native Windows.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import string
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable


PROJECT_DIR = Path(__file__).resolve().parent.parent
SETTINGS_FILE = PROJECT_DIR / ".vscode" / "settings.json"
CPP_PROPS_FILE = PROJECT_DIR / ".vscode" / "c_cpp_properties.json"
BUILD_DIR = PROJECT_DIR / ".build"

CYAN = "\033[0;36m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
RED = "\033[0;31m"
NC = "\033[0m"

BOARDS = [
    ("rp2040:rp2040:rpipico", "Raspberry Pi Pico", "RP2040"),
    ("rp2040:rp2040:rpipicow", "Raspberry Pi Pico W", "RP2040"),
    ("rp2040:rp2040:rpipico2", "Raspberry Pi Pico 2", "RP2350"),
    ("rp2040:rp2040:rpipico2w", "Raspberry Pi Pico 2 W", "RP2350"),
    ("rp2040:rp2040:waveshare_rp2040_zero", "Waveshare RP2040-Zero", "RP2040"),
    ("rp2040:rp2040:waveshare_rp2040_plus", "Waveshare RP2040-Plus", "RP2040"),
]


def info(message: str) -> None:
    print(f"{CYAN}[INFO]{NC} {message}")


def ok(message: str) -> None:
    print(f"{GREEN}[OK]{NC} {message}")


def warn(message: str) -> None:
    print(f"{YELLOW}[WARN]{NC} {message}")


def err(message: str) -> None:
    print(f"{RED}[ERROR]{NC} {message}")


def load_settings() -> dict:
    if not SETTINGS_FILE.is_file():
        return {}
    with SETTINGS_FILE.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def save_settings(settings: dict) -> None:
    SETTINGS_FILE.parent.mkdir(parents=True, exist_ok=True)
    with SETTINGS_FILE.open("w", encoding="utf-8") as handle:
        json.dump(settings, handle, indent=4)
        handle.write("\n")


def expand_setting(value: str | None) -> str:
    if not isinstance(value, str):
        return ""

    expanded = value
    expanded = expanded.replace("${workspaceFolder}", str(PROJECT_DIR))
    expanded = expanded.replace("${workspaceFolderBasename}", PROJECT_DIR.name)
    expanded = expanded.replace("${userHome}", str(Path.home()))

    while "${env:" in expanded:
        start = expanded.find("${env:")
        end = expanded.find("}", start)
        if end == -1:
            break
        key = expanded[start + 6:end]
        replacement = os.environ.get(key, "")
        expanded = expanded[:start] + replacement + expanded[end + 1:]

    return os.path.expandvars(os.path.expanduser(expanded))


def get_setting(settings: dict, key: str, default: str = "") -> str:
    value = settings.get(key, default)
    return expand_setting(value)


def resolve_cli(settings: dict) -> str:
    configured = get_setting(settings, "arduino.cliPath")
    candidates = [configured, "arduino-cli", "arduino-cli.exe"]

    for candidate in candidates:
        if not candidate:
            continue
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
        if Path(candidate).is_file():
            return candidate

    raise SystemExit("arduino-cli not found")


def find_sketch() -> Path:
    for path in sorted(PROJECT_DIR.rglob("*.ino")):
        if ".build" not in path.parts:
            return path
    raise SystemExit("No .ino file found in the project")


def get_build_dir(settings: dict) -> Path:
    build_dir = get_setting(settings, "arduino.buildDir", str(BUILD_DIR))
    return Path(build_dir)


def library_args(settings: dict) -> list[str]:
    sketchbook = get_setting(settings, "arduino.sketchbookPath")
    if not sketchbook:
        return []
    libs_dir = Path(sketchbook) / "libraries"
    if libs_dir.is_dir():
        info(f"  Libraries: {libs_dir}")
        return ["--libraries", str(libs_dir)]
    return []


def compile_project(
    *,
    settings: dict,
    upload: bool = False,
    debug: bool = False,
    only_compilation_database: bool = False,
) -> int:
    cli = resolve_cli(settings)
    fqbn = settings.get("arduino.fqbn", "")
    if not fqbn:
        raise SystemExit("Missing arduino.fqbn in .vscode/settings.json")

    build_dir = get_build_dir(settings)
    sketch = find_sketch()
    args = [
        cli,
        "compile",
        "--fqbn",
        fqbn,
        "--build-path",
        str(build_dir),
        *library_args(settings),
        "--build-property",
        f"compiler.cpp.extra_flags=-I '{sketch.parent}'",
        "--build-property",
        f"compiler.c.extra_flags=-I '{sketch.parent}'",
        "--warnings",
        "all",
    ]

    if debug:
        args.append("--optimize-for-debug")

    if only_compilation_database:
        args.append("--only-compilation-database")

    if upload:
        upload_port = settings.get("arduino.uploadPort", "").strip()
        if not upload_port:
            raise SystemExit("arduino.uploadPort is empty in .vscode/settings.json")
        args.extend(["--upload", "--port", upload_port])

    if str(settings.get("arduino.verbose", False)).lower() == "true":
        args.append("-v")

    args.append(str(sketch.parent))
    return subprocess.run(args, cwd=PROJECT_DIR, check=False).returncode


def _process_response_file(resp_file: Path, includes: set[str], defines: set[str], iprefix: str) -> None:
    if not resp_file.is_file():
        return

    with resp_file.open("r", encoding="utf-8", errors="ignore") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line:
                continue
            if line.startswith("-iwithprefixbefore"):
                rel = line[len("-iwithprefixbefore"):]
                if iprefix and rel:
                    includes.add(os.path.normpath(iprefix + rel))
            elif line.startswith("-I"):
                path = line[2:]
                if path:
                    includes.add(path)
            elif line.startswith("-D"):
                define = line[2:]
                if define:
                    defines.add(define)


def generate_cpp_properties(settings: dict) -> None:
    compile_db_path = get_build_dir(settings) / "compile_commands.json"
    patched_db_path = get_build_dir(settings) / "compile_commands_patched.json"

    with compile_db_path.open("r", encoding="utf-8") as handle:
        commands = json.load(handle)

    includes: set[str] = set()
    defines: set[str] = set()
    compiler_path = ""
    iprefix = ""

    for entry in commands:
        args = entry.get("arguments", [])
        if not args:
            import shlex

            command = entry.get("command", "")
            try:
                args = shlex.split(command)
            except ValueError:
                args = command.split()

        if not args:
            continue

        if not compiler_path and args[0].endswith(
            ("g++", "gcc", "arm-none-eabi-g++", "arm-none-eabi-gcc", ".exe")
        ):
            compiler_path = args[0]

        i = 0
        while i < len(args):
            arg = args[i]
            if arg.startswith("-I"):
                path = arg[2:] if len(arg) > 2 else (args[i + 1] if i + 1 < len(args) else "")
                if path:
                    if not os.path.isabs(path):
                        directory = entry.get("directory", "")
                        if directory:
                            path = os.path.normpath(os.path.join(directory, path))
                    includes.add(path)
                    if len(arg) == 2:
                        i += 1
            elif arg == "-isystem":
                if i + 1 < len(args):
                    includes.add(args[i + 1])
                    i += 1
            elif arg.startswith("-iprefix"):
                iprefix = arg[8:] if len(arg) > 8 else (args[i + 1] if i + 1 < len(args) else "")
                if len(arg) == 8:
                    i += 1
            elif arg.startswith("@") and arg.endswith(".txt"):
                _process_response_file(Path(arg[1:]), includes, defines, iprefix)
            elif arg.startswith("-D"):
                define = arg[2:] if len(arg) > 2 else (args[i + 1] if i + 1 < len(args) else "")
                if define:
                    defines.add(define.strip('"').strip("'"))
                    if len(arg) == 2:
                        i += 1
            i += 1

    sketchbook = get_setting(settings, "arduino.sketchbookPath")
    if sketchbook:
        user_libs = Path(sketchbook) / "libraries"
        if user_libs.is_dir():
            for lib_dir in user_libs.iterdir():
                if not lib_dir.is_dir():
                    continue
                src = lib_dir / "src"
                includes.add(str(src if src.is_dir() else lib_dir))

    build_dir = get_build_dir(settings)
    if build_dir.is_dir():
        includes.add(str(build_dir))
        core_dir = build_dir / "core"
        if core_dir.is_dir():
            includes.add(str(core_dir))

    includes_list = sorted(path for path in includes if Path(path).is_dir())
    includes_list.append("${workspaceFolder}/**")

    project_sources: dict[str, str] = {}
    for root, dirs, files in os.walk(PROJECT_DIR):
        if ".build" in root:
            continue
        for name in files:
            project_sources.setdefault(name, str(Path(root) / name))

    patched_commands = list(commands)
    sketch_dir = os.path.normpath(str(build_dir / "sketch"))
    for entry in commands:
        src_file = entry.get("file", "")
        src_norm = os.path.normpath(src_file)
        if not src_norm.startswith(sketch_dir + os.sep):
            continue
        basename = os.path.basename(src_norm)
        original_name = basename[:-4] if basename.endswith(".ino.cpp") else basename
        original_src = project_sources.get(original_name)
        if original_src and os.path.normpath(original_src) != src_norm:
            patched_entry = dict(entry)
            patched_entry["file"] = original_src
            patched_commands.append(patched_entry)

    with patched_db_path.open("w", encoding="utf-8") as handle:
        json.dump(patched_commands, handle, indent=4)
        handle.write("\n")

    config = {
        "configurations": [
            {
                "name": settings.get("arduino.boardDescription", "Arduino-Pico"),
                "includePath": includes_list,
                "defines": sorted(defines),
                "compilerPath": compiler_path if compiler_path and Path(compiler_path).is_file() else "",
                "compileCommands": str(patched_db_path),
                "cStandard": "c17",
                "cppStandard": "gnu++17",
                "intelliSenseMode": "gcc-arm-none-eabi",
            }
        ],
        "version": 4,
    }

    CPP_PROPS_FILE.parent.mkdir(parents=True, exist_ok=True)
    with CPP_PROPS_FILE.open("w", encoding="utf-8") as handle:
        json.dump(config, handle, indent=4)
        handle.write("\n")

    print(f"  Include paths:    {len(includes_list) - 1}")
    print(f"  Defines:          {len(defines)}")
    print(f"  Compiler:         {compiler_path or 'not found'}")
    print(
        f"  compile_commands: {patched_db_path} "
        f"({len(patched_commands)} entries, {len(patched_commands) - len(commands)} workspace path mappings)"
    )
    print(f"  Output:           {CPP_PROPS_FILE}")


def command_refresh_intellisense(_: argparse.Namespace) -> int:
    settings = load_settings()
    print()
    info("Refreshing IntelliSense configuration...")
    print()
    info(f"arduino-cli: {resolve_cli(settings)}")
    info(f"FQBN: {settings.get('arduino.fqbn', '')}")
    info(f"Sketch: {find_sketch()}")
    print()

    rc = compile_project(settings=settings)
    if rc != 0:
        warn("Compilation failed")
        warn("IntelliSense may be incomplete, continuing anyway...")

    info("Generating compile_commands.json...")
    rc = compile_project(settings=settings, only_compilation_database=True)
    if rc != 0:
        err("Failed to generate compile_commands.json")
        return rc

    compile_db_path = get_build_dir(settings) / "compile_commands.json"
    if not compile_db_path.is_file():
        err("compile_commands.json does not exist after generation")
        return 1

    ok("compile_commands.json generated")
    print()
    info("Generating c_cpp_properties.json...")
    generate_cpp_properties(settings)
    print()
    ok("IntelliSense configuration refreshed")
    print()
    print("  Next steps in VS Code:")
    print("  1. Developer: Reload Window")
    print("  2. C/C++: Rescan Workspace")
    print()
    return 0


def command_build(args: argparse.Namespace) -> int:
    settings = load_settings()
    return compile_project(settings=settings, debug=args.debug, upload=args.upload)


def _windows_bootsel_mounts() -> Iterable[Path]:
    if os.name != "nt":
        return []

    import ctypes

    kernel32 = ctypes.windll.kernel32
    drives_mask = kernel32.GetLogicalDrives()
    results: list[Path] = []

    for letter in string.ascii_uppercase:
        if not (drives_mask & 1):
            drives_mask >>= 1
            continue
        root = f"{letter}:\\"
        volume_name = ctypes.create_unicode_buffer(1024)
        fs_name = ctypes.create_unicode_buffer(1024)
        if kernel32.GetVolumeInformationW(
            ctypes.c_wchar_p(root),
            volume_name,
            len(volume_name),
            None,
            None,
            None,
            fs_name,
            len(fs_name),
        ):
            if volume_name.value in {"RPI-RP2", "RP2350", "RPI-RP2350"}:
                results.append(Path(root))
        drives_mask >>= 1
    return results


def command_upload_uf2(_: argparse.Namespace) -> int:
    settings = load_settings()
    info("Compiling...")
    info(f"  FQBN: {settings.get('arduino.fqbn', '')}")
    rc = compile_project(settings=settings)
    if rc != 0:
        err("Compilation failed")
        return rc

    build_dir = get_build_dir(settings)
    print()
    info("Searching for UF2 file...")
    uf2_files = sorted(build_dir.rglob("*.uf2"))
    if not uf2_files:
        err(f"No .uf2 file found in {build_dir}")
        return 1

    uf2 = uf2_files[0]
    ok(f"Found: {uf2}")
    info("Searching for BOOTSEL drive...")

    mounts: list[Path] = []
    if os.name == "nt":
        mounts = list(_windows_bootsel_mounts())
    else:
        for base in (Path("/media") / os.environ.get("USER", ""), Path("/run/media") / os.environ.get("USER", "")):
            if not base.is_dir():
                continue
            for name in ("RPI-RP2", "RP2350", "RPI-RP2350"):
                candidate = base / name
                if candidate.is_dir():
                    mounts.append(candidate)

    if not mounts:
        err("BOOTSEL drive not found")
        return 1

    mount = mounts[0]
    info(f"Copying to {mount}...")
    shutil.copy2(uf2, mount / uf2.name)
    print()
    ok("UF2 upload finished")
    ok(f"File: {uf2.name} -> {mount}")
    return 0


def command_set_board(args: argparse.Namespace) -> int:
    settings = load_settings()
    selection = args.selection.strip()
    fqbn, desc = selection.split("-", 1)
    settings["arduino.fqbn"] = fqbn.strip()
    settings["arduino.boardDescription"] = desc.strip()
    save_settings(settings)
    ok(f"Board set: {settings['arduino.boardDescription']}")
    ok(f"FQBN: {settings['arduino.fqbn']}")
    return 0


def command_set_port(args: argparse.Namespace) -> int:
    settings = load_settings()
    settings["arduino.uploadPort"] = args.port.strip()
    save_settings(settings)
    ok(f"Port set to: {settings['arduino.uploadPort']}")
    return 0


def command_select_board(_: argparse.Namespace) -> int:
    print()
    print("Select target board:")
    for idx, (fqbn, desc, chip) in enumerate(BOARDS, start=1):
        print(f"  {idx}) {desc} [{chip}]")
    print("  c) Custom FQBN")
    print("  q) Quit")
    print()

    choice = input("Select board: ").strip().lower()
    if choice == "q":
        return 0
    if choice == "c":
        fqbn = input("Enter full FQBN: ").strip()
        desc = input("Enter board description: ").strip() or fqbn
    else:
        try:
            fqbn, desc, _ = BOARDS[int(choice) - 1]
        except (ValueError, IndexError):
            err("Invalid selection")
            return 1

    settings = load_settings()
    settings["arduino.fqbn"] = fqbn
    settings["arduino.boardDescription"] = desc
    save_settings(settings)
    ok(f"Board set: {desc}")
    ok("Run Arduino: Refresh IntelliSense next")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="VS Code Arduino helpers")
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_parser_cmd = subparsers.add_parser("build", help="Compile the sketch")
    build_parser_cmd.add_argument("--debug", action="store_true")
    build_parser_cmd.add_argument("--upload", action="store_true")
    build_parser_cmd.set_defaults(func=command_build)

    refresh_parser = subparsers.add_parser("refresh-intellisense", help="Refresh cpptools config")
    refresh_parser.set_defaults(func=command_refresh_intellisense)

    uf2_parser = subparsers.add_parser("upload-uf2", help="Compile and copy UF2 to BOOTSEL")
    uf2_parser.set_defaults(func=command_upload_uf2)

    set_board_parser = subparsers.add_parser("set-board", help="Set board from VS Code selection")
    set_board_parser.add_argument("--selection", required=True)
    set_board_parser.set_defaults(func=command_set_board)

    set_port_parser = subparsers.add_parser("set-port", help="Set upload port")
    set_port_parser.add_argument("--port", required=True)
    set_port_parser.set_defaults(func=command_set_port)

    select_board_parser = subparsers.add_parser("select-board", help="Interactive board selection")
    select_board_parser.set_defaults(func=command_select_board)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args))
    except SystemExit as exc:
        message = str(exc)
        if message:
            err(message)
        return 1


if __name__ == "__main__":
    sys.exit(main())
