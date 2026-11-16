#!/usr/bin/env python3
"""Hardware description data: schemas, format examples, HAL types, bindings."""

from __future__ import annotations

import json
import re
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from urllib.parse import unquote

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

from hal_type_copies import HalTypeCopies  # noqa: E402
from project_config import jh_executable  # noqa: E402

SCHEMA_DIR = ROOT / "config/hardware"
HAL_BINDINGS = SCHEMA_DIR / "bindings"
TOOL = os.environ.get("JH_SCHEMA_TOOL") or str(jh_executable())

FIXTURES = ROOT / "tests" / "fixtures" / "device_tree"
CASES = json.loads((FIXTURES / "cases.json").read_text(encoding="utf-8"))


class SchemaReaderTests(unittest.TestCase):
    """Native schema CLI against independent fixture and regex expectations.

    Scalar parsing, exact equality, unsupported keywords and pointer ordering
    are covered by jh_hardware and jh_schema in C.
    """

    def native_errors(self, path: Path, schema: str,
                      directory: Path = SCHEMA_DIR) -> list[dict]:
        result = subprocess.run(
            [TOOL, "json", "validate", "--file", str(path), "--schema",
             str(directory / schema), "--json"],
            capture_output=True, encoding="utf-8", check=False, timeout=30)
        output = json.loads(result.stdout)
        errors = output["errors"]
        self.assertEqual(2 if errors else 0, result.returncode, result.stderr)
        self.assertEqual("HAL_ECONFIG" if errors else "HAL_OK", output["status"])
        for error in errors:
            self.assertIn(f":{error['instancePointer'] or '/'}: {error['message']}", result.stderr)
            self.assertTrue(error["reason"])
            self.assertTrue(Path(unquote(error["schemaUri"])).is_file())
            self.assertTrue(error["schemaPointer"].startswith("/"))
        return errors

    def test_every_index_case_is_classified_as_listed(self) -> None:
        for case in CASES["cases"]:
            with self.subTest(case=case["file"]):
                errors = self.native_errors(FIXTURES / case["file"], case["schema"])
                if case["expected"] == "schema":
                    self.assertTrue(errors)
                    self.assertEqual(case["at"], errors[0]["instancePointer"])
                else:
                    self.assertEqual([], errors)

    def test_fixture_bindings_are_valid_types(self) -> None:
        paths = sorted((FIXTURES / "bindings").glob("*.json"))
        paths += sorted(HAL_BINDINGS.glob("*.json"))
        for path in paths:
            with self.subTest(binding=path.name):
                self.assertEqual([], self.native_errors(path, "binding.schema.json"))
        # The C regex backend once treated NBSP differently from Python re.
        # Combining marks/new scalars remain legal at the schema layer; NFC and
        # printable checks belong to the catalogue rules.
        binding = json.loads((HAL_BINDINGS / "jaszczurhal-pwm-output.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unit.json"
            for unit in ("Hz", "Hz ", "\u00a0Hz", "Hz\u00a0", "\u001cHz",
                         "Hz\u0085", "\tHz", "Hz\n", "e\u0301", "\U0001fae9"):
                with self.subTest(unit=unit):
                    binding["properties"]["frequencyHz"]["unit"] = unit
                    path.write_text(json.dumps(binding, ensure_ascii=False), encoding="utf-8")
                    errors = self.native_errors(path, "binding.schema.json")
                    # Python's $ anchor also matches before a final LF;
                    # the catalogue separately rejects surrounding whitespace.
                    self.assertEqual(unit not in ("Hz", "Hz\n", "e\u0301", "\U0001fae9"), bool(errors))
            # printf("%s") used to truncate this valid JSON member's Pointer.
            binding["properties"]["frequencyHz"]["unit"] = "Hz"
            binding["a\0b"] = True
            path.write_text(json.dumps(binding, ensure_ascii=False), encoding="utf-8")
            errors = self.native_errors(path, "binding.schema.json")
            self.assertEqual(["/a\0b"], [error["instancePointer"] for error in errors])
            if TOOL:
                # Every production pattern uses the same host adapter. Check
                # its search/anchor, escape, lookahead and Unicode behavior.
                patterns: set[str] = set()

                def collect(value: object) -> None:
                    if isinstance(value, dict):
                        if "pattern" in value:
                            patterns.add(value["pattern"])
                        for child in value.values():
                            collect(child)
                    elif isinstance(value, list):
                        for child in value:
                            collect(child)

                for schema in SCHEMA_DIR.glob("*.schema.json"):
                    collect(json.loads(schema.read_text(encoding="utf-8")))
                schema_path = Path(directory) / "pattern.schema.json"
                for pattern in sorted(patterns):
                    schema_path.write_text(json.dumps({"pattern": pattern}), encoding="utf-8")
                    for value in ("", "abc", "a-b", "a.b", "a.b.c", "PA15", "HAL_ENABLE_SPI",
                                  "file.json", "../file.json", "/file.json", "a:file.json",
                                  "Hz\n", "\u00a0Hz", "Hz\u001c", "e\u0301", "\U0001fae9", "a\0b"):
                        with self.subTest(pattern=pattern, value=value):
                            path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
                            errors = self.native_errors(path, schema_path.name, Path(directory))
                            self.assertEqual(re.search(pattern, value) is None, bool(errors))


class CatalogueRuleTests(HalTypeCopies):
    """Rules beyond the binding schema, on copies of the HAL types."""

    bindings = HAL_BINDINGS

    def expect(self, code: str, file: str, pointer: str) -> None:
        # The migrated catalogue reports the same code, file and Pointer.
        result = subprocess.run(
            [TOOL, "generate", "device-defaults", "--root", str(ROOT), "--bindings",
             str(self.work), "--output-root", str(self.work / "out"), "--check", "--json"],
            capture_output=True, encoding="utf-8", check=False, timeout=120)
        self.assertEqual(2, result.returncode, result.stderr)
        output = json.loads(result.stdout)
        self.assertEqual((code, file, pointer),
                         (output["code"], Path(output["source"]).name, output["pointer"]))

    def test_file_is_named_after_its_compatible(self) -> None:
        (self.work / "nxp-pcf8574.json").rename(self.work / "pcf8574.json")
        self.expect("JH-HW-BINDING", "pcf8574.json", "/compatible")

    def test_c_type_must_hold_the_json_type(self) -> None:
        self.change("jaszczurhal-gpio-output.json",
                    lambda d: d["properties"]["activeLow"].update(cType="uint8"))
        self.expect("JH-HW-BINDING", "jaszczurhal-gpio-output.json", "/properties/activeLow/cType")

    def test_default_must_be_an_enum_member(self) -> None:
        self.change("jaszczurhal-addressable-led.json",
                    lambda d: d["properties"]["pixelOrder"].update(default="XYZ"))
        self.expect("JH-HW-PROPERTY", "jaszczurhal-addressable-led.json",
                    "/properties/pixelOrder/default")

    def test_unit_whitespace_is_a_schema_error(self) -> None:
        self.change("jaszczurhal-pwm-output.json",
                    lambda d: d["properties"]["frequencyHz"].update(unit="Hz "))
        self.expect("JH-HW-SCHEMA", "jaszczurhal-pwm-output.json", "/properties/frequencyHz/unit")

    def test_overflowing_number_fails_when_read(self) -> None:
        # Read as inf, 1e400 would pass the schema and fail only the default check.
        def add(data: dict) -> None:
            data["properties"]["gain"] = {"type": "number", "cType": "double",
                                          "description": "Gain.", "unit": "1", "default": 0.125}
        self.change("jaszczurhal-pwm-output.json", add)
        path = self.work / "jaszczurhal-pwm-output.json"
        path.write_text(path.read_text(encoding="utf-8").replace("0.125", "1e400"), encoding="utf-8")
        self.expect("JH-HW-SCHEMA", "jaszczurhal-pwm-output.json", "")

    def test_unit_is_nfc_text(self) -> None:
        # U+2126 OHM SIGN passes the schema pattern; NFC turns it into U+03A9.
        self.change("jaszczurhal-pwm-output.json",
                    lambda d: d["properties"]["frequencyHz"].update(unit="kΩ"))
        self.expect("JH-HW-BINDING", "jaszczurhal-pwm-output.json", "/properties/frequencyHz/unit")

    def test_child_type_must_exist(self) -> None:
        self.change("pimoroni-pim730.json",
                    lambda d: d["children"]["radio"].update(compatible="infineon,cyw43455"))
        self.expect("JH-HW-BINDING", "pimoroni-pim730.json", "/children/radio/compatible")

    def test_child_value_must_fit_its_type(self) -> None:
        self.change("waveshare-core1262-hf.json",
                    lambda d: d["children"]["radio"]["properties"].update(tcxoVoltage="5v0"))
        self.expect("JH-HW-PROPERTY", "waveshare-core1262-hf.json",
                    "/children/radio/properties/tcxoVoltage")

    def test_connection_targets_a_declared_child_signal(self) -> None:
        self.change("waveshare-core1262-hf.json",
                    lambda d: d["connections"]["busy"].update(owner="radio.nbusy"))
        self.expect("JH-HW-BINDING", "waveshare-core1262-hf.json", "/connections/busy/owner")

    def test_child_expansion_cannot_cycle(self) -> None:
        self.change("pimoroni-pim730.json",
                    lambda d: d["children"].update(again={"compatible": "pimoroni,pim730",
                                                          "required": False}))
        self.expect("JH-HW-BINDING", "pimoroni-pim730.json", "/children")

    def test_required_features_exist(self) -> None:
        self.change("microchip-mcp2515.json",
                    lambda d: d["requiresFeatures"].append("HAL_ENABLE_MCP2516"))
        self.expect("JH-HW-BINDING", "microchip-mcp2515.json", "/requiresFeatures/2")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
