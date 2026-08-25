#!/usr/bin/env python3
"""Validate the PMD CPD source scope and zero-exception blocking policy."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
MODULE_PATH = ROOT / "scripts/run_cpd.py"
SPEC = importlib.util.spec_from_file_location("jh_run_cpd", MODULE_PATH)
assert SPEC and SPEC.loader
cpd = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = cpd
SPEC.loader.exec_module(cpd)


class CpdGateTests(unittest.TestCase):
    def test_source_scope_excludes_nested_build_artifacts(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-cpd-scope-") as text:
            repo = Path(text)
            owned = repo / "tests/fixture/app.c"
            generated = repo / "tests/fixture/.build/CMakeCCompilerId.c"
            owned.parent.mkdir(parents=True)
            generated.parent.mkdir(parents=True)
            owned.write_text("int app(void) { return 0; }\n", encoding="utf-8")
            generated.write_text(
                "int generated(void) { return 0; }\n", encoding="utf-8"
            )

            sources = {
                path.relative_to(repo).as_posix()
                for path in cpd.collect_sources(
                    repo, ("tests",), production=False
                )
            }

        self.assertEqual({"tests/fixture/app.c"}, sources)

    def test_owned_scope_includes_backends_and_excludes_vendored_sources(self) -> None:
        sources = {
            path.relative_to(ROOT).as_posix()
            for path in cpd.collect_sources(ROOT, ("src",), production=True)
        }
        for expected in (
            "src/hal/impl/.mock/hal_i2c.cpp",
            "src/hal/impl/.mock/hal_wireguard.cpp",
            "src/hal/impl/rp2040/hal_time.cpp",
            "src/hal/impl/rp2040/hal_eeprom.cpp",
            "src/hal/impl/rp2040/hal_i2c.cpp",
            "src/hal/impl/rp2040/hal_can.cpp",
            "src/hal/impl/stm32g474/hal_time.cpp",
            "src/hal/impl/stm32g474/hal_eeprom.cpp",
            "src/hal/impl/stm32g474/hal_i2c.cpp",
            "src/hal/impl/stm32g474/hal_can.cpp",
            "src/hal/network/wireguard/hal_wireguard.cpp",
        ):
            self.assertIn(expected, sources)
        for excluded in (
            "src/utils/unity.c",
            "src/hal/network/wireguard/core/wireguard.c",
        ):
            self.assertNotIn(excluded, sources)

    def test_scripts_python_scope_contains_only_owned_python_files(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-cpd-python-scope-") as text:
            repo = Path(text)
            owned = repo / "scripts/tool.py"
            shell = repo / "scripts/tool.sh"
            generated = repo / "scripts/.build/generated.py"
            owned.parent.mkdir(parents=True)
            generated.parent.mkdir(parents=True)
            owned.write_text("print('owned')\n", encoding="utf-8")
            shell.write_text("echo shell\n", encoding="utf-8")
            generated.write_text("print('generated')\n", encoding="utf-8")

            sources = {
                path.relative_to(repo).as_posix()
                for path in cpd.collect_sources(
                    repo,
                    ("scripts",),
                    production=False,
                    extensions=cpd.PYTHON_SOURCE_EXTENSIONS,
                )
            }

        self.assertEqual({"scripts/tool.py"}, sources)

    def test_report_parser_preserves_tokens_and_occurrences(self) -> None:
        report_text = """<?xml version="1.0" encoding="UTF-8"?>
<pmd-cpd xmlns="https://pmd-code.org/schema/cpd-report">
  <file path="src/a.cpp" totalNumberOfTokens="800"/>
  <file path="src/b.cpp" totalNumberOfTokens="900"/>
  <file path="src/c.cpp" totalNumberOfTokens="500"/>
  <file path="src/d.cpp" totalNumberOfTokens="600"/>
  <duplication lines="20" tokens="600">
    <file path="src/a.cpp" line="10" endline="29"
          begintoken="100" endtoken="699"/>
    <file path="src/b.cpp" line="30" endline="49"
          begintoken="1000" endtoken="1599"/>
  </duplication>
  <duplication lines="12" tokens="400">
    <file path="src/c.cpp" line="1" endline="12"
          begintoken="2000" endtoken="2399"/>
    <file path="src/d.cpp" line="2" endline="13"
          begintoken="3000" endtoken="3399"/>
  </duplication>
