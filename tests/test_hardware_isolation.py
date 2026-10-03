#!/usr/bin/env python3
"""Default checks never read device-test fixtures (AGENTS.md).

Every case builds a throwaway repository with a planted tests/hardware tree
whose content would fail the check if it were read, and runs the real
script against it: without --include-hardware-fixtures the planted files
must not matter, with it they must be found. No real fixture is read.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
SCRIPTS = ROOT / "scripts"


def load_script(name: str):
    path = SCRIPTS / f"{name}.py"
    spec = importlib.util.spec_from_file_location(f"jh_{name}", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class FeatureLintTests(unittest.TestCase):
    def test_lint_skips_hardware_fixtures_unless_asked(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-iso-features-") as text:
            repo = Path(text)
            write(repo / "app/hal_project_config.h", "#define HAL_ENABLE_SPI\n")
            write(
                repo / "tests/hardware/board_check/hal_project_config.h",
                "#define HAL_ENABLE_NOT_A_FEATURE\n",
            )

            def lint(*extra: str) -> subprocess.CompletedProcess[str]:
                return subprocess.run(
                    [
                        sys.executable,
                        str(SCRIPTS / "generate_hal_features.py"),
                        "--lint",
                        "--input-root",
                        str(repo),
                        *extra,
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                )

            default = lint()
            self.assertEqual(0, default.returncode, default.stderr)
            self.assertIn("linted 1 feature inputs", default.stdout)
            opted_in = lint("--include-hardware-fixtures")
            self.assertNotEqual(0, opted_in.returncode)
            self.assertIn("HAL_ENABLE_NOT_A_FEATURE", opted_in.stdout + opted_in.stderr)


class DocumentationTests(unittest.TestCase):
    def test_links_into_and_inside_fixtures_are_left_alone(self) -> None:
        links = load_script("check_documentation_links")
        with tempfile.TemporaryDirectory(prefix="jh-iso-links-") as text:
            repo = Path(text)
            write(repo / "README.md", "[fixture](tests/hardware/x/README.md)\n")
            write(repo / "tests/hardware/x/README.md", "[gone](missing.md)\n")
            (repo / "tests/hardware/x/README.md").unlink()
            write(repo / "tests/hardware/y/README.md", "[gone](../../../missing.md)\n")

            self.assertEqual([], links.check_links(repo))
            found = links.check_links(repo, include_hardware=True)
            self.assertTrue(any("tests/hardware/x/README.md" in f for f in found))
            self.assertTrue(any("missing.md" in f for f in found))

    def test_parity_skips_fixture_readmes_unless_asked(self) -> None:
        parity = load_script("check_documentation_i18n_parity")
        with tempfile.TemporaryDirectory(prefix="jh-iso-parity-") as text:
            repo = Path(text)
            (repo / "doc").mkdir()  # the check stops early without it
            write(repo / "tests/hardware/x/README.md", "# Fixture\n")

            def fixture_failures(include: bool) -> list[str]:
                return [
                    failure
                    for failure in parity.check(repo, include)
                    if "tests/hardware" in failure
                ]

            self.assertEqual([], fixture_failures(False))
            self.assertNotEqual([], fixture_failures(True))


class GeneratedSnapshotTests(unittest.TestCase):
    def test_snapshot_does_not_read_fixture_files(self) -> None:
        sync = load_script("sync_generated")
        with tempfile.TemporaryDirectory(prefix="jh-iso-sync-") as text:
            repo = Path(text)
            write(repo / "src/a.c", "int a;\n")
            write(repo / "tests/hardware/x/app.c", "int b;\n")
            subprocess.run(
                ["git", "init", "-q", str(repo)], check=True, capture_output=True
            )

            state = sync.repository_state(repo)

        self.assertIn(Path("src/a.c"), state)
        self.assertNotIn(Path("tests/hardware/x/app.c"), state)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
