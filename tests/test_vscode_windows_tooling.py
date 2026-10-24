#!/usr/bin/env python3
"""Validate cross-platform VS Code launchers and generated project files."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest import mock


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
SCRIPTS_DIR = ROOT / "scripts"
TOOLS_DIR = ROOT / "vscode" / "tools"
for import_dir in (str(SCRIPTS_DIR), str(TOOLS_DIR)):
    if import_dir not in sys.path:
        sys.path.insert(0, import_dir)

import examples_dispatcher
import component_manager
import compiler_cache
import configure_cortex_debug
import manage_vscode_extensions
from board_registry import tooling_target_registry
from vscode_task_config import (
    VSCODE_ENTRY_CONFIG,
    VSCODE_ENTRY_WINDOWS_CONFIG,
    VSCODE_EXTENSION_RECOMMENDATIONS,
    assembly_debug_builds,
    cortex_debug_launch_document,
    project_tasks_document,
    sync_cortex_debug_launch_document,
    vscode_launch_executable,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def checkout_alias(temp_dir: str) -> Path:
    """Spell a scratch checkout through a symlink; base_dir must keep that spelling."""
    if os.name == "nt":
        # Symlinks need privileges there; the CI runners' TEMP is an 8.3 alias.
        return Path(temp_dir)
    real = Path(temp_dir) / "real"
    real.mkdir()
    alias = Path(temp_dir) / "alias"
    alias.symlink_to(real, target_is_directory=True)
    return alias


def require_launch_contract(document: dict, executable: str, context: str) -> None:
    expected = cortex_debug_launch_document(executable)
    require(document == expected, f"{context} launch configuration drifted")
    profiles = {profile["name"]: profile for profile in document["configurations"]}
    rp2040 = profiles.get("Project: Debug Firmware")
    require(rp2040 is not None, f"{context} omits the RP2040 debug profile")
    require(
        rp2040.get("openOCDLaunchCommands") == ["adapter speed 5000"],
        f"{context} omits the validated RP2040 adapter speed",
    )
    rp2350 = profiles.get("Project: Debug Firmware (RP2350 ARM)")
    require(rp2350 is not None, f"{context} omits the RP2350 debug profile")
    require(
        rp2350.get("openOCDLaunchCommands") == ["adapter speed 2000"],
        f"{context} omits the validated RP2350 adapter speed",
    )
    stm32 = profiles.get("Project: Debug Firmware (STM32G474 / ST-Link)")
    require(stm32 is not None, f"{context} omits the STM32G474 debug profile")
    require(
        stm32["configFiles"] == ["board/st_nucleo_g4.cfg"],
        f"{context} bypasses the NUCLEO-G4 reset-aware OpenOCD profile",
    )
    require(stm32["device"] == "STM32G474RE", f"{context} names the wrong STM32 device")
    require(stm32["runToEntryPoint"] == "main", f"{context} uses an invalid STM32 entry point")
    require(
        stm32.get("openOCDLaunchCommands")
        == ["reset_config srst_only srst_nogate connect_assert_srst"],
        f"{context} omits connect-under-reset for STM32G474",
    )


def run_checked(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        **kwargs,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed with exit code {result.returncode}: {command!r}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


# Validated probe settings of each Cortex-Debug device.
VALIDATED_PROBES = {
    "rp2040": ("RP2040", ["interface/cmsis-dap.cfg", "target/rp2040.cfg"],
               ["adapter speed 5000"]),
    "rp2350-arm": ("RP2350", ["interface/cmsis-dap.cfg", "target/rp2350.cfg"],
                   ["adapter speed 2000"]),
    "stm32g474": ("STM32G474RE", ["board/st_nucleo_g4.cfg"],
                  ["reset_config srst_only srst_nogate connect_assert_srst"]),
}


def require_assembly_debug_profiles(
    document: dict, tasks: dict, example_dir: Path, build_dir: str, context: str
) -> None:
    """Each profile debugs one assembly and variant: the probe of its target,
    its own ELF, and a hidden pre-launch task that builds exactly it."""
    assemblies = load_json(example_dir / "device_tree.json")["assemblies"]
    manifest = load_json(example_dir / ".vscode" / "jaszczurhal.project.json")
    by_label = {task["label"]: task for task in tasks["tasks"]}
    debugged = []
    for profile in document["configurations"]:
        task = by_label.get(profile.get("preLaunchTask"))
        require(task is not None and task.get("hide") is True,
                f"{context}: {profile['name']} has no hidden pre-launch task")
        arguments = task["args"]
        require(arguments[:3] == ["build-debug", "--project", "${workspaceFolder}"]
                and arguments[3] == "--assembly",
                f"{context}: {task['label']} does not build one assembly")
        assembly = arguments[4]
        variant = arguments[6] if arguments[5:6] == ["--variant"] else None
        target = assemblies[assembly]["target"]
        device, config_files, commands = VALIDATED_PROBES[target]
        output = f"{build_dir}/{target}/{assembly}" + (f"/variants/{variant}" if variant else "")
        require(
            profile["type"] == "cortex-debug" and profile["device"] == device
            and profile["configFiles"] == config_files
            and profile.get("openOCDLaunchCommands") == commands
            and profile["runToEntryPoint"] == "main"
            and profile["executable"] == f"{output}/firmware.elf",
            f"{context}: {profile['name']} does not debug {output} with the validated probe",
        )
        debugged.append((target, assembly, variant))
    require(sorted(debugged, key=str) == sorted(assembly_debug_builds(example_dir), key=str),
            f"{context}: the debug profiles do not cover every debugged build")
    require(not debugged or debugged[0][1] == manifest["assembly"]
            or manifest["assembly"] not in {item[1] for item in debugged},
            f"{context}: the manifest's assembly is not the first debug profile")


launch_executable = vscode_launch_executable(
    {
        "buildDir": "${projectDir}/output/debug",
        "artifacts": {"elf": "${buildDir}/custom-firmware.elf"},
    }
)
require(
    launch_executable == "${workspaceFolder}/output/debug/custom-firmware.elf",
    "debug profile synchronization did not translate the manifest ELF path",
)
example_project = ROOT / "examples" / "01_core_runtime"
example_executable = vscode_launch_executable(
    load_json(example_project / ".vscode" / "jaszczurhal.project.json"),
    workspace_dir=example_project,
    jh_root=ROOT,
)
require(
    example_executable
    == "${workspaceFolder}/../../.build/examples/01_core_runtime/firmware.elf",
    "debug profile synchronization emitted an unresolved JaszczurHAL root",
)
legacy_profile = cortex_debug_launch_document(launch_executable)["configurations"][0]
legacy_profile["name"] = "Debug: RP2040 (Pico/Pico W/Zero/Plus)"
custom_profile = {
    "name": "Consumer: Custom debugger",
    "type": "cppdbg",
    "request": "launch",
}
launch_document = {
    "version": "0.1.0",
    "configurations": [legacy_profile, custom_profile],
}
require(
    sync_cortex_debug_launch_document(
        launch_document, cortex_debug_launch_document(launch_executable)
    ),
    "debug profile synchronization did not repair a legacy launch document",
)
require(
    launch_document["configurations"]
    == cortex_debug_launch_document(launch_executable)["configurations"]
    + [custom_profile],
    "debug profile synchronization removed a custom consumer configuration",
)
require(
    not sync_cortex_debug_launch_document(
        launch_document, cortex_debug_launch_document(launch_executable)
    ),
    "debug profile synchronization is not idempotent",
)


entry_python = ROOT / "vscode" / "entry" / "jh_vscode.py"
entry_unix = ROOT / "vscode" / "entry" / "jh-vscode"
entry_windows = ROOT / "vscode" / "entry" / "jh-vscode.cmd"

with tempfile.TemporaryDirectory(prefix="jh entry spacje ") as temp_dir:
    unicode_cwd = Path(temp_dir) / "ścieżka robocza"
    unicode_cwd.mkdir()
    python_version = run_checked(
        [sys.executable, str(entry_python), "--version"],
        cwd=unicode_cwd,
    )
    require(python_version.stdout.strip() == "jh-vscode 0.1.0", "Python entrypoint failed")

    if os.name != "nt":
        unix_version = run_checked([str(entry_unix), "--version"], cwd=unicode_cwd)
        require(
            unix_version.stdout == python_version.stdout,
            "Unix launcher selected another runtime",
        )

unsupported = subprocess.run(
    (
        [str(entry_unix), "unsupported action"]
        if os.name != "nt"
        else [sys.executable, str(entry_python), "unsupported action"]
    ),
    check=False,
    capture_output=True,
    text=True,
)
require(unsupported.returncode == 8, "public launcher did not preserve exit code 8")
require(
    "unsupported action 'unsupported action'" in unsupported.stderr,
    "quoted argument was split",
)

cmd_text = entry_windows.read_text(encoding="utf-8")
managed_index = cmd_text.index('if exist "%JH_MANAGED_PYTHON%"')
py_index = cmd_text.index("where py")
python_index = cmd_text.index("where python")
require(managed_index < py_index < python_index, "Windows Python detection order changed")
require('"%JH_ENTRY_PY%" %*' in cmd_text, "Windows launcher does not forward all arguments")
require("exit /b %ERRORLEVEL%" in cmd_text, "Windows launcher does not forward exit codes")

if os.name == "nt":
    with tempfile.TemporaryDirectory(prefix="jh cmd spacje ") as temp_dir:
        serial_stub = Path(temp_dir) / "serial" / "tools"
        serial_stub.mkdir(parents=True)
        (serial_stub.parent / "__init__.py").write_text("", encoding="utf-8")
        (serial_stub / "__init__.py").write_text("", encoding="utf-8")
        (serial_stub / "list_ports.py").write_text("", encoding="utf-8")
        env = os.environ.copy()
        env["JH_VSCODE_PYTHON"] = sys.executable
        env["PYTHONPATH"] = os.pathsep.join(
            item for item in (str(serial_stub.parents[1]), env.get("PYTHONPATH", "")) if item
        )
        version_command = subprocess.list2cmdline(
            [str(entry_windows), "--version"]
        )
        version = subprocess.run(
            version_command,
            check=False,
            capture_output=True,
            text=True,
            env=env,
            shell=True,
        )
        require(version.returncode == 0, f"Windows launcher failed: {version.stderr}")
        require(version.stdout.strip() == "jh-vscode 0.1.0", "Windows launcher output changed")

        bad_command = subprocess.list2cmdline([str(entry_windows), "unsupported action"])
        bad = subprocess.run(
            bad_command,
            check=False,
            capture_output=True,
            text=True,
            env=env,
            shell=True,
        )
        require(bad.returncode == 8, "Windows launcher did not preserve exit code 8")
        require("unsupported action 'unsupported action'" in bad.stderr, "Windows quoting changed")

attributes = {
    "vscode/entry/jh-vscode": "lf",
    "vscode/entry/jh-vscode.cmd": "crlf",
    "vscode/entry/jh_vscode.py": "lf",
    "src/hal/network/cyw43/vendor/src/cyw43_ll.c.upstream": "lf",
    "src/hal/impl/rp2040/drivers/swserial/swserial.pio": "lf",
    "tests/fixtures/tls_test_ca_der.inc": "lf",
    "src/hal/network/cyw43/LICENSE.RP": "lf",
}
for path, expected in attributes.items():
    result = run_checked(["git", "check-attr", "eol", "--", path], cwd=ROOT)
    require(result.stdout.strip().endswith(f"eol: {expected}"), f"wrong eol policy for {path}")
upstream_files = sorted(
    ROOT.glob("src/hal/network/cyw43/vendor/src/*.upstream")
)
require(len(upstream_files) == 9, "CYW43 upstream fixture set changed unexpectedly")
for path in upstream_files:
    result = run_checked(
        ["git", "check-attr", "eol", "--", path.relative_to(ROOT).as_posix()],
        cwd=ROOT,
    )
    require(result.stdout.strip().endswith("eol: lf"), f"wrong eol policy for {path}")

registry = tooling_target_registry(ROOT)
expected_tasks = project_tasks_document(registry, "rp2040", "pico")
require(
    examples_dispatcher.JH_VSCODE.name
    == ("jh-vscode.cmd" if os.name == "nt" else "jh-vscode"),
    "example dispatcher selected the wrong host launcher",
)
with tempfile.TemporaryDirectory(prefix="jh dispatcher logs ") as temp_dir:
    with mock.patch.object(
        examples_dispatcher.tempfile,
        "gettempdir",
        return_value=temp_dir,
    ):
        dispatcher_log = examples_dispatcher.dispatcher_log_path(
            "rp2350-arm",
            "01_core_runtime",
        )
    require(
        dispatcher_log.parent == Path(temp_dir),
        "example dispatcher bypasses the host temporary directory",
    )
    require(
        dispatcher_log.name
        == "jh_examples_dispatcher_rp2350-arm_01_core_runtime.log",
        "example dispatcher log name changed unexpectedly",
    )
require(
    examples_dispatcher.tasks_for("rp2040", "pico", None) == expected_tasks,
    "example dispatcher bypasses the shared task builder",
)

with tempfile.TemporaryDirectory(prefix="jh dispatcher runner ") as temp_dir:
    log_path = Path(temp_dir) / "dispatcher.log"
    with mock.patch.object(
        examples_dispatcher,
        "dispatcher_log_path",
        return_value=log_path,
    ), mock.patch.object(
        examples_dispatcher.time,
        "monotonic",
        side_effect=[10.0, 12.5],
    ), mock.patch.object(
        examples_dispatcher.subprocess,
        "run",
        return_value=subprocess.CompletedProcess([], 0),
    ) as dispatcher_run:
        runner_result = examples_dispatcher.run_one_example(
            ROOT / "examples" / "05_serial_gps",
            "rp2040",
            ["--board", "pico"],
            [None, "SWSERIAL"],
            3,
            False,
        )
    require(
        runner_result
        == (True, "05_serial_gps@rp2040", log_path, 2.5),
        f"dispatcher worker returned an invalid result tuple: {runner_result}",
    )
    commands = [call.args[0] for call in dispatcher_run.call_args_list]
    require(
        len(commands) == 2
        and "--variant" not in commands[0]
        and commands[1][-2:] == ["--variant", "SWSERIAL"]
        and all(command[command.index("--board") + 1] == "pico" for command in commands),
        "dispatcher worker did not build the base and the variant on the manifest board",
    )
    for call in dispatcher_run.call_args_list:
        require(
            call.kwargs["env"]["CMAKE_BUILD_PARALLEL_LEVEL"] == "3"
            and call.kwargs["env"]["JH_VSCODE_MEMORY_OVERVIEW"] == "0",
            "dispatcher worker did not propagate the bounded parallel environment",
        )

with tempfile.TemporaryDirectory(prefix="jh compiler cache ") as temp_dir:
    scratch = checkout_alias(temp_dir)
    pinned_ccache = scratch / "tool" / ("ccache.exe" if os.name == "nt" else "ccache")
    with mock.patch.object(examples_dispatcher, "REPO_ROOT", scratch), \
         mock.patch.object(component_manager, "ensure_ccache", return_value=pinned_ccache), \
         mock.patch.dict(os.environ, {
             "JH_COMPILER_CACHE_DIR": str(scratch / "persistent"),
             "CCACHE_SLOPPINESS": "system_headers,time_macros",
             "CCACHE_IGNOREHEADERS": "*",
             "CCACHE_PREFIX": "untrusted-wrapper",
             "CMAKE_C_COMPILER_LAUNCHER": "another-cache",
         }), redirect_stdout(io.StringIO()):
        for target in ("rp2040", "esp32s3"):
            executable, cache_environment = compiler_cache.compiler_cache_environment(scratch, target)
            require(executable == pinned_ccache, "matrix did not select the pinned compiler cache")
            require(cache_environment["CCACHE_DIR"] == str((scratch / "persistent").resolve()),
                    "matrix did not retain the explicit persistent cache directory")
            require(not any(key in cache_environment for key in
                            ("CCACHE_SLOPPINESS", "CCACHE_IGNOREHEADERS", "CCACHE_PREFIX")),
                    "inherited ccache settings can bypass dependency checks")
            configuration = Path(cache_environment["CCACHE_CONFIGPATH"]).read_text(encoding="utf-8")
            require("compiler_check = content" in configuration and "direct_mode = false" in configuration,
                    "matrix cache skips compiler or preprocessor checks")
            require("hash_dir = false" in configuration
                    and f"base_dir = {scratch.as_posix()}" in configuration,
                    "cache keys still depend on the checkout's absolute path")
            if target == "esp32s3":
                require(cache_environment["IDF_CCACHE_ENABLE"] == "0",
                        "ESP must not wrap CMake's cache launcher twice")
            require(cache_environment["CMAKE_C_COMPILER_LAUNCHER"] == str(pinned_ccache)
                    and cache_environment["CMAKE_CXX_COMPILER_LAUNCHER"] == str(pinned_ccache),
                    "matrix compilers bypass the CMake cache launcher")
        with mock.patch.dict(os.environ, {
            "XDG_CACHE_HOME": "", "LOCALAPPDATA": "", "JH_COMPILER_CACHE_DIR": "",
        }):
            _, cache_environment = compiler_cache.compiler_cache_environment(scratch, "rp2040")
            default_root = Path.home() / ("AppData/Local" if os.name == "nt" else ".cache")
            require(cache_environment["CCACHE_DIR"] == str((default_root / "jaszczurhal/ccache").resolve()),
                    "empty cache variables placed compiler results in the checkout")

def check_cache_relocation() -> None:
    """Debug builds share results; newly created HAL config headers invalidate them."""
    executable = component_manager.ensure_ccache(ROOT, verify_only=True)
    tool_environment = examples_dispatcher.workflow().cmake_process_environment() or os.environ
    compiler = shutil.which("arm-none-eabi-gcc", path=tool_environment.get("PATH"))
    objcopy = shutil.which("arm-none-eabi-objcopy", path=tool_environment.get("PATH"))
    require(compiler is not None and objcopy is not None, "cache probe requires the Arm toolchain")
    with tempfile.TemporaryDirectory(prefix="jh cache $ ") as temp_dir:
        scratch = Path(temp_dir)
        with mock.patch.object(component_manager, "ensure_ccache", return_value=executable), \
             mock.patch.dict(os.environ, {"JH_COMPILER_CACHE_DIR": str(scratch / "cache")}):
            for checkout, build_name, hit in (("one", "first", False), ("one", "second", True),
                                              ("two", "second", True)):
                root = scratch / checkout
                root.mkdir(exist_ok=True)
                source = root / "probe.c"
                source.write_text('#include "hal/core/hal_project_config_hook.h"\n'
                                  '#ifndef VALUE\n#define VALUE 17\n#endif\n'
                                  'const unsigned value = VALUE + FACTOR;\n', encoding="utf-8")
                build = root / ".build" / build_name
                build.mkdir(parents=True)
                _, environment = compiler_cache.compiler_cache_environment(root, "probe")

                def compile_value(expected: int | None, factor: int = 1) -> None:
                    output = build / "probe.o"
                    output.unlink(missing_ok=True)
                    result = subprocess.run(
                        [str(executable), compiler, "-ggdb", "-I", str(root), "-I", str(ROOT / "src"),
                         f"-DFACTOR={factor}", "-c", str(source), "-o", str(output)],
                        cwd=build, env=environment, capture_output=True, text=True,
                    )
                    if expected is None:
                        require(result.returncode != 0 and "cache_probe_failure" in result.stderr
                                and not output.exists(), "cache concealed a compiler error")
                        return
                    require(result.returncode == 0, result.stderr)
                    binary = build / "probe.bin"
                    subprocess.run([objcopy, "-O", "binary", "--only-section=.rodata",
                                    str(output), str(binary)], check=True, capture_output=True)
                    require(int.from_bytes(binary.read_bytes(), "little") == expected,
                            "cache returned stale source, header or flags")

                compile_value(18)
                stats = subprocess.check_output([str(executable), "--print-log-stats", "--format=json"],
                                                env=environment, text=True)
                require(json.loads(stats)["preprocessed_cache_hit"] == int(hit),
                        f"debug object not shared between build directories/checkouts: {stats}")
                if checkout == "two":
                    header = root / "hal_project_config.h"
                    header.write_text("#define VALUE 21\n", encoding="utf-8")
                    compile_value(22)
                    compile_value(24, factor=3)
                    source.write_text(source.read_text(encoding="utf-8").replace("+ FACTOR", "- FACTOR"),
                                      encoding="utf-8")
                    compile_value(18, factor=3)
                    header.write_text("#error cache_probe_failure\n", encoding="utf-8")
                    compile_value(None)


check_cache_relocation()

build_args = SimpleNamespace(
    example=["01_core_runtime", "05_serial_gps"],
    target="rp2040",
    jobs=6,
    verbose=False,
)
with mock.patch.object(
    examples_dispatcher,
    "run_one_example",
    return_value=(
        True,
        "05_serial_gps@rp2040",
        Path(tempfile.gettempdir()) / "dispatcher.log",
        0.1,
    ),
) as scheduled_runner, redirect_stdout(io.StringIO()):
    require(
        examples_dispatcher.build(build_args) == 0,
        "dispatcher scheduler rejected a valid worker result tuple",
    )
scheduled_runner.assert_has_calls(
    [
        mock.call(
            ROOT / "examples" / "01_core_runtime",
            "rp2040",
            ["--assembly", "pico"],
            [None, "CAPTURE"],
            3,
            False,
        ),
        mock.call(
            ROOT / "examples" / "05_serial_gps",
            "rp2040",
            ["--assembly", "pico"],
            [None, "SWSERIAL"],
            3,
            False,
        ),
    ],
    any_order=True,
)
require(
    scheduled_runner.call_count == 2,
    "dispatcher scheduler did not use the two-project worker budget",
)

for task in expected_tasks["tasks"]:
    require(task.get("command") == VSCODE_ENTRY_CONFIG, f"Unix command missing in {task['label']}")
    require(
        task.get("windows", {}).get("command") == VSCODE_ENTRY_WINDOWS_CONFIG,
        f"Windows command missing in {task['label']}",
    )

run_checked([sys.executable, str(SCRIPTS_DIR / "examples_dispatcher.py"), "check"])
for example_dir in examples_dispatcher.example_dirs():
    name = example_dir.name
    vscode_dir = example_dir / ".vscode"
    manifest = load_json(vscode_dir / "jaszczurhal.project.json")
    tasks = load_json(vscode_dir / "tasks.json")
    settings = load_json(vscode_dir / "settings.json")
    # Examples select an assembly of their device_tree.json; it fixes the
    # target, and each assembly builds in <target>/<assembly>.
    assembly = str(manifest["assembly"])
    target = str(
        load_json(example_dir / "device_tree.json")["assemblies"][assembly]["target"]
    )
    build_dir = f"${{workspaceFolder}}/../../.build/examples/{name}"
    assembly_dir = f"{build_dir}/{target}/{assembly}"
    launch_path = vscode_dir / "launch.json"
    launch_text = launch_path.read_text(encoding="utf-8")
    require_assembly_debug_profiles(
        json.loads(launch_text),
        tasks,
        example_dir,
        build_dir,
        f"checked-in example {name}",
    )
    require(
        all(task.get("windows", {}).get("command") == VSCODE_ENTRY_WINDOWS_CONFIG
            for task in tasks["tasks"]),
        f"checked-in example {name} omits Windows task commands",
    )
    require(
        settings.get("jaszczurhal.vscodeEntryWindows", "").endswith("jh-vscode.cmd"),
        f"checked-in example {name} omits the Windows launcher setting",
    )
    require(
        settings.get("cortex-debug.gdbPath.linux") == "gdb-multiarch",
        f"checked-in example {name} omits Linux Arm GDB selection",
    )
    require(
        settings.get("cmake.generator") == "Ninja",
        f"checked-in example {name} omits the CMake Tools generator",
    )
    require(
        settings.get("cmake.buildDirectory")
        == f"{build_dir}/cmake-tools/{target}-{assembly}",
        f"checked-in example {name} does not isolate the CMake Tools cache",
    )
    configure_settings = settings.get("cmake.configureSettings")
    require(
        isinstance(configure_settings, dict),
        f"checked-in example {name} omits CMake Tools configure settings",
    )
    required_settings = {
        "JH_ROOT": "${workspaceFolder}/../..",
        "JH_PROJECT_DIR": "${workspaceFolder}",
        "JH_MODULE_NAME": str(manifest["module"]),
        "JH_TARGET": target,
        "JH_ASSEMBLY": assembly,
        "JH_ARTIFACT_DIR": assembly_dir,
    }
    require(
        "JH_BOARD" not in configure_settings,
        f"checked-in example {name} selects a board next to its assembly",
    )
    for key, expected in required_settings.items():
        require(
            configure_settings.get(key) == expected,
            f"checked-in example {name} has incorrect {key} for CMake Tools",
        )
    if target.startswith("rp"):
        require(
            configure_settings.get("PICO_SDK_PATH")
            == "${workspaceFolder}/../../third_party/pico-sdk",
            f"checked-in example {name} omits PICO_SDK_PATH for CMake Tools",
        )
    elif target == "stm32g474":
        require(
            configure_settings.get("CMAKE_TOOLCHAIN_FILE")
            == "${workspaceFolder}/../../link_libraries/stm32_lib/toolchain_stm32g474.cmake",
            f"checked-in example {name} omits the STM32 toolchain for CMake Tools",
        )
    require(
        "${config:cortex-debug." not in launch_text,
        f"checked-in example {name} requires private Cortex-Debug settings",
    )
reference_tasks = load_json(ROOT / "vscode" / "examples" / "tasks.json")
require(reference_tasks == expected_tasks, "checked-in VS Code task template drifted")
reference_settings = load_json(ROOT / "vscode" / "examples" / "settings.json")
require(
    reference_settings["jaszczurhal.vscodeEntryWindows"].endswith("jh-vscode.cmd"),
    "checked-in VS Code settings omit the Windows launcher",
)
require(
    reference_settings["cortex-debug.gdbPath.linux"] == "gdb-multiarch",
    "checked-in VS Code settings omit Linux Arm GDB selection",
)
require(
    reference_settings["cmake.configureSettings"]["JH_PROJECT_DIR"]
    == "${workspaceFolder}",
    "checked-in VS Code settings omit JH_PROJECT_DIR for CMake Tools",
)
require(
    reference_settings["cmake.configureSettings"]["PICO_SDK_PATH"]
    == "${workspaceFolder}/../../JaszczurHAL/third_party/pico-sdk",
    "checked-in VS Code settings omit PICO_SDK_PATH for CMake Tools",
)
require_launch_contract(
    load_json(ROOT / "vscode" / "examples" / "launch.json"),
    "${workspaceFolder}/.build/firmware.elf",
    "checked-in VS Code template",
)
require(
    "${config:cortex-debug."
    not in (ROOT / "vscode" / "examples" / "launch.json").read_text(encoding="utf-8"),
    "checked-in VS Code launch template requires private Cortex-Debug settings",
)

with tempfile.TemporaryDirectory(prefix="jh generator spacje ") as temp_dir:
    project_dir = Path(temp_dir) / "Projekt modułu"
    generate_command = [
        sys.executable,
        str(TOOLS_DIR / "create-vscode-example.py"),
        "--output",
        str(project_dir),
        "--name",
        "Generated Windows workflow",
    ]
    narrow_console_env = os.environ.copy()
    narrow_console_env["PYTHONIOENCODING"] = "ascii:strict"
    generated = run_checked(generate_command, env=narrow_console_env)
    require(
        "Projekt modu\\u0142u" in generated.stdout,
        "generator did not preserve a Unicode path on a narrow console",
    )
    first = {
        path.relative_to(project_dir).as_posix(): path.read_bytes()
        for path in project_dir.rglob("*")
        if path.is_file()
    }
    require(
        all(b"\r\n" not in content for content in first.values()),
        "standalone generator wrote Windows CRLF into LF-controlled files",
    )
    run_checked([*generate_command, "--force"], env=narrow_console_env)
    second = {
        path.relative_to(project_dir).as_posix(): path.read_bytes()
        for path in project_dir.rglob("*")
        if path.is_file()
    }
    require(first == second, "standalone project generator is not idempotent")
    generated_tasks = load_json(project_dir / ".vscode" / "tasks.json")
    for task in generated_tasks["tasks"]:
        require(
            task.get("windows", {}).get("command") == VSCODE_ENTRY_WINDOWS_CONFIG,
            f"standalone generator omitted Windows command in {task['label']}",
        )
    generated_settings = load_json(project_dir / ".vscode" / "settings.json")
    require(
        generated_settings["jaszczurhal.vscodeEntryWindows"].endswith("jh-vscode.cmd"),
        "standalone generator omitted Windows entry setting",
    )
    require(
        generated_settings["cortex-debug.gdbPath.linux"] == "gdb-multiarch",
        "standalone generator omitted Linux Arm GDB selection",
    )
    require(
        generated_settings["cmake.generator"] == "Ninja",
        "standalone generator omitted the CMake Tools generator",
    )
    require(
        generated_settings["cmake.buildDirectory"].endswith(
            "/.build/cmake-tools/rp2040-pico"
        ),
        "standalone generator did not isolate the CMake Tools cache",
    )
    generated_cmake_settings = generated_settings.get("cmake.configureSettings")
    require(
        isinstance(generated_cmake_settings, dict),
        "standalone generator omitted CMake Tools configure settings",
    )
    require(
        generated_cmake_settings.get("JH_PROJECT_DIR") == "${workspaceFolder}",
        "standalone generator omitted JH_PROJECT_DIR for CMake Tools",
    )
    require(
        generated_cmake_settings.get("JH_MODULE_NAME") == "example",
        "standalone generator omitted JH_MODULE_NAME for CMake Tools",
    )
    require(
        generated_cmake_settings.get("JH_TARGET") == "rp2040",
        "standalone generator omitted JH_TARGET for CMake Tools",
    )
    require(
        generated_cmake_settings.get("JH_BOARD") == "pico",
        "standalone generator omitted JH_BOARD for CMake Tools",
    )
    require(
        str(generated_cmake_settings.get("PICO_SDK_PATH", "")).endswith(
            "/third_party/pico-sdk"
        ),
        "standalone generator omitted PICO_SDK_PATH for CMake Tools",
    )
    require_launch_contract(
        load_json(project_dir / ".vscode" / "launch.json"),
        "${workspaceFolder}/.build/firmware.elf",
        "standalone generator",
    )
    require(
        "${config:cortex-debug."
        not in (project_dir / ".vscode" / "launch.json").read_text(encoding="utf-8"),
        "standalone generator requires private Cortex-Debug settings",
    )

    generator_path = TOOLS_DIR / "create-vscode-example.py"
    generator_spec = importlib.util.spec_from_file_location(
        "jh_standalone_generator", generator_path
    )
    require(
        generator_spec is not None and generator_spec.loader is not None,
        "standalone generator module could not be loaded",
    )
    generator = importlib.util.module_from_spec(generator_spec)
    generator_spec.loader.exec_module(generator)
    with mock.patch.object(
        generator.os.path,
        "relpath",
        side_effect=ValueError("path is on another Windows volume"),
    ):
        cross_volume = generator.build_files(
            output_dir=project_dir,
            project_name="Cross-volume fixture",
            module="example",
            target="rp2040",
            board="pico",
            usb_manufacturer="Jaszczur",
            usb_product="Cross-volume board",
            by_id_hint="Cross_volume_board",
        )
    cross_manifest = json.loads(
        cross_volume[".vscode/jaszczurhal.project.json"]
    )
    cross_settings = json.loads(cross_volume[".vscode/settings.json"])
    expected_dispatcher = (
        ROOT / "cmake" / "jh_firmware_project"
    ).resolve().as_posix()
    require(
        cross_manifest["cmake"]["sourceDir"] == expected_dispatcher,
        "cross-volume manifest prefixed an absolute dispatcher path",
    )
    require(
        cross_settings["cmake.sourceDirectory"] == expected_dispatcher,
        "cross-volume settings prefixed an absolute dispatcher path",
    )
    cross_cmake_settings = cross_settings["cmake.configureSettings"]
    require(
        cross_cmake_settings["JH_ROOT"] == ROOT.resolve().as_posix(),
        "cross-volume settings did not keep an absolute JaszczurHAL root",
    )
    require(
        cross_cmake_settings["PICO_SDK_PATH"]
        == f"{ROOT.resolve().as_posix()}/third_party/pico-sdk",
        "cross-volume settings prefixed an absolute Pico SDK path",
    )
    require(
        cross_manifest["$schema"].startswith("file:"),
        "cross-volume schema reference is not a file URI",
    )

with tempfile.TemporaryDirectory(prefix="jh code fixture ") as temp_dir:
    code_fixture = Path(temp_dir) / "code-fixture"
    code_fixture.write_text("fixture\n", encoding="utf-8")
    installed = {VSCODE_EXTENSION_RECOMMENDATIONS[0].lower()}
    install_calls: list[str] = []

    def fake_runner(command, **_kwargs):
        if "--list-extensions" in command:
            stdout = "\n".join(sorted(installed)) + "\n"
            return subprocess.CompletedProcess(command, 0, stdout=stdout, stderr="")
        if "--install-extension" in command:
            extension = command[command.index("--install-extension") + 1]
            installed.add(extension.lower())
            install_calls.append(extension)
            return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
        return subprocess.CompletedProcess(command, 1, stdout="", stderr="unexpected command")

    output = io.StringIO()
    with redirect_stdout(output), redirect_stderr(output):
        check_result = manage_vscode_extensions.main(
            ["--code", str(code_fixture)],
            runner=fake_runner,
        )
        require(check_result == 1, "extension check accepted missing recommendations")
        cancelled = manage_vscode_extensions.main(
            ["--code", str(code_fixture), "--install"],
            runner=fake_runner,
            input_fn=lambda _prompt: "n",
        )
        require(
            cancelled == 1 and not install_calls,
            "extension installation bypassed consent",
        )
        installed_result = manage_vscode_extensions.main(
            ["--code", str(code_fixture), "--install", "--yes"],
            runner=fake_runner,
        )
    require(installed_result == 0, "extension installation did not verify its result")
    require(
        installed == {item.lower() for item in VSCODE_EXTENSION_RECOMMENDATIONS},
        "extension installer and recommendation list diverged",
    )

with tempfile.TemporaryDirectory(prefix="jh cortex debug settings ") as temp_dir:
    fixture_root = Path(temp_dir)
    tools_dir = fixture_root / "tools"
    tools_dir.mkdir()
    openocd = tools_dir / "openocd.exe"
    compiler = tools_dir / "arm-none-eabi-gcc.exe"
    gdb = tools_dir / "arm-none-eabi-gdb.exe"
    for executable in (openocd, compiler, gdb):
        executable.write_bytes(b"fixture")
    host_environment = fixture_root / "host-environment.json"
    host_environment.write_text(
        json.dumps(
            {
                "schemaVersion": 1,
                "tools": {
                    "openocd": str(openocd),
                    "gnu-arm": str(compiler),
                },
            }
        ),
        encoding="utf-8",
    )
    settings = fixture_root / "Code" / "User" / "settings.json"
    settings.parent.mkdir(parents=True)
    initial_settings = (
        "{\n"
        "    // Preserve existing user choices.\n"
        '    "workbench.colorTheme": "Dark Modern",\n'
        '    "files.exclude": {\n'
        "        // Nested JSONC remains untouched.\n"
        '        "**/.build": true,\n'
        "    },\n"
        '    "cortex-debug.openocdPath.windows": "C:/stale/openocd.exe", // stale\n'
        "}\n"
    )
    settings.write_text(initial_settings, encoding="utf-8")

    output = io.StringIO()
    with redirect_stdout(output), redirect_stderr(output):
        missing_result = configure_cortex_debug.main(
            [
                "--host-environment",
                str(host_environment),
                "--settings",
                str(settings),
                "--check",
            ]
        )
    require(missing_result == 1, "Cortex-Debug check accepted stale settings")
    require(
        settings.read_text(encoding="utf-8") == initial_settings,
        "Cortex-Debug check modified user settings",
    )

    output = io.StringIO()
    with redirect_stdout(output), redirect_stderr(output):
        configure_result = configure_cortex_debug.main(
            [
                "--host-environment",
                str(host_environment),
                "--settings",
                str(settings),
                "--yes",
            ]
        )
    require(configure_result == 0, "Cortex-Debug configuration failed")
    configured_text = settings.read_text(encoding="utf-8")
    configured = configure_cortex_debug.setting_values(
        configured_text,
        {
            configure_cortex_debug.SETTING_OPENOCD,
            configure_cortex_debug.SETTING_ARM_TOOLCHAIN,
            "workbench.colorTheme",
        },
    )
    require(
        configured[configure_cortex_debug.SETTING_OPENOCD]
        == str(openocd.resolve()),
        "Cortex-Debug OpenOCD setting did not use the verified host tool",
    )
    require(
        configured[configure_cortex_debug.SETTING_ARM_TOOLCHAIN]
        == str(compiler.parent.resolve()),
        "Cortex-Debug GNU Arm setting did not use the verified host toolchain",
    )
    require(
        configured["workbench.colorTheme"] == "Dark Modern"
        and "Preserve existing user choices" in configured_text,
        "Cortex-Debug configuration did not preserve JSONC user settings",
    )
    require(
        "Nested JSONC remains untouched" in configured_text,
        "Cortex-Debug configuration rewrote a nested JSONC setting",
    )
    require(
        Path(f"{settings}.jaszczurhal.bak").read_text(encoding="utf-8")
        == initial_settings,
        "Cortex-Debug configuration did not create a recoverable backup",
    )

    output = io.StringIO()
    with redirect_stdout(output), redirect_stderr(output):
        verified_result = configure_cortex_debug.main(
            [
                "--host-environment",
                str(host_environment),
                "--settings",
                str(settings),
                "--check",
            ]
        )
    require(verified_result == 0, "Cortex-Debug settings were not idempotent")

    fresh_settings = fixture_root / "fresh" / "settings.json"
    output = io.StringIO()
    with redirect_stdout(output), redirect_stderr(output):
        fresh_result = configure_cortex_debug.main(
            [
                "--host-environment",
                str(host_environment),
                "--settings",
                str(fresh_settings),
                "--yes",
            ]
        )
    require(fresh_result == 0, "Cortex-Debug fresh-profile setup failed")
    fresh_values = configure_cortex_debug.setting_values(
        fresh_settings.read_text(encoding="utf-8"),
        {
            configure_cortex_debug.SETTING_OPENOCD,
            configure_cortex_debug.SETTING_ARM_TOOLCHAIN,
        },
    )
    require(
        fresh_values == configure_cortex_debug.resolved_settings(host_environment),
        "Cortex-Debug fresh profile does not contain the verified tool set",
    )
    require(
        not Path(f"{fresh_settings}.jaszczurhal.bak").exists(),
        "Cortex-Debug created a backup for a previously absent profile",
    )
