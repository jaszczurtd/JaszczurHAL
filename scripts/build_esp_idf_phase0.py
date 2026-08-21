#!/usr/bin/env python3
"""Compatibility wrapper for the former ESP-IDF Phase 0 build command."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
from typing import Sequence

import build_esp_idf


Phase0Error = build_esp_idf.EspIdfError


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--idf-dir", default="")
    parser.add_argument("--target", default=build_esp_idf.DEFAULT_TARGET)
    parser.add_argument("--board", default=build_esp_idf.DEFAULT_BOARD)
    parser.add_argument("--output", default="")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--artifacts-only", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = create_parser().parse_args(argv)
    repo_root = arguments.repo_root.expanduser().resolve()
    action = "artifacts" if arguments.artifacts_only else "build"
    output = arguments.output or str(
        repo_root / ".build/esp-idf/phase0" / arguments.target
    )
    forwarded = [
        action,
        "--repo-root",
        str(repo_root),
        "--project",
        str(repo_root / "tests/fixtures/esp_idf_phase0"),
        "--source",
        "main/phase0_main.c",
        "--name",
        "jh_esp_idf_phase0",
        "--target",
        arguments.target,
        "--board",
        arguments.board,
        "--output",
        output,
    ]
    if arguments.idf_dir:
        forwarded.extend(("--idf-dir", arguments.idf_dir))
    if arguments.clean:
        forwarded.append("--clean")
    return build_esp_idf.main(forwarded)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
