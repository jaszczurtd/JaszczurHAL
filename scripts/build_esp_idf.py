#!/usr/bin/env python3
"""Build, validate, and flash a JaszczurHAL ESP-IDF firmware project."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any, Mapping, Sequence

import component_manager
import generate_board_config
import generate_hal_features


MANIFEST_NAME = "jh_esp_idf_artifacts.json"
LOG_NAME = "build.log"
FLASH_LOG_NAME = "flash.log"
FAILURE_DIAGNOSTIC_NAME = "jh_esp_idf_failure.txt"
GENERATED_DIR_NAME = "generated/jaszczurhal"
GENERATED_CONFIG_NAME = "jh_board_config.h"
PROJECT_CONFIG_CMAKE_NAME = "jh_esp_idf_project.cmake"
PROJECT_CONFIG_JSON_NAME = "jh_esp_idf_project.json"
TOOLCHAIN_PROVENANCE_NAME = "jh_esp_idf_toolchain.json"
SUPPLY_CHAIN_SNAPSHOT = "security/esp_idf_tools.json"
SDKCONFIG_DEFAULTS_NAME = "sdkconfig.defaults"
DEFAULT_TARGET = "esp32s3"
DEFAULT_BOARD = "waveshare-esp32-s3-zero"
EXPORT_KEY = re.compile(r"^[A-Z][A-Z0-9_]*$")
PROJECT_NAME_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_-]*$")
DEFINITION_NAME_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
FEATURE_DEFINITION_PATTERN = re.compile(
    r"^(HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)(?:=1)?$"
)
SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx", ".s", ".S"})
PARTITION_PROFILE_SYMBOLS = (
    "CONFIG_PARTITION_TABLE_SINGLE_APP",
    "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE",
    "CONFIG_PARTITION_TABLE_TWO_OTA",
    "CONFIG_PARTITION_TABLE_TWO_OTA_LARGE",
    "CONFIG_PARTITION_TABLE_CUSTOM",
    "CONFIG_PARTITION_TABLE_SINGLE_APP_ENCRYPTED_NVS",
    "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE_ENC_NVS",
    "CONFIG_PARTITION_TABLE_TWO_OTA_ENCRYPTED_NVS",
    "CONFIG_PARTITION_TABLE_SINGLE_APP_TEE",
    "CONFIG_PARTITION_TABLE_TWO_OTA_TEE",
)
ESP_IDF_BASE_SOURCES = (
    "src/hal/core/hal_assert.cpp",
    "src/hal/core/hal_config.cpp",
    "src/hal/core/jh_handle_pool.cpp",
    "src/hal/debug/hal_debug_format.cpp",
    "src/hal/serial/hal_serial.cpp",
    "src/hal/system/hal_board.cpp",
    "src/hal/timers/hal_timer.cpp",
    "src/hal/timers/hal_timer_ext.cpp",
    "src/hal_app_entry.cpp",
    "src/hal/impl/esp32/hal_adc.cpp",
    "src/hal/impl/esp32/hal_esp32_build_config.cpp",
    "src/hal/impl/esp32/hal_gpio.cpp",
    "src/hal/impl/esp32/hal_pwm.cpp",
    "src/hal/impl/esp32/hal_serial.cpp",
    "src/hal/impl/esp32/hal_sync.cpp",
    "src/hal/impl/esp32/hal_system.cpp",
    "src/hal/impl/esp32/hal_timer.cpp",
    "src/hal/impl/esp32/jh_esp32_fault.cpp",
    "src/hal/impl/esp32/jh_esp32_ledc.cpp",
)
ESP_IDF_TARGET_SOURCES = {
    "HAL_ENABLE_BLE": (
        "src/hal/impl/esp32/jh_esp32_nvs_runtime.cpp",
        "src/hal/impl/esp32/jh_ble_nimble_backend.c",
    ),
    "HAL_ENABLE_BLUETOOTH_GAMEPAD": (
        "src/hal/bluetooth/jh_bluetooth_gamepad_parser.c",
        "src/hal/impl/esp32/jh_esp32_nvs_runtime.cpp",
        "src/hal/impl/esp32/jh_gamepad_bluedroid_backend.c",
    ),
    "HAL_ENABLE_I2C": (
        "src/hal/impl/esp32/hal_i2c.cpp",
    ),
    "HAL_ENABLE_I2C_SLAVE": (
        "src/hal/impl/esp32/hal_i2c_slave.cpp",
    ),
    "HAL_ENABLE_NETWORK_CORE": (
        "src/hal/impl/esp32/jh_esp32_nvs_runtime.cpp",
        "src/hal/impl/esp32/esp32_network_backend.cpp",
    ),
    "HAL_ENABLE_OTA": (
        "src/hal/impl/esp32/esp32_secure_random.cpp",
        "src/hal/impl/esp32/hal_ota.cpp",
    ),
    "HAL_ENABLE_PCNT": ("src/hal/impl/esp32/hal_pcnt.cpp",),
    "HAL_ENABLE_PWM_FREQ": (
        "src/hal/impl/esp32/hal_pwm_freq.cpp",
    ),
    "HAL_ENABLE_RGB_LED": (
        "src/hal/impl/esp32/hal_rgb_led.cpp",
    ),
    "HAL_ENABLE_SPI": (
        "src/hal/impl/esp32/hal_spi.cpp",
    ),
    "HAL_ENABLE_TCP": (
        "src/hal/impl/esp32/esp32_network_sockets.cpp",
    ),
    "HAL_ENABLE_TIME": (
        "src/hal/impl/esp32/hal_time.cpp",
    ),
    "HAL_ENABLE_TLS": (
        "src/hal/impl/esp32/esp32_secure_random.cpp",
    ),
    "HAL_ENABLE_UDP": (
        "src/hal/impl/esp32/esp32_network_sockets.cpp",
    ),
    "HAL_ENABLE_UART": ("src/hal/impl/esp32/hal_uart.cpp",),
    "HAL_ENABLE_WIREGUARD": (
        "src/hal/impl/esp32/esp32_secure_random.cpp",
        "src/hal/impl/esp32/esp32_lwip_extension_port.cpp",
    ),
}
ESP_IDF_PUBLIC_COMPONENT_DEPENDENCIES = ("freertos",)
ESP_IDF_BASE_PRIVATE_COMPONENT_DEPENDENCIES = (
    "app_update",
    "esp_adc",
    "esp_driver_gpio",
    "esp_driver_gptimer",
    "esp_driver_ledc",
    "esp_driver_tsens",
    "esp_driver_usb_serial_jtag",
    "esp_hw_support",
    "esp_psram",
    "esp_rom",
    "esp_system",
    "esp_timer",
    "heap",
    "vfs",
)
ESP_IDF_FEATURE_COMPONENT_DEPENDENCIES = {
    "HAL_ENABLE_BLE": ("bt", "nvs_flash"),
    "HAL_ENABLE_BLUETOOTH_GAMEPAD": ("bt", "esp_hid", "nvs_flash"),
    "HAL_ENABLE_I2C": ("esp_driver_i2c",),
    "HAL_ENABLE_I2C_SLAVE": ("esp_driver_i2c",),
    "HAL_ENABLE_NETWORK_CORE": (
        "esp_event",
        "esp_netif",
        "esp_wifi",
        "lwip",
        "nvs_flash",
    ),
    "HAL_ENABLE_OTA": ("app_update", "esp_partition"),
    "HAL_ENABLE_PCNT": ("esp_driver_pcnt",),
    "HAL_ENABLE_RGB_LED": ("esp_driver_rmt",),
    "HAL_ENABLE_SPI": ("esp_driver_spi",),
    "HAL_ENABLE_UART": ("esp_driver_uart",),
}
ESP_IDF_MANAGED_DEPENDENCIES = {
    "bearssl": ("jh_bearssl",),
}
GENERATED_BOARD_CONTRACT_INPUTS = (
    "jh_board_resolved.json",
    "jh_board_config.cmake",
    "jh_board_config.h",
    "jh_link_contract.h",
    "jh_link_contract_definition.c",
    "jh_link_contract_reference.c",
)
IDF_TARGET_COMPILER_TOOLS = {
    "esp32": "xtensa-esp-elf",
    "esp32s3": "xtensa-esp-elf",
}


class EspIdfError(RuntimeError):
    """Raised when the ESP-IDF build or artifact contract is invalid."""


def _canonical_path(path: Path) -> Path:
    """Return one filesystem identity for lexical and Windows 8.3 aliases."""
    return path.resolve(strict=False)


def _inside(path: Path, parent: Path) -> bool:
    try:
        _canonical_path(path).relative_to(_canonical_path(parent))
        return True
    except ValueError:
        return False


def _relative_path(path: Path, parent: Path) -> str:
    """Return a stable relative path after resolving filesystem aliases."""
    return _canonical_path(path).relative_to(_canonical_path(parent)).as_posix()


def resolve_project_dir(requested: Path) -> Path:
    project_dir = requested.expanduser().resolve()
    if not project_dir.is_dir():
        raise EspIdfError(f"Project directory does not exist: {project_dir}")
    return project_dir


def resolve_build_dir(
    repo_root: Path,
    project_dir: Path,
    requested: str,
    target: str,
    board: str,
) -> Path:
    project_build_root = (project_dir / ".build").resolve(strict=False)
    repository_build_root = (repo_root / ".build").resolve(strict=False)
    candidate = (
        Path(requested).expanduser()
        if requested
        else project_build_root / "esp-idf" / target / board
    )
    if not candidate.is_absolute():
        candidate = project_dir / candidate
    resolved = candidate.resolve(strict=False)
    allowed_roots = (project_build_root, repository_build_root)
    if any(resolved == root for root in allowed_roots) or not any(
        _inside(resolved, root) for root in allowed_roots
    ):
        roots = ", ".join(str(root) for root in allowed_roots)
        raise EspIdfError(
            f"Build directory must be below a managed .build root ({roots}): "
            f"{resolved}"
        )
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
            raise EspIdfError(f"Invalid ESP-IDF environment key: {key!r}")
        if key == "PATH":
            value = value.replace(old_path_token, baseline.get("PATH", ""))
        environment[key] = value
    if "IDF_PYTHON_ENV_PATH" not in environment:
        raise EspIdfError("ESP-IDF export did not provide IDF_PYTHON_ENV_PATH")
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
        raise EspIdfError("ESP-IDF environment export failed")
    environment = parse_exported_environment(completed.stdout, os.environ)
    environment["IDF_PATH"] = str(idf_dir)
    python = idf_python(environment)
    if not python.is_file():
        raise EspIdfError(f"ESP-IDF Python interpreter is missing: {python}")
    return environment


def _load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise EspIdfError(f"Cannot read JSON file {path}: {error}") from error
    if not isinstance(value, dict):
        raise EspIdfError(f"JSON file must contain an object: {path}")
    return value


def _artifact_path(build_dir: Path, value: str) -> tuple[Path, str]:
    relative = Path(value)
    path = relative if relative.is_absolute() else build_dir / relative
    resolved = _canonical_path(path)
    if not _inside(resolved, build_dir):
        raise EspIdfError(f"Artifact escapes the build directory: {value}")
    if not resolved.is_file():
        raise EspIdfError(f"Missing build artifact: {resolved}")
    if resolved.stat().st_size == 0:
        raise EspIdfError(f"Build artifact is empty: {resolved}")
    return resolved, _relative_path(resolved, build_dir)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _normalized_lf_sha256(path: Path) -> str:
    """Hash text while treating Git's CRLF checkout as upstream LF."""
    content = path.read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(content).hexdigest()


