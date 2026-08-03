#!/usr/bin/env python3
"""Compatibility entrypoint for the shared persistent serial monitor."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[3]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from vscode.runtime.monitor.core import run


if __name__ == "__main__":
    raise SystemExit(run())
