#!/usr/bin/env python3
"""Compatibility entrypoint for the shared JaszczurHAL runtime."""

from __future__ import annotations

from pathlib import Path
import sys
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from vscode.runtime import jh_vscode as _shared


EXIT_GENERIC = _shared.EXIT_GENERIC
main = _shared.main


def __getattr__(name: str) -> Any:
    return getattr(_shared, name)


def __dir__() -> list[str]:
    return sorted(set(globals()) | set(dir(_shared)))


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        raise SystemExit(EXIT_GENERIC)
