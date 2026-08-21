#!/usr/bin/env python3
"""Validate the cross-platform CMake build-environment contract."""

from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest import mock


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from vscode.runtime import jh_vscode as runtime
from vscode.runtime.platform_api import SerialPortRecord

sys.path.insert(0, str(ROOT / "scripts"))
from board_registry import tooling_target_registry


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(runtime.cmake_generator({}) == "Ninja", "Ninja is not the default generator")
require(
    runtime.cmake_generator({"cmake": {"generator": "Unix Makefiles"}})
    == "Unix Makefiles",
    "explicit generator override was ignored",
)

esp32s3_tooling = tooling_target_registry(ROOT)["esp32s3"]
require(
    esp32s3_tooling["provider"] == "esp-idf"
    and esp32s3_tooling["toolchain"] == "esp-idf",
    "ESP32-S3 is not exposed through the ESP-IDF tooling provider",
)
require(
    esp32s3_tooling["defaultBoard"] == "waveshare-esp32-s3-zero"
    and [board["id"] for board in esp32s3_tooling["boards"]]
    == ["waveshare-esp32-s3-zero"],
    "ESP32-S3 tooling duplicated or lost the board registry selection",
)
require(
    esp32s3_tooling["requiredFeatures"] == ["HAL_ENABLE_FREERTOS"],
    "ESP32-S3 tooling lost target-required FreeRTOS",
)
require(
    esp32s3_tooling["espIdf"]
    == {
        "runner": "${jhRoot}/scripts/build_esp_idf.py",
        "artifactManifest": "${buildDir}/jh_esp_idf_artifacts.json",
    },
    "ESP32-S3 tooling runner contract changed",
)
require(
    not esp32s3_tooling["cache"]
    and all("cache" not in board for board in esp32s3_tooling["boards"]),
    "ESP-IDF tooling leaked CMake target/board selectors",
)
require(
    esp32s3_tooling["boards"][0]["identity"]
    == {"enabled": True, "usbVid": 0x303A, "usbPid": 0x1001},
    "ESP32-S3 tooling did not derive USB identity from board.programming",
)

esp32s3_project = ROOT / "tests" / "hardware" / "esp32s3_phase1"
esp32s3_config_dump_output = io.StringIO()
esp32s3_config_dump_args = runtime.build_parser().parse_args(
    ["config-dump", "--project", str(esp32s3_project), "--json"]
)
with redirect_stdout(esp32s3_config_dump_output):
    require(
        runtime.command_config_dump(esp32s3_config_dump_args) == 0,
        "ESP32-S3 config-dump failed",
    )
esp32s3_config_dump = json.loads(esp32s3_config_dump_output.getvalue())
require(
    esp32s3_config_dump["toolchain"] == "esp-idf"
    and esp32s3_config_dump["_sources"]["espIdf"]
    == "registry:esp32s3.build.provider"
    and esp32s3_config_dump["identity"]
    == {"enabled": True, "usbVid": 0x303A, "usbPid": 0x1001}
    and esp32s3_config_dump["_sources"]["identity"]
    == "registry:esp32s3.boards.waveshare-esp32-s3-zero.programming.usb"
    and esp32s3_config_dump["featureResolution"]["requestedFeatures"] == []
    and esp32s3_config_dump["featureResolution"]["resolvedFeatures"]
    == ["HAL_ENABLE_FREERTOS"]
    and esp32s3_config_dump["featureResolution"]["provenance"]
    == {
        "HAL_ENABLE_FREERTOS": [
            "target:esp32s3:requiredFeatures[0]"
        ]
    },
    "ESP32-S3 config dump differs from target-required feature resolution",
)