</pmd-cpd>
"""
        with tempfile.TemporaryDirectory(prefix="jh-cpd-report-") as text:
            report = Path(text) / "report.xml"
            report.write_text(report_text, encoding="utf-8")
            parsed = cpd.parse_report(report, ROOT)
        self.assertEqual([600, 400], [group.tokens for group in parsed.groups])
        self.assertEqual(800, parsed.file_tokens["src/a.cpp"])
        self.assertEqual(100, parsed.groups[0].occurrences[0].begin_token)
        self.assertEqual(699, parsed.groups[0].occurrences[0].end_token)

    def test_every_reported_group_is_blocking_without_a_baseline(self) -> None:
        self.assertEqual(100, cpd.MINIMUM_TOKENS)
        self.assertEqual(50, cpd.SCRIPTS_PYTHON_MINIMUM_TOKENS)
        help_text = cpd.build_parser().format_help()
        self.assertIn("--minimum-tokens", help_text)
        self.assertNotIn("baseline", help_text.lower())
        self.assertFalse((ROOT / "tests/cpd-production-baseline.json").exists())

    def test_duplicate_coverage_unions_overlapping_ranges_per_component(self) -> None:
        report = cpd.CpdReport(
            [
                cpd.Duplication(
                    100,
                    (
                        cpd.Occurrence(
                            "src/hal/impl/.mock/a.cpp", 1, 0, 99
                        ),
                        cpd.Occurrence(
                            "src/hal/impl/rp2040/a.cpp", 1, 1000, 1099
                        ),
                    ),
                ),
                cpd.Duplication(
                    50,
                    (
                        cpd.Occurrence(
                            "src/hal/impl/.mock/a.cpp", 10, 50, 99
                        ),
                        cpd.Occurrence(
                            "src/hal/time/a.cpp", 1, 2000, 2049
                        ),
                    ),
                ),
            ],
            {
                "src/hal/impl/.mock/a.cpp": 200,
                "src/hal/impl/rp2040/a.cpp": 400,
                "src/hal/time/a.cpp": 100,
                "src/utils/tools.cpp": 100,
            },
        )
        coverage = {item.scope: item for item in cpd.duplicate_coverage(report)}
        self.assertEqual(100, coverage["mock"].duplicated_tokens)
        self.assertEqual(100, coverage["rp2040"].duplicated_tokens)
        self.assertEqual(50, coverage["portable-hal"].duplicated_tokens)
        self.assertEqual(0, coverage["portable-other"].duplicated_tokens)
        self.assertEqual(250, coverage["global"].duplicated_tokens)
        self.assertEqual(800, coverage["global"].total_tokens)
        self.assertAlmostEqual(31.25, coverage["global"].percentage)

    def test_python_scripts_have_a_distinct_coverage_scope(self) -> None:
        report = cpd.CpdReport(
            [],
            {"scripts/tool.py": 75},
        )
        coverage = {item.scope: item for item in cpd.duplicate_coverage(report)}
        self.assertEqual(75, coverage["scripts-python"].total_tokens)
        self.assertEqual(75, coverage["global"].total_tokens)

    def test_main_runs_the_blocking_python_scripts_scan(self) -> None:
        empty_report = cpd.CpdReport([], {})
        with tempfile.TemporaryDirectory(prefix="jh-cpd-main-") as text:
            with (
                mock.patch.object(cpd, "resolve_pmd", return_value=Path("pmd")),
                mock.patch.object(
                    cpd, "run_scan", return_value=empty_report
                ) as run_scan,
                mock.patch.object(
                    sys,
                    "argv",
                    [
                        "run_cpd.py",
                        "--repo-root",
                        str(ROOT),
                        "--output-dir",
                        text,
                    ],
                ),
            ):
                result = cpd.main()

        self.assertEqual(0, result)
        scans = [
            (call.args[3], call.args[4], call.args[6])
            for call in run_scan.call_args_list
        ]
        self.assertEqual(
            [
                ("production", "cpp", 100),
                ("tests_examples", "cpp", 100),
                ("scripts_python", "python", 50),
            ],
            scans,
        )
        script_sources = run_scan.call_args_list[2].args[5]
        self.assertTrue(script_sources)
        self.assertTrue(all(path.suffix == ".py" for path in script_sources))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
