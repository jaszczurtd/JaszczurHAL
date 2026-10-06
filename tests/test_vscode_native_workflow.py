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


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
ENTRY = ROOT / "vscode" / "entry" / (
    "jh-vscode.cmd" if sys.platform == "win32" else "jh-vscode"
)
CORE_RUNTIME = ROOT / "examples" / "01_core_runtime"
FREERTOS_SUITE = ROOT / "examples" / "18_freertos_suite"

sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT))
import examples_dispatcher
import generate_hal_features
from vscode.runtime import jh_vscode as workflow_runtime


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def same_path(actual: str, expected: str | Path) -> bool:
    return Path(actual).resolve() == Path(expected).resolve()


def builds_of(project: Path) -> dict[str, list[str | None]]:
    """Variants (None for the base) each target of a project builds."""
    manifest = load_json(project / ".vscode" / "jaszczurhal.project.json")
    result: dict[str, list[str | None]] = {}
    for build in workflow_runtime.project_builds(project, manifest):
        result.setdefault(build.target.id, []).append(
            build.variant.id if build.variant else None
        )
    return result


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
    "esp32s3",
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
        ROOT / "link_libraries/stm32_lib" / "toolchain_stm32g474.cmake",
    ),
    "stm32g474: cross toolchain must be selected before CMake project()",
)

require(
    {
        "rp2040",
        "rp2350-arm",
        "rp2350-riscv",
        "stm32g474",
    }.issubset(builds_of(CORE_RUNTIME)),
    "01_core_runtime does not expose the complete target matrix",
)

freertos_suite = load_json(
    FREERTOS_SUITE / ".vscode" / "jaszczurhal.project.json"
)
freertos_builds = builds_of(FREERTOS_SUITE)
require(
    "NETWORK" not in freertos_builds["rp2350-riscv"]
    and all(
        "NETWORK" in freertos_builds[target]
        for target in ("rp2040", "rp2350-arm", "stm32g474", "esp32s3")
    ),
    "unsupported RP2350 RISC-V + CYW43 combination is selectable",
)
require(
    workflow_runtime.load_project_config(
        FREERTOS_SUITE, target_override="rp2350-arm", use_local_state=False
    )["board"]
    == "pico2w",
    "WiFi example does not map RP2350 ARM to Pico 2 W",
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
        "NETWORK",
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
    stm32_wifi_config["cmake"]["cache"]["JH_VARIANT"] == "NETWORK"
    and "HAL_ENABLE_WIFI"
    in stm32_wifi_config["featureResolution"]["requestedFeatures"],
    "FreeRTOS network variant does not enable WiFi",
)
require(
    not any(
        name.startswith("HAL_CYW43_")
        for name in workflow_runtime.project_build(
            stm32_wifi_config, FREERTOS_SUITE
        ).config.macros
    ),
    "FreeRTOS network variant duplicates PIM730 wiring outside the board profile",
)
require(
    not workflow_runtime.build_preflight_diagnostics(
        stm32_wifi_config, FREERTOS_SUITE
    ),
    "WiFi preflight does not accept the registry-owned PIM730 backend",
)

lora_project = ROOT / "examples" / "27_lora_point_to_point"
lora_responder_result = subprocess.run(
    [
        str(ENTRY),
        "config-dump",
        "--project",
        str(lora_project),
        "--target",
        "rp2040",
        "--board",
        "rp2040-lora-lf",
        "--variant",
        "RESPONDER",
        "--json",
    ],
    check=True,
    capture_output=True,
    text=True,
)
lora_responder = json.loads(lora_responder_result.stdout)
lora_base_build_dir = ROOT / ".build" / "examples" / "27_lora_point_to_point"
lora_responder_build_dir = lora_base_build_dir / "variants" / "RESPONDER"
require(
    same_path(lora_responder["buildDir"], lora_responder_build_dir),
    "example variant shares the base stable artifact directory",
)
require(
    same_path(
        lora_responder["cmake"]["cache"]["JH_ARTIFACT_DIR"],
        lora_responder_build_dir,
    ),
    "example variant CMake artifacts share the base stable artifact directory",
)
require(
    same_path(
        lora_responder["artifacts"]["uf2"],
        lora_responder_build_dir / "firmware.uf2",
    ),
    "example variant upload artifact still resolves to the base firmware",
)
require(
    same_path(
        lora_responder["cmakeBuildDir"],
        lora_base_build_dir / "cmake" / "variants" / "RESPONDER",
    ),
    "example variant lost its isolated CMake directory",
)


