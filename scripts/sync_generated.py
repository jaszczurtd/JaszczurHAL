#!/usr/bin/env python3
"""Synchronize or verify every tracked generated artifact in the repository."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]

# The launcher of the jh host CLI of this checkout: it builds the tool first
# when its inputs changed (scripts/jh_bootstrap.cmake). subprocess cannot run
# a .ps1 file itself, so Windows goes through powershell.exe.
JH_LAUNCHER: tuple[str, ...] = (
    ("powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
     str(REPO_ROOT / "scripts" / "jh.ps1"))
    if os.name == "nt"
    else (str(REPO_ROOT / "scripts" / "jh"),)
)


@dataclass(frozen=True)
class GeneratorStep:
    """Commands used to write and verify one group of generated files.

    Python scripts of this repository run through the current interpreter;
    a step of the jh host CLI runs its command words through JH_LAUNCHER.
    """

    name: str
    write_commands: tuple[tuple[str, ...], ...]
    check_commands: tuple[tuple[str, ...], ...]
    host_cli: bool = False


GENERATOR_STEPS = (
    GeneratorStep(
        "HAL feature registry",
        (("scripts/generate_hal_features.py", "--write"),),
        (("scripts/generate_hal_features.py", "--check"),),
    ),
    GeneratorStep(
        "static board registry",
        ((
            "scripts/generate_board_config.py",
            "--boards-root",
            "boards",
            "--write-static",
        ),),
        ((
            "scripts/generate_board_config.py",
            "--boards-root",
            "boards",
            "--check-static",
        ),),
    ),
    GeneratorStep(
        "device type defaults",
        (("generate", "device-defaults", "--root", str(REPO_ROOT), "--write"),),
        (("generate", "device-defaults", "--root", str(REPO_ROOT), "--check"),),
        host_cli=True,
    ),
    GeneratorStep(
        "example VS Code files",
        (("scripts/examples_dispatcher.py", "generate"),),
        (("scripts/examples_dispatcher.py", "check"),),
    ),
    GeneratorStep(
        "root VS Code files",
        (("scripts/vscode_library_workspace.py", "sync-vscode"),),
        (("scripts/vscode_library_workspace.py", "sync-vscode", "--check"),),
    ),
    GeneratorStep(
        "repository SBOM",
        (("scripts/generate_sbom.py",),),
        (("scripts/generate_sbom.py", "--check"),),
    ),
)


class SyncError(RuntimeError):
    """Expected synchronization failure with actionable context."""


def repository_files(root: Path) -> list[Path]:
    """Return tracked and non-ignored untracked files known to Git."""
    git = shutil.which("git")
    if git is None:
        raise SyncError("cannot enumerate repository files: Git is not available")
    result = subprocess.run(
        (*JH_LAUNCHER, "repository", "files", "--root", str(root),
         "--git", str(Path(git).resolve())),
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise SyncError(f"cannot enumerate repository files: {detail}")
    return [
        root / Path(raw.decode("utf-8", errors="surrogateescape"))
        for raw in result.stdout.split(b"\0")
        if raw
    ]


def file_fingerprint(path: Path) -> tuple[int, str] | None:
    """Return a stable mode/content fingerprint, or None for non-files."""
    try:
        metadata = path.lstat()
        if stat.S_ISLNK(metadata.st_mode):
            content = path.readlink().as_posix().encode("utf-8")
        elif stat.S_ISREG(metadata.st_mode):
            content = path.read_bytes()
        else:
            return None
    except FileNotFoundError:
        return None
    return stat.S_IMODE(metadata.st_mode), hashlib.sha256(content).hexdigest()


# No generator writes into device-test fixtures, and the default checks never
# read them (AGENTS.md), so their files stay out of the snapshot.
HARDWARE_FIXTURES = Path("tests") / "hardware"


def repository_state(root: Path) -> dict[Path, tuple[int, str]]:
    """Snapshot repository file modes and contents, without tests/hardware."""
    state: dict[Path, tuple[int, str]] = {}
    for path in repository_files(root):
        if path.relative_to(root).parts[:2] == HARDWARE_FIXTURES.parts:
            continue
        fingerprint = file_fingerprint(path)
        if fingerprint is not None:
            state[path.relative_to(root)] = fingerprint
    return state


def changed_paths(
    before: dict[Path, tuple[int, str]],
    after: dict[Path, tuple[int, str]],
) -> list[Path]:
    """Return sorted paths added, removed, or changed between snapshots."""
    return sorted(
        path
        for path in before.keys() | after.keys()
        if before.get(path) != after.get(path)
    )


def render_report(mode: str, changes: Iterable[Path]) -> str:
    """Render the stable end-of-run generated-artifact report."""
    paths = list(changes)
    lines = [f"Mode: {mode}", f"Generated artifacts changed: {len(paths)}"]
    lines.extend(f"  - {path.as_posix()}" for path in paths)
    if not paths:
        lines.append("  (none)")
    return "\n".join(lines) + "\n"


def write_report(path: Path | None, report: str) -> None:
    if path is None:
        return
    destination = path if path.is_absolute() else REPO_ROOT / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(report, encoding="utf-8", newline="\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Synchronize or verify every tracked generated artifact."
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--write", action="store_true", help="refresh generated files"
    )
    mode.add_argument(
        "--check", action="store_true", help="verify without changing files"
    )
    parser.add_argument(
        "--report-file",
        type=Path,
        help="also write the final changed-artifact list to this path",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    mode = "check" if args.check else "write"
    before = repository_state(REPO_ROOT)
    failure: tuple[tuple[str, ...], int] | None = None

    for step in GENERATOR_STEPS:
        print(f"\n== {step.name} ==", flush=True)
        commands = step.check_commands if args.check else step.write_commands
        for command in commands:
            if step.host_cli:
                argv = (*JH_LAUNCHER, *command)
                printable = " ".join(("scripts/jh", *command))
            else:
                argv = (sys.executable, *command)
                printable = " ".join(("python3", *command))
            print(f"$ {printable}", flush=True)
            result = subprocess.run(argv, cwd=REPO_ROOT, check=False)
            if result.returncode != 0:
                failure = command, result.returncode
                break
        if failure is not None:
            break

    after = repository_state(REPO_ROOT)
    changes = changed_paths(before, after)
    report = render_report(mode, changes)
    print(f"\n{report}", end="")
    write_report(args.report_file, report)

    if args.check and changes:
        print("error: check mode changed repository files", file=sys.stderr)
        return 2
    if failure is not None:
        command, returncode = failure
        print(
            f"error: generator failed with exit code {returncode}: "
            f"{' '.join(command)}",
            file=sys.stderr,
        )
        return returncode
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, SyncError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2) from error