with tempfile.TemporaryDirectory(prefix="jh build env spacje ") as temporary_text:
    temporary = Path(temporary_text)
    project = temporary / "Projekt modułu" / "firmware module"
    source = project / "cmake source"
    source.mkdir(parents=True)
    state_path = temporary / "host environment.json"
    build_root = temporary / "b"
    tools_root = temporary / "narzędzia hosta"
    tools = {
        "cmake": str(tools_root / "cmake.exe"),
        "ninja": str(tools_root / "ninja.exe"),
        "picotool": str(tools_root / "picotool.exe"),
        "gnu-arm": str(tools_root / "arm" / "bin" / "arm-none-eabi-gcc.exe"),
        "riscv": str(tools_root / "riscv" / "bin" / "riscv32-unknown-elf-gcc.exe"),
        "openocd": str(tools_root / "openocd.exe"),
    }
    arm_objdump = Path(tools["gnu-arm"]).with_name("arm-none-eabi-objdump.exe")
    arm_gdb = Path(tools["gnu-arm"]).with_name("arm-none-eabi-gdb.exe")
    riscv_objdump = Path(tools["riscv"]).with_name("riscv32-unknown-elf-objdump.exe")
    arm_objdump.parent.mkdir(parents=True)
    riscv_objdump.parent.mkdir(parents=True)
    arm_objdump.touch()
    arm_gdb.touch()
    riscv_objdump.touch()
    openocd = Path(tools["openocd"])
    openocd.touch()
    openocd_scripts = openocd.parent / "scripts"
    for relative in (
        "board/st_nucleo_g4.cfg",
        "interface/cmsis-dap.cfg",
        "interface/stlink.cfg",
        "target/rp2040.cfg",
        "target/rp2350.cfg",
        "target/stm32g4x.cfg",
    ):
        path = openocd_scripts / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    state_path.write_text(
        json.dumps(
            {
                "schemaVersion": 1,
                "toolsRoot": str(tools_root),
                "buildRoot": str(build_root),
                "python": sys.executable,
                "tools": tools,
            }
        ),
        encoding="utf-8",
    )
    config = {
        "toolchain": "cmake",
        "target": "rp2040",
        "board": "pico",
        "cmake": {
            "sourceDir": str(source),
            "cache": {
                "JH_EXTRA_INCLUDES": f"{project / 'include one'};{project / 'include two'}",
                "JH_EXTRA_SOURCES": (
                    r"C:\development\Fiesta\src\common\fiesta_app_entry.cpp;"
                    r"D:\consumer path\extra.cpp"
                ),
                "JH_USB_PRODUCT": "Fiesta board with spaces",
            },
        },
    }
    captured: list[tuple[list[str], dict[str, str] | None]] = []

    def fake_run(command, *, verbose=False, environment=None):
        del verbose
        Path(command[command.index("-B") + 1]).mkdir(parents=True, exist_ok=True)
        captured.append((list(command), environment))
        return 0

    with mock.patch.dict(
        os.environ,
        {
            "JH_TEST_WINDOWS_HOST": "1",
            "JH_WINDOWS_HOST_ENVIRONMENT": str(state_path),
        },
        clear=False,
    ), mock.patch.object(runtime, "run_command", side_effect=fake_run):
        build_dir = runtime.get_cmake_build_dir(config, project)
        require(
            runtime.path_within(build_dir, build_root),
            "native Windows CMake build escaped the short managed build root",
        )
        require(runtime.configure_cmake_project(config, project) == 0, "configure failed")
        require(
            runtime.objdump_program(config) == str(arm_objdump),
            "verified GNU Arm objdump was ignored",
        )
        require(
            runtime.objdump_program({"target": "rp2350-riscv"}) == str(riscv_objdump),
            "verified GNU RISC-V objdump was ignored",
        )
        debug_tools = runtime.debug_tool_paths(config)
        require(debug_tools is not None, "verified debug tools were not resolved")
        require(
            debug_tools["openocd"] == tools["openocd"],
            "verified OpenOCD was ignored",
        )
        require(
            debug_tools["gdb"] == str(arm_gdb),
            "GNU Arm GDB beside the compiler was ignored",
        )
        require(
            debug_tools["scripts"] == str(openocd_scripts),
            "OpenOCD scripts root was not resolved",
        )
        require(
            debug_tools["targetConfig"] == "target/rp2040.cfg",
            "RP2040 OpenOCD target config changed",
        )

        debug_config = runtime.cmake_build_config(config, debug=True)
        require(
            runtime.get_cmake_build_dir(debug_config, project)
            == build_dir / "debug",
            "debug and optimized builds share one CMake cache",
        )
        require(
            "-DCMAKE_BUILD_TYPE=Debug"
            in runtime.cmake_cache_args(debug_config, project),
            "debug build did not select the CMake Debug configuration",
        )
        require(
            runtime.cmake_build_config(config, debug=False) is config,
            "optimized build configuration was needlessly replaced",
        )

    command, environment = captured[0]
    require(command[0] == tools["cmake"], "verified CMake executable was ignored")
    require(command[command.index("-G") + 1] == "Ninja", "Ninja was not selected")
    require(
        f"-DCMAKE_MAKE_PROGRAM={tools['ninja']}" in command,
        "verified Ninja executable was not passed to CMake",
    )
    require(
        f"-DPython3_EXECUTABLE={Path(sys.executable).resolve()}" in command,
        "the running verified Python interpreter was not passed to CMake",
    )
    require(
        f"-DJH_PICOTOOL_EXECUTABLE={tools['picotool']}" in command,
        "resolved Windows picotool was not passed to CMake",
    )
    require(
        any(item.startswith("-DJH_EXTRA_INCLUDES=") and ";" in item for item in command),
        "CMake list cache value was split into multiple process arguments",
    )
    require(
        (
            "-DJH_EXTRA_SOURCES=C:/development/Fiesta/src/common/"
            "fiesta_app_entry.cpp;D:/consumer path/extra.cpp"
        )
        in command,
        "Windows paths in CMake cache values were not normalized",
    )
    require(
        "-DJH_USB_PRODUCT=Fiesta board with spaces" in command,
        "cache value containing spaces was split or dropped",
    )
    require(environment is not None, "resolved tool directories were omitted from PATH")
    require(
        str(tools_root) in environment["PATH"],
        "resolved tool directory was omitted from the CMake environment",
    )
    stm_config = {
        "target": "stm32g474",
        "identity": {
            "enabled": True,
            "usbManufacturer": "Jaszczur",
            "usbProduct": "Fiesta board with spaces",
        },
    }
    require(
        not any(
            item.startswith("-DJH_USB_")
            for item in runtime.cmake_cache_args(stm_config, project)
        ),
        "RP-only USB identity cache values leaked into the STM32 recipe",
    )
    require(
        not any(
            item.startswith("-DJH_PICOTOOL_EXECUTABLE=")
            for item in runtime.platform_cmake_cache_args(stm_config)
        ),
        "RP-only picotool cache value leaked into the STM32 recipe",
    )

    rp_cmake_dir = runtime.get_cmake_build_dir(config, project)
    rp_cmake_dir.mkdir(parents=True)
    rp_outputs = {
        "firmware.elf": "rp elf",
        "firmware.bin": "rp bin",
        "firmware.hex": "rp hex",
        "firmware.uf2": "rp uf2",
        "firmware.elf.map": "rp map",
    }
    for name, value in rp_outputs.items():
        (rp_cmake_dir / name).write_text(value, encoding="utf-8")
    require(
        runtime.synchronize_cmake_firmware_artifacts(config, project) == 0,
        "RP artifacts were not synchronized",
    )

    stm_sync_config = dict(stm_config)
    stm_sync_config.update(
        {
            "toolchain": "cmake",
            "board": "nucleo-g474re",
            "buildDir": str(project / ".build"),
        }
    )
    stm_cmake_dir = runtime.get_cmake_build_dir(stm_sync_config, project)
    stm_cmake_dir.mkdir(parents=True)
    for name in ("firmware.elf", "firmware.bin", "firmware.hex", "firmware.map"):
        (stm_cmake_dir / name).write_text(f"stm {name}", encoding="utf-8")
    require(
        runtime.synchronize_cmake_firmware_artifacts(stm_sync_config, project) == 0,
        "STM32 artifacts were not synchronized",
    )
    require(
        not (project / ".build" / "firmware.uf2").exists(),
        "STM32 artifact switch retained an RP-only UF2",
    )
    require(
        (project / ".build" / "firmware.elf").read_text(encoding="utf-8")
        == "stm firmware.elf",
        "STM32 artifact switch retained an RP firmware image",
    )
    require(
        runtime.synchronize_cmake_firmware_artifacts(config, project) == 0,
        "RP artifacts were not restored after switching back",
    )
    require(
        (project / ".build" / "firmware.uf2").read_text(encoding="utf-8")
        == "rp uf2",
        "RP artifact switch did not restore the UF2",
    )
    stable_build_dir = project / ".build"
    (stable_build_dir / "compile_commands_patched.json").write_text(
        "[]\n", encoding="utf-8"
    )
    require(
        runtime.invalidate_stable_firmware_artifacts(config, project) == 0,
        "stable firmware artifacts could not be invalidated",
    )
    require(
        not any(
            (stable_build_dir / name).exists()
            for name in runtime.STABLE_FIRMWARE_ARTIFACTS
        ),
        "failed-build guard retained an uploadable firmware artifact",
    )
    require(
        (stable_build_dir / "compile_commands_patched.json").is_file(),
        "failed-build guard removed the IntelliSense database",
    )

