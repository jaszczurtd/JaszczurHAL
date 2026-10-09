#!/usr/bin/env python3
"""C spelling of hardware values against the device tree specification."""

from __future__ import annotations

import copy
import json
from pathlib import Path
import pickle
import sys
import tempfile
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

import hardware_literals as literals  # noqa: E402
import hardware_schema  # noqa: E402

CASES = json.loads((ROOT / "tests/fixtures/device_tree/cases.json").read_text(encoding="utf-8"))


class LiteralTableTests(unittest.TestCase):
    """Spelling fixed by the literal table of the specification."""

    def test_integer_and_bool_literals(self) -> None:
        cases = [
            ("bool", True, "1"), ("bool", False, "0"),
            ("uint8", 255, "255U"), ("uint32", 2.0, "2U"),
            ("uint64", 2**64 - 1, "18446744073709551615ULL"),
            ("int8", -9, "(-9)"), ("int16", 7, "7"),
            ("int32", -2**31, "(-2147483647 - 1)"),
            ("int64", -5, "(-5LL)"), ("int64", -2**63, "(-9223372036854775807LL - 1LL)"),
            # An integer written with an exponent keeps its exact value.
            ("uint64", hardware_schema.JsonNumber("1.8446744073709551615e19"), "18446744073709551615ULL"),
        ]
        for c_type, value, expected in cases:
            with self.subTest(c_type=c_type, value=value):
                self.assertEqual(expected, literals.c_literal(c_type, value))

    def test_strings_are_escaped_utf8(self) -> None:
        # Octal escapes end after three digits; \xb5 would also take the A.
        # CTest test_device_defaults_literals_{c,cpp} compile such literals.
        self.assertEqual('"a\\"b\\\\c\\012\\302\\265A\\?"',
                         literals.c_literal("string", 'a"b\\c\nµA?'))

    def test_tokens_follow_the_specification(self) -> None:
        self.assertEqual("RADIO_MODULE_RADIO", literals.macro_token("radioModule.radio"))
        self.assertEqual(literals.macro_token("fooBar"), literals.macro_token("fooBAR"))
        self.assertEqual("WAVESHARE_CORE1262_HF", literals.macro_token("waveshare,core1262-hf"))
        self.assertEqual("1V8", literals.macro_token("1v8"))

    def test_numeric_cases_of_the_specification(self) -> None:
        for case in CASES["numericCases"]:
            value = case["input"]
            if case.get("inputEncoding") == "json-number-token":
                value = hardware_schema.JsonNumber(value)
            with self.subTest(c_type=case["cType"], input=case["input"]):
                if "error" in case:
                    self.assertEqual("JH-HW-PROPERTY", case["error"])
                    with self.assertRaises(literals.LiteralError):
                        literals.c_literal(case["cType"], value)
                    continue
                self.assertEqual(case["literal"], literals.c_literal(case["cType"], value))
                if "hashValue" in case:
                    self.assertEqual(case["hashValue"], literals.ieee_bits(case["cType"], value))


class RoundingTests(unittest.TestCase):
    """One rounding from the exact decimal, ties to even."""

    def bits(self, c_type: str, token: str) -> str:
        return literals.ieee_bits(c_type, hardware_schema.JsonNumber(token))

    def test_ties_go_to_the_even_significand(self) -> None:
        # 1 + 2^-24 lies halfway between 1 and the next float; 1 + 3 * 2^-24
        # lies halfway between two floats whose even neighbour is the upper one.
        self.assertEqual("3f800000", self.bits("float", "1.000000059604644775390625"))
        self.assertEqual("3f800002", self.bits("float", "1.000000178813934326171875"))

    def test_format_limits(self) -> None:
        self.assertEqual("7f7fffff", self.bits("float", "3.4028235e38"))
        self.assertEqual("00000001", self.bits("float", "1e-45"))
        self.assertEqual("0000000000000001", self.bits("double", "4e-324"))
        self.assertEqual("7fefffffffffffff", self.bits("double", "1.7976931348623157e308"))
        for c_type, token in (("float", "3.5e38"), ("float", "7e-46"), ("double", "2e-324")):
            with self.subTest(c_type=c_type, token=token):
                with self.assertRaises(literals.LiteralError):
                    self.bits(c_type, token)

    def test_sign_and_zero(self) -> None:
        # A negative macro value keeps its parentheses, as for integers: a-X.
        self.assertEqual("(-2.19999999e-01f)", literals.c_literal("float", hardware_schema.JsonNumber("-0.22")))
        self.assertEqual("(-1.0000000000000000e+00)", literals.c_literal("double", -1))
        self.assertEqual("0000000000000000", self.bits("double", "-0e5"))
        self.assertEqual("1.0000000000000000e+00", literals.c_literal("double", 1))


class JsonNumberTests(unittest.TestCase):
    def test_reader_keeps_the_token(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "value.json"
            path.write_text('{"a": [0.22000000000000000, 2, 1e2]}', encoding="utf-8")
            values = hardware_schema.load_json(path)["a"]
        self.assertEqual("0.22000000000000000", values[0].token)
        self.assertIs(int, type(values[1]))
        self.assertEqual("1e2", values[2].token)

    def test_a_json_number_is_still_a_float(self) -> None:
        value = hardware_schema.JsonNumber("0.250")
        self.assertIsInstance(value, float)
        self.assertEqual(0.25, value)
        self.assertEqual("0.250", copy.deepcopy(value).token)
        self.assertEqual("0.250", pickle.loads(pickle.dumps(value)).token)
        self.assertEqual('{"v": 0.25}', json.dumps({"v": value}))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
