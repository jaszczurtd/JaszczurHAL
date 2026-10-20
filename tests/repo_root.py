"""Resolve the repository root handed to a test script."""

from __future__ import annotations

import sys
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


def hal_include_dirs(root: Path) -> list[Path]:
    """Include directories every HAL header needs: ``src`` and the jhbase
    headers behind hal_status.h and hal_array.h, in the checkout the
    component manager selects (JH_JHBASE_DIR or the pinned third_party/jhbase).
    """
    scripts = str(root / "scripts")
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    from component_manager import jhbase_root

    return [root / "src", jhbase_root(root) / "include"]


def hal_include_args(root: Path) -> list[str]:
    """``-I`` arguments for a compiler call that includes HAL headers."""
    return [argument for path in hal_include_dirs(root) for argument in ("-I", str(path))]
