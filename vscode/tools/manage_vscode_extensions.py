#!/usr/bin/env python3
"""Check or install the VS Code extensions recommended by JaszczurHAL."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Any, Callable


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPTS_DIR = REPOSITORY_ROOT / "scripts"
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from vscode_task_config import VSCODE_EXTENSION_RECOMMENDATIONS


Runner = Callable[..., Any]


def resolve_code(value: str) -> str | None:
    candidate = Path(value).expanduser()
    is_path = candidate.is_absolute() or "/" in value or "\\" in value
    if is_path:
        return str(candidate.resolve()) if candidate.is_file() else None
    return shutil.which(value)


def code_command(executable: str, args: list[str]) -> list[str]:
    if os.name == "nt" and Path(executable).suffix.lower() in {".bat", ".cmd"}:
        return [
            os.environ.get("COMSPEC", "cmd.exe"),
            "/d",
            "/c",
            "call",
            executable,
            *args,
        ]
    return [executable, *args]


def run_code(executable: str, args: list[str], runner: Runner) -> Any:
    return runner(
        code_command(executable, args),
        check=False,
        capture_output=True,
        text=True,
    )


def installed_extensions(executable: str, runner: Runner) -> set[str]:
    result = run_code(executable, ["--list-extensions"], runner)
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "unknown error").strip()
        raise RuntimeError(f"code --list-extensions failed: {detail}")
    return {
        line.strip().lower()
        for line in result.stdout.splitlines()
        if line.strip()
    }


def missing_extensions(installed: set[str]) -> list[str]:
    return [
        extension
        for extension in VSCODE_EXTENSION_RECOMMENDATIONS
        if extension.lower() not in installed
    ]


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--code",
        default=os.environ.get("JH_VSCODE_CODE", "code"),
        help="VS Code command or path, default: code.",
    )
    parser.add_argument(
        "--install",
        action="store_true",
        help="Install missing recommendations after confirmation.",
    )
    parser.add_argument(
        "--yes",
        action="store_true",
        help="Confirm installation non-interactively; requires --install.",
    )
    return parser.parse_args(argv)


def main(
    argv: list[str],
    *,
    runner: Runner = subprocess.run,
    input_fn: Callable[[str], str] = input,
) -> int:
    args = parse_args(argv)
    if args.yes and not args.install:
        print("error: --yes requires --install", file=sys.stderr)
        return 2

    executable = resolve_code(args.code)
    if executable is None:
        print(f"error: VS Code command not found: {args.code}", file=sys.stderr)
        return 1

    try:
        missing = missing_extensions(installed_extensions(executable, runner))
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not missing:
        print("All recommended VS Code extensions are installed.")
        return 0

    print("Missing VS Code extensions:")
    for extension in missing:
        print(f"  {extension}")
    if not args.install:
        print("Run again with --install to install them.")
        return 1

    if not args.yes:
        answer = input_fn("Install these extensions in the current VS Code profile? [y/N] ")
        if answer.strip().lower() not in {"y", "yes"}:
            print("Installation cancelled.")
            return 1

    for extension in missing:
        result = run_code(executable, ["--install-extension", extension], runner)
        if result.returncode != 0:
            detail = (result.stderr or result.stdout or "unknown error").strip()
            print(f"error: failed to install {extension}: {detail}", file=sys.stderr)
            return 1

    try:
        remaining = missing_extensions(installed_extensions(executable, runner))
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if remaining:
        print(
            "error: VS Code did not report installed extensions: " + ", ".join(remaining),
            file=sys.stderr,
        )
        return 1
    print("Recommended VS Code extensions installed and verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
