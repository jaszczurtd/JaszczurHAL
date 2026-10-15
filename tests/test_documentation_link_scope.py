#!/usr/bin/env python3
"""The link check reads the files of the repository, not ignored local ones.

Every case builds a throwaway Git repository with a broken link in a file
Git ignores and runs the real check against it.
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
sys.path.insert(0, str(SCRIPTS))


def load_links():
    path = SCRIPTS / "check_documentation_links.py"
    spec = importlib.util.spec_from_file_location("jh_check_documentation_links", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def git(repo: Path, *args: str) -> None:
    subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True)


class LinkScopeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.links = load_links()
        self.directory = tempfile.TemporaryDirectory(prefix="jh-link-scope-")
        self.repo = Path(self.directory.name)
        write(self.repo / "README.md", "[doc](doc/en/a.md)\n")
        write(self.repo / "doc/en/a.md", "# A\n")
        write(self.repo / "NOW.md", "[outside](../../NOW.md)\n")
        write(self.repo / "doc/en/draft.md", "[gone](missing.md)\n")

    def tearDown(self) -> None:
        self.directory.cleanup()

    def test_ignored_files_are_not_read(self) -> None:
        write(self.repo / ".gitignore", "/NOW.md\n")
        git(self.repo, "init", "-q")

        self.assertEqual(
            ["doc/en/draft.md: missing local link target: 'missing.md'"],
            self.links.check_links(self.repo),
        )
        files = self.links.documentation_files(self.repo)
        self.assertNotIn(self.repo / "NOW.md", files)
        self.assertIn(self.repo / "doc/en/draft.md", files)

    def test_tracked_files_count_even_when_a_pattern_matches_them(self) -> None:
        git(self.repo, "init", "-q")
        git(self.repo, "add", "NOW.md")
        write(self.repo / ".gitignore", "/NOW.md\n")

        found = self.links.check_links(self.repo)
        self.assertIn("NOW.md: local link escapes the repository: '../../NOW.md'", found)

    def test_every_file_counts_outside_a_work_tree(self) -> None:
        write(self.repo / ".gitignore", "/NOW.md\n")

        found = self.links.check_links(self.repo)
        self.assertIn("NOW.md: local link escapes the repository: '../../NOW.md'", found)
        self.assertEqual(2, len(found))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
