#!/usr/bin/env python3
"""Validate local links in maintained documentation."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from urllib.parse import unquote


DOCUMENTATION_GLOBS = (
    "*.md",
    "boards/**/*.md",
    "doc/**/*.md",
    "doc/**/*.txt",
    "examples/**/*.md",
    "rp_native_lib/**/*.md",
    "security/**/*.md",
    "src/**/*.md",
    "stm32_lib/**/*.md",
    "tests/hardware/**/*.md",
    "vscode/**/*.md",
)

LINK_RE = re.compile(r"!?\[[^\]]*]\(([^)\n]+)\)")
HEADING_RE = re.compile(r"^\s{0,3}(#{1,6})\s+(.+?)\s*#*\s*$")
EXPLICIT_ANCHOR_RE = re.compile(
    r"<(?:a|span)\s+(?:[^>]*?\s)?(?:id|name)=[\"']([^\"']+)[\"']",
    re.IGNORECASE,
)


def documentation_files(root: Path) -> list[Path]:
    files: set[Path] = set()
    for pattern in DOCUMENTATION_GLOBS:
        files.update(path for path in root.glob(pattern) if path.is_file())
    return sorted(files)


def split_link_target(raw_target: str) -> tuple[str, str]:
    target = raw_target.strip()
    if target.startswith("<"):
        closing = target.find(">")
        if closing >= 0:
            target = target[1:closing]
    else:
        target = target.split(maxsplit=1)[0]
    target = unquote(target)
    path_text, separator, anchor = target.partition("#")
    return path_text, anchor if separator else ""


def github_slug(text: str) -> str:
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"!\[[^\]]*]\([^)]*\)", "", text)
    text = re.sub(r"\[([^\]]+)]\([^)]*\)", r"\1", text)
    text = re.sub(r"[`*_~]", "", text)
    text = text.strip().lower()
    text = re.sub(r"[^\w\-\s]", "", text, flags=re.UNICODE)
    return re.sub(r"[\s-]+", "-", text).strip("-")


def markdown_anchors(path: Path) -> set[str]:
    anchors: set[str] = set()
    slug_counts: dict[str, int] = {}
    in_fence = False

    for line in path.read_text(encoding="utf-8").splitlines():
        if re.match(r"^\s*(```|~~~)", line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        anchors.update(match.group(1) for match in EXPLICIT_ANCHOR_RE.finditer(line))
        match = HEADING_RE.match(line)
        if not match:
            continue
        base = github_slug(match.group(2))
        if not base:
            continue
        occurrence = slug_counts.get(base, 0)
        slug_counts[base] = occurrence + 1
        anchors.add(base if occurrence == 0 else f"{base}-{occurrence}")

    return anchors


def check_links(root: Path) -> list[str]:
    failures: list[str] = []
    anchor_cache: dict[Path, set[str]] = {}

    for path in documentation_files(root):
        relative = path.relative_to(root)
        text = path.read_text(encoding="utf-8")
        if path.suffix.lower() != ".md":
            continue
        for match in LINK_RE.finditer(text):
            raw_target = match.group(1)
            path_text, anchor = split_link_target(raw_target)
            if re.match(r"^[a-z][a-z0-9+.-]*:", path_text, re.IGNORECASE):
                continue
            if path_text.startswith("//"):
                continue

            target = path if not path_text else (path.parent / path_text).resolve()
            try:
                target.relative_to(root)
            except ValueError:
                failures.append(
                    f"{relative}: local link escapes the repository: {raw_target!r}"
                )
                continue
            if not target.exists():
                failures.append(
                    f"{relative}: missing local link target: {raw_target!r}"
                )
                continue
            if not anchor or target.suffix.lower() != ".md":
                continue
            anchors = anchor_cache.setdefault(target, markdown_anchors(target))
            if anchor not in anchors:
                failures.append(
                    f"{relative}: missing anchor #{anchor} in "
                    f"{target.relative_to(root)}"
                )

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "root",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="JaszczurHAL repository root",
    )
    args = parser.parse_args()
    root = args.root.resolve()

    failures = check_links(root)
    if failures:
        print("Documentation link check failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(
        f"Documentation link check passed "
        f"({len(documentation_files(root))} files)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