with tempfile.TemporaryDirectory(prefix="jh Linux debug tools ") as temporary_text:
    temporary = Path(temporary_text)
    bin_dir = temporary / "usr" / "bin"
    scripts_dir = temporary / "usr" / "share" / "openocd" / "scripts"
    openocd = bin_dir / "openocd"
    gdb_multiarch = bin_dir / "gdb-multiarch"
    openocd.parent.mkdir(parents=True)
    openocd.touch()
    gdb_multiarch.touch()
    for relative in (
        "board/st_nucleo_g4.cfg",
        "interface/stlink.cfg",
        "target/stm32g4x.cfg",
    ):
        path = scripts_dir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()

    def linux_debug_which(program: str) -> str | None:
        return {
            "openocd": str(openocd),
            "arm-none-eabi-gdb": None,
            "gdb-multiarch": str(gdb_multiarch),
        }.get(program)

    # Windows CI has a bootstrap-produced host record; hide it so this fixture
    # exercises the Linux PATH fallback on every host.
    with mock.patch.object(runtime, "resolved_windows_tools", return_value={}), \
            mock.patch.object(runtime.shutil, "which", side_effect=linux_debug_which):
        debug_tools = runtime.debug_tool_paths({"target": "stm32g474"})
        require(debug_tools is not None, "Linux STM32 debug tools were not resolved")
        require(
            debug_tools["gdb"] == str(gdb_multiarch),
            "gdb-multiarch was not selected as the Linux Arm debugger",
        )
        require(
            debug_tools["scripts"] == str(scripts_dir),
            "target-specific STM32 OpenOCD scripts were rejected",
        )
        require(
            runtime.debug_tool_paths({"target": "rp2350-arm"}) is None,
            "an incomplete RP2350 OpenOCD installation was accepted",
        )

