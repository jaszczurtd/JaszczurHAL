#!/usr/bin/env python3
"""Generate the VS Code files of the JaszczurHAL examples and build them.

Each directory under examples/ is an ordinary JaszczurHAL project: its
hal_project_config.h declares targets, features and variants, and its
checked-in .vscode/jaszczurhal.project.json holds the tooling metadata. This
script keeps the derived VS Code files in sync with those two and builds every
configuration through jh-vscode, like any other project.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from typing import Any

from vscode_task_config import write_text_lf


REPO_ROOT = Path(__file__).resolve().parents[1]
EXAMPLES_DIR = REPO_ROOT / "examples"
JH_VSCODE = REPO_ROOT / "vscode" / "entry" / (
    "jh-vscode.cmd" if os.name == "nt" else "jh-vscode"
)
REFERENCE_VSCODE_DIR = REPO_ROOT / "vscode" / "examples"
MANIFEST = Path(".vscode") / "jaszczurhal.project.json"
BUILD_TARGETS = ["rp2040", "rp2350-arm", "rp2350-riscv", "stm32g474", "esp32s3"]


def json_text(data: Any) -> str:
    return json.dumps(data, indent=4, ensure_ascii=False) + "\n"


def workflow() -> Any:
    if str(REPO_ROOT) not in sys.path:
        sys.path.insert(0, str(REPO_ROOT))
    from vscode.runtime import jh_vscode

    return jh_vscode


def target_registry() -> dict[str, dict[str, Any]]:
    from board_registry import tooling_target_registry

    return tooling_target_registry(REPO_ROOT)


def example_dirs() -> list[Path]:
    """Every example project: each directory directly under examples/."""
    return sorted(path for path in EXAMPLES_DIR.iterdir() if path.is_dir())


def read_manifest(example_dir: Path) -> dict[str, Any]:
    path = example_dir / MANIFEST
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"{path}: {error}") from error
    if not isinstance(document, dict):
        raise ValueError(f"{path}: the manifest must be an object")
    return document


def settings_for(example_dir: Path, manifest: dict[str, Any]) -> dict[str, Any]:
    from vscode_task_config import (
        cmake_tools_configure_settings,
        vscode_entry_settings,
    )

    name = example_dir.name
    module = str(manifest["module"])
    target = str(manifest["target"])
    board = str(manifest["board"])
    build_dir = f"${{workspaceFolder}}/../../.build/examples/{name}"
    return {
        "jaszczurhal.buildDir": build_dir,
        "jaszczurhal.verbose": False,
        "jaszczurhal.root": "../..",
        **vscode_entry_settings("../../vscode/entry/jh-vscode"),
        "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
        "C_Cpp.default.compileCommands": f"{build_dir}/compile_commands_patched.json",
        "C_Cpp.errorSquiggles": "enabled",
        "cmake.sourceDirectory": "${workspaceFolder}/../../cmake/jh_firmware_project",
        "cmake.buildDirectory": f"{build_dir}/cmake-tools/{target}-{board}",
        "cmake.generator": "Ninja",
        "cmake.configureSettings": cmake_tools_configure_settings(
            target_registry(),
            target=target,
            board=board,
            module=module,
            jh_root_ref="${workspaceFolder}/../..",
            build_ref=build_dir,
            project_cache=dict(manifest["cmake"]["cache"]),
        ),
        "files.exclude": {"**/.build": True},
        "search.exclude": {"**/.build": True},
    }


def tasks_for(target: str, board: str, project_dir: Path | None) -> dict[str, Any]:
    from vscode_task_config import project_tasks_document

    return project_tasks_document(
        target_registry(), target, board, project_dir=project_dir
    )


def launch_for(name: str) -> dict[str, Any]:
    from vscode_task_config import cortex_debug_launch_document

    executable = f"${{workspaceFolder}}/../../.build/examples/{name}/firmware.elf"
    return cortex_debug_launch_document(executable)


def extensions_for() -> dict[str, Any]:
    from vscode_task_config import extensions_recommendations

    return extensions_recommendations()


def keybindings_for() -> list[dict[str, str]]:
    from vscode_task_config import keybindings_reference

    return keybindings_reference()


def reference_settings() -> dict[str, Any]:
    from vscode_task_config import (
        cmake_tools_configure_settings,
        vscode_entry_settings,
    )

    jh_root = "${workspaceFolder}/../../libraries/JaszczurHAL"
    return {
        "jaszczurhal.root": "../../libraries/JaszczurHAL",
        **vscode_entry_settings(
            "../../libraries/JaszczurHAL/vscode/entry/jh-vscode"
        ),
        "jaszczurhal.buildDir": "${workspaceFolder}/.build",
        "jaszczurhal.verbose": False,
        "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
        "C_Cpp.default.compileCommands": (
            "${workspaceFolder}/.build/compile_commands_patched.json"
        ),
        "C_Cpp.errorSquiggles": "enabled",
        "cmake.sourceDirectory": (
            "${workspaceFolder}/../../libraries/JaszczurHAL/cmake/jh_firmware_project"
        ),
        "cmake.buildDirectory": "${workspaceFolder}/.build/cmake-tools/rp2040-pico",
        "cmake.generator": "Ninja",
        "cmake.configureSettings": cmake_tools_configure_settings(
            target_registry(),
            target="rp2040",
            board="pico",
            module="firmware",
            jh_root_ref=jh_root,
            project_cache={
                "JH_PROJECT_DIR": "${project}",
                "JH_MODULE_NAME": "firmware",
            },
        ),
        "files.exclude": {"**/.build": True},
        "search.exclude": {"**/.build": True},
    }


def reference_template_files() -> dict[str, Any]:
    from vscode_task_config import cortex_debug_launch_document

    return {
        "settings.json": reference_settings(),
        "tasks.json": tasks_for("rp2040", "pico", None),
        "launch.json": cortex_debug_launch_document(
            "${workspaceFolder}/.build/firmware.elf"
        ),
        "extensions.json": extensions_for(),
        "keybindings.reference.json": keybindings_for(),
    }


def example_vscode_files(example_dir: Path) -> dict[str, Any]:
    """VS Code files derived from the example's manifest and header; the
    manifest itself is written by hand."""
    manifest = read_manifest(example_dir)
    findings = workflow().manifest_configuration_findings(
        manifest, str(example_dir / MANIFEST)
    )
    if findings:
        raise ValueError("\n".join(findings))
    return {
        "settings.json": settings_for(example_dir, manifest),
        "tasks.json": tasks_for(
            str(manifest["target"]), str(manifest["board"]), example_dir
        ),
        "launch.json": launch_for(example_dir.name),
        "keybindings.reference.json": keybindings_for(),
        "extensions.json": extensions_for(),
    }


def generated_files() -> dict[Path, Any]:
    files = {
        REFERENCE_VSCODE_DIR / name: data
        for name, data in reference_template_files().items()
    }
    for example_dir in example_dirs():
        for name, data in example_vscode_files(example_dir).items():
            files[example_dir / ".vscode" / name] = data
    return files


def sync_generated_files(*, check: bool) -> int:
    try:
        files = generated_files()
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if check:
        mismatches = [
            path.relative_to(REPO_ROOT).as_posix()
            for path, data in files.items()
            if (path.read_text(encoding="utf-8") if path.is_file() else "")
            != json_text(data)
        ]
        if mismatches:
            print(
                "error: generated VS Code file drift: " + ", ".join(mismatches),
                file=sys.stderr,
            )
            print("Run: scripts/examples_dispatcher.py generate", file=sys.stderr)
            return 1
        return 0
    for path, data in files.items():
        path.parent.mkdir(exist_ok=True)
        write_text_lf(path, json_text(data))
    print(f"generated {len(files)} VS Code files", flush=True)
    return 0


def example_builds(example_dir: Path) -> list[Any]:
    """Every configuration the example builds, from its header and manifest."""
    return workflow().project_builds(example_dir, read_manifest(example_dir))


def manifest_board(example_dir: Path, target: str) -> str:
    """Board the manifest selects for ``target``, ignoring the gitignored
    local selection so every machine builds the same configurations."""
    config = workflow().load_project_config(
        example_dir, target_override=target, use_local_state=False
    )
    return str(config["board"])


def selected_example_dirs(names: list[str]) -> list[Path]:
    if names:
        return [EXAMPLES_DIR / name for name in names]
    return example_dirs()


def command_label(example_dir: Path, target: str, variant: str | None) -> str:
    suffix = f":{variant}" if variant else ""
    return f"{example_dir.name}{suffix}@{target}"


def tail(path: Path, lines: int = 80) -> str:
    try:
        data = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    return "\n".join(data[-lines:])


def dispatcher_log_path(target: str, example_name: str) -> Path:
    return Path(tempfile.gettempdir()) / (
        f"jh_examples_dispatcher_{target}_{example_name}.log"
    )


def run_one_example(
    example_dir: Path,
    target: str,
    board: str,
    variants: list[str | None],
    parallel_level: int,
    verbose: bool,
) -> tuple[bool, str, Path, float]:
    started = time.monotonic()
    log_path = dispatcher_log_path(target, example_dir.name)
    label = f"{example_dir.name}@{target}"
    base_command = [
        str(JH_VSCODE),
        "build",
        "--project",
        str(example_dir),
        "--target",
        target,
        "--board",
        board,
    ]
    with log_path.open("w", encoding="utf-8") as log:
        for variant in variants:
            command = [*base_command, "--variant", variant] if variant else base_command
            if verbose:
                print("+ " + " ".join(command), file=log)
            result = subprocess.run(
                command,
                cwd=REPO_ROOT,
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
                env={
                    **os.environ,
                    "CMAKE_BUILD_PARALLEL_LEVEL": str(parallel_level),
                    "JH_VSCODE_MEMORY_OVERVIEW": "0",
                },
                check=False,
            )
            if result.returncode != 0:
                return (
                    False,
                    command_label(example_dir, target, variant),
                    log_path,
                    time.monotonic() - started,
                )
    return True, label, log_path, time.monotonic() - started


def build(args: argparse.Namespace) -> int:
    groups: list[tuple[Path, str, list[str | None]]] = []
    skipped: list[str] = []
    try:
        for example_dir in selected_example_dirs(args.example or []):
            if not example_dir.is_dir():
                print(f"error: missing example directory: {example_dir}", file=sys.stderr)
                return 1
            variants = [
                build.variant.id if build.variant else None
                for build in example_builds(example_dir)
                if build.target.id == args.target
            ]
            if not variants:
                skipped.append(command_label(example_dir, args.target, None))
                continue
            groups.append(
                (example_dir, manifest_board(example_dir, args.target), variants)
            )
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    for item in skipped:
        print(f"skip {item} (the example does not build for this target)", flush=True)
    if not groups:
        print(f"no examples to build for target {args.target}")
        return 0

    compiler_budget = max(1, int(args.jobs or 1))
    workers = min(2, compiler_budget, len(groups))
    parallel_level = max(1, compiler_budget // workers)
    print(
        f"scheduler: {workers} project worker(s), "
        f"{parallel_level} compiler job(s) each",
        flush=True,
    )
    failures: list[tuple[str, Path]] = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {
            pool.submit(
                run_one_example,
                example_dir,
                args.target,
                board,
                variants,
                parallel_level,
                args.verbose,
            ): example_dir
            for example_dir, board, variants in groups
        }
        for future in as_completed(futures):
            ok, label, log_path, elapsed = future.result()
            if ok:
                print(f"pass {label} ({elapsed:.1f}s)", flush=True)
            else:
                print(
                    f"fail {label} ({elapsed:.1f}s, log: {log_path})",
                    flush=True,
                )
                failures.append((label, log_path))

    if failures:
        label, log_path = failures[0]
        print(f"\nfirst failure: {label}", file=sys.stderr)
        print(tail(log_path), file=sys.stderr)
        return 1
    configurations = sum(len(variants) for _, _, variants in groups)
    print(
        f"built {configurations} configuration(s) from {len(groups)} "
        f"example project(s) for {args.target}",
        flush=True,
    )
    return 0


def list_examples() -> int:
    try:
        for example_dir in example_dirs():
            by_target: dict[str, list[str]] = {}
            for item in example_builds(example_dir):
                by_target.setdefault(item.target.id, []).append(
                    item.variant.id if item.variant else "base"
                )
            print(
                f"{example_dir.name}: "
                + "; ".join(
                    f"{target} ({', '.join(variants)})"
                    for target, variants in by_target.items()
                )
            )
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser(
        "generate",
        help="Write the derived VS Code files of every example and vscode/examples.",
    )
    sub.add_parser(
        "check",
        help="Fail when a derived VS Code file differs from its manifest and header.",
    )
    sub.add_parser("list", help="List every example with its targets and variants.")
    build_parser = sub.add_parser(
        "build", help="Build every configuration of the examples for one target."
    )
    build_parser.add_argument("--target", required=True, choices=BUILD_TARGETS)
    build_parser.add_argument(
        "--example", action="append", help="Build only this example directory name."
    )
    build_parser.add_argument("--jobs", type=int, default=1)
    build_parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    if args.command == "generate":
        return sync_generated_files(check=False)
    if args.command == "check":
        return sync_generated_files(check=True)
    if args.command == "build":
        return build(args)
    if args.command == "list":
        return list_examples()
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
