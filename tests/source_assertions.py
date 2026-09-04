"""Formatting-independent helpers for structural source checks."""

from __future__ import annotations

import re


_SOURCE_FRAGMENT_TOKEN = re.compile(
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|\d+(?:\.\d+)?|[^\s]'
)


def source_fragment_pattern(fragment: str) -> re.Pattern[str]:
    """Build a pattern that preserves tokens but accepts formatting whitespace."""
    tokens = _SOURCE_FRAGMENT_TOKEN.findall(fragment)
    if not tokens:
        raise ValueError("source fragment must not be empty")
    return re.compile(r"\s*".join(re.escape(token) for token in tokens))


def source_has_fragment(source: str, fragment: str) -> bool:
    """Match code structure without depending on formatter line wrapping."""
    return source_fragment_pattern(fragment).search(source) is not None


def source_fragment_position(source: str, fragment: str, start: int = 0) -> int:
    """Return the position of a structural fragment or raise AssertionError."""
    match = source_fragment_pattern(fragment).search(source, start)
    if match is None:
        raise AssertionError(f"source is missing {fragment}")
    return match.start()


def source_section(source: str, start: str, end: str) -> str:
    """Return the source section between formatting-independent markers."""
    start_match = source_fragment_pattern(start).search(source)
    if start_match is None:
        raise AssertionError(f"source section is missing {start}")
    end_match = source_fragment_pattern(end).search(source, start_match.end())
    if end_match is None:
        raise AssertionError(f"source section is missing {end}")
    return source[start_match.end() : end_match.start()]