def variant_dump(project: Path, variant: str | None) -> dict:
    command = [str(ENTRY), "config-dump", "--project", str(project), "--json"]
    if variant is not None:
        command += ["--variant", variant]
    result = subprocess.run(command, capture_output=True, text=True)
    require(result.returncode == 0, f"config-dump failed: {result.stderr}")
    return json.loads(result.stdout)


# A project declares its variants in hal_project_config.h. Its manifest leaves
# JH_ARTIFACT_DIR to the CMake default, which is the base .build directory, so
# the variant must still publish into its own directory.
with tempfile.TemporaryDirectory(prefix="jh-vscode-variants-") as temp_dir:
    variant_project = Path(temp_dir) / "consumer"
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "vscode" / "tools" / "create-vscode-example.py"),
            "--output",
            str(variant_project),
            "--name",
            "Variant workflow test",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    manifest_path = variant_project / ".vscode" / "jaszczurhal.project.json"
    manifest = load_json(manifest_path)
    manifest["cmake"]["cache"].pop("JH_ARTIFACT_DIR", None)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    header_path = variant_project / "hal_project_config.h"
    header_path.write_text(
        header_path.read_text(encoding="utf-8")
        + '\n#define JH_PROJECT_VARIANTS(X) X(BENCH, "Bench test build", BENCH=1)\n',
        encoding="utf-8",
    )

    base = variant_dump(variant_project, None)
    bench = variant_dump(variant_project, "BENCH")
    bench_dir = Path(base["buildDir"]) / "variants" / "BENCH"
    require(
        same_path(bench["buildDir"], bench_dir),
        "project variant shares the base build directory",
    )
    require(
        same_path(bench["cmake"]["cache"].get("JH_ARTIFACT_DIR", ""), bench_dir),
        "project variant without a base JH_ARTIFACT_DIR publishes into the base directory",
    )
    require(
        bench["cmake"]["cache"]["JH_VARIANT"] == "BENCH"
        and bench["module"] == f"{base['module']}_BENCH"
        and bench["cmake"]["cache"]["JH_MODULE_NAME"] == bench["module"],
        "project variant is not selected through JH_VARIANT under <module>_<id>",
    )
    require(
        "JH_ARTIFACT_DIR" not in base["cmake"]["cache"]
        and "JH_VARIANT" not in base["cmake"]["cache"],
        "base build gained an artifact directory or a variant it did not declare",
    )

    custom_dir = Path(temp_dir) / "artifacts"
    manifest["cmake"]["cache"]["JH_ARTIFACT_DIR"] = str(custom_dir)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    require(
        same_path(
            variant_dump(variant_project, "BENCH")["cmake"]["cache"]["JH_ARTIFACT_DIR"],
            custom_dir / "variants" / "BENCH",
        ),
        "project variant publishes into a custom base artifact directory",
    )

    unknown_variant = subprocess.run(
        [str(ENTRY), "config-dump", "--project", str(variant_project), "--variant", "OTHER"],
        capture_output=True,
        text=True,
    )
    require(
        unknown_variant.returncode != 0
        and "unknown variant 'OTHER'; known variants: BENCH" in unknown_variant.stderr,
        "an undeclared variant was accepted",
    )

    from vscode_task_config import project_tasks_document  # noqa: E402

    variant_tasks = {
        task["label"]: task
        for task in project_tasks_document(
            workflow_runtime.load_target_registry(),
            "rp2040",
            "pico",
            module="ECU",
            project_dir=variant_project,
        )["tasks"]
    }
    for action, label in (
        ("build", "Project: Build variant: BENCH"),
        ("upload", "Project: Upload variant: BENCH"),
    ):
        require(
            variant_tasks.get(label, {}).get("args")
            == [action, "--project", "${workspaceFolder}", "--variant", "BENCH"],
            f"variant task '{label}' is missing or runs the wrong action",
        )
    require(
        variant_tasks["Project: Upload variant: BENCH"]["detail"]
        == "Build and upload the BENCH variant: Bench test build.",
        "project variant upload task lost its description from the header",
    )

