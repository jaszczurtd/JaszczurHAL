#!/usr/bin/env python3
"""Host-side tests for the ESP32-S3 Phase 1 hardware report contract."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
VERIFIER_PATH = ROOT / "tests" / "hardware" / "esp32s3_phase1" / "verify_phase1.py"
SPEC = importlib.util.spec_from_file_location("jh_esp32s3_phase1_verifier", VERIFIER_PATH)
assert SPEC and SPEC.loader
verifier = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verifier)


class Phase1ReportTests(unittest.TestCase):
    def test_expected_values_come_from_board_registry(self) -> None:
        self.assertEqual(
            verifier.load_expected_contract(
                "esp32s3", "waveshare-esp32-s3-zero"
            ),
            {
                "target": "esp32s3",
                "board": "waveshare-esp32-s3-zero",
                "cores": 2,
                "flash": 4 * 1024 * 1024,
                "psram": 2 * 1024 * 1024,
            },
        )

    def test_report_parser_accepts_monitor_prefix_and_crlf(self) -> None:
        line = (
            b"I (123) stdout: JH_ESP32_PHASE1 phase=task0 sequence=7 "
            b"target=esp32s3 "
            b"board=waveshare-esp32-s3-zero model_match=1 cores=2 "
            b"expected_cores=2 flash=4194304 expected_flash=4194304 "
            b"psram_initialized=1 psram=2097152 expected_psram=2097152 "
            b"status=PASS\r\n"
        )
        report = verifier.parse_report(line)
        self.assertIsNotNone(report)
        self.assertTrue(verifier.is_task0_heartbeat(report))
        verifier.validate_report(
            report,
            verifier.load_expected_contract(
                "esp32s3", "waveshare-esp32-s3-zero"
            ),
        )

    def test_report_validator_rejects_runtime_memory_mismatch(self) -> None:
        report = verifier.parse_report(
            b"JH_ESP32_PHASE1 phase=task0 sequence=1 target=esp32s3 "
            b"board=waveshare-esp32-s3-zero model_match=1 cores=2 "
            b"expected_cores=2 flash=4194304 expected_flash=4194304 "
            b"psram_initialized=1 psram=1048576 expected_psram=2097152 "
            b"status=FAIL\n"
        )
        self.assertIsNotNone(report)
        with self.assertRaisesRegex(RuntimeError, "hardware contract mismatch"):
            verifier.validate_report(
                report,
                verifier.load_expected_contract(
                    "esp32s3", "waveshare-esp32-s3-zero"
                ),
            )

    def test_start_report_does_not_satisfy_task0_heartbeat(self) -> None:
        report = verifier.parse_report(
            b"JH_ESP32_PHASE1 phase=start sequence=0 target=esp32s3 "
            b"board=waveshare-esp32-s3-zero model_match=1 cores=2 "
            b"expected_cores=2 flash=4194304 expected_flash=4194304 "
            b"psram_initialized=1 psram=2097152 expected_psram=2097152 "
            b"status=PASS\n"
        )
        self.assertIsNotNone(report)
        self.assertFalse(verifier.is_task0_heartbeat(report))
        with self.assertRaisesRegex(RuntimeError, "heartbeat sequence"):
            verifier.validate_report(
                report,
                verifier.load_expected_contract(
                    "esp32s3", "waveshare-esp32-s3-zero"
                ),
            )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
