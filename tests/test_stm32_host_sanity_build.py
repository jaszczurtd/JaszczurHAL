#!/usr/bin/env python3
"""Guard the STM32G474 host-compiler sanity configuration used by analysis."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
HOST_SANITY_FLAG = "-DJH_STM32_HOST_SANITY=ON"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


recipe = (ROOT / "stm32_lib" / "CMakeLists.txt").read_text(encoding="utf-8")
require(
    "option(JH_STM32_HOST_SANITY" in recipe,
    "stm32_lib does not declare the host-compiler sanity option",
)
require(
    'set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH' in recipe,
    "stm32_lib no longer pins its toolchain into the CMake cache",
)
require(
    "-DJH_STM32_HOST_SANITY=ON for the host-compiler analysis build." in recipe,
    "the missing-toolchain error does not name the host-compiler mode",
)

# Every caller configuring stm32_lib must state its mode: a cross toolchain for
# firmware, or the host-compiler analysis mode.
for relative in ("runalltests.sh", ".github/workflows/ci.yml"):
    lines = (ROOT / relative).read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines):
        if "-S stm32_lib" not in line:
            continue
        # Follow shell and PowerShell continuations so the whole flag list is read.
        window = [line]
        cursor = index
        while cursor + 1 < len(lines) and window[-1].rstrip().endswith(("\\", "`")):
            cursor += 1
            window.append(lines[cursor])
        text = "\n".join(window)
        require(
            "-DCMAKE_TOOLCHAIN_FILE" in text or HOST_SANITY_FLAG in text,
            f"{relative}:{index + 1} configures stm32_lib without declaring its mode",
        )

ci_workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
host_test_job = ci_workflow.split("\n  test:\n", 1)[1].split("\n  memcheck:\n", 1)[0]
for package in (
    "gcc-arm-none-eabi",
    "libnewlib-arm-none-eabi",
    "libstdc++-arm-none-eabi-dev",
):
    require(
        package in host_test_job,
        f"the Linux host-test job does not provision {package}",
    )
for dependency in ("ensure_btstack.sh --force", "ensure_freertos_kernel.sh --force"):
    require(
        dependency in host_test_job,
        f"the Linux host-test job does not prepare {dependency.split()[0]}",
    )

memcheck_job = ci_workflow.split("\n  memcheck:\n", 1)[1].split(
    "\n  static-analysis:\n", 1
)[0]
for package in (
    "gcc-arm-none-eabi",
    "libnewlib-arm-none-eabi",
    "libstdc++-arm-none-eabi-dev",
):
    require(
        package in memcheck_job,
        f"the memcheck job does not provision {package}",
    )
for dependency in ("ensure_btstack.sh --force", "ensure_freertos_kernel.sh --force"):
    require(
        dependency in memcheck_job,
        f"the memcheck job does not prepare {dependency.split()[0]}",
    )

windows_tooling_job = ci_workflow.split("\n  windows-tooling:\n", 1)[1].split(
    "\n  windows-firmware:\n", 1
)[0]
for fragment in (
    "$previousErrorActionPreference = $ErrorActionPreference",
    "$ErrorActionPreference = 'Continue'",
    "$ErrorActionPreference = $previousErrorActionPreference",
):
    require(
        fragment in windows_tooling_job,
        "the Windows FreeRTOS configure probe does not safely capture CMake stderr",
    )

if sys.platform == "win32":
    print("host-compiler STM32 configure probe: skipped on native Windows")
    raise SystemExit(0)

cmake = shutil.which("cmake")
require(cmake is not None, "cmake is required to verify the host sanity configure")

# Board-profile generation only writes below a managed build tree.
scratch = ROOT / ".build" / "tests" / f"stm32-host-sanity-{os.getpid()}"
accepted = scratch / "with-flag"
rejected = scratch / "without-flag"
project_config = scratch / "project-config"
project_features = scratch / "project-features"
invalid_unity = scratch / "invalid-unity"
invalid_unity_list = scratch / "invalid-unity-list"
direct_unity = scratch / "direct-unity"
radio_package = scratch / "radio-package"
install_prefix = scratch / "install-prefix"
direct_consumer = scratch / "direct-consumer"
incremental_source = scratch / "incremental-source"
incremental_config = scratch / "incremental-config"
incremental_build = scratch / "incremental-build"
try:
    with_flag = subprocess.run(
        [cmake, "-S", str(ROOT / "stm32_lib"), "-B", str(accepted), HOST_SANITY_FLAG],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        with_flag.returncode == 0,
        "host-compiler STM32 sanity configure failed:\n"
        f"{with_flag.stdout}\n{with_flag.stderr}",
    )
    require(
        "host-compiler sanity build" in with_flag.stdout,
        "host-compiler STM32 configure does not report its mode",
    )

    project_config.mkdir(parents=True)
    (project_config / "hal_project_config.h").write_text(
        "#define HAL_ENABLE_MQTT 1\n"
        "#define HAL_ENABLE_TLS 1\n"
        "#define HAL_ENABLE_BLE 1\n"
        "#define HAL_ENABLE_FREERTOS 1\n"
        "#define HAL_ENABLE_UNITY 1\n",
        encoding="utf-8",
    )
    project_feature_configure = subprocess.run(
        [
            cmake,
            "-S",
            str(ROOT / "stm32_lib"),
            "-B",
            str(project_features),
            HOST_SANITY_FLAG,
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            "-DJH_BOARD=nucleo-g474re-pim730",
            f"-DHAL_PROJECT_CONFIG_DIR={project_config}",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        project_feature_configure.returncode == 0,
        "project-header STM32 sanity configure failed:\n"
        f"{project_feature_configure.stdout}\n"
        f"{project_feature_configure.stderr}",
    )
    compile_commands = json.loads(
        (project_features / "compile_commands.json").read_text(encoding="utf-8")
    )
    compiled_sources = {
        Path(str(entry["file"])).resolve().as_posix()
        for entry in compile_commands
    }
    for suffix in (
        "/frameworks/PubSubClient/src/PubSubClient.cpp",
        "/third_party/BearSSL/src/aead/ccm.c",
        "/third_party/BTstack/src/ble/att_db.c",
        "/third_party/FreeRTOS-Kernel/tasks.c",
        "/src/utils/unity.c",
    ):
        require(
            any(source.endswith(suffix) for source in compiled_sources),
            f"project-header feature did not select STM32 source: {suffix}",
        )

    invalid_unity_configure = subprocess.run(
        [
            cmake,
            "-S",
            str(ROOT / "stm32_lib"),
            "-B",
            str(invalid_unity),
            HOST_SANITY_FLAG,
            "-DHAL_ENABLE_UNITY=ON",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        invalid_unity_configure.returncode != 0
        and "[JH-CFG-VALUE]" in invalid_unity_configure.stderr,
        "STM32 static configure accepted HAL_ENABLE_UNITY=ON:\n"
        f"{invalid_unity_configure.stdout}\n"
        f"{invalid_unity_configure.stderr}",
    )

    invalid_unity_list_configure = subprocess.run(
        [
            cmake,
            "-S",
            str(ROOT / "stm32_lib"),
            "-B",
            str(invalid_unity_list),
            HOST_SANITY_FLAG,
            "-DHAL_ENABLE_UNITY=1;APP_INJECTED=1",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        invalid_unity_list_configure.returncode != 0
        and "[JH-CFG-VALUE]" in invalid_unity_list_configure.stderr,
        "STM32 static configure accepted a list-valued direct feature:\n"
        f"{invalid_unity_list_configure.stdout}\n"
        f"{invalid_unity_list_configure.stderr}",
    )

    direct_unity_configure = subprocess.run(
        [
            cmake,
            "-S",
            str(ROOT / "stm32_lib"),
            "-B",
            str(direct_unity),
            HOST_SANITY_FLAG,
            "-DHAL_ENABLE_UNITY=1",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        direct_unity_configure.returncode == 0,
        "direct Unity STM32 configure failed:\n"
        f"{direct_unity_configure.stdout}\n{direct_unity_configure.stderr}",
    )
    direct_commands = json.loads(
        (direct_unity / "compile_commands.json").read_text(encoding="utf-8")
    )
    unity_commands = [
        str(entry.get("command") or " ".join(entry.get("arguments", [])))
        for entry in direct_commands
        if str(entry["file"]).replace("\\", "/").endswith("/src/utils/unity.c")
    ]
    require(
        unity_commands and "HAL_ENABLE_UNITY=1" in unity_commands[0],
        "direct CMake Unity feature did not reach the compile definition",
    )
    direct_resolved = json.loads(
        (
            direct_unity
            / "generated/boards/stm32g474/nucleo-g474re/jh_board_resolved.json"
        ).read_text(encoding="utf-8")
    )
    require(
        "HAL_ENABLE_UNITY" in direct_resolved["resolvedFeatures"],
        "direct CMake Unity feature did not reach the board feature hash input",
    )

    arm_cc = shutil.which("arm-none-eabi-gcc")
    require(
        arm_cc is not None,
        "arm-none-eabi-gcc is required for the installed radio-package probe",
    )
    radio_package_configure = subprocess.run(
        [
            cmake,
            "-S",
            str(ROOT / "stm32_lib"),
            "-B",
            str(radio_package),
            f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'stm32_lib/toolchain_stm32g474.cmake'}",
            "-DJH_BOARD=nucleo-g474re-pim730",
            "-DEXTRA_HAL_DEFINES=HAL_ENABLE_WIFI",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        radio_package_configure.returncode == 0,
        "STM32 radio package configure failed:\n"
        f"{radio_package_configure.stdout}\n{radio_package_configure.stderr}",
    )
    package_build = subprocess.run(
        [cmake, "--build", str(radio_package), "--parallel", "4"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        package_build.returncode == 0,
        "STM32 package build failed:\n"
        f"{package_build.stdout}\n{package_build.stderr}",
    )
    stale_installed_registry = (
        install_prefix / "include/generated/jh_board_registry.h"
    )
    stale_installed_registry.parent.mkdir(parents=True)
    stale_installed_registry.write_text(
        "stale installed registry\n", encoding="utf-8"
    )
    package_install = subprocess.run(
        [cmake, "--install", str(radio_package), "--prefix", str(install_prefix)],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        package_install.returncode == 0,
        "STM32 package install failed:\n"
        f"{package_install.stdout}\n{package_install.stderr}",
    )
    installed_paths = (
        install_prefix / "include/hal/generated/jh_hal_features.h",
        install_prefix / "include/hal/generated/jh_board_registry.h",
        install_prefix / "include/hal/generated/jh_board_fallback_config.h",
        install_prefix / "include/generated/jh_board_config.h",
        install_prefix / "include/generated/jh_link_contract.h",
        install_prefix
        / "share/JaszczurHAL/generated/jh_link_contract_reference.c",
        install_prefix / "share/JaszczurHAL/generated/jh_board_resolved.json",
        install_prefix / "lib/libJaszczurHAL.a",
    )
    for installed_path in installed_paths:
        require(
            installed_path.is_file(),
            f"STM32 package is missing {installed_path.relative_to(install_prefix)}",
        )
    require(
        not stale_installed_registry.exists(),
        "STM32 package installed a second board registry",
    )

    installed_resolution = json.loads(installed_paths[-2].read_text(encoding="utf-8"))
    require(
        "HAL_ENABLE_DS18B20" in installed_resolution["requestedFeatures"]
        and {"HAL_ENABLE_ONEWIRE", "HAL_ENABLE_CRC"}
        <= set(installed_resolution["resolvedFeatures"]),
        "installed feature resolution lacks the DS18B20 closure",
    )
    require(
        installed_resolution["board"] == "nucleo-g474re-pim730"
        and "HAL_ENABLE_WIFI" in installed_resolution["requestedFeatures"],
        "installed package is not the radio-board WiFi probe",
    )
    require(
        {
            "HAL_NETWORK_BACKEND_CYW43",
            "HAL_CYW43_BUS_STM32_GSPI",
            "HAL_CYW43_STACK_LWIP",
            "HAL_CYW43_PIN_WL_ON=30u",
            "HAL_CYW43_PIN_CHIP_SELECT=28u",
            "HAL_CYW43_PIN_DATA=31u",
            "HAL_CYW43_PIN_CLOCK=29u",
        }
        <= set(installed_resolution["boardCompileDefinitions"]),
        "installed resolution lacks radio-board provider definitions",
    )
    direct_consumer.mkdir(parents=True)
    consumer_source = direct_consumer / "consumer.c"
    consumer_source.write_text(
        "#include <hal/hal_config.h>\n"
        "#include <hal/generated/jh_board_registry.h>\n"
        "#ifndef HAL_ENABLE_DS18B20\n"
        '#error "missing requested DS18B20"\n'
        "#endif\n"
        "#if !defined(HAL_ENABLE_ONEWIRE) || !defined(HAL_ENABLE_CRC)\n"
        '#error "missing generated DS18B20 closure"\n'
        "#endif\n"
        "#if !defined(HAL_NETWORK_BACKEND_CYW43) || "
        "!defined(HAL_CYW43_BUS_STM32_GSPI) || "
        "!defined(HAL_CYW43_STACK_LWIP)\n"
        '#error "missing generated radio-board provider selection"\n'
        "#endif\n"
        "#if HAL_CYW43_PIN_WL_ON != 30u || "
        "HAL_CYW43_PIN_CHIP_SELECT != 28u || "
        "HAL_CYW43_PIN_DATA != 31u || HAL_CYW43_PIN_CLOCK != 29u\n"
        '#error "missing generated radio-board pin contract"\n'
        "#endif\n"
        "int main(void) { return 0; }\n",
        encoding="utf-8",
    )
    direct_command = [
        arm_cc,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-mcpu=cortex-m4",
        "-mthumb",
        "-mfpu=fpv4-sp-d16",
        "-mfloat-abi=hard",
        "-ffunction-sections",
        "-fdata-sections",
        f"-I{install_prefix / 'include'}",
        f"-I{install_prefix / 'include/generated'}",
        "-DHAL_TARGET_STM32G474=1",
        *(
            f"-D{feature}=1"
            for feature in installed_resolution["requestedFeatures"]
        ),
        str(consumer_source),
        str(installed_paths[-3]),
        str(installed_paths[-1]),
        "-nostdlib",
        "-Wl,-e,main",
        "-Wl,--gc-sections",
        "-o",
        str(direct_consumer / "consumer"),
    ]
    direct_environment = os.environ.copy()
    direct_tool_path = direct_consumer / "compiler-tools"
    direct_tool_path.mkdir()
    for tool_name in ("arm-none-eabi-as", "arm-none-eabi-ld"):
        tool = shutil.which(tool_name)
        require(tool is not None, f"{tool_name} is required for the package probe")
        (direct_tool_path / tool_name).symlink_to(tool)
    direct_environment["PATH"] = str(direct_tool_path)
    direct_compile = subprocess.run(
        direct_command,
        env=direct_environment,
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        direct_compile.returncode == 0,
        "installed package did not compile/link with cc and no Python in PATH:\n"
        f"{direct_compile.stdout}\n{direct_compile.stderr}",
    )

    incremental_source.mkdir(parents=True)
    incremental_config.mkdir(parents=True)
    incremental_header = incremental_config / "hal_project_config.h"
    incremental_header.write_text(
        "#define HAL_ENABLE_WIFI 1\n", encoding="utf-8"
    )
    incremental_cmake = """\