if sys.platform != "win32":
    linux_args = runtime.platform_cmake_cache_args({"target": "rp2040"})
    require(
        any(
            item.endswith("/.build/tools/picotool/picotool")
            for item in linux_args
            if item.startswith("-DJH_PICOTOOL_EXECUTABLE=")
        ),
        "Linux picotool executable path was not resolved by the runtime",
    )


def write_esp_idf_artifacts(build_dir: Path) -> None:
    files = {
        "bootloader/bootloader.bin": b"bootloader",
        "partition_table/partition-table.bin": b"partitions",
        "application.bin": b"application",
        "application.elf": b"elf",
        "application.map": b"map",
        "sdkconfig": b"config",
    }
    for relative, content in files.items():
        path = build_dir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    (build_dir / "compile_commands.json").write_text(
        json.dumps(
            [
                {
                    "directory": str(build_dir),
                    "file": "app.cpp",
                    "command": "xtensa-esp32s3-elf-g++ -c app.cpp",
                }
            ]
        ),
        encoding="utf-8",
    )
    manifest = {
        "schemaVersion": 1,
        "target": "esp32s3",
        "board": "waveshare-esp32-s3-zero",
        "integration": {"component": "jaszczurhal"},
        "flashImages": [
            {"offset": "0x0", "path": "bootloader/bootloader.bin"},
            {
                "offset": "0x8000",
                "path": "partition_table/partition-table.bin",
            },
            {"offset": "0x10000", "path": "application.bin"},
        ],
        "artifacts": {
            "applicationBinary": "application.bin",
            "applicationElf": "application.elf",
            "applicationMap": "application.map",
            "compileCommands": "compile_commands.json",
            "sdkconfig": "sdkconfig",
        },
    }
    (build_dir / "jh_esp_idf_artifacts.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )


with tempfile.TemporaryDirectory(prefix="jh ESP-IDF VS Code ") as temporary_text:
    temporary = Path(temporary_text)
    project = temporary / "firmware project"
    vscode_dir = project / ".vscode"
    build_dir = project / ".build"
    vscode_dir.mkdir(parents=True)
    (project / "hal_project_config.h").write_text(
        "#pragma once\n#define HAL_PROVIDE_APP_ENTRY\n", encoding="utf-8"
    )
    esp_config = {
        "project": "ESP-IDF fixture",
        "module": "esp_fixture",
        "toolchain": "esp-idf",
        "target": "esp32s3",
        "board": "waveshare-esp32-s3-zero",
        "buildDir": str(build_dir),
        "espIdf": {
            "runner": str(ROOT / "scripts" / "build_esp_idf.py"),
            "artifactManifest": str(
                build_dir / "jh_esp_idf_artifacts.json"
            ),
        },
        "cmake": {
            "cache": {
                "JH_EXTRA_DEFINES": (
                    "HAL_ENABLE_WIFI;CUSTOM_BUFFER=7;HAL_DISABLE_BLE"
                ),
                "EXTRA_HAL_DEFINES": "HAL_ENABLE_WIFI=1;TRACE_BUILD",
            }
        },
        "upload": {"strategy": "esp-idf"},
        "uploadPort": "/dev/ttyACM7",
        "identity": {
            "enabled": True,
            "usbVid": 0x303A,
            "usbPid": 0x1001,
        },
        "featureResolution": {"resolvedFeatures": []},
    }

    def serial_adapter(records: list[SerialPortRecord]):
        def record_for(port: str):
            normalized_port = str(port)
            return next(
                (
                    record
                    for record in records
                    if normalized_port == record.device
                    or normalized_port in record.aliases
                    or normalized_port
                    in {
                        str(Path("/dev/serial/by-id") / alias)
                        for alias in record.aliases
                        if not Path(alias).is_absolute()
                    }
                ),
                None,
            )

        return SimpleNamespace(
            platform_name="linux",
            list_serial_ports=lambda: list(records),
            resolve_serial_port=lambda port: (
                record_for(port).device if record_for(port) else port
            ),
            serial_port_exists=lambda port: record_for(port) is not None,
            serial_port_record=record_for,
        )

    matching_record = SerialPortRecord(
        device="/dev/ttyACM7",
        vid=0x303A,
        pid=0x1001,
        manufacturer="Espressif",
        product="USB JTAG/serial debug unit",
        platform="linux",
    )
    matching_adapter = serial_adapter([matching_record])
    runner_commands: list[list[str]] = []

    def fake_esp_runner(command, *, verbose=False, environment=None):
        del verbose, environment
        runner_commands.append(list(command))
        if command[2] == "build":
            write_esp_idf_artifacts(build_dir)
        return 0

    build_args = runtime.build_parser().parse_args(
        ["build", "--project", str(project)]
    )
    with mock.patch.object(
        runtime,
        "load_config_for_action",
        return_value=(project, esp_config, 0),
    ), mock.patch.object(runtime, "run_command", side_effect=fake_esp_runner):
        require(runtime.command_build(build_args) == 0, "ESP-IDF build failed")

    require(
        [command[2] for command in runner_commands] == ["build", "artifacts"],
        "ESP-IDF build did not materialize and validate its artifact contract",
    )
    build_command = runner_commands[0]
    for option, value in (
        ("--project", str(project)),
        ("--target", "esp32s3"),
        ("--board", "waveshare-esp32-s3-zero"),
        ("--output", str(build_dir)),
    ):
        require(
            build_command[build_command.index(option) + 1] == value,
            f"ESP-IDF build runner lost {option}",
        )
    require(
        build_command.count("--feature") == 1
        and build_command[build_command.index("--feature") + 1]
        == "HAL_ENABLE_WIFI",
        "ESP-IDF build duplicated or lost manifest feature arguments",
    )
    forwarded_defines = {
        build_command[index + 1]
        for index, value in enumerate(build_command)
        if value == "--define"
    }
    require(
        forwarded_defines == {
            "CUSTOM_BUFFER=7",
            "HAL_DISABLE_BLE",
            "TRACE_BUILD",
        },
        f"ESP-IDF build forwarded wrong manifest definitions: {forwarded_defines}",
    )

    manifest, artifacts, flash_images = (
        runtime.validate_esp_idf_artifact_manifest(esp_config, project)
    )
    require(
        manifest["target"] == "esp32s3"
        and artifacts["applicationElf"] == build_dir / "application.elf"
        and len(flash_images) == 3,
        "jh-vscode collapsed or misresolved the ESP-IDF multi-image manifest",
    )

    escaped = json.loads(
        (build_dir / "jh_esp_idf_artifacts.json").read_text(encoding="utf-8")
    )
    escaped["flashImages"][0]["path"] = "../outside.bin"
    (temporary / "outside.bin").write_bytes(b"outside")
    (build_dir / "jh_esp_idf_artifacts.json").write_text(
        json.dumps(escaped), encoding="utf-8"
    )
    try:
        runtime.validate_esp_idf_artifact_manifest(esp_config, project)
    except ValueError as error:
        require(
            "escapes the build directory" in str(error),
            "ESP-IDF escaped-artifact diagnostic changed",
        )
    else:
        raise AssertionError("ESP-IDF artifact manifest accepted an escaped path")
    write_esp_idf_artifacts(build_dir)

    runner_commands.clear()
    upload_args = runtime.build_parser().parse_args(
        [
            "upload",
            "--project",
            str(project),
            "--port",
            "/dev/ttyACM7",
        ]
    )
    with mock.patch.object(
        runtime,
        "load_config_for_action",
        return_value=(project, esp_config, 0),
    ), mock.patch.object(
        runtime, "get_platform_adapter", return_value=matching_adapter
    ), mock.patch.object(
        runtime, "release_port_for_upload", return_value=0
    ) as release_port, mock.patch.object(
        runtime, "end_upload_release"
    ) as end_release, mock.patch.object(
        runtime, "run_command", side_effect=fake_esp_runner
    ):
        require(runtime.command_upload(upload_args) == 0, "ESP-IDF upload failed")
    require(
        [command[2] for command in runner_commands]
        == ["build", "artifacts", "flash"]
        and runner_commands[2][runner_commands[2].index("--port") + 1]
        == "/dev/ttyACM7",
        "ESP-IDF upload did not rebuild or hand the resolved port to the runner",
    )
    release_port.assert_called_once_with("/dev/ttyACM7", project)
    end_release.assert_called_once_with(project)

    stable_alias = "usb-Espressif_USB_JTAG_serial_debug_unit-fixture-if00"
    moving_records = [
        SerialPortRecord(
            device="/dev/ttyACM7",
            vid=0x303A,
            pid=0x1001,
            aliases=(stable_alias,),
            platform="linux",
        )
    ]
    moving_adapter = serial_adapter(moving_records)
    auto_config = dict(esp_config)
    auto_config.pop("uploadPort", None)
    auto_upload_args = runtime.build_parser().parse_args(
        ["upload", "--project", str(project)]
    )

    def move_verified_device_during_build(*_args, **_kwargs):
        moving_records[:] = [
            SerialPortRecord(
                device="/dev/ttyACM8",
                vid=0x303A,
                pid=0x1001,
                aliases=(stable_alias,),
                platform="linux",
            )
        ]
        return 0

    with mock.patch.object(
        runtime,
        "load_config_for_action",
        return_value=(project, auto_config, 0),
    ), mock.patch.object(
        runtime, "build_preflight_diagnostics", return_value=[]
    ), mock.patch.object(
        runtime, "get_platform_adapter", return_value=moving_adapter
    ), mock.patch.object(
        runtime, "command_build", side_effect=move_verified_device_during_build
    ), mock.patch.object(
        runtime, "release_port_for_upload", return_value=0
    ) as moving_release, mock.patch.object(
        runtime, "end_upload_release"
    ), mock.patch.object(
        runtime, "run_esp_idf_action", return_value=0
    ) as moving_flash:
        require(
            runtime.command_upload(auto_upload_args) == 0,
            "ESP-IDF upload lost the verified device after its tty path changed",
        )
    moving_release.assert_called_once_with("/dev/ttyACM8", project)
    moving_flash.assert_called_once_with(
        auto_config, project, "flash", port="/dev/ttyACM8"
    )

    release_moving_records = [
        SerialPortRecord(
            device="/dev/ttyACM7",
            vid=0x303A,
            pid=0x1001,
            aliases=(stable_alias,),
            platform="linux",
        )
    ]
    release_moving_adapter = serial_adapter(release_moving_records)

    def move_verified_device_during_release(port, released_project):
        require(
            port == "/dev/ttyACM7" and released_project == project,
            "ESP-IDF upload released an unexpected pre-handoff port",
        )
        release_moving_records[:] = [
            SerialPortRecord(
                device="/dev/ttyACM8",
                vid=0x303A,
                pid=0x1001,
                aliases=(stable_alias,),
                platform="linux",
            )
        ]
        return 0

    with mock.patch.object(
        runtime,
        "load_config_for_action",
        return_value=(project, auto_config, 0),
    ), mock.patch.object(
        runtime, "build_preflight_diagnostics", return_value=[]
    ), mock.patch.object(
        runtime, "get_platform_adapter", return_value=release_moving_adapter
    ), mock.patch.object(
        runtime, "command_build", return_value=0
    ), mock.patch.object(
        runtime,
        "release_port_for_upload",
        side_effect=move_verified_device_during_release,
    ), mock.patch.object(
        runtime, "end_upload_release"
    ) as release_moving_end, mock.patch.object(
        runtime, "run_esp_idf_action", return_value=0
    ) as release_moving_flash:
        require(
            runtime.command_upload(auto_upload_args) == 0,
            "ESP-IDF upload lost the stable alias during monitor release",
        )
    release_moving_flash.assert_called_once_with(
        auto_config, project, "flash", port="/dev/ttyACM8"
    )
    release_moving_end.assert_called_once_with(project)

    def post_build_identity_change(
        replacement_records: list[SerialPortRecord],
    ) -> tuple[int, mock.Mock, mock.Mock]:
        current_records = [matching_record]
        adapter = serial_adapter(current_records)

        def mutate_identity(*_args, **_kwargs):
            current_records[:] = replacement_records
            return 0

        with mock.patch.object(
            runtime,
            "load_config_for_action",
            return_value=(project, esp_config, 0),
        ), mock.patch.object(
            runtime, "build_preflight_diagnostics", return_value=[]
        ), mock.patch.object(
            runtime, "get_platform_adapter", return_value=adapter
        ), mock.patch.object(
            runtime, "command_build", side_effect=mutate_identity
        ), mock.patch.object(
            runtime, "release_port_for_upload", return_value=0
        ) as release, mock.patch.object(
            runtime, "end_upload_release"
        ), mock.patch.object(
            runtime, "run_esp_idf_action", return_value=0
        ) as flash:
            return runtime.command_upload(upload_args), release, flash

    changed_identity_status, changed_identity_release, changed_identity_flash = (
        post_build_identity_change(
            [
                SerialPortRecord(
                    device="/dev/ttyACM7",
                    vid=0xFFFF,
                    pid=0xFFFF,
                    platform="linux",
                )
            ]
        )
    )
    require(
        changed_identity_status == runtime.EXIT_UNSAFE_DEVICE,
        "ESP-IDF upload did not reject an identity swap during the build",
    )
    changed_identity_release.assert_not_called()
    changed_identity_flash.assert_not_called()

    stale_after_build_status, stale_after_build_release, stale_after_build_flash = (
        post_build_identity_change([])
    )
    require(
        stale_after_build_status == runtime.EXIT_UNSAFE_DEVICE,
        "ESP-IDF upload did not reject a stale port after the build",
    )
    stale_after_build_release.assert_not_called()
    stale_after_build_flash.assert_not_called()

    release_swap_records = [matching_record]
    release_swap_adapter = serial_adapter(release_swap_records)

    def swap_identity_during_release(*_args, **_kwargs):
        release_swap_records[:] = [
            SerialPortRecord(
                device="/dev/ttyACM7",
                vid=0xFFFF,
                pid=0xFFFF,
                platform="linux",
            )
        ]
        return 0

    with mock.patch.object(
        runtime,
        "load_config_for_action",
        return_value=(project, esp_config, 0),
    ), mock.patch.object(
        runtime, "build_preflight_diagnostics", return_value=[]
    ), mock.patch.object(
        runtime, "get_platform_adapter", return_value=release_swap_adapter
    ), mock.patch.object(
        runtime, "command_build", return_value=0
    ), mock.patch.object(
        runtime,
        "release_port_for_upload",
        side_effect=swap_identity_during_release,
    ), mock.patch.object(
        runtime, "end_upload_release"
    ) as release_swap_end, mock.patch.object(
        runtime, "run_esp_idf_action", return_value=0
    ) as release_swap_flash:
        release_swap_status = runtime.command_upload(upload_args)
    require(
        release_swap_status == runtime.EXIT_UNSAFE_DEVICE,
        "ESP-IDF upload did not reject an identity swap during monitor release",
    )
    release_swap_flash.assert_not_called()
    release_swap_end.assert_called_once_with(project)

    def rejected_esp_upload(
        port: str,
        adapter,
        *,
        allow_unverified: bool = False,
        config: dict | None = None,
    ) -> tuple[int, mock.Mock, mock.Mock]:
        arguments = ["upload", "--project", str(project)]
        if port:
            arguments.extend(["--port", port])
        if allow_unverified:
            arguments.append("--allow-unverified-port")
        args = runtime.build_parser().parse_args(arguments)
        selected_config = config or esp_config
        with mock.patch.object(
            runtime,
            "load_config_for_action",
            return_value=(project, selected_config, 0),
        ), mock.patch.object(
            runtime, "build_preflight_diagnostics", return_value=[]
        ), mock.patch.object(
            runtime, "get_platform_adapter", return_value=adapter
        ), mock.patch.object(
            runtime, "command_build", return_value=0
        ) as build, mock.patch.object(
            runtime, "release_port_for_upload", return_value=0
        ), mock.patch.object(
            runtime, "end_upload_release"
        ), mock.patch.object(
            runtime, "run_esp_idf_action", return_value=0
        ) as flash:
            return runtime.command_upload(args), build, flash

    wrong_identity_adapter = serial_adapter(
        [
            SerialPortRecord(
                device="/dev/ttyACM8",
                vid=0xFFFF,
                pid=0xFFFF,
                platform="linux",
            )
        ]
    )
    status, rejected_build, rejected_flash = rejected_esp_upload(
        "/dev/ttyACM8", wrong_identity_adapter
    )
    require(
        status == runtime.EXIT_UNSAFE_DEVICE,
        "ESP-IDF upload accepted an explicit port with the wrong VID/PID",
    )
    rejected_build.assert_not_called()
    rejected_flash.assert_not_called()

    status, stale_build, stale_flash = rejected_esp_upload(
        "/dev/ttyACM9", serial_adapter([])
    )
    require(
        status == runtime.EXIT_UNSAFE_DEVICE,
        "ESP-IDF upload accepted a stale explicit port",
    )
    stale_build.assert_not_called()
    stale_flash.assert_not_called()

    multiple_config = dict(esp_config)
    multiple_config.pop("uploadPort", None)
    multiple_adapter = serial_adapter(
        [
            matching_record,
            SerialPortRecord(
                device="/dev/ttyACM8",
                vid=0x303A,
                pid=0x1001,
                platform="linux",
            ),
        ]
    )
    status, multiple_build, multiple_flash = rejected_esp_upload(
        "", multiple_adapter, config=multiple_config
    )
    require(
        status == runtime.EXIT_UNSAFE_DEVICE,
        "ESP-IDF upload selected one of multiple matching boards",
    )
    multiple_build.assert_not_called()
    multiple_flash.assert_not_called()

    status, escaped_build, escaped_flash = rejected_esp_upload(
        "/dev/ttyACM8",
        wrong_identity_adapter,
        allow_unverified=True,
    )
    require(
        status == 0,
        "--allow-unverified-port no longer overrides ESP-IDF USB identity",
    )
    escaped_build.assert_called_once()
    escaped_flash.assert_called_once_with(
        esp_config, project, "flash", port="/dev/ttyACM8"
    )

    monitor_commands: list[list[str]] = []
    monitor_args = runtime.build_parser().parse_args(
        ["monitor", "--project", str(project)]
    )
    monitor_adapter = serial_adapter([matching_record])
    monitor_adapter.persistent_monitor_path = lambda: (
        ROOT
        / "vscode"
        / "linux"
        / "runtime"
        / "serial_persistent.py"
    )
    with mock.patch.object(
        runtime,
        "load_config_for_action",
        return_value=(project, esp_config, 0),
    ), mock.patch.object(
        runtime, "get_platform_adapter", return_value=monitor_adapter
    ), mock.patch.object(
        runtime,
        "run_command",
        side_effect=lambda command, **kwargs: monitor_commands.append(
            list(command)
        )
        or 0,
    ):
        require(
            runtime.command_monitor(monitor_args, "pico") == 0,
            "ESP-IDF monitor failed",
        )
    monitor_command = monitor_commands[0]
    require(
        monitor_command[monitor_command.index("--mode") + 1] == "esp"
        and "--follow-identity" in monitor_command
        and monitor_command[-1] == "/dev/ttyACM7",
        "ESP-IDF monitor did not inherit the upload port and provider mode",
    )
    monitor_identity = json.loads(
        monitor_command[monitor_command.index("--identity-json") + 1]
    )
    require(
        monitor_identity["usbVid"] == 0x303A
        and monitor_identity["usbPid"] == 0x1001,
        "ESP-IDF monitor did not follow the registry USB identity",
    )

    runner_commands.clear()
    refresh_args = runtime.build_parser().parse_args(
        ["refresh-intellisense", "--project", str(project)]
    )
    with mock.patch.object(
        runtime,
        "load_config_for_action",
        return_value=(project, esp_config, 0),
    ), mock.patch.object(runtime, "run_command", side_effect=fake_esp_runner):
        require(
            runtime.command_refresh_intellisense(refresh_args) == 0,
            "ESP-IDF IntelliSense refresh failed",
        )
    patched_database = json.loads(
        (build_dir / "compile_commands_patched.json").read_text(
            encoding="utf-8"
        )
    )
    cpp_properties = json.loads(
        (vscode_dir / "c_cpp_properties.json").read_text(encoding="utf-8")
    )["configurations"][0]
    require(
        "xtensa-esp32s3-elf-g++" in patched_database[0]["command"]
        and "intelliSenseMode" not in cpp_properties,
        "ESP-IDF IntelliSense replaced Xtensa compile commands with an Arm mode",
    )

from vscode.runtime.monitor import core as monitor_core

require(
    monitor_core.classify_port(SimpleNamespace(vid=0x303A, pid=0x1001))
    == "esp",
    "Espressif USB JTAG/serial was not classified for the persistent monitor",
)
