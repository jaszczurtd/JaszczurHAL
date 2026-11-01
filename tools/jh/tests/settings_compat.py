#!/usr/bin/env python3
"""jh::read_settings against load_json_file of the Python HAL tools.

Usage: settings_compat.py <settings_probe> <JaszczurHAL root> [<root>...]

Reads every JSON file of a .vscode directory below the roots, skipping
dependency and build trees and the device tests of tests/hardware, and the
edge cases of load_json_file in a temporary directory. Both readers must reach
the same verdict as the unchanged load_json_file, and for an accepted file the
same model: member order, strings and the original number tokens, which a
second Python read keeps through the parse hooks of json.loads. The accepted
differences are checked one by one.
"""

from __future__ import annotations

import functools
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

PROBE = Path(sys.argv[1])
ROOT = Path(sys.argv[2]).resolve()
sys.path.insert(0, str(ROOT))
from vscode.runtime import jh_vscode  # noqa: E402

SKIP = {".build", ".git", "build", "third_party", "node_modules", "reference_sources", "esp-idf"}
TOKENS = {name: lambda text: ("number", text) for name in ("parse_int", "parse_float", "parse_constant")}


def model(value: object) -> object:
    if isinstance(value, dict):
        return ("object", [(key, model(item)) for key, item in value.items()])
    if isinstance(value, list):
        return ("array", [model(item) for item in value])
    return value


def python_side(path: Path) -> tuple:
    try:
        jh_vscode.load_json_file(path)
    except OSError:
        return ("file",)
    except ValueError as error:  # UnicodeDecodeError is one too.
        return ("type",) if "must contain a JSON object" in str(error) else ("invalid",)
    # The hooks keep the number tokens and reject nothing the plain read took.
    with mock.patch.object(jh_vscode.json, "loads", functools.partial(json.loads, **TOKENS)):
        return ("ok", model(jh_vscode.load_json_file(path)))


def probe_side(path: Path) -> tuple:
    result = subprocess.run([str(PROBE), str(path)], capture_output=True, check=True, timeout=60)
    output = result.stdout.decode("utf-8")
    if not output.startswith("ERROR "):
        return ("ok", model(json.loads(output, **TOKENS)))
    status, exit_code = output.split()[1:3]
    if exit_code == "3":
        return ("file",)
    if exit_code == "4":
        return ("limit",)
    return {"HAL_ETYPE": ("type",), "HAL_EPROTO": ("invalid",)}.get(status, ("unexpected", output.strip()))


def vscode_files(root: Path):
    hardware = root / "tests" / "hardware"
    for directory, names, entries in os.walk(root):
        names[:] = sorted(name for name in names if name not in SKIP and Path(directory, name) != hardware)
        if Path(directory).name == ".vscode":
            yield from (Path(directory, entry) for entry in sorted(entries) if entry.endswith(".json"))


CASES = {
    "array.json": b"[1]",
    "number.json": b"1",
    "empty.json": b"",
    "jsonc.json": b'{"a": 1, // line\r\n "a": [NaN, -Infinity, 1.50e3, -0],\r'
                  b' /* block */ "b": "\\u017c\xc5\xbc", "c": {"d": true, "d": null}}',
    "unterminated.json": b'{"a": {}} /* open',
    "bom.json": b"\xef\xbb\xbf{}",
    "utf8.json": b'{"a": "\xff"}',
    "invalid.json": b'{"a": }',
}

# Accepted differences with the verdicts of Python and jh. jhbase rejects a \u
# escape of an unpaired surrogate, and jh limits nesting to 256 containers.
# int() of Python 3.11+ refuses an integer above sys.get_int_max_str_digits()
# digits (4300 by default, none for 0), which jh keeps as a token.
DIGITS = getattr(sys, "get_int_max_str_digits", lambda: 0)()
DIFFERENCES = {
    "surrogate.json": (b'{"a": "\\ud800"}', "ok", "invalid"),
    "nesting.json": (b'{"a": ' + b"[" * 256 + b"]" * 256 + b"}", "ok", "limit"),
    "integer.json": (b'{"a": ' + b"7" * (max(DIGITS, 4300) + 1) + b"}", "invalid" if DIGITS else "ok", "ok"),
}

mismatches: list[str] = []
verdicts: set[str] = set()


def check(path: Path) -> None:
    expected, actual = python_side(path), probe_side(path)
    verdicts.add(expected[0])
    if expected != actual:
        mismatches.append(f"{path}: python {expected!r}, jh {actual!r}")


files = manifests = 0
for root in [ROOT] + [Path(extra).resolve() for extra in sys.argv[3:]]:
    for path in vscode_files(root):
        files += 1
        manifests += path.name == "jaszczurhal.project.json"
        check(path)
if manifests == 0:
    sys.exit(f"no jaszczurhal.project.json below {ROOT}")
with tempfile.TemporaryDirectory() as temporary:
    work = Path(temporary)
    for name, content in CASES.items():
        (work / name).write_bytes(content)
        check(work / name)
    check(work / "missing.json")
    (work / "directory.json").mkdir()
    check(work / "directory.json")
    for name, (content, python, jh) in DIFFERENCES.items():
        (work / name).write_bytes(content)
        verdict = (python_side(work / name)[0], probe_side(work / name)[0])
        if verdict != (python, jh):
            mismatches.append(f"{name}: python {verdict[0]}, jh {verdict[1]}; expected {python}, {jh}")
    # The walk leaves a planted tests/hardware tree unread.
    planted = work / "planted"
    for directory in ("tests/hardware/device/.vscode", "examples/app/.vscode"):
        (planted / directory).mkdir(parents=True)
        (planted / directory / "settings.json").write_bytes(b"{}")
    walked = [path.relative_to(planted).as_posix() for path in vscode_files(planted)]
    if walked != ["examples/app/.vscode/settings.json"]:
        mismatches.append(f"the walk of a planted tree read {walked}")
if verdicts != {"ok", "type", "invalid", "file"}:
    mismatches.append(f"edge cases reached only {sorted(verdicts)}")
for line in mismatches:
    print(line, file=sys.stderr)
print(f"settings compatibility: {files} files ({manifests} manifests), {len(CASES) + 2} edge cases, "
      f"{len(DIFFERENCES)} accepted differences, {len(mismatches)} mismatches")
sys.exit(1 if mismatches else 0)