def _snapshot_tool_entries(
    snapshot: Mapping[str, Any],
    key: str,
    *,
    require_source_license: bool,
) -> list[dict[str, str]]:
    raw_entries = snapshot.get(key)
    if not isinstance(raw_entries, list) or not raw_entries:
        raise EspIdfError(f"ESP-IDF supply-chain snapshot has no {key}")
    normalization = snapshot.get("licenseNormalization", {})
    if not isinstance(normalization, dict) or not all(
        isinstance(source, str)
        and source
        and isinstance(license_id, str)
        and license_id
        for source, license_id in normalization.items()
    ):
        raise EspIdfError(
            "ESP-IDF supply-chain snapshot has invalid license normalization"
        )

    entries: list[dict[str, str]] = []
    observed: set[str] = set()
    required = ["name", "version", "license", "upstream"]
    if require_source_license:
        required.append("sourceLicense")
    for index, raw_entry in enumerate(raw_entries):
        if not isinstance(raw_entry, dict) or not all(
            isinstance(raw_entry.get(field), str) and raw_entry[field]
            for field in required
        ):
            raise EspIdfError(
                f"ESP-IDF supply-chain snapshot {key}[{index}] is invalid"
            )
        entry = {field: str(raw_entry[field]) for field in required}
        name = entry["name"]
        if name in observed:
            raise EspIdfError(
                f"ESP-IDF supply-chain snapshot repeats {key} entry {name!r}"
            )
        observed.add(name)
        if require_source_license:
            expected_license = normalization.get(
                entry["sourceLicense"], entry["sourceLicense"]
            )
            if entry["license"] != expected_license:
                raise EspIdfError(
                    f"ESP-IDF supply-chain snapshot license mismatch for "
                    f"{name}: expected {expected_license!r}, found "
                    f"{entry['license']!r}"
                )
        entries.append(entry)
    return entries


def load_supply_chain_contract(
    repo_root: Path,
    *,
    idf_version: str,
    idf_commit: str,
    idf_target: str,
) -> dict[str, Any]:
    path = repo_root / SUPPLY_CHAIN_SNAPSHOT
    snapshot = _load_object(path)
    if snapshot.get("schemaVersion") != 2:
        raise EspIdfError("ESP-IDF supply-chain snapshot has unsupported schema")
    esp_idf = snapshot.get("espIdf")
    if not isinstance(esp_idf, dict):
        raise EspIdfError("ESP-IDF supply-chain snapshot has no espIdf contract")
    for key, expected in (("version", idf_version), ("commit", idf_commit)):
        if esp_idf.get(key) != expected:
            raise EspIdfError(
                f"ESP-IDF supply-chain snapshot {key} mismatch: expected "
                f"{expected!r}, found {esp_idf.get(key)!r}"
            )
    targets = esp_idf.get("targets")
    if (
        not isinstance(targets, list)
        or not all(isinstance(item, str) and item for item in targets)
        or len(targets) != len(set(targets))
    ):
        raise EspIdfError("ESP-IDF supply-chain snapshot has invalid targets")
    if idf_target not in targets:
        raise EspIdfError(
            f"ESP-IDF supply-chain snapshot does not cover target {idf_target!r}"
        )
    tools_digest = esp_idf.get("toolsJsonSha256")
    if not isinstance(tools_digest, str) or not re.fullmatch(
        r"[0-9a-f]{64}", tools_digest
    ):
        raise EspIdfError(
            "ESP-IDF supply-chain snapshot has invalid tools.json digest"
        )
    binary_tools = _snapshot_tool_entries(
        snapshot, "binaryTools", require_source_license=True
    )
    python_tools = _snapshot_tool_entries(
        snapshot, "pythonTools", require_source_license=False
    )
    if not any(entry["name"] == "esptool" for entry in python_tools):
        raise EspIdfError(
            "ESP-IDF supply-chain snapshot has no esptool Python distribution"
        )
    provenance = {
        "snapshot": {
            "path": SUPPLY_CHAIN_SNAPSHOT,
            "schemaVersion": snapshot["schemaVersion"],
            "sha256": _sha256(path),
        },
        "binaryTools": [
            {
                "name": entry["name"],
                "version": entry["version"],
                "license": entry["license"],
            }
            for entry in binary_tools
        ],
        "pythonTools": [
            {
                "name": entry["name"],
                "version": entry["version"],
                "license": entry["license"],
            }
            for entry in python_tools
        ],
    }
    return {
        "idfTarget": idf_target,
        "snapshot": snapshot,
        "snapshotSha256": provenance["snapshot"]["sha256"],
        "binaryTools": binary_tools,
        "pythonTools": python_tools,
        "provenance": provenance,
    }


def validate_binary_tools_contract(
    tools_document: Mapping[str, Any], contract: Mapping[str, Any]
) -> None:
    raw_tools = tools_document.get("tools")
    if not isinstance(raw_tools, list):
        raise EspIdfError("ESP-IDF tools.json has no tools array")
    tools: dict[str, dict[str, Any]] = {}
    for raw_tool in raw_tools:
        if not isinstance(raw_tool, dict) or not isinstance(
            raw_tool.get("name"), str
        ):
            raise EspIdfError("ESP-IDF tools.json contains an invalid tool")
        name = raw_tool["name"]
        if name in tools:
            raise EspIdfError(f"ESP-IDF tools.json repeats tool {name!r}")
        tools[name] = raw_tool

    snapshot_targets = set(contract["snapshot"]["espIdf"]["targets"])
    required_tools = {
        name
        for name, tool in tools.items()
        if tool.get("install") == "always"
        and isinstance(tool.get("supported_targets"), list)
        and (
            "all" in tool["supported_targets"]
            or snapshot_targets.intersection(tool["supported_targets"])
        )
    }
    snapshot_tools = {entry["name"] for entry in contract["binaryTools"]}
    if snapshot_tools != required_tools:
        raise EspIdfError(
            "ESP-IDF supply-chain snapshot binary tool coverage drift: "
            f"expected {sorted(required_tools)!r}, found "
            f"{sorted(snapshot_tools)!r}"
        )

    for expected in contract["binaryTools"]:
        name = expected["name"]
        actual = tools.get(name)
        if actual is None:
            raise EspIdfError(f"ESP-IDF tools.json has no binary tool {name!r}")
        versions = actual.get("versions")
        recommended = (
            {
                str(item.get("name"))
                for item in versions
                if isinstance(item, dict)
                and item.get("status") == "recommended"
            }
            if isinstance(versions, list)
            else set()
        )
        if expected["version"] not in recommended:
            raise EspIdfError(
                f"ESP-IDF binary tool {name!r} recommended version drift: "
                f"expected {expected['version']!r}, found {sorted(recommended)!r}"
            )
        if actual.get("license") != expected["sourceLicense"]:
            raise EspIdfError(
                f"ESP-IDF binary tool {name!r} source license drift: "
                f"expected {expected['sourceLicense']!r}, found "
                f"{actual.get('license')!r}"
            )
        if actual.get("info_url") != expected["upstream"]:
            raise EspIdfError(
                f"ESP-IDF binary tool {name!r} upstream drift: expected "
                f"{expected['upstream']!r}, found {actual.get('info_url')!r}"
            )


def verify_esp_idf_tools_contract(
    idf_dir: Path, contract: Mapping[str, Any]
) -> dict[str, Any]:
    tools_path = idf_dir / "tools/tools.json"
    expected_digest = contract["snapshot"]["espIdf"]["toolsJsonSha256"]
    if not tools_path.is_file():
        raise EspIdfError(f"ESP-IDF tool registry is missing: {tools_path}")
    actual_digest = _normalized_lf_sha256(tools_path)
    if actual_digest != expected_digest:
        raise EspIdfError(
            "ESP-IDF tools/tools.json digest drift: expected "
            f"{expected_digest}, found {actual_digest}"
        )
    tools_document = _load_object(tools_path)
    validate_binary_tools_contract(tools_document, contract)
    return tools_document