storage = load_json(
    ROOT / "examples" / "10_storage" / ".vscode" / "jaszczurhal.project.json"
)
require(
    set(expected).issubset(builds_of(ROOT / "examples" / "10_storage")),
    "10_storage: native storage target matrix is incomplete",
)
require(
    storage["target"] == "rp2040",
    "10_storage: native RP2040 is not the project default",
)

require(
    set(expected).issubset(freertos_builds),
    "18_freertos_suite does not expose the native RP target matrix",
)
require(
    freertos_suite["target"] == "rp2040",
    "18_freertos_suite does not default to the native RP2040 target",
)
require(
    "HAL_ENABLE_FREERTOS"
    in (ROOT / "examples" / "18_freertos_suite" / "hal_project_config.h").read_text(
        encoding="utf-8"
    ),
    "18_freertos_suite does not enable FreeRTOS through project configuration",
)

example_dirs = examples_dispatcher.example_dirs()
manifest_example_names = {
    path.parent.parent.name
    for path in (ROOT / "examples").glob("*/.vscode/jaszczurhal.project.json")
}
listed_examples = subprocess.run(
    [sys.executable, str(ROOT / "scripts" / "examples_dispatcher.py"), "list"],
    check=True,
    capture_output=True,
    text=True,
)
listed_names = {
    line.split(":", 1)[0] for line in listed_examples.stdout.splitlines()
}
require(
    len(example_dirs) == 30 and listed_names == manifest_example_names
    and {path.name for path in example_dirs} == manifest_example_names,
    "every directory under examples/ must be one of the 30 example projects",
)

example_counts = {target: 0 for target in known_targets}
full_configuration_counts = {target: 0 for target in known_targets}
requested_example_features: set[str] = set()
for example_dir in example_dirs:
    manifest = load_json(example_dir / ".vscode" / "jaszczurhal.project.json")
    builds = workflow_runtime.project_builds(example_dir, manifest)
    targets = {build.target.id for build in builds}
    require(
        targets and targets.issubset(known_targets),
        f"{example_dir.name}: unknown target classification {targets}",
    )
    for target in targets:
        example_counts[target] += 1
        board = workflow_runtime.load_project_config(
            example_dir, target_override=target, use_local_state=False
        )["board"]
        require(
            board in target_boards[target],
            f"{example_dir.name}: invalid {target} board {board}",
        )
    for build in builds:
        full_configuration_counts[build.target.id] += 1
        requested_example_features.update(build.requested_features())

