#!/usr/bin/env python3
"""Validate the cross-platform CMake build-environment contract."""

from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
from unittest import mock


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from vscode.runtime import jh_vscode as runtime


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(runtime.cmake_generator({}) == "Ninja", "Ninja is not the default generator")
require(
    runtime.cmake_generator({"cmake": {"generator": "Unix Makefiles"}})
    == "Unix Makefiles",
    "explicit generator override was ignored",
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
    riscv_objdump = Path(tools["riscv"]).with_name("riscv32-unknown-elf-objdump.exe")
    arm_objdump.parent.mkdir(parents=True)
    riscv_objdump.parent.mkdir(parents=True)
    arm_objdump.touch()
    riscv_objdump.touch()
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
