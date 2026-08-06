#!/usr/bin/env python3
"""Validate cross-platform VS Code launchers and generated project files."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
SCRIPTS_DIR = ROOT / "scripts"
TOOLS_DIR = ROOT / "vscode" / "tools"
for import_dir in (str(SCRIPTS_DIR), str(TOOLS_DIR)):
    if import_dir not in sys.path:
        sys.path.insert(0, import_dir)

import examples_dispatcher
import configure_cortex_debug
import manage_vscode_extensions
from board_registry import tooling_target_registry
from vscode_task_config import (
    VSCODE_ENTRY_CONFIG,
    VSCODE_ENTRY_WINDOWS_CONFIG,
    VSCODE_EXTENSION_RECOMMENDATIONS,
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
example_project = ROOT / "examples" / "01_blink"
example_executable = vscode_launch_executable(
    load_json(example_project / ".vscode" / "jaszczurhal.project.json"),
    workspace_dir=example_project,
    jh_root=ROOT,
)
require(
    example_executable
    == "${workspaceFolder}/../../.build/examples/01_blink/firmware.elf",
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
    sync_cortex_debug_launch_document(launch_document, launch_executable),
    "debug profile synchronization did not repair a legacy launch document",
)
require(
    launch_document["configurations"]
    == cortex_debug_launch_document(launch_executable)["configurations"]
    + [custom_profile],
    "debug profile synchronization removed a custom consumer configuration",
)
require(
    not sync_cortex_debug_launch_document(launch_document, launch_executable),
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
    "src/hal/impl/shared/drivers/cyw43-driver/vendor/src/cyw43_ll.c.upstream": "lf",
    "src/hal/impl/rp2040/drivers/swserial/swserial.pio": "lf",
    "tests/fixtures/tls_test_ca_der.inc": "lf",
    "src/hal/impl/shared/drivers/cyw43-driver/LICENSE.RP": "lf",
}
for path, expected in attributes.items():
    result = run_checked(["git", "check-attr", "eol", "--", path], cwd=ROOT)
    require(result.stdout.strip().endswith(f"eol: {expected}"), f"wrong eol policy for {path}")
upstream_files = sorted(
    ROOT.glob("src/hal/impl/shared/drivers/cyw43-driver/vendor/src/*.upstream")
)
require(len(upstream_files) == 9, "CYW43 upstream fixture set changed unexpectedly")
for path in upstream_files:
    result = run_checked(
        ["git", "check-attr", "eol", "--", path.relative_to(ROOT).as_posix()],
        cwd=ROOT,
    )
    require(result.stdout.strip().endswith("eol: lf"), f"wrong eol policy for {path}")
pdf_attr = run_checked(
    ["git", "check-attr", "text", "--", "doc/datasheets/RP2040.pdf"],
    cwd=ROOT,
)
require(pdf_attr.stdout.strip().endswith("text: unset"), "PDF files are not marked binary")

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
            "01_blink",
        )
    require(
        dispatcher_log.parent == Path(temp_dir),
        "example dispatcher bypasses the host temporary directory",
    )
    require(
        dispatcher_log.name == "jh_examples_dispatcher_rp2350-arm_01_blink.log",
        "example dispatcher log name changed unexpectedly",
    )
require(
    examples_dispatcher.base_tasks("rp2040", "pico", []) == expected_tasks,
    "example dispatcher bypasses the shared task builder",
)
for task in expected_tasks["tasks"]:
    require(task.get("command") == VSCODE_ENTRY_CONFIG, f"Unix command missing in {task['label']}")
    require(
        task.get("windows", {}).get("command") == VSCODE_ENTRY_WINDOWS_CONFIG,
        f"Windows command missing in {task['label']}",
    )

run_checked([sys.executable, str(SCRIPTS_DIR / "examples_dispatcher.py"), "check-template"])
require(
    not examples_dispatcher.generated_file_mismatches(),
    "checked-in example VS Code files are outside the generator drift gate",
)
require(len(examples_dispatcher.EXAMPLES) == 59, "example registry size changed unexpectedly")
for entry in examples_dispatcher.EXAMPLES:
    vscode_dir = ROOT / "examples" / str(entry["dir"]) / ".vscode"
    tasks = load_json(vscode_dir / "tasks.json")
    settings = load_json(vscode_dir / "settings.json")
    launch_path = vscode_dir / "launch.json"
    launch_text = launch_path.read_text(encoding="utf-8")
    require_launch_contract(
        json.loads(launch_text),
        f"${{workspaceFolder}}/../../.build/examples/{entry['dir']}/firmware.elf",
        f"checked-in example {entry['dir']}",
    )
    require(
        all(task.get("windows", {}).get("command") == VSCODE_ENTRY_WINDOWS_CONFIG
            for task in tasks["tasks"]),
        f"checked-in example {entry['dir']} omits Windows task commands",
    )
    require(
        settings.get("jaszczurhal.vscodeEntryWindows", "").endswith("jh-vscode.cmd"),
        f"checked-in example {entry['dir']} omits the Windows launcher setting",
    )
    require(
        settings.get("cortex-debug.gdbPath.linux") == "gdb-multiarch",
        f"checked-in example {entry['dir']} omits Linux Arm GDB selection",
    )
    require(
        "${config:cortex-debug." not in launch_text,
        f"checked-in example {entry['dir']} requires private Cortex-Debug settings",
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
