#!/usr/bin/env python3
"""Configure Cortex-Debug from the verified Windows host environment."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
from typing import Callable


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_HOST_ENVIRONMENT = (
    REPOSITORY_ROOT / ".build" / "windows" / "host-environment.json"
)
SETTING_OPENOCD = "cortex-debug.openocdPath.windows"
SETTING_ARM_TOOLCHAIN = "cortex-debug.armToolchainPath.windows"


class JsoncError(ValueError):
    """Raised when a VS Code settings object cannot be updated safely."""


@dataclass(frozen=True)
class JsoncProperty:
    key: str
    value_start: int
    value_end: int
    key_start: int


def default_settings_path() -> Path:
    appdata = os.environ.get("APPDATA")
    if not appdata:
        raise RuntimeError("APPDATA is not set; pass --settings explicitly")
    return Path(appdata) / "Code" / "User" / "settings.json"


def _skip_space_and_comments(text: str, index: int) -> int:
    while index < len(text):
        if text[index].isspace():
            index += 1
            continue
        if text.startswith("//", index):
            newline = text.find("\n", index + 2)
            if newline < 0:
                return len(text)
            return _skip_space_and_comments(text, newline + 1)
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                raise JsoncError("unterminated block comment")
            index = end + 2
            continue
        return index
    return index


def _string_end(text: str, index: int) -> int:
    if index >= len(text) or text[index] != '"':
        raise JsoncError("expected a JSON string")
    index += 1
    while index < len(text):
        if text[index] == "\\":
            index += 2
            continue
        if text[index] == '"':
            return index + 1
        index += 1
    raise JsoncError("unterminated JSON string")


def _composite_end(text: str, index: int) -> int:
    pairs = {"{": "}", "[": "]"}
    if index >= len(text) or text[index] not in pairs:
        raise JsoncError("expected an object or array")
    stack = [pairs[text[index]]]
    index += 1
    while index < len(text):
        if text[index] == '"':
            index = _string_end(text, index)
            continue
        if text.startswith("//", index) or text.startswith("/*", index):
            index = _skip_space_and_comments(text, index)
            continue
        if text[index] in pairs:
            stack.append(pairs[text[index]])
        elif text[index] in "}]":
            if text[index] != stack[-1]:
                raise JsoncError("mismatched object or array delimiter")
            stack.pop()
            if not stack:
                return index + 1
        index += 1
    raise JsoncError("unterminated object or array")


def _value_end(text: str, index: int) -> int:
    index = _skip_space_and_comments(text, index)
    if index >= len(text):
        raise JsoncError("missing property value")
    if text[index] == '"':
        return _string_end(text, index)
    if text[index] in "{[":
        return _composite_end(text, index)
    end = index
    while end < len(text) and not text[end].isspace() and text[end] not in ",}":
        if text.startswith("//", end) or text.startswith("/*", end):
            break
        end += 1
    if end == index:
        raise JsoncError("missing property value")
    return end


def jsonc_properties(text: str) -> tuple[list[JsoncProperty], int]:
    index = _skip_space_and_comments(text, 0)
    if index >= len(text) or text[index] != "{":
        raise JsoncError("VS Code settings must contain a top-level object")
    index += 1
    properties: list[JsoncProperty] = []
    while True:
        index = _skip_space_and_comments(text, index)
        if index >= len(text):
            raise JsoncError("unterminated settings object")
        if text[index] == "}":
            return properties, index
        key_start = index
        key_end = _string_end(text, index)
        try:
            key = json.loads(text[key_start:key_end])
        except json.JSONDecodeError as exc:
            raise JsoncError(f"invalid property name: {exc}") from exc
        index = _skip_space_and_comments(text, key_end)
        if index >= len(text) or text[index] != ":":
            raise JsoncError(f"missing ':' after property {key!r}")
        value_start = _skip_space_and_comments(text, index + 1)
        value_end = _value_end(text, value_start)
        properties.append(JsoncProperty(key, value_start, value_end, key_start))
        index = _skip_space_and_comments(text, value_end)
        if index < len(text) and text[index] == ",":
            index += 1
            continue
        if index < len(text) and text[index] == "}":
            return properties, index
        raise JsoncError(f"missing ',' after property {key!r}")


def setting_values(text: str, keys: set[str] | None = None) -> dict[str, object]:
    values: dict[str, object] = {}
    seen: set[str] = set()
    for prop in jsonc_properties(text)[0]:
        if prop.key in seen:
            raise JsoncError(f"duplicate setting {prop.key!r}")
        seen.add(prop.key)
        if keys is not None and prop.key not in keys:
            continue
        try:
            values[prop.key] = json.loads(text[prop.value_start:prop.value_end])
        except json.JSONDecodeError as exc:
            raise JsoncError(f"invalid value for {prop.key!r}: {exc}") from exc
    return values


def merge_settings(text: str, desired: dict[str, str]) -> str:
    properties, _ = jsonc_properties(text)
    by_key: dict[str, JsoncProperty] = {}
    for prop in properties:
        if prop.key in by_key:
            raise JsoncError(f"duplicate setting {prop.key!r}")
        by_key[prop.key] = prop

    replacements = []
    for key, value in desired.items():
        prop = by_key.get(key)
        if prop is not None:
            replacements.append(
                (prop.value_start, prop.value_end, json.dumps(value, ensure_ascii=False))
            )
    for start, end, value in sorted(replacements, reverse=True):
        text = text[:start] + value + text[end:]

    properties, closing = jsonc_properties(text)
    missing = [key for key in desired if key not in {prop.key for prop in properties}]
    if not missing:
        return text

    newline = "\r\n" if "\r\n" in text else "\n"
    if properties:
        first = properties[0]
        line_start = text.rfind("\n", 0, first.key_start) + 1
        indent = text[line_start:first.key_start]
        if indent.strip():
            indent = "    "
        last = properties[-1]
        after_last = _skip_space_and_comments(text, last.value_end)
        if after_last >= len(text) or text[after_last] != ",":
            text = text[:last.value_end] + "," + text[last.value_end:]
            properties, closing = jsonc_properties(text)
    else:
        close_line_start = text.rfind("\n", 0, closing) + 1
        closing_indent = text[close_line_start:closing]
        indent = closing_indent + "    " if not closing_indent.strip() else "    "

    close_line_start = text.rfind("\n", 0, closing) + 1
    closing_indent = text[close_line_start:closing]
    lines = [
        f"{indent}{json.dumps(key)}: {json.dumps(desired[key], ensure_ascii=False)}"
        for key in missing
    ]
    for index in range(len(lines) - 1):
        lines[index] += ","

    if not closing_indent.strip():
        insertion = newline.join(lines) + newline
        return text[:close_line_start] + insertion + text[close_line_start:]
    insertion = newline + newline.join(lines) + newline
    return text[:closing] + insertion + text[closing:]


def resolved_settings(host_environment: Path) -> dict[str, str]:
    try:
        state = json.loads(host_environment.read_text(encoding="utf-8"))
        tools = state["tools"]
        openocd = Path(tools["openocd"])
        compiler = Path(tools["gnu-arm"])
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        raise RuntimeError(
            f"invalid Windows host environment: {host_environment}"
        ) from exc

    gdb_suffix = ".exe" if compiler.suffix.lower() == ".exe" else ""
    gdb = compiler.with_name(f"arm-none-eabi-gdb{gdb_suffix}")
    if not openocd.is_file():
        raise RuntimeError(f"resolved OpenOCD does not exist: {openocd}")
    if not compiler.is_file():
        raise RuntimeError(f"resolved GNU Arm compiler does not exist: {compiler}")
    if not gdb.is_file():
        raise RuntimeError(f"GNU Arm GDB does not exist beside the compiler: {gdb}")
    return {
        SETTING_OPENOCD: str(openocd.resolve()),
        SETTING_ARM_TOOLCHAIN: str(compiler.parent.resolve()),
    }


def read_settings(path: Path) -> str:
    if not path.exists():
        return "{}\n"
    try:
        return path.read_bytes().decode("utf-8-sig")
    except (OSError, UnicodeDecodeError) as exc:
        raise RuntimeError(f"cannot read VS Code settings: {path}") from exc


def write_settings(path: Path, updated: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        shutil.copy2(path, Path(f"{path}.jaszczurhal.bak"))
    temporary_name = ""
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            newline="",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as output:
            output.write(updated)
            temporary_name = output.name
        os.replace(temporary_name, path)
        temporary_name = ""
    finally:
        if temporary_name:
            Path(temporary_name).unlink(missing_ok=True)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host-environment",
        type=Path,
        default=DEFAULT_HOST_ENVIRONMENT,
        help="Verified host-environment.json written by runmefirst.ps1.",
    )
    parser.add_argument(
        "--settings",
        type=Path,
        help="VS Code user settings.json; defaults to the standard Windows profile.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify without changing settings.",
    )
    parser.add_argument("--yes", action="store_true", help="Confirm the settings update.")
    return parser.parse_args(argv)


def main(argv: list[str], *, input_fn: Callable[[str], str] = input) -> int:
    args = parse_args(argv)
    if args.check and args.yes:
        print("error: --check cannot be combined with --yes", file=sys.stderr)
        return 2
    try:
        settings_path = args.settings or default_settings_path()
        desired = resolved_settings(args.host_environment.resolve())
        original = read_settings(settings_path)
        current = setting_values(original, set(desired))
    except (RuntimeError, JsoncError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    mismatched = {
        key: value for key, value in desired.items() if current.get(key) != value
    }
    if not mismatched:
        print(
            "Cortex-Debug already uses the verified Windows tools "
            f"({settings_path})."
        )
        return 0
    if args.check:
        print(
            "error: VS Code Cortex-Debug settings are missing or stale: "
            + ", ".join(mismatched),
            file=sys.stderr,
        )
        return 1

    print(f"VS Code user settings: {settings_path}")
    for key, value in mismatched.items():
        print(f"  {key} = {value}")
    if not args.yes:
        answer = input_fn("Apply these Cortex-Debug settings? [y/N] ")
        if answer.strip().lower() not in {"y", "yes"}:
            print("Cortex-Debug configuration cancelled.")
            return 1
    try:
        updated = merge_settings(original, desired)
        write_settings(settings_path, updated)
        verified = setting_values(read_settings(settings_path), set(desired))
    except (OSError, RuntimeError, JsoncError) as exc:
        print(f"error: failed to update VS Code settings: {exc}", file=sys.stderr)
        return 1
    if any(verified.get(key) != value for key, value in desired.items()):
        print("error: updated Cortex-Debug settings could not be verified", file=sys.stderr)
        return 1
    print(
        "Cortex-Debug configured for the verified Windows OpenOCD and GNU Arm tools."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
