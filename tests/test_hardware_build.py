#!/usr/bin/env python3
"""A firmware project with device_tree.json through cmake/jh_firmware_project
on STM32G474: the first generation, an incremental build, an edited and a
broken description, and the switch to the board path and back when the file
disappears and returns. Every step checks what the compiler really used."""

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
WORK = ROOT / ".build" / "tests" / "hardware build"
FIXTURE = ROOT / "tests" / "fixtures" / "device_tree" / "hal" / "nucleo-canhat"
GENERATOR = ROOT / "scripts" / "generate_hardware_config.py"
TOOLCHAIN = ROOT / "link_libraries" / "stm32_lib" / "toolchain_stm32g474.cmake"
# The application names its build mode in a symbol of its own object file.
APP = """#include <JaszczurHAL.h>

#ifdef JH_HW_SCHEMA_VERSION
static const hal_can_config_t can1 = JH_HW_NODE_HAT_CAN1_CONFIG_INIT;
int jh_probe_device_tree(void) { return (int)sizeof(can1); }
#else
int jh_probe_board(void) { return 1; }
#endif

void app_start(void) {}
void app_task0(void) {}
"""


def run(*command: str | Path) -> subprocess.CompletedProcess:
    return subprocess.run([str(item) for item in command], capture_output=True, text=True)


@unittest.skipUnless(shutil.which("arm-none-eabi-gcc") and shutil.which("cmake"),
                     "the firmware build needs arm-none-eabi-gcc and cmake")
