#!/usr/bin/env python3
"""Problems of a project's device_tree.json, placed at a line and column.

Every assembly is resolved the way a build resolves it, so the editor shows
the same errors the build would stop on. The resolver reports the first error
of each assembly as a JSON Pointer; this module finds the member the pointer
names in the file text. An error shared by several assemblies is reported
once, with all of their names.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any

DEVICE_TREE = "device_tree.json"


@dataclass(frozen=True)
class Problem:
    path: Path
    line: int
    column: int
    code: str
    message: str
    assemblies: tuple[str, ...] = ()


class _Members:
    """Offsets of the values of one JSON text by JSON Pointer; a member is
    found at its key. Scanning stops quietly at the first syntax error."""

    def __init__(self, text: str) -> None:
        self.text = text
        self.index = 0
        self.found: dict[str, int] = {}
        try:
            self.value("", None)
        except (IndexError, ValueError):
            pass

    def space(self) -> None:
        while self.index < len(self.text) and self.text[self.index] in " \t\r\n":
            self.index += 1

    def expect(self, character: str) -> None:
        self.space()
        if self.text[self.index] != character:
            raise ValueError(character)
        self.index += 1

    def string(self) -> str:
        self.expect('"')
        value, self.index = json.decoder.scanstring(self.text, self.index)
        return value

    def value(self, pointer: str, anchor: int | None) -> None:
        self.space()
        self.found.setdefault(pointer, self.index if anchor is None else anchor)
        opening = self.text[self.index]
        if opening in "{[":
            closing = "}" if opening == "{" else "]"
            self.index += 1
            self.space()
            if self.text[self.index] == closing:
                self.index += 1
                return
            position = 0
            while True:
                if opening == "{":
                    self.space()
                    key_offset = self.index
                    key = self.string()
                    self.expect(":")
                    escaped = key.replace("~", "~0").replace("/", "~1")
                    self.value(f"{pointer}/{escaped}", key_offset)
                else:
                    self.value(f"{pointer}/{position}", None)
                    position += 1
                self.space()
                if self.text[self.index] == ",":
                    self.index += 1
                    continue
                self.expect(closing)
                return
        if opening == '"':
            self.string()
            return
        while self.index < len(self.text) and self.text[self.index] not in ",]} \t\r\n":
            self.index += 1


def locate(text: str, pointer: str) -> tuple[int, int]:
    """1-based line and column of the member ``pointer`` names; a pointer
    the text lacks falls back to its nearest existing parent."""
    found = _Members(text).found
    while pointer and pointer not in found:
        pointer = pointer.rsplit("/", 1)[0]
    offset = found.get(pointer, 0)
    line_start = text.rfind("\n", 0, offset) + 1
    return text.count("\n", 0, offset) + 1, offset - line_start + 1


def _problem(error: Exception, fallback: Path, assemblies: tuple[str, ...] = ()) -> Problem:
    from hardware_model import HardwareError

    if not isinstance(error, HardwareError):
        return Problem(fallback, 1, 1, "JH-HW", str(error), assemblies)
    path = Path(error.source)
    detail = error.detail
    if error.related:
        detail += f" (earlier: {', '.join(error.related)})"
    syntax = error.__cause__
    if isinstance(syntax, json.JSONDecodeError):
        return Problem(path, syntax.lineno, syntax.colno, error.code, syntax.msg, assemblies)
    if error.line is not None:
        return Problem(path, error.line, 1, error.code, detail, assemblies)
    try:
        line, column = locate(path.read_text(encoding="utf-8"), error.pointer)
    except (OSError, UnicodeDecodeError):
        line, column = 1, 1
    return Problem(path, line, column, error.code, detail, assemblies)


def problems(config_dir: Path) -> list[Problem]:
    """The problems of ``config_dir``/device_tree.json: one for a file that
    does not load, else the first error of each assembly."""
    import hardware_resolver
    from hardware_model import HardwareError

    path = Path(config_dir) / DEVICE_TREE
    if not path.is_file():
        return [Problem(path, 1, 1, "JH-HW-SCHEMA", f"{DEVICE_TREE} does not exist")]
    try:
        data = hardware_resolver.load_device_tree(path)
        registry = hardware_resolver.load_registry()
    except (HardwareError, ValueError) as error:
        return [_problem(error, path)]
    found: dict[tuple, tuple[Exception, list[str]]] = {}
    for name in sorted(data["assemblies"]):
        try:
            hardware_resolver.Resolver(path, registry).resolve(assembly=name)
        except (HardwareError, ValueError) as error:
            key = (getattr(error, "source", ""), getattr(error, "pointer", ""),
                   getattr(error, "code", ""), str(error))
            found.setdefault(key, (error, []))[1].append(name)
    return [_problem(error, path, tuple(names)) for error, names in found.values()]


def watched_files(config_dir: Path) -> list[Path]:
    """device_tree.json and the project bindings it lists: the files whose
    edits change the result."""
    path = Path(config_dir) / DEVICE_TREE
    files = [path]
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return files
    bindings = data.get("bindings") if isinstance(data, dict) else None
    for item in bindings if isinstance(bindings, list) else []:
        if isinstance(item, str):
            files.append(Path(config_dir) / item)
    return files


def format_problem(problem: Problem, config_dir: Path) -> str:
    """``<file>:<line>:<column>: error: [<code>] <message>``; a file inside
    the project is named relative to it, with forward slashes."""
    try:
        shown: Any = problem.path.resolve().relative_to(Path(config_dir).resolve()).as_posix()
    except ValueError:
        shown = problem.path
    where = ""
    if problem.assemblies:
        label = "assembly" if len(problem.assemblies) == 1 else "assemblies"
        where = f" ({label} {', '.join(problem.assemblies)})"
    return f"{shown}:{problem.line}:{problem.column}: error: [{problem.code}] {problem.message}{where}"
