#!/usr/bin/env python3
"""Validate native RP target resolution in the shared VS Code workflow."""

from __future__ import annotations

from contextlib import redirect_stderr
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from unittest import mock


ROOT = Path(sys.argv[1]).resolve()
ENTRY = ROOT / "vscode" / "entry" / (
    "jh-vscode.cmd" if sys.platform == "win32" else "jh-vscode"
)
CORE_RUNTIME = ROOT / "examples" / "01_core_runtime"
FREERTOS_SUITE = ROOT / "examples" / "18_freertos_suite"

sys.path.insert(0, str(ROOT / "scripts"))
import examples_dispatcher
import generate_hal_features


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def same_path(actual: str, expected: str | Path) -> bool:
    return Path(actual).resolve() == Path(expected).resolve()


def resolved(target: str, board: str) -> dict:
    result = subprocess.run(
        [
            str(ENTRY),
            "config-dump",
            "--project",
            str(CORE_RUNTIME),
            "--target",
            target,
            "--board",
            board,
            "--json",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)


expected = {
    "rp2040": ("pico", None),
    "rp2350-arm": ("pico2", None),
    "rp2350-riscv": (
        "pico2",
        str(ROOT / "third_party" / "riscv-toolchain"),
    ),
}
known_targets = {
    "rp2040",
    "rp2350-arm",
    "rp2350-riscv",
    "stm32g474",
}
profile_descriptors = [
    load_json(path) for path in sorted((ROOT / "boards" / "profiles").glob("*.json"))
]
target_boards = {
    target: {
        str(profile["id"])
        for profile in profile_descriptors
        if target in profile["compatibleTargets"]
    }
    for target in known_targets
}
for target, (board, toolchain) in expected.items():
    config = resolved(target, board)
    cache = config["cmake"]["cache"]
    require(
        same_path(
            config["buildDir"], ROOT / ".build" / "examples" / "01_core_runtime"
        ),
        f"{target}: buildDir escaped the central artifact tree",
    )
    require(
        cache["JH_ARTIFACT_DIR"] == config["buildDir"],
        f"{target}: firmware artifacts do not follow buildDir",
    )
    require(config["target"] == target, f"{target}: resolver changed target")
    require(config["board"] == board, f"{target}: resolver changed board")
    require(cache["JH_TARGET"] == target, f"{target}: JH_TARGET mismatch")
    require(cache["JH_BOARD"] == board, f"{target}: JH_BOARD mismatch")
    require(
        "PICO_PLATFORM" not in cache and "PICO_BOARD" not in cache,
        f"{target}: raw Pico SDK selectors leaked from the authoritative registry",
    )
    require(
        same_path(cache["PICO_SDK_PATH"], ROOT / "third_party" / "pico-sdk"),
        f"{target}: Pico SDK path mismatch",
    )
    require(
        "JH_PICOTOOL_EXECUTABLE" not in cache,
        f"{target}: target registry leaked a host-specific picotool path",
    )
    if toolchain is not None:
        require(
            same_path(cache["PICO_TOOLCHAIN_PATH"], toolchain),
            f"{target}: RISC-V toolchain path mismatch",
        )

stm32 = resolved("stm32g474", "nucleo-g474re")
stm32_cache = stm32["cmake"]["cache"]
require(
    stm32_cache["JH_BOARD"] == "nucleo-g474re",
    "stm32g474: JH_BOARD mismatch",
)
require(
    same_path(
        stm32_cache["CMAKE_TOOLCHAIN_FILE"],
        ROOT / "stm32_lib" / "toolchain_stm32g474.cmake",
    ),
    "stm32g474: cross toolchain must be selected before CMake project()",
)

core_runtime = load_json(
    CORE_RUNTIME / ".vscode" / "jaszczurhal.project.json"
)
core_runtime_targets = set(core_runtime["example"]["targets"])
require(
    {
        "rp2040",
        "rp2350-arm",
        "rp2350-riscv",
        "stm32g474",
    }.issubset(core_runtime_targets),
    "01_core_runtime does not expose the complete target matrix",
)

freertos_suite = load_json(
    FREERTOS_SUITE / ".vscode" / "jaszczurhal.project.json"
)
freertos_metadata = freertos_suite["example"]
network_variant = next(
    variant
    for variant in freertos_metadata["variants"]
    if variant["id"] == "network"
)
require(
    freertos_metadata["boards"]["rp2350-arm"] == "pico2w",
    "WiFi example does not map RP2350 ARM to Pico 2 W",
)
require(
    "rp2350-riscv" not in network_variant["targets"],
    "unsupported RP2350 RISC-V + CYW43 combination is selectable",
)
require(
    "HAL_ENABLE_WIFI" in network_variant["extraDefines"],
    "FreeRTOS network variant lost its WiFi feature",
)
require(
    freertos_metadata["boards"]["stm32g474"] == "nucleo-g474re-pim730",
    "WiFi example does not map STM32G474 to NUCLEO-G474RE with PIM730",
)
stm32_wifi = subprocess.run(
    [
        str(ENTRY),
        "config-dump",
        "--project",
        str(FREERTOS_SUITE),
        "--target",
        "stm32g474",
        "--board",
        "nucleo-g474re-pim730",
        "--variant",
        "network",
        "--json",
    ],
    check=True,
    capture_output=True,
    text=True,
)
stm32_wifi_config = json.loads(stm32_wifi.stdout)
require(
    stm32_wifi_config["board"] == "nucleo-g474re-pim730",
    "WiFi resolver changed the STM32G474 PIM730 board profile",
)
require(
    "HAL_ENABLE_WIFI"
    in stm32_wifi_config["cmake"]["cache"]["JH_EXTRA_DEFINES"].split(";"),
    "FreeRTOS network variant does not enable WiFi",
)
require(
    not any(
        define.startswith("HAL_CYW43_")
        for define in stm32_wifi_config["cmake"]["cache"][
            "JH_EXTRA_DEFINES"
        ].split(";")
    ),
    "FreeRTOS network variant duplicates PIM730 wiring outside the board profile",
)
sys.path.insert(0, str(ROOT))
from vscode.runtime import jh_vscode as workflow_runtime

require(
    not workflow_runtime.build_preflight_diagnostics(
        stm32_wifi_config, FREERTOS_SUITE
    ),
    "WiFi preflight does not accept the registry-owned PIM730 backend",
)

storage = load_json(
    ROOT / "examples" / "10_storage" / ".vscode" / "jaszczurhal.project.json"
)
require(
    set(expected).issubset(storage["example"]["targets"]),
    "10_storage: native storage target matrix is incomplete",
)
require(
    storage["target"] == "rp2040",
    "10_storage: native RP2040 is not the project default",
)

require(
    set(expected).issubset(freertos_metadata["targets"]),
    "18_freertos_suite does not expose the native RP target matrix",
)
require(
    freertos_suite["target"] == "rp2040",
    "18_freertos_suite does not default to the native RP2040 target",
)
require(
    freertos_suite["cmake"]["cache"]["JH_RP2040_FREERTOS"] is True,
    "18_freertos_suite does not enable native RP FreeRTOS",
)

ota_fixture = load_json(
    ROOT
    / "tests"
    / "hardware"
    / "rp_ota"
    / ".vscode"
    / "jaszczurhal.project.json"
)
require(
    set(ota_fixture["example"]["targets"]) == {"rp2040", "rp2350-arm"},
    "OTA hardware fixture target matrix changed",
)
require(
    ota_fixture["example"]["boards"]
    == {"rp2040": "picow", "rp2350-arm": "pico2w"},
    "OTA hardware fixture defaults changed from the supported W boards",
)
ota_variants = {
    variant["id"]: variant for variant in ota_fixture["example"]["variants"]
}
require(
    set(ota_variants["freertos"]["extraDefines"])
    == {"HAL_ENABLE_FREERTOS"},
    "OTA hardware fixture FreeRTOS variant lost required defines",
)
require(
    ota_fixture["ota"]["passwordEnv"] == "JH_OTA_TEST_PASSWORD",
    "OTA hardware fixture must not store a tracked password",
)

example_dirs = examples_dispatcher.selected_example_dirs([])
manifest_example_names = {
    path.parent.parent.name
    for path in (ROOT / "examples").glob(
        "[0-9][0-9]_*/.vscode/jaszczurhal.project.json"
    )
}
listed_examples = subprocess.run(
    [sys.executable, str(ROOT / "scripts" / "examples_dispatcher.py"), "list"],
    check=True,
    capture_output=True,
    text=True,
)
registered_names = {
    line.split(":", 1)[0] for line in listed_examples.stdout.splitlines()
}
examples_dispatcher.validate_example_registry()
require(
    len(examples_dispatcher.EXAMPLES) == 26 and len(registered_names) == 26,
    "dispatcher registry must contain exactly 26 active examples",
)
require(
    registered_names == manifest_example_names,
    "dispatcher registry and generated example manifests differ",
)

example_counts = {target: 0 for target in known_targets}
full_configuration_counts = {target: 0 for target in known_targets}
gate_configuration_counts = {target: 0 for target in known_targets}
legacy_coverage: list[str] = []
requested_example_features: set[str] = set()
for example_dir in example_dirs:
    manifest_path = example_dir / ".vscode" / "jaszczurhal.project.json"
    require(manifest_path.is_file(), f"{example_dir.name}: missing manifest")
    manifest = load_json(manifest_path)
    requested_example_features.update(
        re.findall(
            r"^\s*#\s*define\s+(HAL_ENABLE_[A-Z0-9_]+)",
            (example_dir / "hal_project_config.h").read_text(encoding="utf-8"),
            flags=re.MULTILINE,
        )
    )
    base_defines = manifest.get("cmake", {}).get("cache", {}).get(
        "JH_EXTRA_DEFINES", ""
    )
    requested_example_features.update(
        token.split("=", 1)[0]
        for token in str(base_defines).split(";")
        if token.startswith("HAL_ENABLE_")
    )
    metadata = manifest.get("example")
    require(
        isinstance(metadata, dict),
        f"{example_dir.name}: missing explicit target classification",
    )
    targets = {str(target) for target in metadata.get("targets", [])}
    require(targets, f"{example_dir.name}: empty target classification")
    require(
        targets.issubset(known_targets),
        f"{example_dir.name}: unknown target classification {targets}",
    )
    gate_targets = {
        str(target) for target in metadata.get("gateTargets", targets)
    }
    require(
        gate_targets.issubset(targets),
        f"{example_dir.name}: gateTargets escape supported targets",
    )
    covers = metadata.get("covers")
    require(
        isinstance(covers, list) and covers,
        f"{example_dir.name}: missing legacy coverage classification",
    )
    legacy_coverage.extend(str(item) for item in covers)
    boards = metadata.get("boards")
    require(
        isinstance(boards, dict),
        f"{example_dir.name}: missing board classification",
    )
    require(
        set(boards) == targets,
        f"{example_dir.name}: board classification does not match targets",
    )
    for target in targets:
        example_counts[target] += 1
        full_configuration_counts[target] += 1
        if target in gate_targets:
            gate_configuration_counts[target] += 1
        require(
            boards[target] in target_boards[target],
            f"{example_dir.name}: invalid {target} board {boards[target]}",
        )
    variants = metadata.get("variants", [])
    require(
        isinstance(variants, list),
        f"{example_dir.name}: invalid variant classification",
    )
    variant_ids = []
    for variant in variants:
        require(
            isinstance(variant, dict) and variant.get("id"),
            f"{example_dir.name}: variant is missing an id",
        )
        variant_ids.append(str(variant["id"]))
        requested_example_features.update(
            str(token).split("=", 1)[0]
            for token in variant.get("extraDefines", [])
            if str(token).startswith("HAL_ENABLE_")
        )
        variant_targets = {
            str(target) for target in variant.get("targets", targets)
        }
        require(
            variant_targets.issubset(targets),
            f"{example_dir.name}:{variant.get('id')}: variant target "
            "classification escapes its example",
        )
        variant_gate_targets = {
            str(target)
            for target in variant.get("gateTargets", variant_targets)
        }
        require(
            variant_gate_targets.issubset(variant_targets),
            f"{example_dir.name}:{variant.get('id')}: gateTargets escape "
            "variant targets",
        )
        for target in variant_targets:
            full_configuration_counts[target] += 1
            if target in variant_gate_targets:
                gate_configuration_counts[target] += 1
    require(
        len(variant_ids) == len(set(variant_ids)),
        f"{example_dir.name}: duplicate variant id",
    )

require(
    example_counts
    == {
        "rp2040": 25,
        "rp2350-arm": 24,
        "rp2350-riscv": 21,
        "stm32g474": 24,
    },
    f"declared example target matrix changed without review: {example_counts}",
)
require(
    full_configuration_counts
    == {
        "rp2040": 27,
        "rp2350-arm": 26,
        "rp2350-riscv": 22,
        "stm32g474": 25,
    }
    and sum(full_configuration_counts.values()) == 100,
    "full example build matrix must contain exactly 100 configurations: "
    f"{full_configuration_counts}",
)
require(
    gate_configuration_counts
    == {
        "rp2040": 27,
        "rp2350-arm": 0,
        "rp2350-riscv": 0,
        "stm32g474": 25,
    }
    and sum(gate_configuration_counts.values()) == 52,
    "example gate matrix must contain exactly 52 configurations: "
    f"{gate_configuration_counts}",
)
require(
    len(legacy_coverage) == 59
    and len(set(legacy_coverage)) == 59
    and set(legacy_coverage) == set(examples_dispatcher.LEGACY_EXAMPLE_IDS),
    "the 59 legacy examples must each be covered exactly once",
)

legacy_feature_surface = {
    "HAL_ENABLE_A7670",
    "HAL_ENABLE_ADP5360",
    "HAL_ENABLE_APP_TASK1",
    "HAL_ENABLE_BH1750",
    "HAL_ENABLE_BLE",
    "HAL_ENABLE_BLE_STREAM",
    "HAL_ENABLE_BSD_SOCKETS",
    "HAL_ENABLE_CJSON",
    "HAL_ENABLE_CRYPTO",
    "HAL_ENABLE_DACLESS",
    "HAL_ENABLE_DHT",
    "HAL_ENABLE_DISPLAY",
    "HAL_ENABLE_DS18B20",
    "HAL_ENABLE_DS3231",
    "HAL_ENABLE_EXTERNAL_ADC",
    "HAL_ENABLE_FREERTOS",
    "HAL_ENABLE_GPS",
    "HAL_ENABLE_HC595",
    "HAL_ENABLE_HD44780",
    "HAL_ENABLE_HTTP_CLIENT",
    "HAL_ENABLE_HTTP_FILES",
    "HAL_ENABLE_HTTP_SERVER",
    "HAL_ENABLE_I2C",
    "HAL_ENABLE_I2C_SLAVE",
    "HAL_ENABLE_ILI9341",
    "HAL_ENABLE_IRSMALL_DECODER",
    "HAL_ENABLE_JPEG_AS_BASE64",
    "HAL_ENABLE_KV",
    "HAL_ENABLE_LITTLEFS",
    "HAL_ENABLE_MAX6675",
    "HAL_ENABLE_MCP23017",
    "HAL_ENABLE_MCP2515",
    "HAL_ENABLE_MCP3221",
    "HAL_ENABLE_MCP4725",
    "HAL_ENABLE_MCP9600",
    "HAL_ENABLE_MFRC522",
    "HAL_ENABLE_MQTT",
    "HAL_ENABLE_NET_COMMANDS",
    "HAL_ENABLE_NET_CONSOLE",
    "HAL_ENABLE_OTA",
    "HAL_ENABLE_PCA9654E",
    "HAL_ENABLE_PCF8563",
    "HAL_ENABLE_PCF8574",
    "HAL_ENABLE_PGA2311",
    "HAL_ENABLE_PN532",
    "HAL_ENABLE_PNG_AS_BASE64",
    "HAL_ENABLE_RGB_LED",
    "HAL_ENABLE_RTC",
    "HAL_ENABLE_SDLOGGER",
    "HAL_ENABLE_SSD1306",
    "HAL_ENABLE_SSD16XX",
    "HAL_ENABLE_STM32G474_FDCAN",
    "HAL_ENABLE_STMPE610",
    "HAL_ENABLE_SWSERIAL",
    "HAL_ENABLE_TIME",
    "HAL_ENABLE_TLS",
    "HAL_ENABLE_TSC2007",
    "HAL_ENABLE_UART",
    "HAL_ENABLE_WEBSOCKET",
    "HAL_ENABLE_WIFI",
    "HAL_ENABLE_WIREGUARD",
}
feature_model = generate_hal_features.load_registry(ROOT / "config")
resolved_example_features = set(
    feature_model.resolve_many(requested_example_features)
)
missing_legacy_features = sorted(
    legacy_feature_surface.difference(resolved_example_features)
)
require(
    not missing_legacy_features,
    "consolidated examples lost legacy feature coverage: "
    + ", ".join(missing_legacy_features),
)

serial_gps = load_json(
    ROOT / "examples" / "05_serial_gps" / ".vscode" / "jaszczurhal.project.json"
)
serial_variants = {
    variant["id"]: variant for variant in serial_gps["example"]["variants"]
}
require(
    set(serial_variants["swserial"]["targets"])
    == {"rp2040", "rp2350-arm", "rp2350-riscv"}
    and set(serial_variants["swserial"]["gateTargets"]) == {"rp2040"},
    "05_serial_gps:swserial must remain RP-only",
)

ble_stream = load_json(
    ROOT / "examples" / "26_ble_stream" / ".vscode" / "jaszczurhal.project.json"
)
require(
    set(ble_stream["example"]["covers"])
    == {"58_ble_peripheral", "59_ble_stream"}
    and set(ble_stream["example"]["targets"]) == {"rp2040", "stm32g474"},
    "26_ble_stream no longer represents both supported BLE examples",
)


def require_parity_fixture(name: str, base_define: str) -> None:
    manifest = load_json(
        ROOT
        / "tests"
        / "hardware"
        / name
        / ".vscode"
        / "jaszczurhal.project.json"
    )
    metadata = manifest["example"]
    require(
        set(metadata["targets"]) == set(expected),
        f"{name}: native target matrix is incomplete",
    )
    require(
        metadata["boards"]
        == {
            "rp2040": "pico",
            "rp2350-arm": "pico2",
            "rp2350-riscv": "pico2",
        },
        f"{name}: plain-board matrix changed",
    )
    variants = {variant["id"]: variant for variant in metadata["variants"]}
    require(
        set(variants) == {"freertos"},
        f"{name}: runtime matrix must contain the FreeRTOS variant",
    )
    require(
        set(variants["freertos"]["targets"]) == set(expected),
        f"{name}: FreeRTOS target matrix is incomplete",
    )
    require(
        set(variants["freertos"]["extraDefines"])
        == {"HAL_ENABLE_FREERTOS"},
        f"{name}: FreeRTOS feature classification changed",
    )
    header = (ROOT / "tests" / "hardware" / name / "hal_project_config.h")
    require(
        base_define in header.read_text(encoding="utf-8"),
        f"{name}: base feature is missing from hal_project_config.h",
    )
    require(
        base_define
        not in manifest["cmake"]["cache"].get("JH_EXTRA_DEFINES", "").split(";"),
        f"{name}: base feature is duplicated in the manifest",
    )


require_parity_fixture("rp_usb_multicore", "HAL_ENABLE_APP_TASK1")
require_parity_fixture("rp_sdlogger", "HAL_ENABLE_SDLOGGER")

required_tasks = {
    "Project: Build",
    "Project: Upload",
    "Project: Upload (OTA)",
    "Project: Discover OTA devices",
    "Project: Serial Monitor",
    "Project: Clean",
    "Project: Sync board picker",
}
tasks = load_json(CORE_RUNTIME / ".vscode" / "tasks.json")
labels = {task.get("label") for task in tasks["tasks"]}
require(required_tasks.issubset(labels), "stable VS Code task verbs changed")
reference_tasks = load_json(ROOT / "vscode" / "examples" / "tasks.json")
reference_labels = {
    task.get("label")
    for task in reference_tasks["tasks"]
}
require(
    required_tasks.issubset(reference_labels),
    "reference VS Code tasks omitted stable workflow actions",
)

example_sync_task = next(
    task
    for task in tasks["tasks"]
    if task.get("label") == "Project: Sync board picker"
)
require(
    example_sync_task.get("runOptions", {}).get("runOn") == "folderOpen",
    "example board picker synchronization is not automatic",
)

with tempfile.TemporaryDirectory(prefix="jh-vscode-project-") as temp_dir:
    project_dir = Path(temp_dir) / "generated"
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "vscode" / "tools" / "create-vscode-example.py"),
            "--output",
            str(project_dir),
            "--name",
            "Generated workflow test",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    generated_tasks_path = project_dir / ".vscode" / "tasks.json"
    generated_launch_path = project_dir / ".vscode" / "launch.json"
    generated_tasks = load_json(generated_tasks_path)
    generated_by_label = {
        task.get("label"): task
        for task in generated_tasks["tasks"]
    }
    require(
        required_tasks.issubset(generated_by_label),
        "standalone project generator omitted stable VS Code tasks",
    )
    require(
        generated_by_label["Project: Upload (OTA)"].get("args")
        == [
            "upload-ota",
            "--project",
            "${workspaceFolder}",
            "--interactive",
        ],
        "standalone project generator emitted an invalid OTA upload task",
    )
    require(
        generated_by_label["Project: Discover OTA devices"].get("args")
        == ["ota-discover", "--project", "${workspaceFolder}"],
        "standalone project generator emitted an invalid OTA discovery task",
    )
    require(
        generated_by_label["Project: Sync board picker"]
        .get("runOptions", {})
        .get("runOn")
        == "folderOpen",
        "standalone project board picker synchronization is not automatic",
    )

    board_input = next(
        item
        for item in generated_tasks["inputs"]
        if item.get("id") == "boardSelection"
    )
    expected_options = board_input["options"]
    expected_default = board_input["default"]
    board_input["options"] = ["stale:board - Stale board"]
    board_input["default"] = "stale:board - Stale board"
    generated_tasks["tasks"] = [
        task
        for task in generated_tasks["tasks"]
        if task.get("label") != "Project: Sync board picker"
    ]
    generated_tasks["tasks"].append(
        {
            "label": "Project: Consumer custom task",
            "type": "shell",
            "command": "true",
        }
    )
    generated_tasks_path.write_text(
        json.dumps(generated_tasks, indent=2) + "\n",
        encoding="utf-8",
    )
    generated_launch = load_json(generated_launch_path)
    legacy_profile = dict(generated_launch["configurations"][0])
    legacy_profile["name"] = "Debug: RP2040 (Pico/Pico W/Zero/Plus)"
    custom_profile = {
        "name": "Consumer: Custom debugger",
        "type": "cppdbg",
        "request": "launch",
    }
    generated_launch["configurations"] = [legacy_profile, custom_profile]
    generated_launch_path.write_text(
        json.dumps(generated_launch, indent=2) + "\n",
        encoding="utf-8",
    )

    subprocess.run(
        [
            str(ENTRY),
            "sync-board-picker",
            "--project",
            str(project_dir),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    synchronized_tasks = load_json(generated_tasks_path)
    synchronized_input = next(
        item
        for item in synchronized_tasks["inputs"]
        if item.get("id") == "boardSelection"
    )
    require(
        synchronized_input["options"] == expected_options,
        "sync-board-picker did not restore registry options",
    )
    require(
        synchronized_input["default"] == expected_default,
        "sync-board-picker did not restore the project default",
    )
    synchronized_labels = {
        task.get("label")
        for task in synchronized_tasks["tasks"]
    }
    require(
        "Project: Consumer custom task" in synchronized_labels,
        "sync-board-picker removed a consumer task",
    )
    require(
        "Project: Sync board picker" in synchronized_labels,
        "sync-board-picker did not install its automatic task",
    )
    synchronized_launch = load_json(generated_launch_path)
    synchronized_profiles = {
        profile.get("name"): profile
        for profile in synchronized_launch["configurations"]
    }
    require(
        "Debug: RP2040 (Pico/Pico W/Zero/Plus)" not in synchronized_profiles,
        "sync-board-picker did not remove a legacy managed debug profile",
    )
    require(
        {
            "Project: Debug Firmware",
            "Project: Debug Firmware (RP2350 ARM)",
            "Project: Debug Firmware (STM32G474 / ST-Link)",
            "Consumer: Custom debugger",
        }
        == set(synchronized_profiles),
        "sync-board-picker did not install managed profiles or preserve a custom profile",
    )
    synchronized_stm32 = synchronized_profiles[
        "Project: Debug Firmware (STM32G474 / ST-Link)"
    ]
    require(
        synchronized_stm32["configFiles"] == ["board/st_nucleo_g4.cfg"],
        "sync-board-picker installed an invalid STM32G474 OpenOCD profile",
    )
    require(
        all(
            profile.get("executable") == "${workspaceFolder}/.build/firmware.elf"
            for name, profile in synchronized_profiles.items()
            if name.startswith("Project: Debug Firmware")
        ),
        "sync-board-picker did not derive the debug artifact from the manifest",
    )

    synchronized_tasks.pop("inputs")
    generated_tasks_path.write_text(
        json.dumps(synchronized_tasks, indent=2) + "\n",
        encoding="utf-8",
    )
    generated_launch_path.unlink()
    subprocess.run(
        [
            str(ENTRY),
            "sync-board-picker",
            "--project",
            str(project_dir),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    synchronized_tasks = load_json(generated_tasks_path)
    require(
        any(
            item.get("id") == "boardSelection"
            for item in synchronized_tasks["inputs"]
        ),
        "sync-board-picker did not restore a missing inputs section",
    )
    require(
        {
            profile.get("name")
            for profile in load_json(generated_launch_path)["configurations"]
        }
        == {
            "Project: Debug Firmware",
            "Project: Debug Firmware (RP2350 ARM)",
            "Project: Debug Firmware (STM32G474 / ST-Link)",
        },
        "sync-board-picker did not recreate a missing launch.json",
    )

    synchronized_text = generated_tasks_path.read_text(encoding="utf-8")
    synchronized_launch_text = generated_launch_path.read_text(encoding="utf-8")
    subprocess.run(
        [
            str(ENTRY),
            "sync-board-picker",
            "--project",
            str(project_dir),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(
        generated_tasks_path.read_text(encoding="utf-8") == synchronized_text,
        "sync-board-picker rewrote an already current tasks.json",
    )
    require(
        generated_launch_path.read_text(encoding="utf-8") == synchronized_launch_text,
        "sync-board-picker rewrote an already current launch.json",
    )


def write_feature_value_fixture(
    project_dir: Path,
    *,
    cache: dict[str, object] | None = None,
    target_profiles: dict[str, object] | None = None,
    variants: list[dict[str, object]] | None = None,
    header: str = "#pragma once\n",
) -> None:
    manifest: dict[str, object] = {
        "project": "jh-vscode feature value test",
        "module": "feature_value_test",
        "toolchain": "cmake",
        "target": "rp2040",
        "board": "pico",
        "buildDir": "${project}/.build",
        "cmakeBuildDir": "${project}/.build/cmake",
        "cmake": {
            "sourceDir": str(ROOT / "cmake" / "jh_firmware_project"),
            "cache": {
                "JH_PROJECT_DIR": "${project}",
                "JH_MODULE_NAME": "feature_value_test",
                **(cache or {}),
            },
        },
    }
    if target_profiles is not None:
        manifest["targetProfiles"] = target_profiles
    if variants is not None:
        manifest["example"] = {"variants": variants}
    vscode_dir = project_dir / ".vscode"
    vscode_dir.mkdir(parents=True)
    (vscode_dir / "jaszczurhal.project.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    (project_dir / "hal_project_config.h").write_text(header, encoding="utf-8")


def run_feature_value_dump(
    project_dir: Path, *extra_args: str
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            str(ENTRY),
            "config-dump",
            "--project",
            str(project_dir),
            *extra_args,
            "--json",
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def require_value_rejection(
    result: subprocess.CompletedProcess[str], symbol: str, source: str
) -> None:
    require(
        result.returncode == workflow_runtime.EXIT_CONFIG,
        f"invalid {symbol} returned {result.returncode}: {result.stderr}",
    )
    require("[JH-CFG-VALUE]" in result.stderr, f"{symbol}: missing diagnostic id")
    require(symbol in result.stderr, f"{symbol}: missing symbol in diagnostic")
    require(source in result.stderr, f"{symbol}: missing source in diagnostic")


with tempfile.TemporaryDirectory(prefix="jh-vscode-feature-values-") as temp_dir:
    fixture_root = Path(temp_dir)

    base_invalid = fixture_root / "base-invalid"
    write_feature_value_fixture(
        base_invalid,
        cache={"JH_EXTRA_DEFINES": "HAL_ENABLE_WIFI=0"},
    )
    require_value_rejection(
        run_feature_value_dump(base_invalid),
        "HAL_ENABLE_WIFI",
        "cmake.cache.JH_EXTRA_DEFINES",
    )

    genex_invalid = fixture_root / "genex-invalid"
    write_feature_value_fixture(
        genex_invalid,
        cache={
            "JH_EXTRA_DEFINES": "$<1:HAL_$<1:ENABLE>_MQTT=0>",
        },
    )
    require_value_rejection(
        run_feature_value_dump(genex_invalid),
        "HAL_$<1:ENABLE>_MQTT",
        "cmake.cache.JH_EXTRA_DEFINES",
    )

    whitespace_invalid = fixture_root / "whitespace-invalid"
    write_feature_value_fixture(
        whitespace_invalid,
        cache={"EXTRA_HAL_DEFINES": "HAL_ENABLE_UDP=1 HAL_ENABLE_TCP"},
    )
    require_value_rejection(
        run_feature_value_dump(whitespace_invalid),
        "HAL_ENABLE_UDP",
        "cmake.cache.EXTRA_HAL_DEFINES",
    )

    spaced_assignment_invalid = fixture_root / "spaced-assignment-invalid"
    write_feature_value_fixture(
        spaced_assignment_invalid,
        cache={"JH_EXTRA_DEFINES": "HAL_ENABLE_WIFI = 1"},
    )
    require_value_rejection(
        run_feature_value_dump(spaced_assignment_invalid),
        "HAL_ENABLE_WIFI",
        "cmake.cache.JH_EXTRA_DEFINES",
    )

    direct_cache_invalid = fixture_root / "direct-cache-invalid"
    write_feature_value_fixture(
        direct_cache_invalid,
        cache={"HAL_ENABLE_WIFI": 0},
    )
    require_value_rejection(
        run_feature_value_dump(direct_cache_invalid),
        "HAL_ENABLE_WIFI",
        "cmake.cache.HAL_ENABLE_WIFI",
    )
    for action in ("build", "upload"):
        args = workflow_runtime.build_parser().parse_args(
            [action, "--project", str(base_invalid)]
        )
        with mock.patch.object(
            workflow_runtime, "configure_cmake_project"
        ) as configure:
            with redirect_stderr(io.StringIO()) as stderr:
                status = workflow_runtime.dispatch(args)
        require(
            status == workflow_runtime.EXIT_CONFIG,
            f"{action}: invalid feature value did not return EXIT_CONFIG: "
            f"{stderr.getvalue()}",
        )
        configure.assert_not_called()

    profile_invalid = fixture_root / "profile-invalid"
    write_feature_value_fixture(
        profile_invalid,
        target_profiles={
            "rp2350-arm": {
                "cmake": {
                    "cache": {
                        "EXTRA_HAL_DEFINES": "-DHAL_ENABLE_TLS=2",
                    }
                }
            }
        },
    )
    require_value_rejection(
        run_feature_value_dump(
            profile_invalid,
            "--target",
            "rp2350-arm",
            "--board",
            "pico2",
        ),
        "HAL_ENABLE_TLS",
        "cmake.cache.EXTRA_HAL_DEFINES",
    )

    variant_invalid = fixture_root / "variant-invalid"
    write_feature_value_fixture(
        variant_invalid,
        variants=[
            {
                "id": "invalid",
                "extraDefines": ["HAL_ENABLE_UDP", "HAL_ENABLE_TCP=false"],
            }
        ],
    )
    require_value_rejection(
        run_feature_value_dump(variant_invalid, "--variant", "invalid"),
        "HAL_ENABLE_TCP",
        "cmake.cache.JH_EXTRA_DEFINES",
    )

    header_invalid = fixture_root / "header-invalid"
    write_feature_value_fixture(
        header_invalid,
        header=(
            "#pragma once\n"
            "#define HAL_ENDPOINT \"https://example.invalid/*\"\n"
            "// another marker /*\n"
            "#define \\\n HAL_ENABLE_MQTT /* value follows\n"
            "the multiline comment */ 0\n"
        ),
    )
    require_value_rejection(
        run_feature_value_dump(header_invalid),
        "HAL_ENABLE_MQTT",
        "hal_project_config.h:4",
    )

    valid = fixture_root / "valid"
    write_feature_value_fixture(
        valid,
        cache={
            "JH_EXTRA_DEFINES": "HAL_ENABLE_WIFI;-DHAL_ENABLE_TLS=1",
            "EXTRA_HAL_DEFINES": "HAL_ENABLE_UDP=1;HAL_ENABLE_TCP",
        },
        target_profiles={
            "rp2350-arm": {
                "cmake": {
                    "cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_TIME=0"}
                }
            }
        },
        variants=[
            {
                "id": "inactive-invalid",
                "extraDefines": ["HAL_ENABLE_HTTP_SERVER=0"],
            }
        ],
        header=(
            "#pragma once\n"
            "#define HAL_ENABLE_MQTT\n"
            "#define HAL_ENABLE_TIME 1 // explicit enabled value\n"
            "// hidden by a continued line comment \\\n"
            "#define HAL_ENABLE_WIFI 0\n"
        ),
    )
    valid_result = run_feature_value_dump(valid)
    require(
        valid_result.returncode == 0,
        f"valid bare/=1 definitions were rejected: {valid_result.stderr}",
    )

    resolved_network = fixture_root / "resolved-network"
    write_feature_value_fixture(
        resolved_network,
        header=(
            "#pragma once\n"
            "#define HAL_ENABLE_HTTP_CLIENT 1\n"
            "#define HAL_DISABLE_ASSERTS\n"
        ),
    )
    resolved_network_result = run_feature_value_dump(
        resolved_network,
        "--target",
        "stm32g474",
        "--board",
        "nucleo-g474re",
    )
    require(
        resolved_network_result.returncode == 0,
        "resolved feature configuration was rejected: "
        f"{resolved_network_result.stderr}",
    )
    resolved_network_config = json.loads(resolved_network_result.stdout)
    feature_resolution = resolved_network_config["featureResolution"]
    require(
        feature_resolution["requestedFeatures"]
        == ["HAL_DISABLE_ASSERTS", "HAL_ENABLE_HTTP_CLIENT"],
        "jh-vscode changed the direct feature request set",
    )
    require(
        {
            "HAL_DISABLE_ASSERTS",
            "HAL_ENABLE_HTTP_CLIENT",
            "HAL_ENABLE_NETWORK_CORE",
            "HAL_ENABLE_TCP",
            "HAL_ENABLE_WIFI",
        }.issubset(feature_resolution["resolvedFeatures"]),
        "jh-vscode did not resolve the HTTP client dependency chain",
    )
    require(
        len(feature_resolution["resolvedFeaturesDigest"]) == 64,
        "jh-vscode omitted the deterministic resolved feature digest",
    )
    require(
        "HAL_DISABLE_ASSERTS" in feature_resolution["provenance"],
        "jh-vscode omitted HAL_DISABLE_* provenance",
    )
    require(
        "HAL_ENABLE_TCP"
        not in json.dumps(resolved_network_config["cmake"]["cache"]),
        "jh-vscode injected resolved features into requested CMake inputs",
    )
    network_diagnostics = workflow_runtime.build_preflight_diagnostics(
        resolved_network_config, resolved_network
    )
    require(
        any(
            "CYW43 gSPI/lwIP backend profile" in diagnostic
            and "HAL_ENABLE_TCP" in diagnostic
            for diagnostic in network_diagnostics
        ),
        "STM32 preflight ignored an implied network feature",
    )

    overridden = fixture_root / "overridden"
    write_feature_value_fixture(
        overridden,
        cache={"JH_EXTRA_DEFINES": "HAL_ENABLE_WIFI=0"},
        target_profiles={
            "rp2040": {
                "cmake": {
                    "cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_WIFI=1"}
                }
            }
        },
    )
    overridden_result = run_feature_value_dump(overridden)
    require(
        overridden_result.returncode == 0,
        "feature values were validated before the active target profile merge: "
        f"{overridden_result.stderr}",
    )