class FirmwareBuild(unittest.TestCase):
    """One build directory carried through the steps in order."""

    @classmethod
    def setUpClass(cls) -> None:
        if WORK.exists():
            shutil.rmtree(WORK)
        cls.project = WORK / "project"
        cls.project.mkdir(parents=True)
        shutil.copy2(FIXTURE / "device_tree.json", cls.project / "device_tree.json")
        (cls.project / "hal_project_config.h").write_text(
            "#pragma once\n#define HAL_ENABLE_STM32G474_FDCAN\n", encoding="utf-8")
        (cls.project / "app.c").write_text(APP, encoding="utf-8")
        cls.build = WORK / "build"
        cls.generated = cls.build / "generated" / "hardware"

    def configure(self, *arguments: str) -> subprocess.CompletedProcess:
        return run("cmake", "-S", ROOT / "cmake" / "jh_firmware_project", "-B", self.build,
                   f"-DJH_PROJECT_DIR={self.project}", f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN}",
                   *arguments)

    def compile(self) -> subprocess.CompletedProcess:
        return run("cmake", "--build", self.build, "--parallel", str(os.cpu_count() or 2))

    def app_symbols(self) -> str:
        objects = list(self.build.rglob("app.c.obj"))
        self.assertEqual(1, len(objects))
        return run("arm-none-eabi-nm", objects[0]).stdout

    def contract(self) -> str:
        symbols = run("arm-none-eabi-nm", self.build / "firmware.elf").stdout
        return re.search(r"jh_board_contract_\w+", symbols).group(0)

    def flags(self) -> str:
        return "".join(path.read_text(encoding="utf-8")
                       for path in (self.build / "CMakeFiles" / "firmware.dir").glob("flags.make"))

    def edit_relay(self, pin: str) -> None:
        path = self.project / "device_tree.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data["assemblies"]["canhat"]["nodes"]["hat.relay1"]["pins"]["out"] = pin
        path.write_text(json.dumps(data, indent=2), encoding="utf-8")

    def identity(self) -> str:
        # The firmware signs with the ABI of its compile flags.
        flags = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]
        result = run(sys.executable, GENERATOR, "identity", "--config-dir", self.project, "--assembly", "canhat",
                     "--compiler", "arm-none-eabi-gcc", *(f"--compiler-flag={flag}" for flag in flags))
        self.assertEqual(0, result.returncode, result.stderr)
        return json.loads(result.stdout)["configSha256"]

    def test_device_tree_build_lifecycle(self) -> None:
        with self.subTest(step="first generation"):
            result = self.configure("-DJH_TARGET=stm32g474", "-DJH_ASSEMBLY=canhat")
            self.assertEqual(0, result.returncode, result.stderr)
            result = self.compile()
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertNotIn("warning:", result.stdout + result.stderr)
            self.assertIn("jh_probe_device_tree", self.app_symbols())
            self.assertTrue(self.contract().endswith(self.identity()[:12]))
            flags = self.flags()
            self.assertIn("JH_HARDWARE_HEADER=", flags)
            # The header owns the clock tree; the compiler gets no second copy.
            self.assertNotIn("HAL_STM32G474_CLOCK_HSE_160MHZ", flags)
            self.assertFalse((self.build / "generated" / "boards").exists())

        with self.subTest(step="incremental build without changes"):
            stamps = {path.name: path.stat().st_mtime_ns for path in self.generated.iterdir() if path.is_file()}
            result = self.compile()
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertNotRegex(result.stdout, r"Building|Linking|Configuring done")
            self.assertEqual(stamps, {path.name: path.stat().st_mtime_ns
                                      for path in self.generated.iterdir() if path.is_file()})

        with self.subTest(step="configure again without changes"):
            # Regenerating identical files keeps their times: nothing rebuilds.
            self.assertEqual(0, self.configure().returncode)
            result = self.compile()
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertNotRegex(result.stdout, r"Building|Linking")

        with self.subTest(step="edited description"):
            self.edit_relay("PC4")
            result = self.compile()
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("Configuring done", result.stdout)
            self.assertTrue(self.contract().endswith(self.identity()[:12]))
            self.assertIn("#define JH_HW_NODE_HAT_RELAY1_PIN_OUT 36U",
                          (self.generated / "jh_hardware.h").read_text(encoding="utf-8"))

        with self.subTest(step="broken description"):
            self.edit_relay("PC2")
            result = self.compile()
            self.assertNotEqual(0, result.returncode)
            self.assertIn("[JH-HW-PIN-CONFLICT]", result.stdout + result.stderr)
            # Nothing of the earlier model is left for a later build to take.
            self.assertEqual([], sorted(path.name for path in self.generated.iterdir() if path.is_file()))
            self.edit_relay("PC4")

        with self.subTest(step="device tree removed"):
            (self.project / "device_tree.json").rename(WORK / "device_tree.json")
            result = self.compile()
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("jh_probe_board", self.app_symbols())
            self.assertFalse(self.generated.exists())
            self.assertNotIn("JH_HARDWARE_HEADER=", self.flags())
            # The assembly the device tree build used is not a board-path input.
            self.assertNotRegex((self.build / "CMakeCache.txt").read_text(encoding="utf-8"),
                                r"JH_ASSEMBLY:STRING=\S")

        with self.subTest(step="device tree back"):
            (WORK / "device_tree.json").rename(self.project / "device_tree.json")
            result = self.compile()
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("jh_probe_device_tree", self.app_symbols())
            self.assertFalse((self.build / "generated" / "boards").exists())
            # The board the board path resolved is not a device tree input.
            self.assertNotRegex((self.build / "CMakeCache.txt").read_text(encoding="utf-8"),
                                r"JH_BOARD:STRING=\S")

        with self.subTest(step="board chosen for a device tree"):
            result = self.configure("-DJH_BOARD=nucleo-g474re")
            self.assertNotEqual(0, result.returncode)
            self.assertIn("[JH-HW-BOARD]", result.stderr)

    def test_an_assembly_needs_a_device_tree(self) -> None:
        project = WORK / "board project"
        project.mkdir(parents=True, exist_ok=True)
        (project / "app.c").write_text(APP, encoding="utf-8")
        result = run("cmake", "-S", ROOT / "cmake" / "jh_firmware_project", "-B", WORK / "board build",
                     f"-DJH_PROJECT_DIR={project}", f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN}",
                     "-DJH_TARGET=stm32g474", "-DJH_ASSEMBLY=canhat")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("[JH-HW-ASSEMBLY]", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
