#!/usr/bin/env python3
"""Build and verify the isolated ESP-IDF Phase 0 integration spike."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any, Mapping

import component_manager


PROJECT_NAME = "jh_esp_idf_phase0"
MANIFEST_NAME = "jh_esp_idf_phase0_artifacts.json"
LOG_NAME = "build.log"
GENERATED_DIR_NAME = "generated/jaszczurhal"
GENERATED_CONFIG_NAME = "jh_esp_idf_phase0_config.h"
EXPORT_KEY = re.compile(r"^[A-Z][A-Z0-9_]*$")


class Phase0Error(RuntimeError):
    """Raised when the Phase 0 build or artifact contract is invalid."""


def _inside(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def resolve_build_dir(repo_root: Path, requested: str, target: str) -> Path:
    build_root = (repo_root / ".build").resolve()
    candidate = (
        Path(requested).expanduser()
        if requested
        else build_root / "esp-idf" / "phase0" / target
    )
    if not candidate.is_absolute():
        candidate = repo_root / candidate
    resolved = candidate.resolve(strict=False)
    if resolved == build_root or not _inside(resolved, build_root):
        raise Phase0Error(f"Build directory must be below {build_root}: {resolved}")
    return resolved


def parse_exported_environment(
    output: str,
    baseline: Mapping[str, str],
    *,
    platform: str = sys.platform,
) -> dict[str, str]:
    environment = dict(baseline)
    old_path_token = "%PATH%" if platform == "win32" else "$PATH"
    for line in output.splitlines():
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if not EXPORT_KEY.fullmatch(key):
            raise Phase0Error(f"Invalid ESP-IDF environment key: {key!r}")
        if key == "PATH":
            value = value.replace(old_path_token, baseline.get("PATH", ""))
        environment[key] = value
    if "IDF_PYTHON_ENV_PATH" not in environment:
        raise Phase0Error("ESP-IDF export did not provide IDF_PYTHON_ENV_PATH")
    return environment


def idf_python(environment: Mapping[str, str], *, platform: str = sys.platform) -> Path:
    root = Path(environment["IDF_PYTHON_ENV_PATH"])
    if platform == "win32":
        return root / "Scripts" / "python.exe"
    return root / "bin" / "python"


def prepare_sdk(repo_root: Path, directory_override: str) -> Path:
    try:
        directory = component_manager.ensure_git_component(
            "esp-idf",
            repo_root,
            verify_only=True,
            directory_override=directory_override,
        )
        component_manager.ensure_esp_idf_tools(
            repo_root, directory, verify_only=True
        )
        return directory
    except component_manager.ComponentError:
        directory = component_manager.ensure_git_component(
            "esp-idf",
            repo_root,
            verify_only=False,
            directory_override=directory_override,
        )
        component_manager.ensure_esp_idf_tools(
            repo_root, directory, verify_only=False
        )
        return directory


def exported_environment(idf_dir: Path) -> dict[str, str]:
    command = (
        sys.executable,
        str(idf_dir / "tools/idf_tools.py"),
        "--idf-path",
        str(idf_dir),
        "export",
        "--format",
        "key-value",
    )
    completed = subprocess.run(
        command, check=False, capture_output=True, text=True, env=os.environ.copy()
    )
    if completed.stderr:
        print(completed.stderr, file=sys.stderr, end="")
    if completed.returncode:
        raise Phase0Error("ESP-IDF environment export failed")
    environment = parse_exported_environment(completed.stdout, os.environ)
    environment["IDF_PATH"] = str(idf_dir)
    python = idf_python(environment)
    if not python.is_file():
        raise Phase0Error(f"ESP-IDF Python interpreter is missing: {python}")
    return environment


def _load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise Phase0Error(f"Cannot read JSON artifact {path}: {error}") from error
    if not isinstance(value, dict):
        raise Phase0Error(f"JSON artifact must contain an object: {path}")
    return value


def _artifact_path(build_dir: Path, value: str) -> tuple[Path, str]:
    relative = Path(value)
    path = relative if relative.is_absolute() else build_dir / relative
    resolved = path.resolve(strict=False)
    if not _inside(resolved, build_dir.resolve()):
        raise Phase0Error(f"Artifact escapes the build directory: {value}")
    if not resolved.is_file():
        raise Phase0Error(f"Missing build artifact: {resolved}")
    if resolved.stat().st_size == 0:
        raise Phase0Error(f"Build artifact is empty: {resolved}")
    return resolved, resolved.relative_to(build_dir.resolve()).as_posix()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def generate_component_config(
    build_dir: Path,
    *,
    target: str,
    idf_version: str,
    idf_commit: str,
) -> Path:
    """Generate the Phase 0 C/CMake handoff consumed by the HAL component."""
    if not re.fullmatch(r"[a-z0-9]+", target):
        raise Phase0Error(f"Invalid Phase 0 target identifier: {target!r}")
    if not re.fullmatch(r"[0-9a-f]{40}", idf_commit):
        raise Phase0Error(f"Invalid ESP-IDF commit: {idf_commit!r}")

    generated_dir = build_dir / GENERATED_DIR_NAME
    generated_dir.mkdir(parents=True, exist_ok=True)
    contract_symbol = f"jh_esp_idf_phase0_contract_{target}_{idf_commit[:12]}"
    config = "\n".join(
        [
            "#pragma once",
            "/* Generated by build_esp_idf_phase0.py; do not edit. */",
            "#include <stdint.h>",
            f'#define JH_ESP_IDF_PHASE0_TARGET "{target}"',
            f'#define JH_ESP_IDF_PHASE0_IDF_VERSION "{idf_version}"',
            f'#define JH_ESP_IDF_PHASE0_IDF_COMMIT "{idf_commit}"',
            "#define JH_ESP_IDF_PHASE0_FLASH_BYTES UINT32_C(4194304)",
            "",
        ]
    )
    link_header = "\n".join(
        [
            "#pragma once",
            "/* Generated by build_esp_idf_phase0.py; do not edit. */",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif",
            f"void {contract_symbol}(void);",
            "#ifdef __cplusplus",
            "}",
            "#endif",
            f"#define JH_BOARD_CONTRACT_SYMBOL {contract_symbol}",
            "",
        ]
    )
    link_definition = "\n".join(
        [
            "/* Generated by build_esp_idf_phase0.py; do not edit. */",
            '#include "jh_link_contract.h"',
            "void JH_BOARD_CONTRACT_SYMBOL(void) {}",
            "",
        ]
    )
    (generated_dir / GENERATED_CONFIG_NAME).write_text(config, encoding="utf-8")
    (generated_dir / "jh_link_contract.h").write_text(
        link_header, encoding="utf-8"
    )
    (generated_dir / "jh_link_contract_definition.c").write_text(
        link_definition, encoding="utf-8"
    )
    return generated_dir


def _compiled_source(entries: list[Any], suffix: str) -> bool:
    normalized_suffix = suffix.replace("\\", "/")
    return any(
        isinstance(entry, dict)
        and str(entry.get("file", "")).replace("\\", "/").endswith(
            normalized_suffix
        )
        for entry in entries
    )


def validate_artifacts(
    build_dir: Path,
    *,
    target: str,
    idf_version: str,
    idf_commit: str,
    write_manifest: bool = True,
) -> dict[str, Any]:
    build_dir = build_dir.resolve()
    required = {
        "applicationElf": f"{PROJECT_NAME}.elf",
        "applicationMap": f"{PROJECT_NAME}.map",
        "applicationBinary": f"{PROJECT_NAME}.bin",
        "sdkconfig": "sdkconfig",
        "flasherArgs": "flasher_args.json",
        "projectDescription": "project_description.json",
        "compileCommands": "compile_commands.json",
        "buildLog": LOG_NAME,
        "generatedConfig": f"{GENERATED_DIR_NAME}/{GENERATED_CONFIG_NAME}",
        "generatedLinkContract": f"{GENERATED_DIR_NAME}/jh_link_contract.h",
    }
    artifacts: dict[str, str] = {}
    for name, relative in required.items():
        _, artifacts[name] = _artifact_path(build_dir, relative)

    description = _load_object(build_dir / required["projectDescription"])
    if description.get("target") != target:
        raise Phase0Error(
            "project_description.json target mismatch: "
            f"expected {target!r}, found {description.get('target')!r}"
        )
    if description.get("project_name") != PROJECT_NAME:
        raise Phase0Error(
            "project_description.json project mismatch: "
            f"expected {PROJECT_NAME!r}, found {description.get('project_name')!r}"
        )
    for key, expected in (
        ("app_elf", f"{PROJECT_NAME}.elf"),
        ("app_bin", f"{PROJECT_NAME}.bin"),
    ):
        if description.get(key) != expected:
            raise Phase0Error(
                f"project_description.json {key} mismatch: "
                f"expected {expected!r}, found {description.get(key)!r}"
            )

    build_components = description.get("build_components")
    if not isinstance(build_components, list) or "jaszczurhal" not in (
        str(component).lower() for component in build_components
    ):
        raise Phase0Error(
            "project_description.json does not include the jaszczurhal component"
        )

    try:
        compile_commands = json.loads(
            (build_dir / required["compileCommands"]).read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise Phase0Error(f"Invalid compile_commands.json: {error}") from error
    if not isinstance(compile_commands, list) or not compile_commands:
        raise Phase0Error("compile_commands.json must contain build entries")
    required_sources = (
        "/main/phase0_main.c",
        "/src/hal/core/jh_handle_pool.cpp",
        "/generated/jaszczurhal/jh_link_contract_definition.c",
    )
    missing_sources = [
        source
        for source in required_sources
        if not _compiled_source(compile_commands, source)
    ]
    if missing_sources:
        raise Phase0Error(
            "compile_commands.json is missing integration sources: "
            + ", ".join(missing_sources)
        )

    generated_config = (build_dir / required["generatedConfig"]).read_text(
        encoding="utf-8"
    )
    for expected in (
        f'#define JH_ESP_IDF_PHASE0_TARGET "{target}"',
        f'#define JH_ESP_IDF_PHASE0_IDF_VERSION "{idf_version}"',
        f'#define JH_ESP_IDF_PHASE0_IDF_COMMIT "{idf_commit}"',
        "#define JH_ESP_IDF_PHASE0_FLASH_BYTES UINT32_C(4194304)",
    ):
        if expected not in generated_config:
            raise Phase0Error(
                f"Generated Phase 0 component config is missing: {expected}"
            )

    flasher = _load_object(build_dir / required["flasherArgs"])
    extra_args = flasher.get("extra_esptool_args")
    if not isinstance(extra_args, dict) or extra_args.get("chip") != target:
        raise Phase0Error("flasher_args.json does not select the exact target")
    flash_settings = flasher.get("flash_settings")
    if not isinstance(flash_settings, dict) or flash_settings.get(
        "flash_size"
    ) != "4MB":
        raise Phase0Error("flasher_args.json does not select the 4 MB flash")
    flash_files = flasher.get("flash_files")
    if not isinstance(flash_files, dict) or not flash_files:
        raise Phase0Error("flasher_args.json has no flash_files object")

    flash_images = []
    observed_paths: set[str] = set()
    observed_offsets: set[int] = set()
    for raw_offset, raw_path in flash_files.items():
        if not isinstance(raw_offset, str) or not isinstance(raw_path, str):
            raise Phase0Error("flasher_args.json flash_files must map strings")
        try:
            offset = int(raw_offset, 0)
        except ValueError as error:
            raise Phase0Error(f"Invalid flash offset: {raw_offset!r}") from error
        if offset < 0 or offset in observed_offsets:
            raise Phase0Error(f"Invalid or duplicate flash offset: {raw_offset!r}")
        path, relative = _artifact_path(build_dir, raw_path)
        if relative in observed_paths:
            raise Phase0Error(f"Duplicate flash image: {relative}")
        observed_offsets.add(offset)
        observed_paths.add(relative)
        flash_images.append(
            {
                "offset": f"0x{offset:x}",
                "path": relative,
                "size": path.stat().st_size,
                "sha256": _sha256(path),
            }
        )

    expected_flash_paths = {
        f"{PROJECT_NAME}.bin",
        "bootloader/bootloader.bin",
        "partition_table/partition-table.bin",
    }
    missing = sorted(expected_flash_paths - observed_paths)
    if missing:
        raise Phase0Error(
            "flasher_args.json is missing required images: " + ", ".join(missing)
        )

    flash_images.sort(key=lambda item: int(item["offset"], 0))
    manifest = {
        "schemaVersion": 1,
        "project": PROJECT_NAME,
        "target": target,
        "espIdf": {"version": idf_version, "commit": idf_commit},
        "integration": {
            "component": "jaszczurhal",
            "sharedSource": "src/hal/core/jh_handle_pool.cpp",
        },
        "flashImages": flash_images,
        "artifacts": artifacts,
    }
    if write_manifest:
        (build_dir / MANIFEST_NAME).write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    return manifest


def build(
    repo_root: Path,
    project_dir: Path,
    build_dir: Path,
    idf_dir: Path,
    target: str,
    generated_dir: Path,
    environment: Mapping[str, str],
) -> None:
    build_environment = dict(environment)
    build_environment["JH_ESP_IDF_PHASE0"] = "1"
    build_environment["JH_PHASE0_GENERATED_DIR"] = str(generated_dir)
    command = (
        str(idf_python(environment)),
        str(idf_dir / "tools/idf.py"),
        "-C",
        str(project_dir),
        "-B",
        str(build_dir),
        "-D",
        f"IDF_TARGET={target}",
        "-D",
        f"SDKCONFIG={build_dir / 'sdkconfig'}",
        "-D",
        "CMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "build",
    )
    completed = subprocess.run(
        command,
        cwd=repo_root,
        env=build_environment,
        check=False,
        capture_output=True,
        text=True,
    )
    build_dir.mkdir(parents=True, exist_ok=True)
    log = completed.stdout + completed.stderr
    (build_dir / LOG_NAME).write_text(log, encoding="utf-8")
    print(log, end="")
    if completed.returncode:
        raise Phase0Error(
            f"ESP-IDF Phase 0 build failed with exit code {completed.returncode}"
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--idf-dir", default="")
    parser.add_argument("--target", default="esp32s3", choices=("esp32s3",))
    parser.add_argument("--output", default="")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument(
        "--artifacts-only",
        action="store_true",
        help="validate an existing build without preparing the SDK or rebuilding",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    repo_root = arguments.repo_root.resolve()
    project_dir = repo_root / "tests/fixtures/esp_idf_phase0"
    try:
        config = component_manager.parse_config(
            repo_root / "third_party/esp_idf_version.conf"
        )
        component_manager.require_values(
            config,
            ("ESP_IDF_REF", "ESP_IDF_VERSION", "ESP_IDF_TARGETS"),
            repo_root / "third_party/esp_idf_version.conf",
        )
        if arguments.target not in config["ESP_IDF_TARGETS"].split(","):
            raise Phase0Error(
                f"Target {arguments.target!r} is not enabled by the ESP-IDF pin"
            )
        build_dir = resolve_build_dir(
            repo_root, arguments.output, arguments.target
        )
        if arguments.clean and arguments.artifacts_only:
            raise Phase0Error("--clean cannot be combined with --artifacts-only")
        if arguments.clean and build_dir.exists():
            shutil.rmtree(build_dir)
        if not arguments.artifacts_only:
            directory_override = arguments.idf_dir or os.environ.get(
                "JH_ESP_IDF_DIR", ""
            )
            idf_dir = prepare_sdk(repo_root, directory_override)
            environment = exported_environment(idf_dir)
            generated_dir = generate_component_config(
                build_dir,
                target=arguments.target,
                idf_version=config["ESP_IDF_VERSION"],
                idf_commit=config["ESP_IDF_REF"],
            )
            build(
                repo_root,
                project_dir,
                build_dir,
                idf_dir,
                arguments.target,
                generated_dir,
                environment,
            )
        manifest = validate_artifacts(
            build_dir,
            target=arguments.target,
            idf_version=config["ESP_IDF_VERSION"],
            idf_commit=config["ESP_IDF_REF"],
        )
    except (
        OSError,
        ValueError,
        Phase0Error,
        component_manager.ComponentError,
    ) as error:
        print(f"build_esp_idf_phase0.py: {error}", file=sys.stderr)
        return 1

    print(
        f"ESP-IDF Phase 0 verified: {manifest['target']} with "
        f"{len(manifest['flashImages'])} flash images in {build_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