def validate_compiler_contract(
    compiler: Path,
    compiler_output: str,
    tools_document: Mapping[str, Any],
    contract: Mapping[str, Any],
) -> tuple[str, str]:
    idf_target = str(contract["idfTarget"])
    tool_name = IDF_TARGET_COMPILER_TOOLS.get(idf_target)
    if tool_name is None:
        raise EspIdfError(
            f"No compiler supply-chain contract for ESP-IDF target {idf_target!r}"
        )
    expected = next(
        (item for item in contract["binaryTools"] if item["name"] == tool_name),
        None,
    )
    raw_tools = tools_document.get("tools")
    actual = next(
        (
            item
            for item in raw_tools
            if isinstance(item, dict) and item.get("name") == tool_name
        ),
        None,
    ) if isinstance(raw_tools, list) else None
    if expected is None or actual is None:
        raise EspIdfError(
            f"ESP-IDF compiler tool {tool_name!r} is missing from the contract"
        )
    version_command = actual.get("version_cmd")
    version_regex = actual.get("version_regex")
    if (
        not isinstance(version_command, list)
        or not version_command
        or not isinstance(version_command[0], str)
        or not isinstance(version_regex, str)
        or not version_regex
    ):
        raise EspIdfError(
            f"ESP-IDF compiler tool {tool_name!r} has no version probe"
        )
    official_name = Path(version_command[0]).name.removesuffix(".exe").lower()
    target_name = official_name.replace("-esp-", f"-{idf_target}-", 1)
    compiler_name = compiler.name.removesuffix(".exe").lower()
    if compiler_name not in {official_name, target_name}:
        raise EspIdfError(
            f"Executed C compiler {compiler.name!r} does not match ESP-IDF "
            f"tool {tool_name!r}"
        )
    match = re.search(version_regex, compiler_output)
    if match is None or not match.groups():
        raise EspIdfError(
            f"Cannot parse ESP-IDF compiler tool version: {compiler_output!r}"
        )
    observed_version = match.group(1)
    if observed_version != expected["version"]:
        raise EspIdfError(
            f"Executed C compiler version drift: expected "
            f"{expected['version']!r}, found {observed_version!r}"
        )
    return tool_name, observed_version


def collect_python_tools(
    python: Path,
    environment: Mapping[str, str],
    contract: Mapping[str, Any],
) -> list[dict[str, str]]:
    names = [entry["name"] for entry in contract["pythonTools"]]
    program = "\n".join(
        (
            "import importlib.metadata as metadata",
            "import json",
            "import sys",
            "names = json.loads(sys.argv[1])",
            "versions = {}",
            "missing = []",
            "for name in names:",
            "    try:",
            "        versions[name] = metadata.version(name)",
            "    except metadata.PackageNotFoundError:",
            "        missing.append(name)",
            "print(json.dumps({'missing': missing, 'versions': versions}, sort_keys=True))",
        )
    )
    completed = subprocess.run(
        [str(python), "-c", program, json.dumps(names, separators=(",", ":"))],
        check=False,
        capture_output=True,
        text=True,
        env=dict(environment),
    )
    if completed.returncode:
        raise EspIdfError(
            "Cannot query IDF Python distributions "
            f"(exit {completed.returncode})"
        )
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise EspIdfError(
            "Cannot parse IDF Python distribution versions"
        ) from error
    versions = result.get("versions") if isinstance(result, dict) else None
    missing = result.get("missing") if isinstance(result, dict) else None
    if not isinstance(versions, dict) or not isinstance(missing, list):
        raise EspIdfError("Invalid IDF Python distribution version response")
    if missing:
        raise EspIdfError(
            "IDF Python environment is missing distributions: "
            + ", ".join(sorted(str(item) for item in missing))
        )
    installed: list[dict[str, str]] = []
    for expected in contract["pythonTools"]:
        name = expected["name"]
        version = versions.get(name)
        if version != expected["version"]:
            raise EspIdfError(
                f"IDF Python distribution {name!r} version drift: expected "
                f"{expected['version']!r}, found {version!r}"
            )
        installed.append(
            {
                "name": name,
                "version": str(version),
                "license": expected["license"],
            }
        )
    return installed


def _tool_version(
    command: Sequence[str], environment: Mapping[str, str], tool_name: str
) -> str:
    completed = subprocess.run(
        list(command),
        check=False,
        capture_output=True,
        text=True,
        env=dict(environment),
    )
    output = (completed.stdout + completed.stderr).strip()
    if completed.returncode or not output:
        raise EspIdfError(
            f"Cannot query {tool_name} version (exit {completed.returncode})"
        )
    return output.splitlines()[0].strip()


def _version_match(output: str, pattern: str, tool_name: str) -> str:
    match = re.search(pattern, output, re.IGNORECASE)
    if match is None:
        raise EspIdfError(f"Cannot parse {tool_name} version output: {output!r}")
    return match.group(1)


