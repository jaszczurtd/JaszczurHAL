#!/usr/bin/env python3
"""The full assembly x variant matrix of a device tree project built through
the examples dispatcher and jh-vscode on STM32G474, from an empty state: two
assemblies of one target and board keep separate trees and link signatures,
the variant has its own tree, and an unchanged build compiles nothing."""

from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest
from unittest import mock

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

import examples_dispatcher  # noqa: E402

WORK = ROOT / ".build" / "tests" / "device tree matrix"
EXAMPLES = WORK / "examples"
PROJECT = EXAMPLES / "70_device tree"
ENTRY = ROOT / "vscode" / "entry" / ("jh-vscode.cmd" if os.name == "nt" else "jh-vscode")
CANHAT = ROOT / "tests" / "fixtures" / "device_tree" / "hal" / "nucleo-canhat" / "device_tree.json"
HEADER = """#pragma once
#define HAL_ENABLE_STM32G474_FDCAN
#define JH_PROJECT_VARIANTS(X) X(SERIAL, "Adds the UART", HAL_ENABLE_UART)
#if defined(HAL_ENABLE_UART) && JH_HW_NODE_HAT_RELAY1_PIN_OUT == 36U
#error "the UART collides with the relay on PC4"
#endif
"""
APP = """#include <JaszczurHAL.h>

static const hal_can_config_t can1 = JH_HW_NODE_HAT_CAN1_CONFIG_INIT;
int jh_probe_relay(void) { return (int)JH_HW_NODE_HAT_RELAY1_PIN_OUT + (int)sizeof(can1); }

void app_start(void) {}
void app_task0(void) {}
"""


def contract(elf: Path) -> str:
    symbols = subprocess.run(["arm-none-eabi-nm", str(elf)], capture_output=True, text=True).stdout
    return re.search(r"jh_board_contract_\w+", symbols).group(0)


@unittest.skipUnless(shutil.which("arm-none-eabi-gcc") and shutil.which("cmake"),
                     "the matrix build needs arm-none-eabi-gcc and cmake")