require(
    example_counts
    == {
        "rp2040": 29,
        "rp2350-arm": 28,
        "rp2350-riscv": 22,
        "stm32g474": 27,
        "esp32s3": 23,
    },
    f"declared example target matrix changed without review: {example_counts}",
)
require(
    full_configuration_counts
    == {
        "rp2040": 46,
        "rp2350-arm": 39,
        "rp2350-riscv": 25,
        "stm32g474": 42,
        "esp32s3": 26,
    }
    and sum(full_configuration_counts.values()) == 178,
    "full example build matrix must contain exactly 178 configurations: "
    f"{full_configuration_counts}",
)
# The examples gate builds every configuration on its three targets.
require(
    sum(full_configuration_counts[target] for target in ("rp2040", "stm32g474", "esp32s3"))
    == 114,
    "example gate matrix must contain exactly 114 configurations",
)
required_feature_surface = {
    "HAL_ENABLE_A7670",
    "HAL_ENABLE_ADP5360",
    "HAL_ENABLE_APP_TASK1",
    "HAL_ENABLE_BH1750",
    "HAL_ENABLE_BLE",
    "HAL_ENABLE_BLE_COMMANDS",
    "HAL_ENABLE_BLE_STREAM",
    "HAL_ENABLE_BLUETOOTH_A2DP_SINK",
    "HAL_ENABLE_BLUETOOTH_AVRCP_TARGET",
    "HAL_ENABLE_BLUETOOTH_CLASSIC",
    "HAL_ENABLE_BLUETOOTH_GAMEPAD",
    "HAL_ENABLE_BLUETOOTH_HID_HOST",
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
    "HAL_ENABLE_INTERNAL_RTC",
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
    "HAL_ENABLE_SERIAL_COMMANDS",
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
missing_required_features = sorted(
    required_feature_surface.difference(resolved_example_features)
)
require(
    not missing_required_features,
    "active examples lost required feature coverage: "
    + ", ".join(missing_required_features),
)

serial_gps_builds = builds_of(ROOT / "examples" / "05_serial_gps")
require(
    {target for target, variants in serial_gps_builds.items() if "SWSERIAL" in variants}
    == {"rp2040", "rp2350-arm", "rp2350-riscv"},
    "05_serial_gps:swserial must remain RP-only",
)

require(
    set(builds_of(ROOT / "examples" / "26_ble_stream"))
    == {"rp2040", "rp2350-arm", "stm32g474"},
    "26_ble_stream no longer represents the supported BLE targets",
)


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
    manifest_fields: dict[str, object] | None = None,
    header: str = "#pragma once\n",
    board: str = "pico",
) -> None:
    manifest: dict[str, object] = {
        "project": "jh-vscode feature value test",
        "module": "feature_value_test",
        "toolchain": "cmake",
        "target": "rp2040",
        "board": board,
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
    manifest.update(manifest_fields or {})
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


def require_manifest_rejection(
    result: subprocess.CompletedProcess[str], location: str
) -> None:
    require(
        result.returncode == workflow_runtime.EXIT_CONFIG,
        f"a manifest carrying {location} returned {result.returncode}: {result.stderr}",
    )
    require(
        "[JH-CFG-MANIFEST]" in result.stderr and location in result.stderr,
        f"{location}: missing manifest diagnostic: {result.stderr}",
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

    # The manifest holds tooling metadata only; project configuration in it is
    # an error, wherever it sits.
    manifest_cases = (
        ({"cache": {"JH_EXTRA_DEFINES": "HAL_ENABLE_WIFI"}}, "cmake.cache.JH_EXTRA_DEFINES"),
        ({"cache": {"EXTRA_HAL_DEFINES": "HAL_ENABLE_UDP"}}, "cmake.cache.EXTRA_HAL_DEFINES"),
        ({"cache": {"JH_PROJECT_SOURCES": "app.c"}}, "cmake.cache.JH_PROJECT_SOURCES"),
        ({"cache": {"HAL_ENABLE_WIFI": 1}}, "cmake.cache.HAL_ENABLE_WIFI"),
        ({"cache": {"HAL_DISABLE_ASSERTS": 1}}, "cmake.cache.HAL_DISABLE_ASSERTS"),
        ({"cache": {"JH_VARIANT": "BENCH"}}, "cmake.cache.JH_VARIANT"),
        (
            {"target_profiles": {"rp2350-arm": {"cmake": {"cache": {"EXTRA_HAL_DEFINES": "HAL_ENABLE_TLS"}}}}},
            "targetProfiles.rp2350-arm.cmake.cache.EXTRA_HAL_DEFINES",
        ),
        ({"manifest_fields": {"variants": [{"id": "bench"}]}}, "'variants' is not a manifest field"),
        ({"manifest_fields": {"example": {"targets": ["rp2040"]}}}, "'example' is not a manifest field"),
    )
    for index, (fields, location) in enumerate(manifest_cases):
        project = fixture_root / f"manifest-{index}"
        write_feature_value_fixture(project, **fields)
        require_manifest_rejection(run_feature_value_dump(project), location)
    base_invalid = fixture_root / "manifest-0"
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
            f"{action}: a manifest with project configuration did not return "
            f"EXIT_CONFIG: {stderr.getvalue()}",
        )
        configure.assert_not_called()

    variant_invalid = fixture_root / "variant-invalid"
    write_feature_value_fixture(
        variant_invalid,
        header=(
            "#pragma once\n"
            "#define JH_PROJECT_VARIANTS(X) \\\n"
            "    X(INVALID, \"Invalid\", HAL_ENABLE_UDP, HAL_ENABLE_TCP=false)\n"
        ),
    )
    require_value_rejection(
        run_feature_value_dump(variant_invalid, "--variant", "INVALID"),
        "HAL_ENABLE_TCP",
        "JH_PROJECT_VARIANTS(INVALID)",
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
        header=(
            "#pragma once\n"
            "#define JH_PROJECT_VARIANTS(X) \\\n"
            "    X(INACTIVE_INVALID, \"Not built\", HAL_ENABLE_HTTP_SERVER=0)\n"
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


def require_board_rejection(
    result: subprocess.CompletedProcess[str], board: str, source: str
) -> None:
    require(
        result.returncode == workflow_runtime.EXIT_CONFIG,
        f"board {board} from {source} returned {result.returncode}: {result.stderr}",
    )
    require("[JH-CFG-BOARD]" in result.stderr, f"{board}: missing diagnostic id")
    require(f"'{board}'" in result.stderr, f"{board}: missing board in diagnostic")
    require(source in result.stderr, f"{board}: missing source in diagnostic")


def require_board(
    result: subprocess.CompletedProcess[str], board: str, source: str
) -> None:
    require(result.returncode == 0, f"expected board {board}: {result.stderr}")
    dumped = json.loads(result.stdout)
    require(dumped["board"] == board, f"resolved {dumped['board']}, expected {board}")
    require(
        dumped["cmake"]["cache"]["JH_BOARD"] == board,
        f"JH_BOARD does not follow the resolved board {board}",
    )
    require(
        dumped["_sources"]["board"] == source,
        f"board {board} attributed to {dumped['_sources']['board']}, expected {source}",
    )


def write_local_state(project_dir: Path, state: dict[str, str]) -> None:
    (project_dir / ".vscode" / "jaszczurhal.local.json").write_text(
        json.dumps(state) + "\n", encoding="utf-8"
    )


def run_select_board(
    project_dir: Path, *extra_args: str
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(ENTRY), "select-board", "--project", str(project_dir), *extra_args],
        check=False,
        capture_output=True,
        text=True,
    )


MANIFEST_SOURCE = ".vscode/jaszczurhal.project.json"
LOCAL_SOURCE = ".vscode/jaszczurhal.local.json"

with tempfile.TemporaryDirectory(prefix="jh-vscode-board-selection-") as temp_dir:
    fixture_root = Path(temp_dir)

    unknown = fixture_root / "unknown"
    write_feature_value_fixture(unknown, board="pico-removed")
    require_board_rejection(
        run_feature_value_dump(unknown), "pico-removed", MANIFEST_SOURCE
    )
    require(
        run_select_board(unknown).returncode == workflow_runtime.EXIT_CONFIG,
        "select-board listed a manifest with an unknown board",
    )
    require(
        run_select_board(unknown, "--target", "rp2040", "--board", "picow").returncode
        == workflow_runtime.EXIT_CONFIG,
        "select-board accepted a manifest whose board does not resolve",
    )
    write_local_state(unknown, {"target": "rp2040", "board": "picow"})
    require_board_rejection(
        run_feature_value_dump(unknown), "pico-removed", MANIFEST_SOURCE
    )

    foreign = fixture_root / "foreign"
    write_feature_value_fixture(foreign, board="pico2w")
    require_board_rejection(run_feature_value_dump(foreign), "pico2w", MANIFEST_SOURCE)

    base = fixture_root / "base"
    write_feature_value_fixture(base)
    require_board(run_feature_value_dump(base), "pico", MANIFEST_SOURCE)
    require_board(
        run_feature_value_dump(base, "--target", "rp2350-arm"),
        "pico2",
        "registry:rp2350-arm.defaultBoard",
    )
    require_board_rejection(
        run_feature_value_dump(base, "--board", "pico-removed"), "pico-removed", "cli"
    )
    require_board_rejection(
        run_feature_value_dump(base, "--board", "pico2w"), "pico2w", "cli"
    )

    write_local_state(base, {"target": "rp2350-arm", "board": "pico2w"})
    require_board(
        run_feature_value_dump(base, "--target", "rp2040"), "pico", MANIFEST_SOURCE
    )
    require_board(run_feature_value_dump(base), "pico2w", LOCAL_SOURCE)
    write_local_state(base, {"target": "rp2040", "board": "pico-removed"})
    require_board_rejection(run_feature_value_dump(base), "pico-removed", LOCAL_SOURCE)
    require(
        run_select_board(base).returncode == workflow_runtime.EXIT_CONFIG,
        "select-board listed a broken local selection",
    )
    repaired = run_select_board(base, "--target", "rp2040", "--board", "picow")
    require(repaired.returncode == 0, f"select-board could not replace a broken local selection: {repaired.stderr}")
    require_board(run_feature_value_dump(base), "picow", LOCAL_SOURCE)

    overlay = fixture_root / "overlay"
    write_feature_value_fixture(
        overlay,
        target_profiles={
            "rp2040": {"board": "rp2040-zero"},
            "rp2350-arm": {"board": "pico2w"},
        },
    )
    require_board(
        run_feature_value_dump(overlay),
        "rp2040-zero",
        f"{MANIFEST_SOURCE} targetProfiles.rp2040",
    )
    require_board(
        run_feature_value_dump(overlay, "--target", "rp2350-arm"),
        "pico2w",
        f"{MANIFEST_SOURCE} targetProfiles.rp2350-arm",
    )
    require_board(run_feature_value_dump(overlay, "--board", "picow"), "picow", "cli")
    selected = run_select_board(overlay, "--target", "rp2350-arm")
    require(selected.returncode == 0, f"target-only selection failed: {selected.stderr}")
    require("board=pico2w" in selected.stdout, "select-board hid the resolved board")
    require(
        "board" not in load_json(overlay / ".vscode" / "jaszczurhal.local.json"),
        "a target-only selection pinned a board over targetProfiles",
    )
    require_board(
        run_feature_value_dump(overlay),
        "pico2w",
        f"{MANIFEST_SOURCE} targetProfiles.rp2350-arm",
    )

    pinned_cache = fixture_root / "pinned-cache"
    write_feature_value_fixture(
        pinned_cache,
        cache={"JH_BOARD": "pico", "JH_TARGET": "stm32g474"},
        target_profiles={"rp2040": {"board": "rp2040-zero"}},
        header='#pragma once\n#define JH_PROJECT_VARIANTS(X) X(PINNED, "Pinned", PINNED=1)\n',
    )
    pinned_variant = run_feature_value_dump(pinned_cache, "--variant", "PINNED")
    require_board(pinned_variant, "rp2040-zero", f"{MANIFEST_SOURCE} targetProfiles.rp2040")
    require(
        json.loads(pinned_variant.stdout)["cmake"]["cache"]["JH_TARGET"] == "rp2040",
        "a cached JH_TARGET replaced the resolved target",
    )
    require_board(
        run_feature_value_dump(pinned_cache),
        "rp2040-zero",
        f"{MANIFEST_SOURCE} targetProfiles.rp2040",
    )
    require_board(run_feature_value_dump(pinned_cache, "--board", "picow"), "picow", "cli")

    overlay_foreign = fixture_root / "overlay-foreign"
    write_feature_value_fixture(
        overlay_foreign, target_profiles={"rp2350-arm": {"board": "pico"}}
    )
    require_board_rejection(
        run_feature_value_dump(overlay_foreign),
        "pico",
        f"{MANIFEST_SOURCE} targetProfiles.rp2350-arm",
    )
    repair = run_select_board(overlay_foreign, "--target", "rp2040", "--board", "picow")
    require(
        repair.returncode == workflow_runtime.EXIT_CONFIG
        and "[JH-CFG-BOARD]" in repair.stderr,
        "select-board reported success for a manifest that still does not resolve",
    )
    require(
        not (overlay_foreign / ".vscode" / "jaszczurhal.local.json").exists(),
        "select-board persisted a selection that does not resolve",
    )