def _load_cmake_cache(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise EspIdfError(f"Cannot read CMake cache {path}: {error}") from error
    values: dict[str, str] = {}
    for line in lines:
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key = key_and_type.split(":", 1)[0]
        values[key] = value
    return values


def collect_toolchain_provenance(
    idf_dir: Path,
    environment: Mapping[str, str],
    build_dir: Path,
    *,
    repo_root: Path,
    idf_version: str,
    idf_commit: str,
    idf_target: str,
) -> dict[str, Any]:
    description = _load_object(build_dir / "project_description.json")
    compiler_value = description.get("c_compiler")
    if not isinstance(compiler_value, str) or not compiler_value:
        raise EspIdfError("project_description.json has no C compiler")
    compiler = Path(compiler_value)
    if not compiler.is_file():
        raise EspIdfError(f"ESP-IDF C compiler is missing: {compiler}")
    contract = load_supply_chain_contract(
        repo_root,
        idf_version=idf_version,
        idf_commit=idf_commit,
        idf_target=idf_target,
    )
    tools_document = verify_esp_idf_tools_contract(idf_dir, contract)
    tools_json = idf_dir / "tools/tools.json"
    cmake_cache = _load_cmake_cache(build_dir / "CMakeCache.txt")
    if cmake_cache.get("CMAKE_GENERATOR") != "Ninja":
        raise EspIdfError("ESP-IDF production builds require the Ninja generator")
    cmake = Path(cmake_cache.get("CMAKE_COMMAND", ""))
    ninja = Path(cmake_cache.get("CMAKE_MAKE_PROGRAM", ""))
    if not cmake.is_file() or not ninja.is_file():
        raise EspIdfError("CMake cache does not identify the executed build tools")
    python = idf_python(environment)
    compiler_output = _tool_version(
        (str(compiler), "--version"), environment, "C compiler"
    )
    compiler_tool, compiler_tool_version = validate_compiler_contract(
        compiler, compiler_output, tools_document, contract
    )
    cmake_output = _tool_version(
        (str(cmake), "--version"), environment, "CMake"
    )
    ninja_output = _tool_version(
        (str(ninja), "--version"), environment, "Ninja"
    )
    python_output = _tool_version(
        (str(python), "--version"), environment, "IDF Python"
    )
    esptool_output = _tool_version(
        (str(python), "-m", "esptool", "version"), environment, "esptool"
    )
    python_tools = collect_python_tools(python, environment, contract)
    esptool_version = _version_match(
        esptool_output,
        r"esptool(?:\.py)?(?:\s+version)?\s+v?"
        r"([0-9][0-9A-Za-z.+-]*)",
        "esptool",
    )
    expected_esptool = next(
        item["version"]
        for item in contract["pythonTools"]
        if item["name"] == "esptool"
    )
    if esptool_version != expected_esptool:
        raise EspIdfError(
            "esptool command version drift: expected "
            f"{expected_esptool!r}, found {esptool_version!r}"
        )
    provenance = {
        "schemaVersion": 3,
        "cCompiler": {
            "name": compiler.name,
            "version": compiler_output,
            "tool": compiler_tool,
            "toolVersion": compiler_tool_version,
        },
        "cmake": {
            "name": cmake.name,
            "version": _version_match(
                cmake_output,
                r"cmake\s+version\s+([0-9][0-9A-Za-z.+-]*)",
                "CMake",
            ),
        },
        "ninja": {
            "name": ninja.name,
            "version": _version_match(
                ninja_output, r"^([0-9][0-9A-Za-z.+-]*)$", "Ninja"
            ),
        },
        "idfPython": {
            "name": python.name,
            "version": _version_match(
                python_output,
                r"python\s+([0-9][0-9A-Za-z.+-]*)",
                "IDF Python",
            ),
        },
        "esptool": {
            "name": "esptool",
            "version": esptool_version,
        },
        "espIdfTools": {
            "path": "tools/tools.json",
            "sha256": _normalized_lf_sha256(tools_json),
        },
        "supplyChain": {
            **contract["provenance"],
            "pythonTools": python_tools,
        },
    }
    output = build_dir / GENERATED_DIR_NAME / TOOLCHAIN_PROVENANCE_NAME
    output.write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return provenance


def _relative_to_project(path: Path, project_dir: Path) -> str:
    try:
        return _relative_path(path, project_dir)
    except ValueError as error:
        raise EspIdfError(f"Project input escapes {project_dir}: {path}") from error


def resolve_project_sources(
    project_dir: Path, requested_sources: Sequence[str]
) -> list[Path]:
    sources: list[Path] = []
    if requested_sources:
        candidates = [
            Path(item).expanduser()
            if Path(item).is_absolute()
            else project_dir / item
            for item in requested_sources
        ]
    else:
        candidates = [
            path
            for path in project_dir.iterdir()
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        ]
        source_root = project_dir / "src"
        if source_root.is_dir():
            candidates.extend(
                path
                for path in source_root.rglob("*")
                if path.is_file() and path.suffix in SOURCE_SUFFIXES
            )
    for candidate in candidates:
        source = candidate.resolve()
        _relative_to_project(source, project_dir)
        if not source.is_file():
            raise EspIdfError(f"Project source does not exist: {source}")
        if source.suffix not in SOURCE_SUFFIXES:
            raise EspIdfError(f"Unsupported project source suffix: {source}")
        sources.append(source)
    sources = sorted(set(sources), key=lambda path: path.as_posix())
    if not sources:
        raise EspIdfError(
            f"No C/C++/assembly sources found in {project_dir}; use --source"
        )
    return sources


def normalize_project_name(requested: str, project_dir: Path) -> str:
    if requested:
        name = requested
    else:
        name = re.sub(r"[^A-Za-z0-9_-]", "_", project_dir.name)
        if not name or name[0].isdigit() or name[0] == "-":
            name = f"jh_{name}"
    if not PROJECT_NAME_PATTERN.fullmatch(name):
        raise EspIdfError(f"Invalid ESP-IDF project name: {name!r}")
    return name


def _normalize_definition(value: str) -> str:
    definition = value.strip().removeprefix("-D")
    if not definition or any(token in definition for token in ("\n", "\r", ";")):
        raise EspIdfError(f"Invalid compile definition: {value!r}")
    name, separator, macro_value = definition.partition("=")
    if not DEFINITION_NAME_PATTERN.fullmatch(name):
        raise EspIdfError(f"Invalid compile definition name: {value!r}")
    if separator and not macro_value:
        raise EspIdfError(f"Compile definition has an empty value: {value!r}")
    if "$<" in definition:
        raise EspIdfError("CMake generator expressions are not supported")
    if name.startswith(("HAL_TARGET_", "HAL_BOARD_PROFILE_", "JH_ESP_IDF_")):
        raise EspIdfError(f"The ESP-IDF dispatcher owns {name}")
    if name == "HAL_PROVIDE_APP_ENTRY":
        raise EspIdfError("The ESP-IDF dispatcher owns HAL_PROVIDE_APP_ENTRY")
    return definition


def collect_project_features(
    repo_root: Path,
    project_dir: Path,
    features: Sequence[str],
    definitions: Sequence[str],
) -> tuple[list[str], set[str], list[str], set[str]]:
    normalized_definitions = [_normalize_definition(item) for item in definitions]
    try:
        generate_board_config.validate_definitions(normalized_definitions)
        normalized_features = generate_board_config.normalize_features(list(features))
    except generate_board_config.DescriptorError as error:
        raise EspIdfError(str(error)) from error

    requests = generate_hal_features.collect_header_requests(
        project_dir / "hal_project_config.h", "hal_project_config.h"
    )
    header_symbols = {request.symbol for request in requests}
    command_line_symbols: set[str] = set()
    for index, item in enumerate(normalized_features):
        symbol = item.removesuffix("=1")
        command_line_symbols.add(symbol)
        requests.append(
            generate_hal_features.FeatureRequest(
                symbol=symbol,
                value="1",
                source=f"command-line:--feature[{index}]",
            )
        )
    non_feature_definitions: list[str] = []
    for index, definition in enumerate(normalized_definitions):
        match = FEATURE_DEFINITION_PATTERN.fullmatch(definition)
        if match is None:
            non_feature_definitions.append(definition)
            continue
        symbol = match.group(1)
        command_line_symbols.add(symbol)
        requests.append(
            generate_hal_features.FeatureRequest(
                symbol=symbol,
                value="1",
                source=f"command-line:--define[{index}]",
            )
        )
    try:
        model = generate_hal_features.load_registry(repo_root / "config")
        resolution, findings = generate_hal_features.resolve_feature_requests(
            requests, model, "ESP-IDF project configuration"
        )
    except generate_hal_features.RegistryError as error:
        raise EspIdfError(str(error)) from error
    if findings:
        raise EspIdfError("\n".join(findings))
    return (
        list(resolution.requested),
        header_symbols,
        non_feature_definitions,
        command_line_symbols,
    )


def validate_supported_features(
    target: Mapping[str, Any],
    requested_features: Sequence[str],
    resolved_features: Sequence[str],
) -> None:
    supported_input = target.get("supportedFeatures")
    if supported_input is None:
        return
    supported = {
        item.removesuffix("=1")
        for item in generate_board_config.normalize_features(supported_input)
    }
    unsupported_requested = sorted(set(requested_features) - supported)
    unsupported_resolved = sorted(set(resolved_features) - supported)
    if not unsupported_requested and not unsupported_resolved:
        return
    details: list[str] = []
    if unsupported_requested:
        details.append("requested=" + ",".join(unsupported_requested))
    if unsupported_resolved:
        details.append("resolved=" + ",".join(unsupported_resolved))
    allowed = ",".join(sorted(supported)) or "none"
    raise EspIdfError(
        f"[JH-CFG-UNSUPPORTED] target {target['id']} supports only {allowed}; "
        + "; ".join(details)
    )


def resolve_component_build_inputs(
    resolved_features: Sequence[str],
    feature_model: generate_hal_features.FeatureModel | None = None,
) -> tuple[list[str], list[str], list[str]]:
    """Resolve the ESP-IDF source/dependency graph from the feature set."""
    if feature_model is None:
        feature_model = generate_hal_features.load_registry(
            Path(__file__).resolve().parents[1] / "config"
        )
    enabled = set(resolved_features)
    try:
        feature_sources, portable_sources, managed_dependencies = (
            feature_model.resolve_build_effects(enabled)
        )
    except KeyError as error:
        raise EspIdfError(f"Unknown resolved feature: {error.args[0]}") from error
    sources = list(ESP_IDF_BASE_SOURCES)
    sources.extend(feature_sources)
    sources.extend(portable_sources)
    private_dependencies = list(ESP_IDF_BASE_PRIVATE_COMPONENT_DEPENDENCIES)
    for feature in sorted(ESP_IDF_TARGET_SOURCES):
        if feature in enabled:
            sources.extend(ESP_IDF_TARGET_SOURCES[feature])
            private_dependencies.extend(
                ESP_IDF_FEATURE_COMPONENT_DEPENDENCIES.get(feature, ())
            )
    for dependency in managed_dependencies:
        components = ESP_IDF_MANAGED_DEPENDENCIES.get(dependency)
        if components is None:
            raise EspIdfError(
                f"ESP-IDF does not support managed dependency {dependency!r}"
            )
        private_dependencies.extend(components)
    return (
        list(dict.fromkeys(sources)),
        list(ESP_IDF_PUBLIC_COMPONENT_DEPENDENCIES),
        list(dict.fromkeys(private_dependencies)),
    )


def _header_defines(project_dir: Path, name: str) -> bool:
    path = project_dir / "hal_project_config.h"
    if not path.is_file():
        return False
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise EspIdfError(f"Cannot read {path}: {error}") from error
    pattern = re.compile(rf"^[ \t]*#[ \t]*define[ \t]+{re.escape(name)}(?:[ \t(]|$)")
    return any(
        pattern.match(line)
        for _, line in generate_hal_features.preprocessor_logical_lines(text)
    )


def resolve_build_model(
    repo_root: Path,
    project_dir: Path,
    *,
    target: str,
    board: str,
    project_name: str,
    requested_sources: Sequence[str],
    features: Sequence[str],
    definitions: Sequence[str],
) -> dict[str, Any]:
    repo_root = _canonical_path(repo_root)
    project_dir = _canonical_path(project_dir)
    try:
        targets, boards, capabilities = generate_board_config.load_registry(
            repo_root / "boards"
        )
    except generate_board_config.DescriptorError as error:
        raise EspIdfError(str(error)) from error
    if target not in targets:
        raise EspIdfError(f"Unknown JaszczurHAL target: {target!r}")
    target_descriptor = targets[target]
    selected_board = board or target_descriptor["defaultBoard"]
    if selected_board not in boards:
        raise EspIdfError(f"Unknown JaszczurHAL board: {selected_board!r}")
    board_descriptor = boards[selected_board]
    if target not in board_descriptor["compatibleTargets"]:
        raise EspIdfError(
            f"Board {selected_board!r} is not compatible with {target!r}"
        )
    if target_descriptor["build"]["provider"] != "esp-idf" or board_descriptor[
        "build"
    ]["provider"] != "esp-idf":
        raise EspIdfError(
            f"Target/board {target}:{selected_board} does not use ESP-IDF"
        )
    idf_target = target_descriptor["build"].get("idfTarget")
    if not isinstance(idf_target, str) or not idf_target:
        raise EspIdfError(f"Target {target!r} has no ESP-IDF target mapping")

    sources = resolve_project_sources(project_dir, requested_sources)
    (
        requested_features,
        header_symbols,
        non_feature_definitions,
        command_line_features,
    ) = collect_project_features(repo_root, project_dir, features, definitions)
    try:
        _, resolved_features, _ = generate_board_config.resolve_features(
            requested_features, target_descriptor
        )
    except generate_board_config.DescriptorError as error:
        raise EspIdfError(str(error)) from error
    validate_supported_features(
        target_descriptor, requested_features, resolved_features
    )
    (
        integration_sources,
        component_dependencies,
        private_component_dependencies,
    ) = resolve_component_build_inputs(
        resolved_features,
        generate_hal_features.load_registry(repo_root / "config"),
    )
    compile_features = set(command_line_features)
    for required in target_descriptor.get("requiredFeatures", []):
        symbol = required.removesuffix("=1")
        if symbol not in header_symbols:
            compile_features.add(symbol)

    compile_definitions = {
        target_descriptor["hal"]["targetSelector"],
        *compile_features,
        *generate_board_config.board_compile_definitions(
            target_descriptor, board_descriptor
        ),
        *non_feature_definitions,
    }
    if not _header_defines(project_dir, "HAL_PROVIDE_APP_ENTRY"):
        compile_definitions.add("HAL_PROVIDE_APP_ENTRY")
    include_dirs = sorted(
        {project_dir.resolve(), *(source.parent for source in sources)},
        key=lambda path: path.as_posix(),
    )
    return {
        "repoRoot": repo_root,
        "target": target,
        "board": selected_board,
        "idfTarget": idf_target,
        "projectName": project_name,
        "projectSources": sources,
        "projectIncludeDirs": include_dirs,
        "requestedFeatures": requested_features,
        "resolvedFeatures": resolved_features,
        "integrationSources": integration_sources,
        "componentDependencies": component_dependencies,
        "privateComponentDependencies": private_component_dependencies,
        "compileDefinitions": sorted(compile_definitions),
        "targetDescriptor": target_descriptor,
        "boardDescriptor": board_descriptor,
        "boards": boards,
        "capabilities": capabilities,
    }


def _cmake_quote(value: str) -> str:
    if any(token in value for token in ("\n", "\r", ";")):
        raise EspIdfError(f"Value cannot be represented in generated CMake: {value!r}")
    return '"' + value.replace("\\", "/").replace('"', '\\"') + '"'


def _render_cmake_list(name: str, values: Sequence[str]) -> list[str]:
    lines = [f"set({name}"]
    lines.extend(f"    {_cmake_quote(value)}" for value in values)
    lines.append(")")
    return lines


def _render_sdkconfig_defaults(model: Mapping[str, Any]) -> str:
    board = model["boardDescriptor"]
    flash_bytes = board["memory"]["flash"]["expectedBytes"]
    mib = 1024 * 1024
    if flash_bytes <= 0 or flash_bytes % mib:
        raise EspIdfError(
            f"ESP-IDF board flash size must be a positive whole MiB: {flash_bytes}"
        )
    lines = [
        "# Generated by build_esp_idf.py; do not edit.",
        f"CONFIG_ESPTOOLPY_FLASHSIZE_{flash_bytes // mib}MB=y",
    ]
    if model["idfTarget"] == "esp32s3":
        lines.append("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y")
    elif model["idfTarget"] == "esp32":
        lines.append("CONFIG_ESP_CONSOLE_UART_DEFAULT=y")
    if "HAL_ENABLE_NETWORK_CORE" in model["resolvedFeatures"]:
        # Four HAL TCP sockets, two listeners and four UDP sockets must not
        # consume the entire lwIP descriptor pool; services such as DNS,
        # ping, TLS and OTA need their own short-lived descriptors. Core
        # locking provides the selected backend's nested stack guard and the
        # raw-netif extension used by WireGuard.
        lines.extend(
            (
                "CONFIG_LWIP_MAX_SOCKETS=16",
                "CONFIG_LWIP_TCPIP_CORE_LOCKING=y",
            )
        )
    if "HAL_ENABLE_BLE" in model["resolvedFeatures"]:
        lines.extend(
            (
                "CONFIG_BT_ENABLED=y",
                "CONFIG_BT_NIMBLE_ENABLED=y",
                "CONFIG_BT_NIMBLE_ROLE_CENTRAL=y",
                "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y",
                "CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y",
                "CONFIG_BT_NIMBLE_ROLE_OBSERVER=y",
                "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1",
            )
        )
    if "HAL_ENABLE_BLUETOOTH_GAMEPAD" in model["resolvedFeatures"]:
        lines.extend(
            (
                "CONFIG_BT_ENABLED=y",
                "CONFIG_BT_BLUEDROID_ENABLED=y",
                "CONFIG_BT_CLASSIC_ENABLED=y",
                "CONFIG_BT_HID_ENABLED=y",
                "CONFIG_BT_HID_HOST_ENABLED=y",
                "CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y",
            )
        )
    if "HAL_ENABLE_OTA" in model["resolvedFeatures"]:
        # The public OTA backend stages into the inactive ESP-IDF app slot
        # and relies on the bootloader's pending-verify state for trial boot
        # and rollback. A single-app layout cannot satisfy that contract.
        lines.extend(
            (
                "CONFIG_PARTITION_TABLE_TWO_OTA_LARGE=y",
                "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
            )
        )
    if "HAL_ENABLE_STACK_GUARD" in model["resolvedFeatures"]:
        # ESP-IDF owns task stack creation, so the HAL feature selects both
        # supported FreeRTOS protection layers instead of installing a
        # second guard implementation at runtime.
        lines.extend(
            (
                "CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y",
                "CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y",
            )
        )
    psram = board["memory"].get("psram")
    if psram is not None:
        lines.append("CONFIG_SPIRAM=y")
        interface = psram["interface"]
        if interface == "quad":
            lines.append("CONFIG_SPIRAM_MODE_QUAD=y")
        elif interface == "octal":
            lines.append("CONFIG_SPIRAM_MODE_OCT=y")
        else:
            raise EspIdfError(f"Unsupported ESP-IDF PSRAM interface: {interface}")
        lines.append("CONFIG_SPIRAM_SPEED_80M=y")
    return "\n".join(lines) + "\n"


def _project_sdkconfig_defaults(project_dir: Path, build_dir: Path) -> list[Path]:
    project_dir = _canonical_path(project_dir)
    build_dir = _canonical_path(build_dir)
    defaults = [build_dir / GENERATED_DIR_NAME / SDKCONFIG_DEFAULTS_NAME]
    project_defaults = project_dir / "sdkconfig.defaults"
    if project_defaults.is_file():
        defaults.append(_canonical_path(project_defaults))
    return defaults


def _render_project_cmake(
    model: Mapping[str, Any], sdkconfig_defaults: Sequence[Path]
) -> str:
    lines = [
        "# Generated by build_esp_idf.py; do not edit.",
        f"set(JH_ESP_IDF_TARGET {_cmake_quote(model['idfTarget'])})",
        f"set(JH_ESP_IDF_BOARD {_cmake_quote(model['board'])})",
        f"set(JH_ESP_IDF_PROJECT_NAME {_cmake_quote(model['projectName'])})",
    ]
    lines.extend(
        _render_cmake_list(
            "JH_ESP_IDF_PROJECT_SOURCES",
            [
                _canonical_path(path).as_posix()
                for path in model["projectSources"]
            ],
        )
    )
    lines.extend(
        _render_cmake_list(
            "JH_ESP_IDF_PROJECT_INCLUDE_DIRS",
            [
                _canonical_path(path).as_posix()
                for path in model["projectIncludeDirs"]
            ],
        )
    )
    lines.extend(
        _render_cmake_list(
            "JH_ESP_IDF_COMPILE_DEFINITIONS", model["compileDefinitions"]
        )
    )
    lines.extend(
        _render_cmake_list(
            "JH_ESP_IDF_RESOLVED_FEATURES", model["resolvedFeatures"]
        )
    )
    lines.extend(
        _render_cmake_list(
            "JH_ESP_IDF_COMPONENT_SOURCES",
            [
                _canonical_path(model["repoRoot"] / path).as_posix()
                for path in model["integrationSources"]
            ],
        )
    )
    lines.extend(
        _render_cmake_list(
            "JH_ESP_IDF_COMPONENT_REQUIRES",
            model["componentDependencies"],
        )
    )
    lines.extend(
        _render_cmake_list(
            "JH_ESP_IDF_COMPONENT_PRIV_REQUIRES",
            model["privateComponentDependencies"],
        )
    )
    lines.extend(
        _render_cmake_list(
            "JH_ESP_IDF_SDKCONFIG_DEFAULTS",
            [_canonical_path(path).as_posix() for path in sdkconfig_defaults],
        )
    )
    return "\n".join(lines) + "\n"


def _project_input_digest(path: Path, display: str) -> str:
    if not path.is_file():
        raise EspIdfError(f"Project {display} is missing: {path}")
    try:
        return _sha256(path)
    except OSError as error:
        raise EspIdfError(f"Cannot hash project {display} {path}: {error}") from error


def _project_config_contract(
    project_dir: Path,
    build_dir: Path,
    model: Mapping[str, Any],
    sdkconfig_defaults: Sequence[Path],
) -> dict[str, Any]:
    project_dir = _canonical_path(project_dir)
    build_dir = _canonical_path(build_dir)
    source_paths = {
        _relative_to_project(path, project_dir): _project_input_digest(
            path, "source"
        )
        for path in model["projectSources"]
    }
    default_paths = {
        (
            _relative_to_project(path, project_dir)
            if _inside(path, project_dir)
            else _relative_path(path, build_dir)
        ): _project_input_digest(path, "sdkconfig defaults")
        for path in sdkconfig_defaults
    }
    config_header_path = project_dir / "hal_project_config.h"
    config_header = (
        {
            "path": "hal_project_config.h",
            "sha256": _project_input_digest(
                config_header_path, "configuration header"
            ),
        }
        if config_header_path.is_file()
        else None
    )
    return {
        "schemaVersion": 2,
        "target": model["target"],
        "board": model["board"],
        "idfTarget": model["idfTarget"],
        "projectName": model["projectName"],
        "projectSources": list(source_paths),
        "projectSourceSha256": source_paths,
        "projectIncludeDirs": [
            _relative_to_project(path, project_dir)
            for path in model["projectIncludeDirs"]
        ],
        "projectConfigHeader": config_header,
        "requestedFeatures": model["requestedFeatures"],
        "compileDefinitions": model["compileDefinitions"],
        "sdkconfigDefaults": list(default_paths),
        "sdkconfigDefaultSha256": default_paths,
    }


def materialize_build_inputs(
    repo_root: Path,
    project_dir: Path,
    build_dir: Path,
    model: Mapping[str, Any],
) -> Path:
    repo_root = _canonical_path(repo_root)
    project_dir = _canonical_path(project_dir)
    build_dir = _canonical_path(build_dir)
    generated_dir = build_dir / GENERATED_DIR_NAME
    generated_dir.mkdir(parents=True, exist_ok=True)
    try:
        generate_board_config.generate(
            model["targetDescriptor"],
            model["boardDescriptor"],
            model["boards"],
            model["capabilities"],
            generated_dir,
            list(model["requestedFeatures"]),
        )
    except generate_board_config.DescriptorError as error:
        raise EspIdfError(str(error)) from error

    generated_defaults = generated_dir / SDKCONFIG_DEFAULTS_NAME
    generated_defaults.write_text(
        _render_sdkconfig_defaults(model), encoding="utf-8"
    )
    sdkconfig_defaults = _project_sdkconfig_defaults(project_dir, build_dir)
    (generated_dir / PROJECT_CONFIG_CMAKE_NAME).write_text(
        _render_project_cmake(model, sdkconfig_defaults), encoding="utf-8"
    )

    project_config = _project_config_contract(
        project_dir, build_dir, model, sdkconfig_defaults
    )
    (generated_dir / PROJECT_CONFIG_JSON_NAME).write_text(
        json.dumps(project_config, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return generated_dir


def validate_generated_board_contract(
    build_dir: Path, model: Mapping[str, Any]
) -> None:
    """Reject artifacts built from a stale target or board model."""
    generated_dir = build_dir / GENERATED_DIR_NAME
    with tempfile.TemporaryDirectory(
        prefix=".jh-board-contract-check-", dir=build_dir
    ) as temporary:
        expected_dir = Path(temporary)
        try:
            generate_board_config.generate(
                model["targetDescriptor"],
                model["boardDescriptor"],
                model["boards"],
                model["capabilities"],
                expected_dir,
                list(model["requestedFeatures"]),
            )
        except generate_board_config.DescriptorError as error:
            raise EspIdfError(str(error)) from error

        for name in GENERATED_BOARD_CONTRACT_INPUTS:
            actual = generated_dir / name
            expected = expected_dir / name
            if not actual.is_file():
                raise EspIdfError(
                    f"Generated board contract input is missing: {actual}"
                )
            if actual.read_bytes() != expected.read_bytes():
                raise EspIdfError(
                    "Generated board contract differs from the current "
                    f"target/board model: {name}"
                )

    defaults = generated_dir / SDKCONFIG_DEFAULTS_NAME
    if not defaults.is_file():
        raise EspIdfError(
            f"Generated ESP-IDF sdkconfig defaults are missing: {defaults}"
        )
    if defaults.read_text(encoding="utf-8") != _render_sdkconfig_defaults(
        model
    ):
        raise EspIdfError(
            "Generated ESP-IDF sdkconfig defaults differ from the current "
            "target/board model"
        )


def validate_generated_project_contract(
    project_dir: Path, build_dir: Path, model: Mapping[str, Any]
) -> dict[str, Any]:
    """Reject artifacts built from stale project-owned build inputs."""
    project_dir = _canonical_path(project_dir)
    build_dir = _canonical_path(build_dir)
    generated_dir = build_dir / GENERATED_DIR_NAME
    sdkconfig_defaults = _project_sdkconfig_defaults(project_dir, build_dir)
    expected_cmake = _render_project_cmake(model, sdkconfig_defaults)
    cmake_path = generated_dir / PROJECT_CONFIG_CMAKE_NAME
    try:
        actual_cmake = cmake_path.read_text(encoding="utf-8")
    except OSError as error:
        raise EspIdfError(
            f"Cannot read generated ESP-IDF project config {cmake_path}: {error}"
        ) from error
    if actual_cmake != expected_cmake:
        raise EspIdfError(
            "Generated ESP-IDF project CMake config differs from the current "
            "project model"
        )

    actual = _load_object(generated_dir / PROJECT_CONFIG_JSON_NAME)
    expected = _project_config_contract(
        project_dir, build_dir, model, sdkconfig_defaults
    )
    if actual != expected:
        for key in sorted(set(actual) | set(expected)):
            if actual.get(key) != expected.get(key):
                raise EspIdfError(
                    f"Generated project config {key} mismatch: expected "
                    f"{expected.get(key)!r}, found {actual.get(key)!r}"
                )
        raise EspIdfError("Generated project config differs from the current model")
    return actual


def validate_ninja_freshness(
    build_dir: Path,
    project_name: str,
    toolchain_provenance: Mapping[str, Any],
    environment: Mapping[str, str] | None = None,
) -> None:
    """Use Ninja's dependency graph to reject stale application artifacts."""
    cache = _load_cmake_cache(build_dir / "CMakeCache.txt")
    if cache.get("CMAKE_GENERATOR") != "Ninja":
        raise EspIdfError("ESP-IDF artifact freshness requires Ninja")
    ninja = Path(cache.get("CMAKE_MAKE_PROGRAM", ""))
    expected_ninja = toolchain_provenance.get("ninja")
    if (
        not ninja.is_absolute()
        or not ninja.is_file()
        or ninja.name.lower() not in {"ninja", "ninja.exe", "ninja-build"}
        or not isinstance(expected_ninja, dict)
        or ninja.name != expected_ninja.get("name")
    ):
        raise EspIdfError(
            "CMake cache does not identify the provenanced Ninja executable"
        )
    try:
        completed = subprocess.run(
            [str(ninja), "-n", f"{project_name}.elf"],
            cwd=build_dir,
            env=_idf_environment(
                os.environ if environment is None else environment,
                build_dir,
            ),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
            errors="replace",
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise EspIdfError(f"Cannot check Ninja artifact freshness: {error}") from error
    output = (completed.stdout or "").strip()
    if completed.returncode:
        raise EspIdfError(
            "Ninja artifact freshness check failed with exit "
            f"{completed.returncode}: {output}"
        )
    if output != "ninja: no work to do.":
        preview = "\n".join(output.splitlines()[:8])
        raise EspIdfError(
            "ESP-IDF artifacts are stale; Ninja reports pending work:\n" + preview
        )


def _compiled_source(entries: list[Any], source: Path | str) -> bool:
    if isinstance(source, Path):
        expected = _canonical_path(source)
        return any(
            isinstance(entry, dict)
            and isinstance(entry.get("file"), str)
            and _canonical_path(Path(entry["file"])) == expected
            for entry in entries
        )
    normalized = str(source).replace("\\", "/")
    return any(
        isinstance(entry, dict)
        and str(entry.get("file", "")).replace("\\", "/").endswith(normalized)
        for entry in entries
    )


def _validate_clean_build_log(path: Path) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    findings = [
        line
        for line in text.splitlines()
        if re.search(r"(?:^|\s)(?:warning|error):", line, re.IGNORECASE)
        or "unknown kconfig" in line.lower()
    ]
    if findings:
        preview = "\n".join(findings[:8])
        raise EspIdfError(f"ESP-IDF build log contains diagnostics:\n{preview}")


def _load_sdkconfig(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise EspIdfError(f"Cannot read sdkconfig {path}: {error}") from error
    values: dict[str, str] = {}
    for line in lines:
        if not line.startswith("CONFIG_") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value
    return values


def _partition_configuration(
    sdkconfig_path: Path, flash_images: Sequence[Mapping[str, Any]]
) -> dict[str, Any]:
    sdkconfig = _load_sdkconfig(sdkconfig_path)
    active_profiles = [
        symbol
        for symbol in PARTITION_PROFILE_SYMBOLS
        if sdkconfig.get(symbol) == "y"
    ]
    if len(active_profiles) != 1:
        raise EspIdfError(
            "sdkconfig must select exactly one supported partition profile"
        )
    offset_value = sdkconfig.get("CONFIG_PARTITION_TABLE_OFFSET")
    if offset_value is None:
        raise EspIdfError("sdkconfig has no partition table offset")
    try:
        offset = int(offset_value, 0)
    except ValueError as error:
        raise EspIdfError(
            f"sdkconfig has an invalid partition table offset: {offset_value!r}"
        ) from error
    partition_images = [
        item
        for item in flash_images
        if item.get("path") == "partition_table/partition-table.bin"
    ]
    if len(partition_images) != 1:
        raise EspIdfError("Flash manifest has no unique partition table image")
    partition = partition_images[0]
    expected_offset = f"0x{offset:x}"
    if partition.get("offset") != expected_offset:
        raise EspIdfError(
            "Partition table flash offset differs from sdkconfig: "
            f"expected {expected_offset}, found {partition.get('offset')!r}"
        )
    profile = active_profiles[0].removeprefix(
        "CONFIG_PARTITION_TABLE_"
    ).lower().replace("_", "-")
    return {
        "sdkconfigSha256": _sha256(sdkconfig_path),
        "partitionTable": {
            "profile": profile,
            "path": partition["path"],
            "offset": partition["offset"],
            "sha256": partition["sha256"],
        },
    }


def _validate_toolchain_provenance(
    value: dict[str, Any], contract: Mapping[str, Any]
) -> None:
    compiler = value.get("cCompiler")
    tools = value.get("espIdfTools")
    if value.get("schemaVersion") != 3:
        raise EspIdfError("Toolchain provenance has an unsupported schema")
    if not isinstance(compiler, dict) or not all(
        isinstance(compiler.get(key), str) and compiler[key]
        for key in ("name", "version", "tool", "toolVersion")
    ):
        raise EspIdfError("Toolchain provenance has no C compiler version")
    compiler_tool = IDF_TARGET_COMPILER_TOOLS.get(str(contract["idfTarget"]))
    expected_compiler = next(
        (
            item
            for item in contract["binaryTools"]
            if item["name"] == compiler_tool
        ),
        None,
    )
    if (
        expected_compiler is None
        or compiler["tool"] != compiler_tool
        or compiler["toolVersion"] != expected_compiler["version"]
    ):
        raise EspIdfError(
            "Toolchain provenance C compiler differs from the supply-chain "
            "snapshot"
        )
    for key, display in (
        ("cmake", "CMake"),
        ("ninja", "Ninja"),
        ("idfPython", "IDF Python"),
        ("esptool", "esptool"),
    ):
        tool = value.get(key)
        if (
            not isinstance(tool, dict)
            or not isinstance(tool.get("name"), str)
            or not tool["name"]
            or not re.fullmatch(
                r"[0-9][0-9A-Za-z.+-]*", str(tool.get("version", ""))
            )
        ):
            raise EspIdfError(
                f"Toolchain provenance has no valid {display} version"
            )
    if value["esptool"]["name"] != "esptool":
        raise EspIdfError("Toolchain provenance has an invalid esptool name")
    if (
        not isinstance(tools, dict)
        or tools.get("path") != "tools/tools.json"
        or not re.fullmatch(r"[0-9a-f]{64}", str(tools.get("sha256", "")))
    ):
        raise EspIdfError("Toolchain provenance has no valid ESP-IDF tools digest")
    expected_tools_digest = contract["snapshot"]["espIdf"]["toolsJsonSha256"]
    if tools["sha256"] != expected_tools_digest:
        raise EspIdfError(
            "Toolchain provenance ESP-IDF tools digest differs from the "
            "supply-chain snapshot"
        )
    if value.get("supplyChain") != contract["provenance"]:
        raise EspIdfError(
            "Toolchain provenance differs from the ESP-IDF supply-chain snapshot"
        )
    pending: list[Any] = [value]
    while pending:
        item = pending.pop()
        if isinstance(item, dict):
            pending.extend(item.values())
        elif isinstance(item, list):
            pending.extend(item)
        elif isinstance(item, str) and (
            Path(item).is_absolute() or PureWindowsPath(item).is_absolute()
        ):
            raise EspIdfError(
                "Toolchain provenance contains an absolute host path"
            )


def validate_artifacts(
    repo_root: Path,
    project_dir: Path,
    build_dir: Path,
    model: Mapping[str, Any],
    *,
    idf_version: str,
    idf_commit: str,
    write_manifest: bool = True,
    build_environment: Mapping[str, str] | None = None,
) -> dict[str, Any]:
    repo_root = _canonical_path(repo_root)
    project_dir = _canonical_path(project_dir)
    build_dir = _canonical_path(build_dir)
    project_name = model["projectName"]
    required = {
        "applicationElf": f"{project_name}.elf",
        "applicationMap": f"{project_name}.map",
        "applicationBinary": f"{project_name}.bin",
        "sdkconfig": "sdkconfig",
        "flasherArgs": "flasher_args.json",
        "projectDescription": "project_description.json",
        "compileCommands": "compile_commands.json",
        "cmakeCache": "CMakeCache.txt",
        "buildLog": LOG_NAME,
        "generatedConfig": f"{GENERATED_DIR_NAME}/{GENERATED_CONFIG_NAME}",
        "generatedLinkContract": f"{GENERATED_DIR_NAME}/jh_link_contract.h",
        "resolvedBoardConfig": f"{GENERATED_DIR_NAME}/jh_board_resolved.json",
        "projectConfig": f"{GENERATED_DIR_NAME}/{PROJECT_CONFIG_JSON_NAME}",
        "toolchainProvenance": (
            f"{GENERATED_DIR_NAME}/{TOOLCHAIN_PROVENANCE_NAME}"
        ),
    }
    artifacts: dict[str, str] = {}
    artifact_paths: dict[str, Path] = {}
    for name, relative in required.items():
        artifact_paths[name], artifacts[name] = _artifact_path(
            build_dir, relative
        )
    validate_generated_board_contract(build_dir, model)
    project_config = validate_generated_project_contract(
        project_dir, build_dir, model
    )
    _validate_clean_build_log(build_dir / required["buildLog"])
    toolchain_provenance = _load_object(
        build_dir / required["toolchainProvenance"]
    )
    supply_chain_contract = load_supply_chain_contract(
        repo_root,
        idf_version=idf_version,
        idf_commit=idf_commit,
        idf_target=model["idfTarget"],
    )
    _validate_toolchain_provenance(
        toolchain_provenance, supply_chain_contract
    )

    description = _load_object(build_dir / required["projectDescription"])
    if description.get("target") != model["idfTarget"]:
        raise EspIdfError(
            "project_description.json target mismatch: "
            f"expected {model['idfTarget']!r}, found {description.get('target')!r}"
        )
    if description.get("project_name") != project_name:
        raise EspIdfError(
            "project_description.json project mismatch: "
            f"expected {project_name!r}, found {description.get('project_name')!r}"
        )
    for key, expected in (
        ("app_elf", f"{project_name}.elf"),
        ("app_bin", f"{project_name}.bin"),
    ):
        if description.get(key) != expected:
            raise EspIdfError(
                f"project_description.json {key} mismatch: expected "
                f"{expected!r}, found {description.get(key)!r}"
            )
    build_components = description.get("build_components")
    if not isinstance(build_components, list) or "jaszczurhal" not in (
        str(component).lower() for component in build_components
    ):
        raise EspIdfError(
            "project_description.json does not include the jaszczurhal component"
        )

    try:
        compile_commands = json.loads(
            (build_dir / required["compileCommands"]).read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise EspIdfError(f"Invalid compile_commands.json: {error}") from error
    if not isinstance(compile_commands, list) or not compile_commands:
        raise EspIdfError("compile_commands.json must contain build entries")
    required_sources: list[Path | str] = [
        *model["projectSources"],
        *(repo_root / path for path in model["integrationSources"]),
        f"/{GENERATED_DIR_NAME}/jh_link_contract_reference.c",
        f"/{GENERATED_DIR_NAME}/jh_link_contract_definition.c",
    ]
    missing_sources = [
        str(source)
        for source in required_sources
        if not _compiled_source(compile_commands, source)
    ]
    if missing_sources:
        raise EspIdfError(
            "compile_commands.json is missing integration sources: "
            + ", ".join(missing_sources)
        )

    resolved_board = _load_object(build_dir / required["resolvedBoardConfig"])
    for key, expected in (
        ("target", model["target"]),
        ("board", model["board"]),
        ("idfTarget", model["idfTarget"]),
        ("requestedFeatures", model["requestedFeatures"]),
        ("resolvedFeatures", model["resolvedFeatures"]),
    ):
        if resolved_board.get(key) != expected:
            raise EspIdfError(
                f"Resolved board config {key} mismatch: expected {expected!r}, "
                f"found {resolved_board.get(key)!r}"
            )

    flasher = _load_object(build_dir / required["flasherArgs"])
    extra_args = flasher.get("extra_esptool_args")
    if not isinstance(extra_args, dict) or extra_args.get("chip") != model[
        "idfTarget"
    ]:
        raise EspIdfError("flasher_args.json does not select the exact target")
    flash_bytes = model["boardDescriptor"]["memory"]["flash"]["expectedBytes"]
    expected_flash_size = f"{flash_bytes // (1024 * 1024)}MB"
    flash_settings = flasher.get("flash_settings")
    if not isinstance(flash_settings, dict) or flash_settings.get(
        "flash_size"
    ) != expected_flash_size:
        raise EspIdfError(
            "flasher_args.json flash size differs from the board descriptor"
        )
    flash_files = flasher.get("flash_files")
    if not isinstance(flash_files, dict) or not flash_files:
        raise EspIdfError("flasher_args.json has no flash_files object")

    flash_images = []
    observed_paths: set[str] = set()
    observed_offsets: set[int] = set()
    for raw_offset, raw_path in flash_files.items():
        if not isinstance(raw_offset, str) or not isinstance(raw_path, str):
            raise EspIdfError("flasher_args.json flash_files must map strings")
        try:
            offset = int(raw_offset, 0)
        except ValueError as error:
            raise EspIdfError(f"Invalid flash offset: {raw_offset!r}") from error
        if offset < 0 or offset in observed_offsets:
            raise EspIdfError(f"Invalid or duplicate flash offset: {raw_offset!r}")
        path, relative = _artifact_path(build_dir, raw_path)
        if relative in observed_paths:
            raise EspIdfError(f"Duplicate flash image: {relative}")
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
        f"{project_name}.bin",
        "bootloader/bootloader.bin",
        "partition_table/partition-table.bin",
    }
    missing = sorted(expected_flash_paths - observed_paths)
    if missing:
        raise EspIdfError(
            "flasher_args.json is missing required images: " + ", ".join(missing)
        )

    flash_images.sort(key=lambda item: int(item["offset"], 0))
    configuration = _partition_configuration(
        artifact_paths["sdkconfig"], flash_images
    )
    validate_ninja_freshness(
        build_dir,
        project_name,
        toolchain_provenance,
        build_environment,
    )
    manifest = {
        "schemaVersion": 1,
        "project": project_name,
        "target": model["target"],
        "board": model["board"],
        "espIdf": {"version": idf_version, "commit": idf_commit},
        "toolchain": toolchain_provenance,
        "configuration": configuration,
        "integration": {
            "component": "jaszczurhal",
            "sources": list(model["integrationSources"]),
            "projectSources": project_config["projectSources"],
            "featureHash": resolved_board["featureHash"],
            "requestedFeatures": resolved_board["requestedFeatures"],
            "resolvedFeatures": resolved_board["resolvedFeatures"],
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


def _idf_environment(
    environment: Mapping[str, str], build_dir: Path
) -> dict[str, str]:
    generated_dir = build_dir / GENERATED_DIR_NAME
    build_environment = dict(environment)
    build_environment["JH_ESP_IDF_GENERATED_DIR"] = str(generated_dir)
    build_environment["JH_ESP_IDF_PROJECT_CONFIG"] = str(
        generated_dir / PROJECT_CONFIG_CMAKE_NAME
    )
    return build_environment


def idf_command(
    idf_dir: Path,
    environment: Mapping[str, str],
    project_template: Path,
    build_dir: Path,
    idf_target: str,
    action: str,
    *,
    port: str = "",
) -> list[str]:
    command = [
        str(idf_python(environment)),
        str(idf_dir / "tools/idf.py"),
        "-C",
        str(project_template),
        "-B",
        str(build_dir),
        "-D",
        f"IDF_TARGET={idf_target}",
        "-D",
        f"SDKCONFIG={build_dir / 'sdkconfig'}",
        "-D",
        "CMAKE_EXPORT_COMPILE_COMMANDS=ON",
    ]
    if action == "flash":
        command.extend(("-p", port))
    command.append(action)
    return command


def run_idf(
    repo_root: Path,
    build_dir: Path,
    idf_dir: Path,
    idf_target: str,
    environment: Mapping[str, str],
    action: str,
    *,
    port: str = "",
) -> None:
    project_template = repo_root / "cmake/esp-idf/project"
    command = idf_command(
        idf_dir,
        environment,
        project_template,
        build_dir,
        idf_target,
        action,
        port=port,
    )
    build_dir.mkdir(parents=True, exist_ok=True)
    log_name = FLASH_LOG_NAME if action == "flash" else LOG_NAME
    log_path = build_dir / log_name
    try:
        completed = subprocess.run(
            command,
            cwd=repo_root,
            env=_idf_environment(environment, build_dir),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as error:
        log = f"Cannot start ESP-IDF {action}: {error}\n"
        log_path.write_text(log, encoding="utf-8")
        print(log, end="", flush=True)
        raise EspIdfError(log.rstrip()) from error
    log = completed.stdout or ""
    log_path.write_text(log, encoding="utf-8")
    print(log, end="", flush=True)
    if completed.returncode:
        raise EspIdfError(
            f"ESP-IDF {action} failed with exit code {completed.returncode}"
        )


def _clear_failure_diagnostic(build_dir: Path) -> None:
    (build_dir / FAILURE_DIAGNOSTIC_NAME).unlink(missing_ok=True)


def _report_failure(
    stage: str, error: BaseException, build_dir: Path | None
) -> None:
    message = f"build_esp_idf.py: stage={stage} failed: {error}"
    print(message, file=sys.stderr, flush=True)
    if build_dir is None:
        return
    try:
        build_dir.mkdir(parents=True, exist_ok=True)
        (build_dir / FAILURE_DIAGNOSTIC_NAME).write_text(
            message + "\n", encoding="utf-8"
        )
    except Exception as diagnostic_error:
        print(
            "build_esp_idf.py: cannot persist failure diagnostic: "
            f"{diagnostic_error}",
            file=sys.stderr,
            flush=True,
        )


def _pin_config(repo_root: Path) -> dict[str, str]:
    path = repo_root / "third_party/esp_idf_version.conf"
    config = component_manager.parse_config(path)
    component_manager.require_values(
        config, ("ESP_IDF_REF", "ESP_IDF_VERSION", "ESP_IDF_TARGETS"), path
    )
    return config


def prepare_feature_dependencies(
    repo_root: Path, model: Mapping[str, Any], *, verify_only: bool
) -> None:
    """Synchronize only third-party sources selected by the feature graph."""
    if "HAL_ENABLE_TLS" in model["resolvedFeatures"]:
        component_manager.ensure_git_component(
            "bearssl", repo_root, verify_only=verify_only
        )


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "flash", "artifacts"))
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument(
        "--repo-root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--idf-dir", default="")
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--board", default="")
    parser.add_argument("--name", default="")
    parser.add_argument("--output", default="")
    parser.add_argument("--source", action="append", default=[])
    parser.add_argument("--feature", action="append", default=[])
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--port", default="")
    parser.add_argument("--clean", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = create_parser().parse_args(argv)
    repo_root = arguments.repo_root.expanduser().resolve()
    build_dir: Path | None = None
    build_environment: Mapping[str, str] | None = None
    stage = "resolve-project"
    try:
        project_dir = resolve_project_dir(arguments.project)
        project_name = normalize_project_name(arguments.name, project_dir)
        stage = "resolve-build-model"
        model = resolve_build_model(
            repo_root,
            project_dir,
            target=arguments.target,
            board=arguments.board,
            project_name=project_name,
            requested_sources=arguments.source,
            features=arguments.feature,
            definitions=arguments.define,
        )
        stage = "resolve-build-directory"
        build_dir = resolve_build_dir(
            repo_root,
            project_dir,
            arguments.output,
            model["target"],
            model["board"],
        )
        stage = "validate-command"
        if arguments.action != "build" and arguments.clean:
            raise EspIdfError("--clean is supported only by the build action")
        if arguments.action == "flash" and not arguments.port:
            raise EspIdfError("The flash action requires --port")
        if arguments.action != "flash" and arguments.port:
            raise EspIdfError("--port is supported only by the flash action")

        stage = "load-version-pin"
        config = _pin_config(repo_root)
        if model["idfTarget"] not in config["ESP_IDF_TARGETS"].split(","):
            raise EspIdfError(
                f"ESP-IDF target {model['idfTarget']!r} is not enabled by the pin"
            )

        stage = "prepare-feature-dependencies"
        prepare_feature_dependencies(
            repo_root, model, verify_only=arguments.action != "build"
        )

        if arguments.action == "build":
            if arguments.clean and build_dir.exists():
                stage = "clean-build-directory"
                shutil.rmtree(build_dir)
            stage = "clear-failure-diagnostic"
            _clear_failure_diagnostic(build_dir)
            stage = "materialize-build-inputs"
            materialize_build_inputs(repo_root, project_dir, build_dir, model)
        else:
            stage = "clear-failure-diagnostic"
            _clear_failure_diagnostic(build_dir)

        if arguments.action in {"build", "flash"}:
            directory_override = arguments.idf_dir or os.environ.get(
                "JH_ESP_IDF_DIR", ""
            )
            stage = f"prepare-sdk-for-{arguments.action}"
            idf_dir = prepare_sdk(repo_root, directory_override)
            stage = f"export-{arguments.action}-environment"
            build_environment = exported_environment(idf_dir)

        if arguments.action == "build":
            stage = "idf-build"
            run_idf(
                repo_root,
                build_dir,
                idf_dir,
                model["idfTarget"],
                build_environment,
                "build",
            )
            stage = "toolchain-provenance"
            collect_toolchain_provenance(
                idf_dir,
                build_environment,
                build_dir,
                repo_root=repo_root,
                idf_version=config["ESP_IDF_VERSION"],
                idf_commit=config["ESP_IDF_REF"],
                idf_target=model["idfTarget"],
            )

        stage = "artifact-validation"
        manifest = validate_artifacts(
            repo_root,
            project_dir,
            build_dir,
            model,
            idf_version=config["ESP_IDF_VERSION"],
            idf_commit=config["ESP_IDF_REF"],
            build_environment=build_environment,
        )

        if arguments.action == "flash":
            stage = "idf-flash"
            run_idf(
                repo_root,
                build_dir,
                idf_dir,
                model["idfTarget"],
                build_environment,
                "flash",
                port=arguments.port,
            )
            stage = "validate-flash-log"
            _validate_clean_build_log(build_dir / FLASH_LOG_NAME)
            stage = "post-flash-toolchain-provenance"
            collect_toolchain_provenance(
                idf_dir,
                build_environment,
                build_dir,
                repo_root=repo_root,
                idf_version=config["ESP_IDF_VERSION"],
                idf_commit=config["ESP_IDF_REF"],
                idf_target=model["idfTarget"],
            )
            stage = "post-flash-artifact-validation"
            manifest = validate_artifacts(
                repo_root,
                project_dir,
                build_dir,
                model,
                idf_version=config["ESP_IDF_VERSION"],
                idf_commit=config["ESP_IDF_REF"],
                build_environment=build_environment,
            )
    except (
        OSError,
        ValueError,
        EspIdfError,
        component_manager.ComponentError,
        generate_hal_features.RegistryError,
    ) as error:
        _report_failure(stage, error, build_dir)
        return 1

    verb = "flashed" if arguments.action == "flash" else "verified"
    print(
        f"ESP-IDF {verb}: {manifest['target']}:{manifest['board']} with "
        f"{len(manifest['flashImages'])} flash images in {build_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
