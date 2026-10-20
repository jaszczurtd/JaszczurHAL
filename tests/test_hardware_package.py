#!/usr/bin/env python3
"""A static library built from a device tree without application sources,
installed as a package and linked by separately built applications. The
application computes the configuration it expects from its own inputs: a
matching package links, any other one is refused by the CMake consumer
before linking and by the linker in a direct compiler build."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
WORK = ROOT / ".build" / "tests" / "hardware package"
FIXTURE = ROOT / "tests" / "fixtures" / "device_tree" / "hal" / "nucleo-canhat"
GENERATOR = ROOT / "scripts" / "generate_hardware_config.py"
TOOLCHAIN = ROOT / "link_libraries" / "stm32_lib" / "toolchain_stm32g474.cmake"
ARCH = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]
# The compiler and flags the application compiles with; its ABI joins the
# configuration it expects.
COMPILER = ["--compiler", "arm-none-eabi-gcc", *(f"--compiler-flag={flag}" for flag in ARCH)]
APP = """#include <hal/core/hal_config.h>
#include <hal/can/hal_can.h>

static const hal_can_config_t can1 = JH_HW_NODE_HAT_CAN1_CONFIG_INIT;

int main(void) { return (int)sizeof(can1) - (int)sizeof(hal_can_config_t); }
"""


def run(*command: str | Path, **options) -> subprocess.CompletedProcess:
    return subprocess.run([str(item) for item in command], capture_output=True, text=True, **options)


def config(name: str, relay_pin: str = "PC3", extra: str = "") -> Path:
    """A configuration directory: the CAN-HAT assembly with one relay pin and
    FDCAN requested by the program."""
    directory = WORK / name
    directory.mkdir(parents=True)
    data = json.loads((FIXTURE / "device_tree.json").read_text(encoding="utf-8"))
    data["assemblies"]["canhat"]["nodes"]["hat.relay1"]["pins"]["out"] = relay_pin
    (directory / "device_tree.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
    (directory / "hal_project_config.h").write_text(
        f"#pragma once\n#define HAL_ENABLE_STM32G474_FDCAN\n{extra}", encoding="utf-8")
    return directory


def identity(config_dir: Path) -> dict:
    result = run(sys.executable, GENERATOR, "identity", "--config-dir", config_dir, "--assembly", "canhat",
                 *COMPILER)
    assert result.returncode == 0, result.stderr
    return json.loads(result.stdout)


def record(path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8")
    return {match.group(1): match.group(2) if match.group(2) is not None else match.group(3)
            for match in re.finditer(r'set\((\w+) (?:"([^"]*)"|\[=*\[(.*?)\]=*\])\)', text)}


@unittest.skipUnless(shutil.which("arm-none-eabi-gcc") and shutil.which("cmake"),
                     "the package checks need arm-none-eabi-gcc and cmake")
class Package(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if WORK.exists():
            shutil.rmtree(WORK)
        cls.config = config("config a")
        cls.prefix = WORK / "package prefix"
        build = WORK / "library"
        result = run(ROOT / "scripts" / "build_link_library.sh", "--target", "stm32g474",
                     "--assembly", "canhat", "-p", cls.config, "--library-only", "--clean",
                     "-o", build, "-j", str(os.cpu_count() or 2))
        assert result.returncode == 0, result.stdout + result.stderr
        cls.build = build
        result = run("cmake", "--install", build, "--prefix", cls.prefix)
        assert result.returncode == 0, result.stdout + result.stderr
        cls.record = record(cls.prefix / "share" / "JaszczurHAL" / "generated" / "jh_hardware_config.cmake")
        # The same configuration built soft-float: another ABI, another package.
        soft = WORK / "soft library"
        result = run("cmake", "-S", ROOT / "link_libraries" / "stm32_lib", "-B", soft,
                     f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN}", f"-DHAL_PROJECT_CONFIG_DIR={cls.config}",
                     "-DJH_ASSEMBLY=canhat", "-DSTM32_FLOAT_ABI=soft")
        assert result.returncode == 0, result.stdout + result.stderr
        result = run("cmake", "--build", soft, "--parallel", str(os.cpu_count() or 2))
        assert result.returncode == 0, result.stdout + result.stderr
        cls.soft_prefix = WORK / "soft prefix"
        result = run("cmake", "--install", soft, "--prefix", cls.soft_prefix)
        assert result.returncode == 0, result.stdout + result.stderr

    def test_the_package_holds_one_configuration(self) -> None:
        for relative in ("lib/libJaszczurHAL.a", "include/generated/jh_hardware.h",
                         "include/generated/jh_board_config.h", "include/generated/jh_link_contract.h",
                         "share/JaszczurHAL/generated/jh_hardware_resolved.json",
                         "share/JaszczurHAL/generated/jh_hardware_config.json",
                         "share/JaszczurHAL/generated/jh_link_contract_reference.c"):
            with self.subTest(relative=relative):
                self.assertTrue((self.prefix / relative).is_file())
        self.assertFalse((self.prefix / "share/JaszczurHAL/generated/jh_board_resolved.json").exists())
        self.assertEqual(identity(self.config)["configSha256"], self.record["JH_CONFIG_SHA256"])
        self.assertEqual("HAL_TARGET_STM32G474", self.record["JH_TARGET_SELECTOR"])
        # The configuration directory alone selects the features: no runner preset.
        flags = (self.build / "CMakeFiles" / "JaszczurHAL.dir" / "flags.make").read_text(encoding="utf-8")
        self.assertNotIn("HAL_ENABLE_I2C_SLAVE", flags)
        self.assertNotIn("HAL_STM32G474_CLOCK_HSE_160MHZ", flags)

    def direct_link(self, name: str, app_config: Path, prefix: Path | None = None,
                    flags: tuple[str, ...] = (), source: str = APP) -> subprocess.CompletedProcess:
        """The application's own reference, compiled with the package's frozen
        headers and linked against its archive. ``flags`` reach only the
        compiler, not the finalize of the application's configuration."""
        generated = WORK / name / "generated"
        for command in (["resolve", "--config-dir", app_config, "--assembly", "canhat",
                         "--output-dir", generated],
                        ["finalize", "--hardware", generated / "jh_hardware_resolved.json",
                         "--hardware-header", generated / "jh_hardware.h",
                         "--config-dir", app_config, "--output-dir", generated, *COMPILER]):
            result = run(sys.executable, GENERATOR, *command)
            self.assertEqual(0, result.returncode, result.stderr)
        app = WORK / name / "app.c"
        app.write_text(source, encoding="utf-8")
        prefix = prefix or self.prefix
        header = prefix / "include" / "generated" / "jh_hardware.h"
        definitions = [f"-D{item}" for item in self.record["JH_BOARD_COMPILE_DEFINITIONS"].split(";") if item]
        return run("arm-none-eabi-gcc", *ARCH, *flags, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-ffunction-sections", "-fdata-sections",
                   f"-I{prefix / 'include'}", f"-I{prefix / 'include' / 'generated'}",
                   f"-I{app_config}", "-DHAL_TARGET_STM32G474=1",
                   f'-DJH_HARDWARE_HEADER="{header.as_posix()}"', *definitions,
                   app, generated / "jh_link_contract_reference.c", prefix / "lib" / "libJaszczurHAL.a",
                   "-nostdlib", "-Wl,-e,main", "-Wl,--gc-sections", "-o", WORK / name / "app.elf")

    def test_a_matching_application_links_directly(self) -> None:
        result = self.direct_link("direct match", config("app a"))
        self.assertEqual(0, result.returncode, result.stderr)

    def test_the_linker_refuses_another_package(self) -> None:
        other = config("app b", relay_pin="PC4")
        expected = identity(other)["configSha256"][:12]
        result = self.direct_link("direct mismatch", other)
        self.assertNotEqual(0, result.returncode)
        self.assertRegex(result.stderr, rf"undefined reference to `jh_board_contract_\w+_{expected}'")

    def consumer(self, name: str, app_config: Path, prefix: Path | None = None,
                 options: str = " ".join(ARCH), *cache: str) -> subprocess.CompletedProcess:
        project = WORK / name / "project"
        project.mkdir(parents=True)
        (project / "app.c").write_text(APP, encoding="utf-8")
        flags = " ".join(ARCH)
        # ``options`` are the target's compile options; they follow
        # CMAKE_C_FLAGS and CMAKE_C_FLAGS_<CONFIG> on the command line.
        (project / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\n"
            "project(package_consumer C)\n"
            f'include("{(ROOT / "cmake" / "jh_hal_package.cmake").as_posix()}")\n'
            "add_executable(app app.c)\n"
            f"target_compile_options(app PRIVATE {options} -Wall -Wextra -Werror)\n"
            f"target_link_options(app PRIVATE {flags} -nostdlib -Wl,-e,main -Wl,--gc-sections)\n"
            f'jh_link_hal_package(app PACKAGE "{(prefix or self.prefix).as_posix()}" '
            f'CONFIG_DIR "{app_config.as_posix()}" ASSEMBLY canhat)\n', encoding="utf-8")
        build = WORK / name / "build"
        configured = run("cmake", "-S", project, "-B", build, f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN}", *cache)
        if configured.returncode:
            return configured
        return run("cmake", "--build", build)

    def test_the_cmake_consumer_links_a_matching_package(self) -> None:
        result = self.consumer("consumer match", config("consumer a"))
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_the_cmake_consumer_refuses_other_hardware_before_linking(self) -> None:
        result = self.consumer("consumer pin", config("consumer b", relay_pin="PC4"))
        self.assertNotEqual(0, result.returncode)
        self.assertIn("[JH-HW-PACKAGE]", result.stderr)
        self.assertIn("another hardware", result.stderr)

    def test_another_abi_is_refused(self) -> None:
        soft = record(self.soft_prefix / "share" / "JaszczurHAL" / "generated" / "jh_hardware_config.cmake")
        self.assertNotEqual(self.record["JH_CONFIG_SHA256"], soft["JH_CONFIG_SHA256"])
        self.assertIn("__SOFTFP__=1", soft["JH_HARDWARE_ABI"])
        self.assertIn("__ARM_PCS_VFP=1", self.record["JH_HARDWARE_ABI"])
        result = self.consumer("consumer abi", config("consumer abi config"), self.soft_prefix)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("[JH-HW-PACKAGE]", result.stderr)
        self.assertIn("compiler ABI", result.stderr)
        result = self.direct_link("direct abi", config("direct abi config"), self.soft_prefix)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("undefined reference to `jh_board_contract_", result.stderr)

    def test_configuration_flags_join_the_abi(self) -> None:
        # The target leaves the float ABI to the configuration flags.
        result = self.consumer("consumer release", config("consumer release config"), None,
                               "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16",
                               "-DCMAKE_BUILD_TYPE=Release",
                               "-DCMAKE_C_FLAGS_RELEASE=-O2 -mfloat-abi=soft")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("[JH-HW-PACKAGE]", result.stderr)
        self.assertIn("compiler ABI", result.stderr)

    def test_flags_configure_cannot_see_stop_the_build(self) -> None:
        # A generator expression is evaluated only when the unit compiles; the
        # link-signature reference then checks the ABI it really gets.
        result = self.consumer("consumer genex", config("consumer genex config"), None,
                               " ".join(ARCH) + " $<$<COMPILE_LANGUAGE:C>:-mfloat-abi=soft>")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("compiled with another ABI than the configuration", result.stdout + result.stderr)
        self.assertFalse((WORK / "consumer genex" / "build" / "app").exists())

    def test_another_struct_layout_is_refused(self) -> None:
        # -fpack-struct changes no predefined macro, only the layout of HAL types.
        result = self.consumer("consumer packed", config("consumer packed config"), None,
                               " ".join(ARCH) + " -fpack-struct=1")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("[JH-HW-PACKAGE]", result.stderr)
        self.assertIn("compiler ABI", result.stderr)
        result = self.consumer("consumer packed genex", config("consumer packed genex config"), None,
                               " ".join(ARCH) + " $<$<COMPILE_LANGUAGE:C>:-fpack-struct=1>")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("another struct layout than the configuration", result.stdout + result.stderr)
        self.assertFalse((WORK / "consumer packed genex" / "build" / "app").exists())
        # The package's own board header refuses the unit, whatever set the layout.
        packed_pragma = "#pragma pack(push, 1)\n" + APP
        for name, flags, source in (("direct packed", ("-fpack-struct=1",), APP),
                                    ("direct short enums", ("-fno-short-enums",), APP),
                                    ("direct pragma", (), packed_pragma)):
            with self.subTest(case=name):
                result = self.direct_link(name, config(f"{name} config"), flags=flags, source=source)
                self.assertNotEqual(0, result.returncode)
                self.assertIn("another struct layout than the configuration", result.stderr)

    def test_a_rebuild_keeps_the_exported_headers(self) -> None:
        # Nothing changed, so the archive is not rebuilt; the headers next to
        # it must survive the configure all the same.
        result = run(ROOT / "scripts" / "build_link_library.sh", "--target", "stm32g474",
                     "--assembly", "canhat", "-p", self.config, "--library-only",
                     "-o", self.build, "-j", str(os.cpu_count() or 2))
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertTrue((self.build / "include" / "generated" / "jh_hardware.h").is_file())

    def test_the_cmake_consumer_refuses_other_flags(self) -> None:
        result = self.consumer("consumer flags", config("consumer c", extra="#define HAL_ENABLE_UART\n"))
        self.assertNotEqual(0, result.returncode)
        self.assertIn("[JH-HW-PACKAGE]", result.stderr)
        self.assertIn("program configuration", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
