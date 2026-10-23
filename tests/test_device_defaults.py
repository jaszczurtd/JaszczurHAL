#!/usr/bin/env python3
"""Generated C defaults of the HAL component types (jh generate device-defaults)."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

import hardware_model  # noqa: E402
from hal_type_copies import HalTypeCopies  # noqa: E402
from repository_layout import DEVICE_DEFAULTS_HEADER_OUTPUT as OUTPUT  # noqa: E402

HEADER = ROOT / OUTPUT
# The generator is the jh host CLI; the tools/jh CTest suite names its
# executable, and there is no other generator to fall back to.
TOOL = os.environ.get("JH_SCHEMA_TOOL") or sys.exit("JH_SCHEMA_TOOL must name the jh executable")


def native(bindings: Path, output_root: Path, mode: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [TOOL, "generate", "device-defaults", "--root", str(ROOT), "--bindings", str(bindings),
         "--output-root", str(output_root), mode, "--json"],
        capture_output=True, encoding="utf-8", check=False, timeout=120)


class HeaderTests(unittest.TestCase):
    def test_header_holds_the_module_values(self) -> None:
        text = HEADER.read_text(encoding="utf-8")
        # Written out by hand from the Waveshare Core1262-HF and MCP2515 data.
        for line in (
            "#define JH_DEFAULT_WAVESHARE_CORE1262_HF_RADIO_RF_SWITCH_MODE "
            "JH_ENUM_SEMTECH_SX1262_RF_SWITCH_MODE_DUAL_GPIO",
            "#define JH_DEFAULT_WAVESHARE_CORE1262_HF_RADIO_TCXO_VOLTAGE "
            "JH_ENUM_SEMTECH_SX1262_TCXO_VOLTAGE_1V8",
            "#define JH_ENUM_SEMTECH_SX1262_TCXO_VOLTAGE_1V8 2U",
            "#define JH_DEFAULT_WAVESHARE_CORE1262_HF_RADIO_TCXO_STARTUP_US 5000U",
            "#define JH_DEFAULT_WAVESHARE_CORE1262_HF_RADIO_MIN_TX_POWER_DBM (-9)",
            "#define JH_DEFAULT_WAVESHARE_CORE1262_HF_RADIO_RF_SWITCH_RX_LEVEL_B 1",
            "#define JH_DEFAULT_WAVESHARE_CORE1262_HF_RADIO_RF_SWITCH_IDLE_LEVEL_A 0",
            # The module value replaces the chip default.
            "#define JH_DEFAULT_SEMTECH_SX1262_MIN_FREQUENCY_HZ 150000000U",
            "#define JH_DEFAULT_WAVESHARE_CORE1262_HF_RADIO_MIN_FREQUENCY_HZ 850000000U",
            "#define JH_DEFAULT_MICROCHIP_MCP2515_OSCILLATOR_HZ 8000000U",
            "#define JH_DEFAULT_MICROCHIP_MCP2515_BITRATE_HZ 500000U",
        ):
            with self.subTest(line=line):
                self.assertIn(line + "\n", text)
        # A property without a default has no chip-level macro.
        self.assertNotIn("JH_DEFAULT_SEMTECH_SX1262_TCXO_VOLTAGE ", text)

    def test_check_reports_a_stale_or_missing_header(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh native defaults ") as text:
            output = Path(text)
            target = output / OUTPUT
            missing = native(hardware_model.HAL_BINDINGS, output, "--check")
            self.assertEqual(1, missing.returncode, missing.stderr)
            self.assertEqual("HAL_ESTATE", json.loads(missing.stdout)["status"])
            self.assertIn("missing generated artifact", missing.stderr)
            written = native(hardware_model.HAL_BINDINGS, output, "--write")
            self.assertEqual(0, written.returncode, written.stderr)
            self.assertEqual(HEADER.read_bytes(), target.read_bytes())
            target.write_text(target.read_text(encoding="utf-8") + "/* edit */\n", encoding="utf-8")
            stale = native(hardware_model.HAL_BINDINGS, output, "--check")
            self.assertEqual(1, stale.returncode)
            self.assertIn("stale", stale.stderr)
            self.assertIn("+++ generated:", stale.stderr)


class NativeRender(HalTypeCopies):
    """The generator on edited copies of the HAL types."""

    def native_result(self) -> subprocess.CompletedProcess:
        result = native(self.work, self.work / "out", "--write")
        self.assertIn(result.returncode, (0, 2), result.stderr)
        return result

    def native_text(self) -> str:
        result = self.native_result()
        self.assertEqual(0, result.returncode, result.stderr)
        return (self.work / "out" / OUTPUT).read_text(encoding="utf-8")

    def native_failure(self) -> dict:
        result = self.native_result()
        self.assertEqual(2, result.returncode, result.stdout)
        return json.loads(result.stdout)


class CollisionTests(NativeRender):
    """Names that meet after token conversion fail instead of being renamed."""

    bindings = hardware_model.HAL_BINDINGS

    def test_enum_values_meeting_as_tokens(self) -> None:
        self.change("jaszczurhal-gpio-input.json",
                    lambda d: d["properties"]["pull"]["enum"].append("Up"))
        self.assertIn("JH_ENUM_JASZCZURHAL_GPIO_INPUT_PULL_UP is produced by",
                      self.native_failure()["message"])

    def test_module_property_meeting_a_child_property(self) -> None:
        def add(data: dict) -> None:
            data["properties"] = {"radioMaxTxPowerDbm": {
                "type": "integer", "cType": "int8", "description": "Clash.",
                "unit": "dBm", "default": 1}}
        self.change("waveshare-core1262-hf.json", add)
        self.assertIn("JH_DEFAULT_WAVESHARE_CORE1262_HF_RADIO_MAX_TX_POWER_DBM is produced by",
                      self.native_failure()["message"])


class FloatingDefaultTests(NativeRender):
    """Float and double defaults are rounded once from the decimal in the type."""

    bindings = hardware_model.HAL_BINDINGS

    def prepare(self, c_type: str, token: str) -> None:
        def add(data: dict) -> None:
            data["properties"]["gain"] = {"type": "number", "cType": c_type,
                                          "description": "Gain.", "unit": "1", "default": 0.5}
        self.change("jaszczurhal-pwm-output.json", add)
        path = self.work / "jaszczurhal-pwm-output.json"
        path.write_text(path.read_text(encoding="utf-8").replace("0.5", token), encoding="utf-8")

    def test_float_and_double_defaults_use_the_literal_table(self) -> None:
        macro = "#define JH_DEFAULT_JASZCZURHAL_PWM_OUTPUT_GAIN "
        for c_type, token, literal in (
                ("float", "0.22", "2.19999999e-01f"),
                ("double", "0.22", "2.2000000000000000e-01"),
                # Just above a binary32 halfway point; rounding through binary64 would lose it.
                ("float", "1.0000000596046447753906250000000000000000001", "1.00000012e+00f")):
            with self.subTest(c_type=c_type, token=token):
                self.prepare(c_type, token)
                self.assertIn(macro + literal + "\n", self.native_text())

    def test_float_default_rounding_to_zero_is_refused(self) -> None:
        self.prepare("float", "1e-50")
        # The catalogue check rejects the default itself, before any rendering.
        failure = self.native_failure()
        self.assertEqual(("JH-HW-BINDING", "/properties/gain/default"),
                         (failure["code"], failure["pointer"]))
        self.assertIn("a value that fits float", failure["message"])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
