#!/usr/bin/env python3
"""The closed clock-tree registry against boards, backends and the Pico SDK."""

from __future__ import annotations

import copy
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

import clock_registry  # noqa: E402

TARGETS = {path.stem: json.loads(path.read_text(encoding="utf-8"))
           for path in (ROOT / "boards" / "targets").glob("*.json")}


def boards() -> dict[str, dict]:
    return {path.stem: json.loads(path.read_text(encoding="utf-8"))
            for path in (ROOT / "boards" / "profiles").glob("*.json")}


class RegistryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.registry = clock_registry.load_registry()

    def test_every_target_has_its_closed_list(self) -> None:
        clock_registry.check_registry(self.registry, TARGETS)
        self.assertEqual({"default", "hse-160mhz"}, set(self.registry["stm32g474"]["trees"]))

    def test_every_board_runs_the_default_tree_of_its_targets(self) -> None:
        clock_registry.check_boards(self.registry, boards(), TARGETS)

    def test_the_hse_tree_needs_the_nucleo_crystal(self) -> None:
        tree = self.registry["stm32g474"]["trees"]["hse-160mhz"]
        inputs = TARGETS["stm32g474"]["clockInputs"]
        nucleo = boards()["nucleo-g474re"]
        self.assertEqual([], clock_registry.board_sources_satisfy(nucleo, tree, inputs))
        without = copy.deepcopy(nucleo)
        del without["clockSources"]["hse"]
        self.assertEqual(["no hse source"], clock_registry.board_sources_satisfy(without, tree, inputs))
        wrong = copy.deepcopy(nucleo)
        wrong["clockSources"]["hse"]["frequencyHz"] = 25000000
        self.assertEqual(1, len(clock_registry.board_sources_satisfy(wrong, tree, inputs)))
        # An empty or partial list would hide the crystal from the reservations.
        for pins in ([], ["PF0"], ["PF0", "PF1", "PA0"]):
            with self.subTest(pins=pins):
                partial = copy.deepcopy(nucleo)
                partial["clockSources"]["hse"]["pins"] = [{"domain": "soc-gpio", "id": pin} for pin in pins]
                self.assertEqual(1, len(clock_registry.board_sources_satisfy(partial, tree, inputs)))

    def test_stm32_oscillator_pins_follow_rm0440(self) -> None:
        # RM0440 7.2.1/7.2.4: a crystal takes OSC_IN and OSC_OUT, bypass only OSC_IN.
        self.assertEqual({"hse": {"crystal": ["PF0", "PF1"], "external-clock": ["PF0"]},
                          "lse": {"crystal": ["PC14", "PC15"], "external-clock": ["PC14"]}},
                         TARGETS["stm32g474"]["clockInputs"])
        bypass = {"kind": "external-clock", "frequencyHz": 24000000,
                  "pins": [{"domain": "soc-gpio", "id": "PF0"}]}
        self.assertEqual([], clock_registry.source_problems(TARGETS["stm32g474"]["clockInputs"],
                                                            "hse", bypass))
        self.assertEqual(1, len(clock_registry.source_problems(TARGETS["rp2040"]["clockInputs"],
                                                               "hse", bypass)))

    def test_a_tree_needs_a_clock_input_of_its_target(self) -> None:
        registry = copy.deepcopy(self.registry)
        registry["rp2040"]["trees"]["default"]["requiredSources"]["hse"] = {
            "kind": "crystal", "frequencyHz": 24000000}
        with self.assertRaisesRegex(clock_registry.ClockError, "no hse input"):
            clock_registry.check_registry(registry, TARGETS)

    def test_a_tree_without_backend_code_is_refused(self) -> None:
        # RP2040 100 MHz: VCO 1500 MHz / 5 / 3, consistent but never set by the HAL.
        fast = copy.deepcopy(self.registry["rp2040"]["trees"]["default"])
        fast["parameters"].update(pllSysPostdiv1=5, pllSysPostdiv2=3)
        fast["frequenciesHz"].update(sys=100000000, peri=100000000)
        esp = copy.deepcopy(self.registry["esp32s3"]["trees"]["default"])
        esp["parameters"]["cpuFreqMhz"] = 240
        esp["frequenciesHz"]["cpu"] = 240000000
        for target, name, tree in (("rp2040", "100mhz", fast), ("esp32s3", "240mhz", esp),
                                   ("mock", "fast", self.registry["mock"]["trees"]["default"])):
            with self.subTest(target=target):
                registry = copy.deepcopy(self.registry)
                registry[target]["trees"][name] = tree
                with self.assertRaisesRegex(clock_registry.ClockError, "only its default tree"):
                    clock_registry.check_registry(registry, TARGETS)
        registry = copy.deepcopy(self.registry)
        registry["stm32g474"]["trees"]["hse-170mhz"] = registry["stm32g474"]["trees"]["hse-160mhz"]
        with self.assertRaisesRegex(clock_registry.ClockError, "startup code builds"):
            clock_registry.check_stm32g474(registry)

    def test_backend_and_sdk_settings_match(self) -> None:
        clock_registry.check_stm32g474(self.registry)
        clock_registry.check_pico_sdk(self.registry)
        clock_registry.check_no_hal_override()

    def test_frequencies_must_follow_from_the_parameters(self) -> None:
        for target, tree, key, value in (
                ("rp2040", "default", "sys", 133000000),
                ("stm32g474", "hse-160mhz", "fdcan", 160000000),
                ("esp32s3", "default", "cpu", 240000000)):
            with self.subTest(target=target, key=key):
                broken = copy.deepcopy(self.registry[target]["trees"][tree])
                broken["frequenciesHz"][key] = value
                with self.assertRaises(clock_registry.ClockError):
                    clock_registry.check_tree(target, tree, broken)

    def test_a_tree_without_its_backend_is_refused(self) -> None:
        tree = copy.deepcopy(self.registry["rp2040"]["trees"]["default"])
        with self.assertRaises(clock_registry.ClockError):
            clock_registry.check_tree("stm32g474", "default", tree)

    def test_registry_files_are_named_after_their_target(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = ROOT / "config" / "hardware" / "clocks" / "rp2040.json"
            shutil.copy(source, Path(directory) / "pico.json")
            with self.assertRaises(clock_registry.ClockError):
                clock_registry.load_registry(Path(directory))

    def test_esp_sdkconfig_must_match(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            good = Path(directory) / "good"
            good.write_text("CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=160\nCONFIG_XTAL_FREQ=40\n",
                            encoding="utf-8")
            clock_registry.check_esp_sdkconfig(self.registry, "esp32s3", good)
            fast = Path(directory) / "fast"
            fast.write_text("CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240\nCONFIG_XTAL_FREQ=40\n",
                            encoding="utf-8")
            with self.assertRaises(clock_registry.ClockError):
                clock_registry.check_esp_sdkconfig(self.registry, "esp32s3", fast)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
