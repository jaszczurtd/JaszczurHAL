#!/usr/bin/env python3
"""Hardware description data: schemas, format examples, HAL types, bindings."""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

import hardware_model  # noqa: E402
from hal_type_copies import HalTypeCopies  # noqa: E402
import hardware_schema  # noqa: E402

FIXTURES = ROOT / "tests" / "fixtures" / "device_tree"
CASES = json.loads((FIXTURES / "cases.json").read_text(encoding="utf-8"))


class SchemaReaderTests(unittest.TestCase):
    """The dependency-free reader against the specification's rules."""

    def setUp(self) -> None:
        self.schemas = hardware_schema.SchemaSet()

    def test_every_index_case_is_classified_as_listed(self) -> None:
        for case in CASES["cases"]:
            with self.subTest(case=case["file"]):
                data = hardware_schema.load_json(FIXTURES / case["file"])
                first = self.schemas.first_error(case["schema"], data)
                if case["expected"] == "schema":
                    self.assertIsNotNone(first)
                    self.assertEqual(case["at"], first.pointer)
                else:
                    self.assertIsNone(first, first)

    def test_fixture_bindings_are_valid_types(self) -> None:
        for path in sorted((FIXTURES / "bindings").glob("*.json")):
            with self.subTest(binding=path.name):
                self.assertEqual([], self.schemas.validate(
                    "binding.schema.json", hardware_schema.load_json(path)))

    def test_json_rules_beyond_the_schemas(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            duplicate = Path(directory) / "duplicate.json"
            duplicate.write_text('{"a": 1, "a": 2}', encoding="utf-8")
            with self.assertRaises(hardware_schema.InputError):
                hardware_schema.load_json(duplicate)
            infinite = Path(directory) / "infinite.json"
            infinite.write_text('{"a": Infinity}', encoding="utf-8")
            with self.assertRaises(hardware_schema.InputError):
                hardware_schema.load_json(infinite)
            # Python reads 1e400 as inf and 1e-400 as 0 unless the reader stops it.
            numbers = Path(directory) / "numbers.json"
            for token in ("1e400", "-1e400", "1e-400", "-2.5e-999"):
                with self.subTest(token=token):
                    numbers.write_text(f'{{"a": [{token}]}}', encoding="utf-8")
                    with self.assertRaisesRegex(hardware_schema.InputError, "binary64"):
                        hardware_schema.load_json(numbers)
            numbers.write_text('{"a": [4.9e-324, 0e400, -0.0, 1.7976931348623157e308]}',
                               encoding="utf-8")
            self.assertEqual({"a": [5e-324, 0.0, 0.0, 1.7976931348623157e308]},
                             hardware_schema.load_json(numbers))
            surrogate = Path(directory) / "surrogate.json"
            surrogate.write_text('{"a": ["\\ud800"]}', encoding="utf-8")
            with self.assertRaisesRegex(hardware_schema.InputError, "surrogate"):
                hardware_schema.load_json(surrogate)
            surrogate.write_text('{"a": "\\ud83d\\ude00"}', encoding="utf-8")
            self.assertEqual({"a": "\U0001f600"}, hardware_schema.load_json(surrogate))

    def test_a_schema_number_is_finite(self) -> None:
        for value in (float("inf"), float("-inf"), float("nan")):
            with self.subTest(value=value):
                self.assertFalse(hardware_schema.type_matches(value, "number"))
        self.assertTrue(hardware_schema.type_matches(10**400, "number"))
        self.assertTrue(hardware_schema.type_matches(1.5, "number"))

    def test_booleans_are_not_integers_and_equality_is_json(self) -> None:
        self.assertFalse(hardware_schema.type_matches(True, "integer"))
        self.assertTrue(hardware_schema.type_matches(2.0, "integer"))
        self.assertTrue(hardware_schema.json_equal(1, 1.0))
        self.assertFalse(hardware_schema.json_equal(True, 1))

    def test_pointers_follow_code_point_and_index_order(self) -> None:
        pointers = ["/nodes/fooBar", "/nodes/fooBAR", "/a/10", "/a/9"]
        self.assertEqual(["/a/9", "/a/10", "/nodes/fooBAR", "/nodes/fooBar"],
                         sorted(pointers, key=hardware_schema.pointer_key))

    def test_unknown_schema_keywords_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "x.schema.json").write_text(
                '{"type": "object", "dependentRequired": {}}', encoding="utf-8")
            with self.assertRaises(ValueError):
                hardware_schema.SchemaSet(Path(directory))