cmake_minimum_required(VERSION 3.20)
project(jh_project_config_dependency NONE)
include("@HELPER@")
jh_collect_project_feature_defines(_features "@CONFIG@")
file(WRITE "${CMAKE_BINARY_DIR}/features.txt" "${_features}\\n")
add_custom_target(config_dependency_probe ALL
    COMMAND "${CMAKE_COMMAND}" -E true)
"""
    incremental_cmake = incremental_cmake.replace(
        "@HELPER@", (ROOT / "cmake/jh_project_features.cmake").as_posix()
    ).replace("@CONFIG@", incremental_config.as_posix())
    (incremental_source / "CMakeLists.txt").write_text(
        incremental_cmake, encoding="utf-8"
    )
    incremental_configure = subprocess.run(
        [cmake, "-S", str(incremental_source), "-B", str(incremental_build)],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        incremental_configure.returncode == 0,
        "incremental project-config fixture failed to configure:\n"
        f"{incremental_configure.stdout}\n{incremental_configure.stderr}",
    )
    require(
        (incremental_build / "features.txt").read_text(encoding="utf-8").strip()
        == "HAL_ENABLE_WIFI",
        "incremental fixture did not collect its initial feature",
    )
    incremental_header.write_text(
        "#define HAL_ENABLE_TLS 1\n", encoding="utf-8"
    )
    # CMAKE_CONFIGURE_DEPENDS re-checks by timestamp. The edit lands in the same
    # second as the generated build system, so push the header clearly ahead of
    # it instead of relying on filesystem timestamp resolution.
    build_system_mtime = max(
        (
            entry.stat().st_mtime
            for entry in incremental_build.rglob("*")
            if entry.is_file()
        ),
        default=incremental_header.stat().st_mtime,
    )
    forced_mtime = max(build_system_mtime, incremental_header.stat().st_mtime) + 2.0
    os.utime(incremental_header, (forced_mtime, forced_mtime))
    incremental_rebuild = subprocess.run(
        [cmake, "--build", str(incremental_build)],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        incremental_rebuild.returncode == 0,
        "project-header change did not trigger a clean incremental build:\n"
        f"{incremental_rebuild.stdout}\n{incremental_rebuild.stderr}",
    )
    require(
        (incremental_build / "features.txt").read_text(encoding="utf-8").strip()
        == "HAL_ENABLE_TLS",
        "project-header change did not trigger CMake reconfigure",
    )

    without_flag = subprocess.run(
        [cmake, "-S", str(ROOT / "stm32_lib"), "-B", str(rejected)],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        without_flag.returncode != 0,
        "stm32_lib accepted a firmware configure without a cross toolchain",
    )
    require(
        "CMAKE_TOOLCHAIN_FILE is required for stm32_lib" in without_flag.stderr,
        "missing-toolchain diagnostic changed",
    )
finally:
    shutil.rmtree(scratch, ignore_errors=True)

print("STM32 host-compiler sanity configuration verified")
