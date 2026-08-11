#!/usr/bin/env python3
"""Regression tests for the release metadata and ancestry gate."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import check_release_metadata as release  # noqa: E402


def git(root: Path, *arguments: str) -> None:
    environment = os.environ.copy()
    for variable in (
        "ASAN_OPTIONS",
        "LSAN_OPTIONS",
        "MSAN_OPTIONS",
        "TSAN_OPTIONS",
        "UBSAN_OPTIONS",
    ):
        environment.pop(variable, None)

    result = subprocess.run(
        ["git", *arguments],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
        env=environment,
    )
    if result.returncode != 0:
        command = " ".join(("git", *arguments))
        raise RuntimeError(
            f"{command} failed with exit code {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def write_metadata(root: Path, version: str) -> None:
    (root / "doc").mkdir(parents=True, exist_ok=True)
    (root / "security").mkdir(parents=True, exist_ok=True)
    (root / "VERSION").write_text(f"version={version}\n", encoding="utf-8")
    (root / "doc/CHANGELOG.md").write_text(
        f"# Changelog\n\n## [Unreleased] - 2026-08-10\n\n"
        f"## [{version}] - 2026-08-07\n",
        encoding="utf-8",
    )
    (root / "security/sbom.cdx.json").write_text(
        json.dumps({"metadata": {"component": {"version": version}}}),
        encoding="utf-8",
    )


class ReleaseMetadataTests(unittest.TestCase):
    def test_git_helper_does_not_forward_sanitizer_options(self) -> None:
        sanitizer_variables = {
            "ASAN_OPTIONS",
            "LSAN_OPTIONS",
            "MSAN_OPTIONS",
            "TSAN_OPTIONS",
            "UBSAN_OPTIONS",
        }
        injected = {variable: "enabled" for variable in sanitizer_variables}
        with mock.patch.dict(os.environ, injected):
            with mock.patch.object(subprocess, "run") as run:
                run.return_value = subprocess.CompletedProcess(
                    args=["git", "status"], returncode=0, stdout="", stderr=""
                )
                git(Path.cwd(), "status")

        forwarded = run.call_args.kwargs["env"]
        self.assertTrue(sanitizer_variables.isdisjoint(forwarded))

    def test_tracked_metadata_must_match(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-release-") as text:
            root = Path(text)
            write_metadata(root, "1.9.1")
            self.assertEqual("1.9.1", release.check(root))
            (root / "VERSION").write_text("version=1.9.2\n", encoding="utf-8")
            with self.assertRaisesRegex(release.ReleaseError, "does not match"):
                release.check(root)

    def test_tag_must_match_version_and_be_ancestor_of_release_ref(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-release-git-") as text:
            root = Path(text)
            git(root, "init", "-q")
            git(root, "config", "user.name", "JaszczurHAL Test")
            git(root, "config", "user.email", "test@jaszczurhal.invalid")
            write_metadata(root, "1.9.1")
            git(root, "add", ".")
            git(root, "commit", "-q", "-m", "release")
            git(root, "tag", "1.9.1")
            self.assertEqual("1.9.1", release.check(root, "1.9.1", "HEAD"))
            with self.assertRaisesRegex(release.ReleaseError, "does not match"):
                release.check(root, "1.9.0", "HEAD")

            git(root, "checkout", "-q", "--orphan", "divergent")
            git(root, "rm", "-q", "-rf", ".")
            write_metadata(root, "1.9.1")
            (root / "other.txt").write_text("other\n", encoding="utf-8")
            git(root, "add", ".")
            git(root, "commit", "-q", "-m", "divergent")
            with self.assertRaisesRegex(release.ReleaseError, "not an ancestor"):
                release.check(root, "1.9.1", "HEAD")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