class HalCatalogueTests(unittest.TestCase):
    def test_hal_types_pass_every_check(self) -> None:
        catalogue = hardware_model.load_hal_catalogue()
        hardware_model.check_catalogue(
            catalogue, hardware_model.hal_features(), hardware_model.board_capabilities())
        for compatible in ("pimoroni,pim730", "waveshare,core1262-hf",
                           "embeddedgarage,can-fd-hat-g474", "microchip,mcp2515",
                           "ilitek,ili9341", "semtech,sx1262", "infineon,cyw43439"):
            self.assertIn(compatible, catalogue.bindings)

    def test_module_children_and_connections_name_declared_parts(self) -> None:
        catalogue = hardware_model.load_hal_catalogue()
        core = catalogue.bindings["waveshare,core1262-hf"].data
        self.assertEqual("radio.rfSwitchA", core["connections"]["rxen"]["owner"])
        self.assertEqual("radio.rfSwitchB", core["connections"]["txen"]["owner"])
        pim = catalogue.bindings["pimoroni,pim730"].data
        self.assertEqual(["radio.btOn"], pim["connections"]["wlOn"]["aliases"])


class CatalogueRuleTests(HalTypeCopies):
    """Rules beyond the binding schema, on copies of the HAL types."""

    bindings = hardware_model.HAL_BINDINGS

    def expect(self, code: str, file: str, pointer: str) -> None:
        with self.assertRaises(hardware_model.HardwareError) as raised:
            catalogue = hardware_model.load_hal_catalogue(self.work)
            hardware_model.check_catalogue(catalogue, hardware_model.hal_features(),
                                           hardware_model.board_capabilities())
        self.assertEqual((code, file, pointer), (raised.exception.code,
                                                 Path(raised.exception.source).name,
                                                 raised.exception.pointer))

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


class ProjectBindingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.work = Path(tempfile.mkdtemp(prefix="jh bindings "))
        self.hal = hardware_model.load_hal_catalogue()
        shutil.copy(FIXTURES / "bindings" / "gpio-output.json", self.work / "led.json")

    def tearDown(self) -> None:
        shutil.rmtree(self.work)

    def write(self, name: str, compatible: str) -> None:
        data = json.loads((self.work / "led.json").read_text(encoding="utf-8"))
        data["compatible"] = compatible
        (self.work / name).write_text(json.dumps(data), encoding="utf-8")

    def expect(self, entries: list[str], pointer: str) -> hardware_model.HardwareError:
        with self.assertRaises(hardware_model.HardwareError) as raised:
            hardware_model.load_catalogue(self.work / "device_tree.json", entries, self.hal)
        self.assertEqual("JH-HW-BINDING", raised.exception.code)
        self.assertEqual(pointer, raised.exception.pointer)
        return raised.exception

    def test_valid_fixtures_load_their_project_types(self) -> None:
        for case in CASES["cases"]:
            if case["schema"] != "device_tree.schema.json" or case["expected"] != "accept":
                continue
            path = FIXTURES / case["file"]
            entries = hardware_schema.load_json(path).get("bindings", [])
            catalogue = hardware_model.load_catalogue(path, entries, self.hal)
            project = {b.compatible for b in catalogue.bindings.values() if b.origin == "project"}
            self.assertEqual(len(entries), len(project), case["file"])

    def test_one_file_named_twice_is_read_once(self) -> None:
        (self.work / "sub").mkdir()
        catalogue = hardware_model.load_catalogue(
            self.work / "sub" / "device_tree.json", ["../led.json", ".././led.json"], self.hal)
        self.assertEqual(1, sum(b.origin == "project" for b in catalogue.bindings.values()))

    def test_repeated_compatible_fails(self) -> None:
        self.write("second.json", "test,gpio-output")
        self.expect(["led.json", "second.json"], "/compatible")

    def test_project_type_cannot_shadow_a_hal_type(self) -> None:
        self.write("shadow.json", "jaszczurhal,gpio-output")
        error = self.expect(["shadow.json"], "/compatible")
        self.assertIn("config/hardware/bindings", error.detail)

    def test_only_relative_single_files_are_accepted(self) -> None:
        for entry in ("/abs/led.json", "https://example.invalid/led.json", "sub\\led.json",
                      "*.json", "missing.json"):
            with self.subTest(entry=entry):
                self.expect([entry], "/bindings/0")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