class MatrixBuild(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if WORK.exists():
            shutil.rmtree(WORK)
        (PROJECT / ".vscode").mkdir(parents=True)
        data = json.loads(CANHAT.read_text(encoding="utf-8"))
        second = json.loads(json.dumps(data["assemblies"]["canhat"]))
        second["nodes"]["hat.relay1"]["pins"]["out"] = "PC4"
        data["assemblies"]["canhat-b"] = second
        (PROJECT / "device_tree.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
        (PROJECT / "hal_project_config.h").write_text(HEADER, encoding="utf-8")
        (PROJECT / "app.c").write_text(APP, encoding="utf-8")
        (PROJECT / ".vscode" / "jaszczurhal.project.json").write_text(json.dumps({
            "project": "matrix", "module": "matrix", "toolchain": "cmake",
            "cmakeBuildDir": "${buildDir}/cmake",
            "cmake": {"sourceDir": "${jhRoot}/cmake/jh_firmware_project",
                      "cache": {"JH_PROJECT_DIR": "${project}", "JH_MODULE_NAME": "matrix"}}},
            indent=4), encoding="utf-8")
        # A local choice must not narrow the matrix.
        (PROJECT / ".vscode" / "jaszczurhal.local.json").write_text(
            json.dumps({"assembly": "canhat-b"}), encoding="utf-8")
        arguments = mock.Mock(example=None, target="stm32g474", jobs=os.cpu_count() or 2, verbose=False)
        output = io.StringIO()
        with mock.patch.object(examples_dispatcher, "EXAMPLES_DIR", EXAMPLES), redirect_stdout(output):
            cls.status = examples_dispatcher.build(arguments)
        cls.output = output.getvalue()
        cls.trees = {name: PROJECT / ".build" / relative for name, relative in (
            ("canhat", "stm32g474/canhat/cmake"),
            ("canhat-b", "stm32g474/canhat-b/cmake"),
            ("canhat SERIAL", "stm32g474/canhat/cmake/variants/SERIAL"))}
        # The final artifacts each configuration publishes.
        cls.published = {name: PROJECT / ".build" / relative for name, relative in (
            ("canhat", "stm32g474/canhat"),
            ("canhat-b", "stm32g474/canhat-b"),
            ("canhat SERIAL", "stm32g474/canhat/variants/SERIAL"))}

    def test_the_whole_matrix_builds(self) -> None:
        self.assertEqual(0, self.status, self.output)
        self.assertIn("built 3 configuration(s) from 1 example project(s)", self.output)
        for name, tree in self.trees.items():
            with self.subTest(configuration=name):
                self.assertTrue((tree / "firmware.elf").is_file())

    def test_assemblies_of_one_board_keep_their_results(self) -> None:
        signatures = {name: contract(tree / "firmware.elf") for name, tree in self.trees.items()}
        self.assertEqual(3, len(set(signatures.values())), signatures)
        # The published copies keep the same signatures; nothing is shared.
        self.assertEqual(signatures, {name: contract(directory / "firmware.elf")
                                      for name, directory in self.published.items()})
        self.assertFalse((PROJECT / ".build" / "firmware.elf").exists())
        relay = {name: (tree / "generated" / "hardware" / "jh_hardware.h").read_text(encoding="utf-8")
                 for name, tree in self.trees.items() if "SERIAL" not in name}
        self.assertIn("#define JH_HW_NODE_HAT_RELAY1_PIN_OUT 35U", relay["canhat"])
        self.assertIn("#define JH_HW_NODE_HAT_RELAY1_PIN_OUT 36U", relay["canhat-b"])

    def test_cmake_tools_builds_keep_their_results(self) -> None:
        # CMake Tools passes the generated cmake.configureSettings as -D.
        import vscode_task_config

        registry = examples_dispatcher.target_registry()
        manifest = examples_dispatcher.read_manifest(PROJECT)
        elves = {}
        for assembly in ("canhat", "canhat-b"):
            settings = vscode_task_config.cmake_tools_configure_settings(
                registry, target="stm32g474", board="nucleo-g474re", module="matrix",
                jh_root_ref=str(ROOT), project_ref=str(PROJECT), build_ref=str(PROJECT / ".build"),
                project_cache=dict(manifest["cmake"]["cache"]), assembly=assembly)
            tree = WORK / "cmake tools" / assembly
            configured = subprocess.run(
                ["cmake", "-S", str(ROOT / "cmake" / "jh_firmware_project"), "-B", str(tree),
                 *(f"-D{key}={value}" for key, value in settings.items())],
                capture_output=True, text=True)
            self.assertEqual(0, configured.returncode, configured.stdout + configured.stderr)
            built = subprocess.run(["cmake", "--build", str(tree), "--parallel", str(os.cpu_count() or 2)],
                                   capture_output=True, text=True)
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            # Where CMake really published, from its own cache; empty is its
            # default, <project>/.build.
            cache = (tree / "CMakeCache.txt").read_text(encoding="utf-8")
            published = re.search(r"^JH_ARTIFACT_DIR:PATH=(.*)$", cache, re.M).group(1)
            elves[assembly] = Path(published or PROJECT / ".build") / "firmware.elf"
        self.assertNotEqual(*elves.values())
        self.assertNotEqual(*(contract(elf) for elf in elves.values()))
        self.assertFalse((PROJECT / ".build" / "firmware.elf").exists())

    def test_an_unchanged_build_compiles_nothing(self) -> None:
        elf = self.published["canhat"] / "firmware.elf"
        other = self.published["canhat-b"] / "firmware.elf"
        stamps = (elf.stat().st_mtime_ns, other.stat().st_mtime_ns)
        result = subprocess.run([str(ENTRY), "build", "--project", str(PROJECT), "--assembly", "canhat"],
                                capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertNotRegex(result.stdout, r"Building C")
        self.assertEqual(stamps, (elf.stat().st_mtime_ns, other.stat().st_mtime_ns))

    def test_a_variant_the_assembly_rejects_is_not_in_the_matrix(self) -> None:
        self.assertFalse((PROJECT / ".build" / "stm32g474" / "canhat-b" / "variants").exists())
        result = subprocess.run([str(ENTRY), "build", "--project", str(PROJECT), "--assembly", "canhat-b",
                                 "--variant", "SERIAL"], capture_output=True, text=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("collides with the relay", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
