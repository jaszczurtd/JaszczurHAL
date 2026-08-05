#!/usr/bin/env python3
"""Validate native RP target resolution in the shared VS Code workflow."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(sys.argv[1]).resolve()
ENTRY = ROOT / "vscode" / "entry" / (
    "jh-vscode.cmd" if sys.platform == "win32" else "jh-vscode"
)


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
            str(ROOT / "examples" / "01_blink"),
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
        same_path(config["buildDir"], ROOT / ".build" / "examples" / "01_blink"),
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

blink = load_json(
    ROOT / "examples" / "01_blink" / ".vscode" / "jaszczurhal.project.json"
)
blink_targets = set(blink["example"]["targets"])
require(
    {
        "rp2040",
        "rp2350-arm",
        "rp2350-riscv",
        "stm32g474",
    }.issubset(blink_targets),
    "01_blink does not expose the complete target matrix",
)

wifi = load_json(
    ROOT / "examples" / "15_wifi" / ".vscode" / "jaszczurhal.project.json"
)
require(
    wifi["example"]["boards"]["rp2350-arm"] == "pico2w",
    "WiFi example does not map RP2350 ARM to Pico 2 W",
)
require(
    "rp2350-riscv" not in wifi["example"]["targets"],
    "unsupported RP2350 RISC-V + CYW43 combination is selectable",
)
require(
    wifi["example"]["boards"]["stm32g474"] == "nucleo-g474re-pim730",
    "WiFi example does not map STM32G474 to NUCLEO-G474RE with PIM730",
)
stm32_wifi = subprocess.run(
    [
        str(ENTRY),
        "config-dump",
        "--project",
        str(ROOT / "examples" / "15_wifi"),
        "--target",
        "stm32g474",
        "--board",
        "nucleo-g474re-pim730",
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
    "JH_EXTRA_DEFINES" not in stm32_wifi_config["cmake"]["cache"],
    "WiFi example still duplicates PIM730 wiring outside the board profile",
)
sys.path.insert(0, str(ROOT))
from vscode.runtime import jh_vscode as workflow_runtime

require(
    not workflow_runtime.build_preflight_diagnostics(
        stm32_wifi_config, ROOT / "examples" / "15_wifi"
    ),
    "WiFi preflight does not accept the registry-owned PIM730 backend",
)

for name in ("12_kv_store", "16_littlefs", "39_sdlogger"):
    manifest = load_json(
        ROOT / "examples" / name / ".vscode" / "jaszczurhal.project.json"
    )
    require(
        set(expected).issubset(manifest["example"]["targets"]),
        f"{name}: native storage target matrix is incomplete",
    )
    require(
        manifest["target"] == "rp2040",
        f"{name}: native RP2040 is not the project default",
    )

freertos = load_json(
    ROOT / "examples" / "29_freertos_smoke" / ".vscode" / "jaszczurhal.project.json"
)
require(
    set(expected).issubset(freertos["example"]["targets"]),
    "29_freertos_smoke does not expose the native RP target matrix",
)
require(
    freertos["target"] == "rp2040",
    "29_freertos_smoke does not default to the native RP2040 target",
)
require(
    freertos["cmake"]["cache"]["JH_RP2040_FREERTOS"] is True,
    "29_freertos_smoke does not enable native RP FreeRTOS",
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
    == {"HAL_ENABLE_OTA", "HAL_ENABLE_FREERTOS"},
    "OTA hardware fixture FreeRTOS variant lost required defines",
)
require(
    ota_fixture["ota"]["passwordEnv"] == "JH_OTA_TEST_PASSWORD",
    "OTA hardware fixture must not store a tracked password",
)

example_dirs = sorted((ROOT / "examples").glob("[0-9][0-9]_*"))
listed_examples = subprocess.run(
    [sys.executable, str(ROOT / "scripts" / "examples_dispatcher.py"), "list"],
    check=True,
    capture_output=True,
    text=True,
)
registered_names = {
    line.split(":", 1)[0] for line in listed_examples.stdout.splitlines()
}
require(
    registered_names == {example_dir.name for example_dir in example_dirs},
    "dispatcher registry and example directories differ",
)

example_counts = {target: 0 for target in known_targets}
for example_dir in example_dirs:
    manifest_path = example_dir / ".vscode" / "jaszczurhal.project.json"
    require(manifest_path.is_file(), f"{example_dir.name}: missing manifest")
    manifest = load_json(manifest_path)
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
        variant_targets = {
            str(target) for target in variant.get("targets", targets)
        }
        require(
            variant_targets.issubset(targets),
            f"{example_dir.name}:{variant.get('id')}: variant target "
            "classification escapes its example",
        )
    require(
        len(variant_ids) == len(set(variant_ids)),
        f"{example_dir.name}: duplicate variant id",
    )

require(
    example_counts
    == {
        "rp2040": 57,
        "rp2350-arm": 56,
        "rp2350-riscv": 45,
        "stm32g474": 55,
    },
    f"declared example target matrix changed without review: {example_counts}",
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
        == {base_define, "HAL_ENABLE_FREERTOS"},
        f"{name}: FreeRTOS feature classification changed",
    )
    require(
        base_define
        in manifest["cmake"]["cache"].get("JH_EXTRA_DEFINES", "").split(";"),
        f"{name}: bare-metal feature classification changed",
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
tasks = load_json(ROOT / "examples" / "01_blink" / ".vscode" / "tasks.json")
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
