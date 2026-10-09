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


class LiteralTests(unittest.TestCase):
    """Spelling fixed by the literal table of the device tree specification."""

    def test_integer_and_bool_literals(self) -> None:
        cases = [
            ("bool", True, "1"), ("bool", False, "0"),
            ("uint8", 255, "255U"), ("uint32", 2.0, "2U"),
            ("uint64", 2**64 - 1, "18446744073709551615ULL"),
            ("int8", -9, "(-9)"), ("int16", 7, "7"),
            ("int32", -2**31, "(-2147483647 - 1)"),
            ("int64", -5, "(-5LL)"), ("int64", -2**63, "(-9223372036854775807LL - 1LL)"),
        ]
        for c_type, value, expected in cases:
            with self.subTest(c_type=c_type, value=value):
                self.assertEqual(expected, defaults.c_literal(c_type, value))

    def test_strings_are_escaped_utf8(self) -> None:
        self.assertEqual('"a\\"b\\\\c\\x0a\\xc2\\xb5"', defaults.c_literal("string", 'a"b\\c\nµ'))

    def test_floating_defaults_are_refused(self) -> None:
        with self.assertRaises(defaults.DefaultsError):
            defaults.c_literal("float", 0.5)

    def test_tokens_follow_the_specification(self) -> None:
        self.assertEqual("RADIO_MODULE_RADIO", defaults.macro_token("radioModule.radio"))
        self.assertEqual(defaults.macro_token("fooBar"), defaults.macro_token("fooBAR"))
        self.assertEqual("WAVESHARE_CORE1262_HF", defaults.macro_token("waveshare,core1262-hf"))
        self.assertEqual("1V8", defaults.macro_token("1v8"))


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

    def test_floating_default_in_a_type_is_refused(self) -> None:
        def add(data: dict) -> None:
            data["properties"]["gain"] = {"type": "number", "cType": "float",
                                          "description": "Gain.", "unit": "1", "default": 0.5}
        self.change("jaszczurhal-pwm-output.json", add)
        with self.assertRaises(defaults.DefaultsError):
            self.render()


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
