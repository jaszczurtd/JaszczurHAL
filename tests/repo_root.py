"""Resolve the repository root handed to a test script."""

from __future__ import annotations

from pathlib import Path
from typing import Sequence


def repo_root(argv: Sequence[str], script: str) -> Path:
    """Return the JaszczurHAL root from argv[1], or the script's parent.

    A wrong first argument, such as the module name passed by
    ``python3 -m unittest tests.test_x``, stops the test before it writes
    build output into a directory named after that argument.
    """
    script_path = Path(script).resolve()
    root = Path(argv[1]).resolve() if len(argv) > 1 else script_path.parents[1]
    if not (root / "src" / "hal").is_dir() or not (root / "CMakeLists.txt").is_file():
        raise SystemExit(
            f"{script_path.name}: {root} is not the JaszczurHAL root; run "
            f"python3 tests/{script_path.name} <repository root>"
        )
    return root
