"""Scratch copies of the HAL component types for tests that edit them."""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import tempfile
from typing import Any, Callable
import unittest


class HalTypeCopies(unittest.TestCase):
    """Gives each test its own copy of the files in ``bindings``."""

    bindings: Path

    def setUp(self) -> None:
        self.work = Path(tempfile.mkdtemp(prefix="jh types "))
        for path in self.bindings.glob("*.json"):
            shutil.copy(path, self.work)

    def tearDown(self) -> None:
        shutil.rmtree(self.work)

    def change(self, name: str, edit: Callable[[dict[str, Any]], Any]) -> None:
        """Rewrite one copied type after ``edit`` changes its JSON in place."""
        path = self.work / name
        data = json.loads(path.read_text(encoding="utf-8"))
        edit(data)
        path.write_text(json.dumps(data), encoding="utf-8")
