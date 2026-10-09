#!/usr/bin/env python3
"""Generated C defaults of the HAL component types."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

import generate_device_defaults as defaults  # noqa: E402
import hardware_model  # noqa: E402
from hal_type_copies import HalTypeCopies  # noqa: E402

SCRIPT = ROOT / "scripts" / "generate_device_defaults.py"
HEADER = ROOT / "src" / "hal" / "generated" / "jh_device_defaults.h"


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
        with tempfile.TemporaryDirectory(prefix="jh defaults ") as text:
            output = Path(text)
            target = output / defaults.OUTPUT
            command = [sys.executable, str(SCRIPT), "--output-root", str(output)]
            missing = subprocess.run([*command, "--check"], capture_output=True, text=True)
            self.assertEqual(1, missing.returncode, missing.stderr)
            subprocess.run([*command, "--write"], check=True, capture_output=True)
            self.assertEqual(HEADER.read_bytes(), target.read_bytes())
            target.write_text(target.read_text(encoding="utf-8") + "/* edit */\n", encoding="utf-8")
            stale = subprocess.run([*command, "--check"], capture_output=True, text=True)
            self.assertEqual(1, stale.returncode)
            self.assertIn("stale", stale.stderr)


class CollisionTests(HalTypeCopies):
    """Names that meet after token conversion fail instead of being renamed."""

    bindings = hardware_model.HAL_BINDINGS

    def render(self) -> str:
        return defaults.render(hardware_model.load_hal_catalogue(self.work))

    def test_enum_values_meeting_as_tokens(self) -> None:
        self.change("jaszczurhal-gpio-input.json",
                    lambda d: d["properties"]["pull"]["enum"].append("Up"))
        with self.assertRaisesRegex(defaults.DefaultsError, "JH_ENUM_JASZCZURHAL_GPIO_INPUT_PULL_UP"):
            self.render()

    def test_module_property_meeting_a_child_property(self) -> None:
        def add(data: dict) -> None:
            data["properties"] = {"radioMaxTxPowerDbm": {
                "type": "integer", "cType": "int8", "description": "Clash.",
                "unit": "dBm", "default": 1}}
        self.change("waveshare-core1262-hf.json", add)
        with self.assertRaisesRegex(defaults.DefaultsError,
                                    "JH_DEFAULT_WAVESHARE_CORE1262_HF_RADIO_MAX_TX_POWER_DBM"):
            self.render()


class FloatingDefaultTests(HalTypeCopies):
    """Float and double defaults are rounded once from the decimal in the type."""

    bindings = hardware_model.HAL_BINDINGS

    def render_with(self, c_type: str, token: str) -> str:
        def add(data: dict) -> None:
            data["properties"]["gain"] = {"type": "number", "cType": c_type,
                                          "description": "Gain.", "unit": "1", "default": 0.5}
        self.change("jaszczurhal-pwm-output.json", add)
        path = self.work / "jaszczurhal-pwm-output.json"
        path.write_text(path.read_text(encoding="utf-8").replace("0.5", token), encoding="utf-8")
        return defaults.render(hardware_model.load_hal_catalogue(self.work))

    def test_float_and_double_defaults_use_the_literal_table(self) -> None:
        macro = "#define JH_DEFAULT_JASZCZURHAL_PWM_OUTPUT_GAIN "
        for c_type, token, literal in (
                ("float", "0.22", "2.19999999e-01f"),
                ("double", "0.22", "2.2000000000000000e-01"),
                # Just above a binary32 halfway point; rounding through binary64 would lose it.
                ("float", "1.0000000596046447753906250000000000000000001", "1.00000012e+00f")):
            with self.subTest(c_type=c_type, token=token):
                self.assertIn(macro + literal + "\n", self.render_with(c_type, token))

    def test_float_default_rounding_to_zero_is_refused(self) -> None:
        with self.assertRaisesRegex(defaults.DefaultsError, "gain: 1e-50 rounds to zero in float"):
            self.render_with("float", "1e-50")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
