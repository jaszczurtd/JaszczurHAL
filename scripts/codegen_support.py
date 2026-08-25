"""Shared validation and generated-artifact helpers for repository scripts."""

from __future__ import annotations

import difflib
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
from typing import Any, Never


def validation_error(
    error_type: type[Exception],
    path: Path,
    json_path: str,
    value: Any,
    expected: str,
) -> Never:
    """Raise a generator-specific error for an invalid structured value."""
    raise error_type(
        f"{path}: {json_path}: got {value!r}; expected {expected}"
    )


def load_json_object(
    path: Path, *, error_type: type[Exception]
) -> dict[str, Any]:
    """Load a JSON document and require an object at its root."""
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise error_type(f"{path}: invalid JSON: {error}") from error
    if not isinstance(value, dict):
        validation_error(error_type, path, "$", value, "an object")
    return value


def require_exact_fields(
    path: Path,
    json_path: str,
    value: Any,
    required: set[str],
    allowed: set[str],
    *,
    error_type: type[Exception],
) -> dict[str, Any]:
    """Validate an object's required and allowed field names."""
    if not isinstance(value, dict):
        validation_error(error_type, path, json_path, value, "an object")
    missing = sorted(required - value.keys())
    unknown = sorted(value.keys() - allowed)
    if missing:
        validation_error(
            error_type,
            path,
            json_path,
            missing,
            f"required fields {sorted(required)}",
        )
    if unknown:
        validation_error(
            error_type,
            path,
            json_path,
            unknown,
            f"only fields {sorted(allowed)}",
        )
    return value


def atomic_write_text(
    path: Path,
    content: str,
    *,
    error_type: type[Exception],
    mode: int | None = None,
) -> bool:
    """Atomically replace text when content or the requested mode changed."""
    try:
        if path.exists() and path.read_text(encoding="utf-8") == content:
            if mode is None or os.name == "nt":
                return False
            if stat.S_IMODE(path.stat().st_mode) == mode:
                return False
            path.chmod(mode)
            return True

        path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", dir=path.parent, text=True
        )
        try:
            with os.fdopen(
                descriptor, "w", encoding="utf-8", newline="\n"
            ) as stream:
                stream.write(content)
            if mode is not None and os.name != "nt":
                os.chmod(temporary_name, mode)
            os.replace(temporary_name, path)
        finally:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass
    except OSError as error:
        raise error_type(
            f"{path}: cannot write generated output: {error}"
        ) from error
    return True


def write_generated_outputs(
    output_root: Path,
    outputs: dict[Path, str],
    *,
    artifact_kind: str,
    error_type: type[Exception],
    mode: int | None = None,
) -> None:
    """Write generated artifacts and report how many files changed."""
    changed = sum(
        atomic_write_text(
            output_root / relative_path,
            content,
            error_type=error_type,
            mode=mode,
        )
        for relative_path, content in outputs.items()
    )
    print(f"generated {len(outputs)} {artifact_kind} artifacts ({changed} changed)")


def check_generated_outputs(
    output_root: Path,
    outputs: dict[Path, str],
    *,
    artifact_kind: str,
    error_type: type[Exception],
    diff_limit: int,
) -> bool:
    """Check generated artifacts and print bounded unified diffs on drift."""
    valid = True
    for relative_path, expected in outputs.items():
        path = output_root / relative_path
        try:
            actual = path.read_text(encoding="utf-8")
        except FileNotFoundError:
            print(f"error: missing generated artifact {path}", file=sys.stderr)
            valid = False
            continue
        except OSError as error:
            raise error_type(
                f"{path}: cannot read generated output: {error}"
            ) from error
        if actual == expected:
            continue
        valid = False
        print(f"error: stale generated artifact {path}", file=sys.stderr)
        diff = difflib.unified_diff(
            actual.splitlines(),
            expected.splitlines(),
            fromfile=str(path),
            tofile=f"generated:{relative_path}",
            lineterm="",
        )
        for index, line in enumerate(diff):
            if index == diff_limit:
                print("... diff truncated ...", file=sys.stderr)
                break
            print(line, file=sys.stderr)
    if valid:
        print(f"verified {len(outputs)} generated {artifact_kind} artifacts")
    return valid
