#!/usr/bin/env python3
"""Cross-platform manager for pinned JaszczurHAL components and host tools."""

from __future__ import annotations

import argparse
import ast
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import platform
import posixpath
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
from typing import Callable, Iterable, Optional, Sequence
import urllib.request
import urllib.parse
import uuid
import zipfile

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from repository_layout import (
    ARCHIVE_EXCLUSIONS_FILE,
    ARCHIVE_MANIFEST_FILE,
    ARCHIVE_PIN_FILE,
    COMPONENT_VERSION_STAMP,
)
from tooling_contract import (
    ToolingContractError,
    load_tooling_contract,
    require_list,
    require_object,
    require_string,
    require_string_list,
)

PIN_FILE = ARCHIVE_PIN_FILE
MANIFEST_FILE = ARCHIVE_MANIFEST_FILE
VERSION_STAMP = COMPONENT_VERSION_STAMP
EXCLUSIONS_FILE = ARCHIVE_EXCLUSIONS_FILE
ESP_IDF_TOOL_SNAPSHOT = Path("security/esp_idf_tools.json")

_PYTHON_DISTRIBUTION_VERSION_QUERY = (
    "import importlib.metadata as metadata, sys; "
    "print(metadata.version(sys.argv[1]))"
)


class ComponentError(RuntimeError):
    """A managed component does not satisfy its pinned contract."""


@dataclass(frozen=True)
class GitComponent:
    name: str
    label: str
    config: str
    prefix: str
    required_paths: tuple[str, ...]
    clean: bool = False
    submodules_key: Optional[str] = None
    recursive_submodules: bool = False
    version_validator: Optional[Callable[[Path, str], None]] = None


@dataclass(frozen=True)
class SuppliedPin:
    """A tool pin shipped by the jhbase component; this repository only picks
    the installation directory it is read into."""
    config: str
    dir_key: str
    directory: str


@dataclass(frozen=True)
class ToolArchive:
    name: str
    version: str
    url: str
    sha256: str
    executable: str
    version_args: tuple[str, ...]
    version_check: Callable[[str], bool]
    excluded_members: tuple[str, ...] = ()


def info(message: str) -> None:
    print(f"[INFO] {message}")


def ok(message: str) -> None:
    print(f"[OK] {message}")


def _run(
    command: Sequence[str],
    *,
    cwd: Optional[Path] = None,
    check: bool = True,
    env: Optional[dict[str, str]] = None,
    input: Optional[str] = None,
    encoding: Optional[str] = None,
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            [str(item) for item in command],
            cwd=str(cwd) if cwd else None,
            env=env,
            check=False,
            capture_output=True,
            text=True,
            input=input,
            encoding=encoding,
            errors="surrogateescape" if encoding else None,
        )
    except OSError as error:
        if check:
            raise ComponentError(f"Could not run {command[0]}: {error}") from error
        return subprocess.CompletedProcess(command, 127, "", str(error))
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise ComponentError(
            f"Command failed ({result.returncode}): {' '.join(command)}"
            + (f"\n{detail}" if detail else "")
        )
    return result


def parse_config(path: Path) -> dict[str, str]:
    """Read the tracked KEY=value pin format without evaluating shell code."""
    if not path.is_file():
        raise ComponentError(f"Component config not found: {path}")
    values: dict[str, str] = {}
    for number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ComponentError(f"Invalid config line {path}:{number}")
        key, raw_value = line.split("=", 1)
        key = key.strip()
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", key):
            raise ComponentError(f"Invalid config key {path}:{number}: {key}")
        try:
            parsed = shlex.split(raw_value, comments=True, posix=True)
        except ValueError as error:
            raise ComponentError(f"Invalid config value {path}:{number}: {error}") from error
        if len(parsed) > 1:
            raise ComponentError(f"Config value must be quoted at {path}:{number}")
        values[key] = parsed[0] if parsed else ""
    return values


def require_values(config: dict[str, str], names: Iterable[str], path: Path) -> None:
    for name in names:
        if not config.get(name):
            raise ComponentError(f"{name} missing in {path}")


def _remove_tree(path: Path) -> None:
    if not path.exists() and not path.is_symlink():
        return

    def make_writable(function: Callable[[str], object], target: str, _error: object) -> None:
        os.chmod(target, stat.S_IWRITE | stat.S_IREAD)
        function(target)

    for attempt in range(8):
        try:
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path, onerror=make_writable)
            else:
                path.unlink()
            return
        except (OSError, PermissionError):
            if attempt == 7:
                raise
            time.sleep(min(0.1 * (2 ** attempt), 1.0))


def _rename_with_retry(source: Path, destination: Path) -> None:
    for attempt in range(8):
        try:
            source.rename(destination)
            return
        except (OSError, PermissionError):
            if attempt == 7:
                raise
            time.sleep(min(0.1 * (2 ** attempt), 1.0))


def _validate_replacement_path(path: Path) -> None:
    resolved = path.resolve(strict=False)
    anchor = Path(resolved.anchor)
    if not path.name or resolved == anchor or resolved == Path.cwd().resolve():
        raise ComponentError(f"Refusing unsafe component replacement path: {path}")


def _atomic_replace(staging: Path, destination: Path) -> None:
    _validate_replacement_path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    backup = destination.parent / f".{destination.name}.backup.{uuid.uuid4().hex}"
    had_destination = destination.exists() or destination.is_symlink()
    try:
        if had_destination:
            _rename_with_retry(destination, backup)
        _rename_with_retry(staging, destination)
    except Exception:
        if had_destination and backup.exists() and not destination.exists():
            _rename_with_retry(backup, destination)
        raise
    if backup.exists():
        _remove_tree(backup)


def _git_output(directory: Path, *arguments: str, check: bool = True) -> str:
    return _run(
        ("git", "--no-optional-locks", "-c", "core.fsmonitor=false",
         "-c", "core.untrackedCache=false", "-C", str(directory), *arguments),
        check=check, encoding="utf-8",
    ).stdout.strip()


def _git_head(directory: Path) -> str:
    if not (directory / ".git").exists():
        return ""
    return _git_output(directory, "rev-parse", "HEAD", check=False)


def _checkout_state(directory: Path, actual_ref: str) -> str:
    if actual_ref:
        return actual_ref
    return "missing checkout" if not directory.exists() else "non-git directory"


def _clone_pinned(repository: str, reference: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = destination.parent / f".{destination.name}.clone.{uuid.uuid4().hex}"
    _remove_tree(staging)
    try:
        _run(("git", "init", "-q", str(staging)))
        _git_output(staging, "config", "core.autocrlf", "false")
        _git_output(staging, "remote", "add", "origin", repository)
        # Replacing a dirty checkout at the same pin can reuse its objects
        # offline. A fresh index discards hidden changes and their index flags.
        source = str(destination) if _git_head(destination) == reference else "origin"
        _git_output(staging, "fetch", "--depth", "1", source, reference)
        _git_output(staging, "checkout", "--detach", "FETCH_HEAD")
        _atomic_replace(staging, destination)
    except Exception:
        _remove_tree(staging)
        raise


def _verify_required_paths(directory: Path, required_paths: Iterable[str]) -> None:
    missing = [path for path in required_paths if not (directory / path).exists()]
    if missing:
        formatted = "\n".join(f"  missing: {directory / path}" for path in missing)
        raise ComponentError(f"Checkout at {directory} is incomplete:\n{formatted}")


def _git_path(quoted: str) -> str:
    """Decode the ASCII C-quoted paths emitted with core.quotePath=true."""
    return os.fsdecode(ast.literal_eval("b" + quoted)) if quoted.startswith('"') else quoted


def _relative_git_url(repository: str, url: str) -> str:
    if not url.startswith(("./", "../")):
        return url
    if "://" in repository:
        parsed = urllib.parse.urlsplit(repository)
        path = posixpath.normpath(parsed.path.rstrip("/") + "/" + url)
        return urllib.parse.urlunsplit(parsed._replace(path=path))
    prefix, separator, path = repository.rpartition(":")
    return prefix + separator + posixpath.normpath(path.replace("\\", "/").rstrip("/") + "/" + url)


def _git_submodule_urls(directory: Path, repository: str, reference: str) -> dict[str, str]:
    entries = _git_output(
        directory, "config", "--null", "--blob", f"{reference}:.gitmodules",
        "--get-regexp", r"^submodule\..*\.(path|url)$",
    )
    modules: dict[str, dict[str, str]] = {}
    for entry in entries.split("\0"):
        if entry:
            key, value = entry.split("\n", 1)
            name, field = key.rsplit(".", 1)
            modules.setdefault(name, {})[field] = value
    return {
        item["path"]: _relative_git_url(repository, item["url"])
        for item in modules.values() if "path" in item and "url" in item
    }


def _git_blob_digest(payload: bytes, expected: str) -> str:
    digest = hashlib.new("sha1" if len(expected) == 40 else "sha256")
    digest.update(f"blob {len(payload)}\0".encode("ascii"))
    digest.update(payload)
    return digest.hexdigest()


def _git_crlf_paths(directory: Path, names: Sequence[str]) -> set[str]:
    # Large Windows checkouts can have thousands of CRLF files. Read all
    # attributes in one process, with stdin avoiding the command-line limit.
    fields = _run(
        ("git", "-C", str(directory), "check-attr", "-z", "--stdin", "text", "eol"),
        input="".join(name + "\0" for name in names), encoding="utf-8",
    ).stdout.split("\0")
    properties: dict[str, dict[str, str]] = {}
    for name, attribute, value in zip(fields[0::3], fields[1::3], fields[2::3]):
        properties.setdefault(name, {})[attribute] = value
    autocrlf = _git_output(directory, "config", "--get", "core.autocrlf", check=False).lower()
    eol = _git_output(directory, "config", "--get", "core.eol", check=False) or "native"
    configured_crlf = eol == "crlf" or (eol == "native" and os.linesep == "\r\n")
    return {
        name for name, attributes in properties.items()
        if attributes["text"] != "unset" and attributes["eol"] != "lf"
        and (attributes["eol"] == "crlf" or autocrlf in ("true", "yes", "on", "1")
             or (autocrlf != "input" and configured_crlf and attributes["text"] in ("set", "auto")))
    }


_WINDOWS = os.name == "nt"


def _symlink_target_payload(path: Path) -> bytes:
    """Return the link target of PATH the way Git stores it in its blob.

    Git for Windows creates symbolic links with backslash separators, and
    os.readlink() returns them that way; the blob always holds '/'.
    """
    target = os.readlink(path)
    if _WINDOWS:
        target = target.replace("\\", "/")
    return os.fsencode(target)


def _git_crlf_matches(directory: Path, name: str, expected: str) -> bool:
    payload = (directory / name).read_bytes()
    return (
        b"\r\n" in payload and b"\0" not in payload
        and _git_blob_digest(payload.replace(b"\r\n", b"\n"), expected) == expected
    )


def _git_source_problem(directory: Path, repository: str, reference: str) -> str:
    """Check pinned content without refreshing the checkout's index."""
    if _git_head(directory) != reference:
        return f"Pinned checkout revision mismatch at {directory}: expected {reference}"
    origin = _git_output(directory, "remote", "get-url", "origin", check=False)
    if origin != repository:
        return f"Pinned checkout origin mismatch at {directory}: expected {repository}, found {origin}"
    staged = _git_output(
        directory, "diff-index", "--cached", "--no-ext-diff", "--no-textconv",
        "--name-status", reference, "--",
    )
    # No exclude rules: ignored files must also be reported. Unlike status,
    # these index-only queries never run a source file through a clean filter.
    untracked = _git_output(directory, "ls-files", "--others")
    if staged or untracked:
        return f"Pinned checkout has local changes at {directory}:\n{staged}\n{untracked}"
    tree = _git_output(directory, "-c", "core.quotePath=true", "ls-tree", "-r", reference)
    blobs: list[tuple[str, str]] = []
    gitlinks: list[tuple[str, str]] = []
    for entry in tree.splitlines():
        metadata, quoted = entry.split("\t", 1)
        mode, kind, expected = metadata.split()
        path = directory / _git_path(quoted)
        if kind == "commit" and mode == "160000":
            gitlinks.append((quoted, expected))
        elif kind == "blob" and mode == "120000":
            if path.is_symlink():
                payload = _symlink_target_payload(path)
            elif path.is_file() and _git_output(
                directory, "config", "--bool", "core.symlinks", check=False
            ) == "false":
                payload = path.read_bytes()
            else:
                return f"Pinned checkout source bytes or symlink type differ: {path}"
            if _git_blob_digest(payload, expected) != expected:
                return f"Pinned checkout source bytes differ: {path}"
        elif kind == "blob" and mode in ("100644", "100755"):
            if path.is_symlink() or not path.is_file():
                return f"Pinned checkout source bytes or file type differ: {path}"
            blobs.append((quoted, expected))
        else:
            return f"Unsupported pinned tree entry at {directory}: {entry}"
    if blobs:
        # Read actual bytes; do not execute configured clean/process filters.
        actual = _run(
            ("git", "-C", str(directory), "hash-object", "--no-filters", "--stdin-paths"),
            input="".join(quoted + "\n" for quoted, _expected in blobs),
        ).stdout.splitlines()
        if len(actual) != len(blobs):
            return f"Pinned checkout source bytes differ from {reference}: {directory}"
        differences = [
            (_git_path(quoted), expected)
            for found, (quoted, expected) in zip(actual, blobs) if found != expected
        ]
        crlf_paths = (
            _git_crlf_paths(directory, [name for name, _expected in differences])
            if differences else set()
        )
        for name, expected in differences:
            if name not in crlf_paths or not _git_crlf_matches(directory, name, expected):
                return f"Pinned checkout source bytes differ from {reference}: {directory / name}"
    urls = _git_submodule_urls(directory, repository, reference) if gitlinks else {}
    for quoted, expected in gitlinks:
        name = _git_path(quoted)
        path = directory / name
        if path.is_symlink() or not path.is_dir():
            return f"Pinned submodule directory is missing or replaced: {path}"
        if (path / ".git").exists():
            if name not in urls:
                return f"Pinned submodule URL is missing: {path}"
            problem = _git_source_problem(path, urls[name], expected)
            if problem:
                return problem
        elif any(path.iterdir()):
            return f"Unused submodule contains unverified files: {path}"
    return ""


def _remove_legacy_python_bytecode(directory: Path) -> int:
    """Remove only untracked CPython cache files of tracked SDK Python sources.

    Preparation of the managed ESP-IDF checkout calls this migration; external
    checkouts and verify-only calls never do. Keep unrelated files and links.
    """
    def local_path(path: Path) -> bool:
        relative = path.relative_to(directory)
        return not any((directory / parent).is_symlink() for parent in (relative, *relative.parents))

    entries = _git_output(directory, "ls-files", "--stage", "-z").split("\0")
    tracked: set[str] = set()
    modules: list[Path] = []
    for entry in entries:
        if entry:
            metadata, name = entry.split("\t", 1)
            tracked.add(name)
            if metadata.startswith("160000 "):
                modules.append(directory / name)
    removed = 0
    for name in _git_output(directory, "ls-files", "--others", "-z").split("\0"):
        path = directory / name
        match = re.fullmatch(r"(.+)\.cpython-[0-9]+(?:\.opt-[0-9]+)?\.pyc", path.name)
        if not match or path.parent.name != "__pycache__":
            continue
        if not local_path(path):
            continue
        source = path.parent.parent / (match[1] + ".py")
        if source.relative_to(directory).as_posix() not in tracked or not source.is_file() or not path.is_file():
            continue
        try:
            path.unlink()
        except OSError as error:
            raise ComponentError(f"Cannot remove legacy ESP-IDF Python cache {path}: {error}") from error
        removed += 1
        # Only the emptied cache directory; unrelated contents remain intact.
        try:
            path.parent.rmdir()
        except OSError:
            pass
    for module in modules:
        if local_path(module) and (module / ".git").exists():
            removed += _remove_legacy_python_bytecode(module)
    return removed


def _sync_submodules(
    directory: Path,
    submodules: Sequence[str],
    verify_only: bool,
    *,
    recursive: bool = False,
) -> None:
    if recursive:
        status = _git_output(
            directory, "submodule", "status", "--recursive", check=False
        )
        lines = status.splitlines()
        ready = bool(lines) and all(line[0] not in ("-", "+", "U") for line in lines)
        if ready:
            return
        if verify_only:
            raise ComponentError(
                f"Recursive submodules missing or mismatched at {directory} "
                "(verify-only)."
            )
        info(f"Initialising recursive submodules: {directory.name}")
        _git_output(
            directory, "submodule", "update", "--init", "--recursive", "--depth", "1"
        )
        status = _git_output(directory, "submodule", "status", "--recursive")
        lines = status.splitlines()
        if not lines or any(line[0] in ("-", "+", "U") for line in lines):
            raise ComponentError(
                f"Recursive submodule verification failed: {directory}"
            )
        return
    for submodule in submodules:
        status = _git_output(
            directory, "submodule", "status", "--", submodule, check=False
        )
        ready = bool(status) and status[0] not in ("-", "+", "U")
        if ready:
            continue
        if verify_only:
            raise ComponentError(
                f"Submodule missing or mismatched at {directory / submodule} (verify-only)."
            )
        info(f"Initialising submodule: {directory.name}/{submodule}")
        _git_output(
            directory,
            "submodule",
            "update",
            "--init",
            "--depth",
            "1",
            "--",
            submodule,
        )
        status = _git_output(directory, "submodule", "status", "--", submodule)
        if not status or status[0] in ("-", "+", "U"):
            raise ComponentError(f"Submodule verification failed: {directory / submodule}")


def sync_git_checkout(
    repository: str,
    reference: str,
    destination: Path,
    *,
    verify_only: bool = False,
    clean: bool = False,
    submodules: Sequence[str] = (),
    recursive_submodules: bool = False,
    required_paths: Sequence[str] = (),
) -> bool:
    """Synchronize an exact-ref checkout and return whether it changed."""
    actual = _git_head(destination)
    changed = False
    if actual != reference:
        if verify_only:
            found = _checkout_state(destination, actual)
            raise ComponentError(
                f"Pinned checkout mismatch at {destination}: expected {reference}, found {found}."
            )
        _clone_pinned(repository, reference, destination)
        changed = True

    origin = _git_output(destination, "remote", "get-url", "origin", check=False)
    if origin != repository:
        if verify_only:
            raise ComponentError(
                f"Pinned checkout origin mismatch at {destination}: expected {repository}, "
                f"found {origin or 'missing origin'}."
            )
        if origin:
            _git_output(destination, "remote", "set-url", "origin", repository)
        else:
            _git_output(destination, "remote", "add", "origin", repository)
        changed = True

    # Verification wants the pinned sources exactly, submodules included: a
    # local edit would make a gate check other code than CI downloads.
    if clean and not verify_only:
        status_text = _git_output(destination, "status", "--porcelain")
        if status_text:
            _git_output(destination, "reset", "--hard", reference)
            _git_output(destination, "clean", "-fdx")
            changed = True

    _sync_submodules(
        destination, submodules, verify_only, recursive=recursive_submodules
    )
    if clean or verify_only:
        problem = _git_source_problem(destination, repository, reference)
        if problem and not verify_only:
            _clone_pinned(repository, reference, destination)
            _sync_submodules(destination, submodules, False, recursive=recursive_submodules)
            changed = True
            problem = _git_source_problem(destination, repository, reference)
        if problem:
            raise ComponentError(problem)
    _verify_required_paths(destination, required_paths)
    actual = _git_head(destination)
    if actual != reference:
        raise ComponentError(
            f"Ref mismatch in {destination}: expected {reference}, found {actual or 'unknown'}."
        )
    return changed


def _manifest_lines(directory: Path) -> list[str]:
    lines: list[str] = []
    for path in sorted(
        (
            item
            for item in directory.rglob("*")
            if (item.is_file() or item.is_symlink())
            and item.name not in (PIN_FILE, MANIFEST_FILE)
        ),
        key=lambda item: item.relative_to(directory).as_posix(),
    ):
        relative = path.relative_to(directory).as_posix()
        if path.is_symlink():
            target = os.readlink(path)
            digest = hashlib.sha256(target.encode("utf-8")).hexdigest()
            lines.append(f"link:{digest}  ./{relative}")
        else:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            lines.append(f"{digest}  ./{relative}")
    return lines


def _write_archive_metadata(
    directory: Path,
    url: str,
    sha256: str,
    version_stamp: str,
    excluded_members: Sequence[str],
) -> None:
    (directory / PIN_FILE).write_text(
        f"url={url}\nsha256={sha256}\n", encoding="utf-8", newline="\n"
    )
    if version_stamp:
        (directory / VERSION_STAMP).write_text(
            version_stamp + "\n", encoding="utf-8", newline="\n"
        )
    if excluded_members:
        (directory / EXCLUSIONS_FILE).write_text(
            "\n".join(excluded_members) + "\n", encoding="utf-8", newline="\n"
        )
    manifest = "\n".join(_manifest_lines(directory)) + "\n"
    (directory / MANIFEST_FILE).write_text(manifest, encoding="utf-8", newline="\n")


def archive_matches(
    directory: Path,
    url: str,
    sha256: str,
    version_stamp: str = "",
    excluded_members: Sequence[str] = (),
) -> bool:
    if not directory.is_dir():
        return False
    pin = directory / PIN_FILE
    manifest = directory / MANIFEST_FILE
    if not pin.is_file() or not manifest.is_file():
        return False
    if pin.read_text(encoding="utf-8") != f"url={url}\nsha256={sha256}\n":
        return False
    stamp = directory / VERSION_STAMP
    if version_stamp and (
        not stamp.is_file() or stamp.read_text(encoding="utf-8").strip() != version_stamp
    ):
        return False
    exclusions = directory / EXCLUSIONS_FILE
    expected_exclusions = "\n".join(excluded_members) + ("\n" if excluded_members else "")
    if excluded_members:
        if not exclusions.is_file() or exclusions.read_text(encoding="utf-8") != expected_exclusions:
            return False
    elif exclusions.exists():
        return False
    actual = "\n".join(_manifest_lines(directory)) + "\n"
    return manifest.read_text(encoding="utf-8") == actual


def _safe_member_path(root: Path, member: str) -> Path:
    normalized = member.replace("\\", "/")
    if normalized.startswith("/") or re.match(r"^[A-Za-z]:", normalized):
        raise ComponentError(f"Archive contains an absolute path: {member}")
    target = (root / normalized).resolve(strict=False)
    try:
        target.relative_to(root.resolve())
    except ValueError as error:
        raise ComponentError(f"Archive path escapes extraction root: {member}") from error
    return target


def _extract_zip(
    archive: Path, destination: Path, excluded_members: Sequence[str]
) -> None:
    excluded = set(excluded_members)
    found_exclusions: set[str] = set()
    with zipfile.ZipFile(archive) as package:
        for member in package.infolist():
            normalized = member.filename.replace("\\", "/")
            if normalized in excluded:
                found_exclusions.add(normalized)
                continue
            target = _safe_member_path(destination, member.filename)
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            try:
                target.parent.mkdir(parents=True, exist_ok=True)
                with package.open(member) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
            except OSError as error:
                raise ComponentError(
                    f"Archive member cannot be materialized on this host: {normalized}"
                ) from error
    missing = excluded - found_exclusions
    if missing:
        raise ComponentError(
            "Configured archive exclusions were not present: " + ", ".join(sorted(missing))
        )


def _extract_tar(archive: Path, destination: Path) -> None:
    with tarfile.open(archive, mode="r:gz") as package:
        for member in package.getmembers():
            target = _safe_member_path(destination, member.name)
            if member.issym():
                target.parent.mkdir(parents=True, exist_ok=True)
                linked = (target.parent / member.linkname).resolve(strict=False)
                try:
                    linked.relative_to(destination.resolve())
                except ValueError as error:
                    raise ComponentError(
                        f"Archive symlink escapes extraction root: {member.name}"
                    ) from error
                os.symlink(member.linkname, target)
                continue
            if member.islnk():
                linked = _safe_member_path(destination, member.linkname)
                if not linked.is_file():
                    raise ComponentError(
                        f"Archive hard link target is unavailable: {member.name}"
                    )
                target.parent.mkdir(parents=True, exist_ok=True)
                os.link(linked, target)
                continue
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if not member.isfile():
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = package.extractfile(member)
            if source is None:
                raise ComponentError(f"Could not read archive member: {member.name}")
            with source, target.open("wb") as output:
                shutil.copyfileobj(source, output)
            os.chmod(target, member.mode & 0o777)


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _download_windows_https(url: str, destination: Path) -> str:
    system_root = os.environ.get("SystemRoot")
    curl = Path(system_root) / "System32/curl.exe" if system_root else None
    if curl is None or not curl.is_file():
        raise ComponentError(
            "Windows system curl.exe is required for authenticated HTTPS downloads."
        )
    command = (
        str(curl), "--fail", "--location", "--silent", "--show-error",
        "--proto", "=https", "--proto-redir", "=https", "--tlsv1.2",
        "--output", str(destination), url,
    )
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise ComponentError(
            f"Windows HTTPS download failed for {url}: {detail or result.returncode}"
        )
    if not destination.is_file():
        raise ComponentError(f"Windows HTTPS download produced no file for {url}.")
    return _sha256_file(destination)


def _download(url: str, destination: Path) -> str:
    if (
        sys.platform == "win32"
        and urllib.parse.urlparse(url).scheme.lower() == "https"
    ):
        return _download_windows_https(url, destination)

    digest = hashlib.sha256()
    try:
        with urllib.request.urlopen(url) as response, destination.open("wb") as output:
            while True:
                block = response.read(1024 * 1024)
                if not block:
                    break
                output.write(block)
                digest.update(block)
    except Exception as error:
        raise ComponentError(
            f"Could not download {url}. If offline, pre-populate the destination: {error}"
        ) from error
    return digest.hexdigest()


def sync_archive(
    url: str,
    sha256: str,
    destination: Path,
    *,
    verify_only: bool = False,
    version_stamp: str = "",
    excluded_members: Sequence[str] = (),
) -> bool:
    """Install and authenticate a .zip or .tar.gz archive atomically."""
    if archive_matches(destination, url, sha256, version_stamp, excluded_members):
        return False
    if verify_only:
        raise ComponentError(
            f"Pinned archive installation missing or modified at {destination} (verify-only)."
        )
    _validate_replacement_path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = destination.parent / f".{destination.name}.archive.{uuid.uuid4().hex}"
    archive = staging / "component.archive"
    extracted = staging / "extract"
    _remove_tree(staging)
    extracted.mkdir(parents=True)
    try:
        actual = _download(url, archive)
        if actual.lower() != sha256.lower():
            raise ComponentError(
                f"Archive checksum mismatch for {url}; expected SHA-256 {sha256}, got {actual}."
            )
        lowered = urllib.parse.urlparse(url).path.lower()
        if lowered.endswith(".zip") or lowered.endswith(".nupkg"):
            _extract_zip(archive, extracted, excluded_members)
        elif lowered.endswith(".tar.gz") or lowered.endswith(".tgz"):
            _extract_tar(archive, extracted)
        else:
            raise ComponentError(f"Unsupported archive type: {url}")
        _write_archive_metadata(
            extracted, url, sha256, version_stamp, excluded_members
        )
        _atomic_replace(extracted, destination)
    finally:
        _remove_tree(staging)
    return True


def _version_littlefs(directory: Path, expected: str) -> None:
    major, minor, *_ = expected.split(".")
    expected_hex = f"0x{int(major):04x}{int(minor):04x}"
    text = (directory / "lfs.h").read_text(encoding="utf-8")
    match = re.search(r"^#define\s+LFS_VERSION\s+(0x[0-9a-fA-F]+)", text, re.MULTILINE)
    found = match.group(1).lower() if match else "unknown"
    if found != expected_hex:
        raise ComponentError(
            f"littlefs API version mismatch: expected {expected_hex}, found {found}."
        )


def _version_lwip(directory: Path, expected: str) -> None:
    text = (directory / "src/include/lwip/init.h").read_text(encoding="utf-8")
    parts = []
    for suffix in ("MAJOR", "MINOR", "REVISION"):
        match = re.search(
            rf"^#define\s+LWIP_VERSION_{suffix}\s+(\d+)", text, re.MULTILINE
        )
        parts.append(match.group(1) if match else "")
    found = ".".join(parts)
    if found != expected:
        raise ComponentError(f"lwIP version mismatch: expected {expected}, found {found}.")


def _version_freertos(directory: Path, expected: str) -> None:
    text = (directory / "include/task.h").read_text(encoding="utf-8")
    match = re.search(r'tskKERNEL_VERSION_NUMBER\s+"([^"]+)"', text)
    found = match.group(1) if match else "unknown"
    if found != expected:
        raise ComponentError(
            f"FreeRTOS-Kernel version mismatch: expected {expected}, found {found}."
        )


def _version_pico(directory: Path, expected: str) -> None:
    text = (directory / "pico_sdk_version.cmake").read_text(encoding="utf-8")
    parts = []
    for suffix in ("MAJOR", "MINOR", "REVISION"):
        match = re.search(
            rf"set\(PICO_SDK_VERSION_{suffix}\s+(\d+)", text, re.MULTILINE
        )
        parts.append(match.group(1) if match else "")
    found = ".".join(parts)
    if found != expected:
        raise ComponentError(f"Pico SDK version mismatch: expected {expected}, found {found}.")


def _version_esp_idf(directory: Path, expected: str) -> None:
    text = (directory / "tools/cmake/version.cmake").read_text(encoding="utf-8")
    parts = []
    for suffix in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(
            rf"set\(IDF_VERSION_{suffix}\s+(\d+)", text, re.MULTILINE
        )
        parts.append(match.group(1) if match else "")
    found = ".".join(parts)
    if found != expected:
        raise ComponentError(
            f"ESP-IDF version mismatch: expected {expected}, found {found}."
        )


def _esp_idf_python_tool_pins(repo_root: Path) -> tuple[tuple[str, str], ...]:
    path = repo_root / ESP_IDF_TOOL_SNAPSHOT
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ComponentError(
            f"Cannot read ESP-IDF tool snapshot {path}: {error}"
        ) from error
    entries = document.get("pythonTools") if isinstance(document, dict) else None
    if not isinstance(entries, list) or not entries:
        raise ComponentError(f"ESP-IDF tool snapshot has no pythonTools: {path}")
    pins: list[tuple[str, str]] = []
    observed: set[str] = set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise ComponentError(
                f"Invalid ESP-IDF Python tool at {path}:pythonTools[{index}]"
            )
        name = entry.get("name")
        version = entry.get("version")
        if (
            not isinstance(name, str)
            or re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name) is None
            or not isinstance(version, str)
            or re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+!_-]*", version) is None
        ):
            raise ComponentError(
                f"Invalid ESP-IDF Python tool pin at "
                f"{path}:pythonTools[{index}]"
            )
        normalized = name.casefold().replace("_", "-")
        if normalized in observed:
            raise ComponentError(
                f"Duplicate ESP-IDF Python tool pin {name!r} in {path}"
            )
        observed.add(normalized)
        pins.append((name, version))
    return tuple(pins)


def _esp_idf_python_environment(config: dict[str, str]) -> tuple[Path, Path]:
    version = config.get("ESP_IDF_VERSION", "")
    match = re.fullmatch(r"(\d+)\.(\d+)\.\d+", version)
    if match is None:
        raise ComponentError(f"Invalid ESP_IDF_VERSION: {version!r}")
    tools_root = Path(
        os.environ.get("IDF_TOOLS_PATH", str(Path.home() / ".espressif"))
    ).expanduser()
    environment_root = os.environ.get("IDF_PYTHON_ENV_PATH")
    if environment_root:
        python_root = Path(environment_root).expanduser()
    else:
        python_version = f"{sys.version_info.major}.{sys.version_info.minor}"
        idf_series = f"{match.group(1)}.{match.group(2)}"
        python_root = (
            tools_root
            / "python_env"
            / f"idf{idf_series}_py{python_version}_env"
        )
    executable = (
        python_root / "Scripts/python.exe"
        if sys.platform == "win32"
        else python_root / "bin/python"
    )
    constraints = tools_root / (
        f"espidf.constraints.v{match.group(1)}.{match.group(2)}.txt"
    )
    return executable, constraints


def _query_esp_idf_python_tools(
    python: Path, names: Sequence[str]
) -> dict[str, str]:
    versions: dict[str, str] = {}
    for name in names:
        completed = _run(
            (
                str(python),
                "-c",
                _PYTHON_DISTRIBUTION_VERSION_QUERY,
                name,
            ),
            check=False,
        )
        version = completed.stdout.strip()
        if completed.returncode == 0 and version:
            versions[name] = version
    return versions


def python_environment(repo_root: Path = SCRIPT_DIR.parent) -> dict[str, str]:
    """Keep imported dependency bytecode outside the pinned source trees."""
    environment = dict(os.environ)
    environment["PYTHONPYCACHEPREFIX"] = (
        environment.get("PYTHONPYCACHEPREFIX") or str(repo_root / ".build/python-cache")
    )
    return environment


def _esp_idf_python_tool_drift(
    pins: Sequence[tuple[str, str]], versions: dict[str, str]
) -> list[str]:
    return [
        f"{name}: expected {expected}, found {versions.get(name, 'missing')}"
        for name, expected in pins
        if versions.get(name) != expected
    ]


def _synchronize_esp_idf_python_tools(
    repo_root: Path, config: dict[str, str], *, verify_only: bool
) -> None:
    pins = _esp_idf_python_tool_pins(repo_root)
    python, constraints = _esp_idf_python_environment(config)
    names = [name for name, _version in pins]
    versions = _query_esp_idf_python_tools(python, names)
    drift = _esp_idf_python_tool_drift(pins, versions)
    if not drift:
        ok("ESP-IDF Python tools match the reviewed snapshot.")
        return
    if verify_only:
        raise ComponentError(
            "ESP-IDF Python tool version drift: " + "; ".join(drift)
        )
    if not constraints.is_file():
        raise ComponentError(
            f"ESP-IDF Python constraints file is missing: {constraints}"
        )
    info("Synchronizing exact ESP-IDF Python tool versions...")
    requirements = [f"{name}=={version}" for name, version in pins]
    _run(
        (
            str(python),
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "--no-warn-script-location",
            "--upgrade",
            "--constraint",
            str(constraints),
            *requirements,
        )
    )
    versions = _query_esp_idf_python_tools(python, names)
    drift = _esp_idf_python_tool_drift(pins, versions)
    if drift:
        raise ComponentError(
            "ESP-IDF Python tool synchronization failed: " + "; ".join(drift)
        )
    ok("ESP-IDF Python tools synchronized with the reviewed snapshot.")


def ensure_esp_idf_tools(
    repo_root: Path,
    directory: Path,
    *,
    verify_only: bool,
) -> None:
    config_path = repo_root / "third_party/esp_idf_version.conf"
    config = parse_config(config_path)
    require_values(config, ("ESP_IDF_TARGETS",), config_path)
    targets = config["ESP_IDF_TARGETS"]
    if not re.fullmatch(r"[a-z0-9]+(?:,[a-z0-9]+)*", targets):
        raise ComponentError(
            f"Invalid ESP_IDF_TARGETS in {config_path}: {targets}"
        )
    environment = python_environment(repo_root)
    if verify_only:
        environment["PYTHONDONTWRITEBYTECODE"] = "1"

    if not verify_only:
        info(f"Installing ESP-IDF tools for: {targets}")
        if sys.platform == "win32":
            # The working directory is the SDK; keep cmd.exe's command name
            # free of spaces rather than relying on its extra quote parsing.
            command = (
                "cmd.exe", "/d", "/s", "/c", "install.bat",
                targets,
            )
        else:
            command = ("bash", str(directory / "install.sh"), targets)
        # The installers run whichever python PATH finds first; put this
        # managed interpreter there instead of relying on a host install.
        environment["PATH"] = os.pathsep.join(
            (str(Path(sys.executable).parent), environment.get("PATH", ""))
        )
        _run(command, cwd=directory, env=environment)

    idf_tools = directory / "tools/idf_tools.py"
    base_command = (
        sys.executable, str(idf_tools), "--idf-path", str(directory)
    )
    _run((*base_command, "check"), cwd=directory, env=environment)
    _run((*base_command, "check-python-dependencies"), cwd=directory, env=environment)
    _synchronize_esp_idf_python_tools(
        repo_root, config, verify_only=verify_only
    )
    ok(f"ESP-IDF tools ready for {targets}: {directory}")
    info(f"Activate them in the current shell with: . {directory / 'export.sh'}")


def _load_managed_component_catalog() -> tuple[
    dict[str, GitComponent], tuple[str, ...], tuple[str, ...], dict[str, str],
    dict[str, SuppliedPin],
]:
    source = "managed_components.json"
    document = load_tooling_contract(source)
    records = require_list(document, "gitComponents", source=source)
    raw_pins = document.get("jhbasePins", {})
    if not isinstance(raw_pins, dict):
        raise ToolingContractError(f"{source}: jhbasePins must be an object")
    pins: dict[str, SuppliedPin] = {}
    for identifier, raw_pin in raw_pins.items():
        pin_source = f"{source}: jhbasePins.{identifier}"
        if not isinstance(raw_pin, dict):
            raise ToolingContractError(f"{pin_source} must be an object")
        pins[identifier] = SuppliedPin(
            config=require_string(raw_pin.get("config"), "config", source=pin_source),
            dir_key=require_string(raw_pin.get("dirKey"), "dirKey", source=pin_source),
            directory=require_string(
                raw_pin.get("directory"), "directory", source=pin_source
            ),
        )
    validators: dict[str, Callable[[Path, str], None]] = {
        "littlefs": _version_littlefs,
        "lwip": _version_lwip,
        "freertos": _version_freertos,
        "pico": _version_pico,
        "esp-idf": _version_esp_idf,
    }
    components: dict[str, GitComponent] = {}
    for index, raw_record in enumerate(records):
        record_source = f"{source}: gitComponents[{index}]"
        if not isinstance(raw_record, dict):
            raise ToolingContractError(f"{record_source} must be an object")
        name = require_string(raw_record.get("id"), "id", source=record_source)
        if name in components:
            raise ToolingContractError(f"{record_source}: duplicate id {name!r}")
        validator_name = raw_record.get("versionValidator")
        if validator_name is not None and validator_name not in validators:
            raise ToolingContractError(
                f"{record_source}: unknown versionValidator {validator_name!r}"
            )
        for flag in ("clean", "recursiveSubmodules"):
            if flag in raw_record and not isinstance(raw_record[flag], bool):
                raise ToolingContractError(f"{record_source}: {flag} must be boolean")
        submodules_key = raw_record.get("submodulesKey")
        if submodules_key is not None:
            submodules_key = require_string(
                submodules_key, "submodulesKey", source=record_source
            )
        if name in pins:
            if "config" in raw_record:
                raise ToolingContractError(
                    f"{record_source}: config must be omitted; jhbase supplies "
                    f"{pins[name].config}"
                )
            config = pins[name].config
        else:
            config = require_string(raw_record.get("config"), "config", source=record_source)
        components[name] = GitComponent(
            name=name,
            label=require_string(raw_record.get("label"), "label", source=record_source),
            config=config,
            prefix=require_string(raw_record.get("prefix"), "prefix", source=record_source),
            required_paths=require_string_list(
                raw_record.get("requiredPaths"), "requiredPaths", source=record_source
            ),
            clean=raw_record.get("clean", False),
            submodules_key=submodules_key,
            recursive_submodules=raw_record.get("recursiveSubmodules", False),
            version_validator=(
                validators[validator_name] if validator_name is not None else None
            ),
        )

    tools = require_string_list(
        document.get("toolComponents"), "toolComponents", source=source
    )
    default_order = require_string_list(
        document.get("defaultOrder"), "defaultOrder", source=source
    )
    known = {*components, *tools}
    unknown = sorted(set(default_order) - known)
    if unknown:
        raise ToolingContractError(
            f"{source}: defaultOrder contains unknown components: {unknown}"
        )
    if "esp-idf" in default_order:
        raise ToolingContractError(
            f"{source}: optional esp-idf must not be in defaultOrder"
        )
    if pins:
        unknown_pins = sorted((set(pins) - known) | ({"jhbase"} & set(pins)))
        if "jhbase" not in components or unknown_pins:
            raise ToolingContractError(
                f"{source}: jhbasePins need the jhbase git component and may only "
                f"name other managed components (unknown: {unknown_pins})"
            )
        supplied_first = min(
            (default_order.index(name) for name in pins if name in default_order),
            default=len(default_order),
        )
        if "jhbase" not in default_order or default_order.index("jhbase") > supplied_first:
            raise ToolingContractError(
                f"{source}: defaultOrder must synchronize jhbase before the pins it supplies"
            )
    raw_launchers = require_object(document, "launchers", source=source)
    launchers = {
        require_string(identifier, "launcher id", source=source): require_string(
            filename, f"launchers.{identifier}", source=source
        )
        for identifier, filename in raw_launchers.items()
    }
    if set(launchers) != known:
        raise ToolingContractError(
            f"{source}: launchers must cover every managed component"
        )
    return components, tools, default_order, launchers, pins


(
    GIT_COMPONENTS, TOOL_COMPONENTS, SOURCE_COMPONENT_ORDER, COMPONENT_LAUNCHERS,
    JHBASE_PINS,
) = _load_managed_component_catalog()
ALL_COMPONENT_NAMES = (*GIT_COMPONENTS, *TOOL_COMPONENTS)


def _resolve_component_dir(repo_root: Path, configured: str, override: str) -> Path:
    path = Path(override or configured).expanduser()
    if not path.is_absolute():
        path = repo_root / path
    return path.resolve(strict=False)


def jhbase_root(repo_root: Path) -> Path:
    """Directory of the jhbase checkout, the source of the supplied tool pins.

    JH_JHBASE_DIR selects an external checkout, like the other *_DIR overrides.
    """
    spec = GIT_COMPONENTS["jhbase"]
    config_path = repo_root / "third_party" / spec.config
    config = parse_config(config_path)
    require_values(config, (f"{spec.prefix}_DIR",), config_path)
    return _resolve_component_dir(
        repo_root, config[f"{spec.prefix}_DIR"], os.environ.get("JH_JHBASE_DIR", "")
    )


def supplied_pin(repo_root: Path, name: str) -> tuple[dict[str, str], Path]:
    """Read a tool pin shipped by jhbase, adding this repository's install directory."""
    pin = JHBASE_PINS[name]
    path = jhbase_root(repo_root) / "third_party" / pin.config
    if not path.is_file():
        raise ComponentError(
            f"{pin.config} is supplied by the jhbase component and is missing at {path}. "
            "Run scripts/ensure_jhbase.sh; when jhbase is synchronized, JHBASE_REF in "
            "third_party/jhbase_version.conf must name a commit that ships this pin."
        )
    config = parse_config(path)
    config[pin.dir_key] = pin.directory
    return config, path


def ensure_git_component(
    name: str,
    repo_root: Path,
    *,
    verify_only: bool,
    directory_override: str = "",
    submodule_override: Optional[str] = None,
) -> Path:
    spec = GIT_COMPONENTS[name]
    if name in JHBASE_PINS:
        config, config_path = supplied_pin(repo_root, name)
    else:
        config_path = repo_root / "third_party" / spec.config
        config = parse_config(config_path)
    required = (
        f"{spec.prefix}_REPO", f"{spec.prefix}_REF",
        f"{spec.prefix}_VERSION", f"{spec.prefix}_DIR",
    )
    require_values(config, required, config_path)
    destination = _resolve_component_dir(
        repo_root, config[f"{spec.prefix}_DIR"], directory_override
    )
    submodules_text = config.get(spec.submodules_key or "", "")
    if spec.recursive_submodules and submodule_override is not None:
        raise ComponentError(
            f"{spec.label} requires its complete recursive submodule set; "
            "submodule overrides are unsupported."
        )
    if submodule_override is not None:
        submodules_text = submodule_override
    submodules = tuple(shlex.split(submodules_text))
    external = bool(directory_override) and name in (
        "freertos", "pico-sdk", "esp-idf", "jhbase"
    )
    if external:
        actual = _git_head(destination)
        if actual != config[f"{spec.prefix}_REF"]:
            raise ComponentError(
                f"External {spec.label} checkout mismatch at {destination}: expected "
                f"{config[f'{spec.prefix}_REF']}, "
                f"found {_checkout_state(destination, actual)}."
            )
        _sync_submodules(
            destination, submodules, verify_only,
            recursive=spec.recursive_submodules,
        )
        problem = _git_source_problem(
            destination, config[f"{spec.prefix}_REPO"], config[f"{spec.prefix}_REF"]
        )
        if problem:
            raise ComponentError(problem)
        _verify_required_paths(destination, spec.required_paths)
        changed = False
    else:
        changed = sync_git_checkout(
            config[f"{spec.prefix}_REPO"],
            config[f"{spec.prefix}_REF"],
            destination,
            verify_only=verify_only,
            clean=spec.clean,
            submodules=submodules,
            recursive_submodules=spec.recursive_submodules,
            required_paths=spec.required_paths,
        )
        if name == "esp-idf" and not verify_only:
            removed = _remove_legacy_python_bytecode(destination)
            if removed:
                info(f"Removed {removed} legacy ESP-IDF Python cache files from {destination}")
                changed = True
    if spec.version_validator:
        spec.version_validator(destination, config[f"{spec.prefix}_VERSION"])
    status = "synchronized" if changed else "already synchronized"
    ok(
        f"{spec.label} {status}: {destination} "
        f"({config[f'{spec.prefix}_VERSION']}, {config[f'{spec.prefix}_REF']})"
    )
    return destination


def _platform_asset() -> tuple[str, str, str]:
    machine = platform.machine().lower()
    if sys.platform == "win32":
        if machine not in ("amd64", "x86_64"):
            raise ComponentError(f"Unsupported Windows architecture: {machine}")
        return "x64-win", ".zip", "X64_WIN"
    if sys.platform.startswith("linux"):
        if machine in ("amd64", "x86_64"):
            return "x86_64-lin", ".tar.gz", "X86_64_LIN"
        if machine in ("aarch64", "arm64"):
            return "aarch64-lin", ".tar.gz", "AARCH64_LIN"
    raise ComponentError(f"Unsupported host for managed RISC-V toolchain: {sys.platform}/{machine}")


def _find_executable(root: Path, filename: str) -> Path:
    matches = sorted(path for path in root.rglob(filename) if path.is_file())
    if len(matches) != 1:
        raise ComponentError(
            f"Expected exactly one {filename} under {root}, found {len(matches)}."
        )
    return matches[0]


def ensure_riscv_toolchain(
    repo_root: Path,
    *,
    verify_only: bool,
    directory_override: str = "",
) -> Path:
    config, config_path = supplied_pin(repo_root, "riscv-toolchain")
    required = (
        "RISCV_TOOLCHAIN_RELEASE_BASE", "RISCV_TOOLCHAIN_TAG",
        "RISCV_TOOLCHAIN_ASSET_STEM", "RISCV_TOOLCHAIN_GCC_MAJOR",
        "RISCV_TOOLCHAIN_TRIPLE", "RISCV_TOOLCHAIN_DIR",
    )
    require_values(config, required, config_path)
    asset_token, extension, checksum_suffix = _platform_asset()
    checksum_key = f"RISCV_TOOLCHAIN_SHA256_{checksum_suffix}"
    require_values(config, (checksum_key,), config_path)
    asset = f"{config['RISCV_TOOLCHAIN_ASSET_STEM']}-{asset_token}{extension}"
    url = f"{config['RISCV_TOOLCHAIN_RELEASE_BASE']}/{config['RISCV_TOOLCHAIN_TAG']}/{asset}"
    destination = _resolve_component_dir(
        repo_root, config["RISCV_TOOLCHAIN_DIR"], directory_override
    )
    stamp = (
        f"{config['RISCV_TOOLCHAIN_TAG']}|{asset}|{config['RISCV_TOOLCHAIN_TRIPLE']}|"
        f"gcc-{config['RISCV_TOOLCHAIN_GCC_MAJOR']}"
    )
    changed = sync_archive(
        url, config[checksum_key], destination,
        verify_only=verify_only, version_stamp=stamp,
    )
    suffix = ".exe" if sys.platform == "win32" else ""
    gcc = _find_executable(
        destination, f"{config['RISCV_TOOLCHAIN_TRIPLE']}-gcc{suffix}"
    )
    version = _run((str(gcc), "--version")).stdout.splitlines()[0]
    if not re.search(rf"\b{re.escape(config['RISCV_TOOLCHAIN_GCC_MAJOR'])}\.", version):
        raise ComponentError(
            f"RISC-V gcc version mismatch: expected GCC "
            f"{config['RISCV_TOOLCHAIN_GCC_MAJOR']}.x, got {version}."
        )
    status = "installed" if changed else "already installed"
    ok(f"RISC-V toolchain {status}: {gcc}")
    return gcc


def cmake_minimum_bin(repo_root: Path, directory_override: str = "") -> Path:
    """bin directory of the oldest supported CMake (Linux only), the release
    jhbase pins in cmake_minimum_version.conf; the gate builds with it."""
    config, config_path = supplied_pin(repo_root, "cmake-minimum")
    require_values(config, ("CMAKE_MINIMUM_VERSION", "CMAKE_MINIMUM_DIR"), config_path)
    if sys.platform == "win32":
        raise ComponentError("The oldest supported CMake is pinned for Linux hosts only.")
    arch = _platform_asset()[0].removesuffix("-lin")
    destination = _resolve_component_dir(repo_root, config["CMAKE_MINIMUM_DIR"], directory_override)
    return destination / f"cmake-{config['CMAKE_MINIMUM_VERSION']}-linux-{arch}" / "bin"


def ensure_cmake_minimum(
    repo_root: Path,
    *,
    verify_only: bool,
    directory_override: str = "",
) -> Path | None:
    if sys.platform == "win32":
        info("The oldest supported CMake is a Linux gate tool; skipping on Windows.")
        return None
    config, config_path = supplied_pin(repo_root, "cmake-minimum")
    asset_token, extension, checksum_suffix = _platform_asset()
    checksum_key = f"CMAKE_MINIMUM_SHA256_{checksum_suffix}"
    require_values(config, ("CMAKE_MINIMUM_RELEASE_BASE", checksum_key), config_path)
    version = config["CMAKE_MINIMUM_VERSION"]
    bin_dir = cmake_minimum_bin(repo_root, directory_override)
    asset = f"{bin_dir.parent.name}{extension}"
    url = f"{config['CMAKE_MINIMUM_RELEASE_BASE']}/v{version}/{asset}"
    changed = sync_archive(
        url, config[checksum_key], bin_dir.parents[1],
        verify_only=verify_only, version_stamp=f"cmake-minimum|{version}|{asset}",
    )
    reported = _run((str(bin_dir / "cmake"), "--version")).stdout.splitlines()[0]
    if reported != f"cmake version {version}":
        raise ComponentError(f"Oldest CMake mismatch: expected {version}, got {reported}.")
    status = "installed" if changed else "already installed"
    ok(f"CMake {version} {status}: {bin_dir / 'cmake'}")
    return bin_dir


def _pmd_command(executable: Path, *arguments: str) -> tuple[str, ...]:
    if sys.platform == "win32":
        return ("cmd.exe", "/d", "/c", str(executable), *arguments)
    return (str(executable), *arguments)


def ensure_pmd(
    repo_root: Path,
    *,
    verify_only: bool,
    directory_override: str = "",
) -> Path:
    config, config_path = supplied_pin(repo_root, "pmd")
    required = ("PMD_VERSION", "PMD_URL", "PMD_SHA256", "PMD_DIR")
    require_values(config, required, config_path)
    destination = _resolve_component_dir(
        repo_root, config["PMD_DIR"], directory_override
    )
    stamp = f"pmd|{config['PMD_VERSION']}|{config['PMD_SHA256']}"
    changed = sync_archive(
        config["PMD_URL"],
        config["PMD_SHA256"],
        destination,
        verify_only=verify_only,
        version_stamp=stamp,
    )
    executable_name = "pmd.bat" if sys.platform == "win32" else "pmd"
    executable = _find_executable(destination, executable_name)
    if sys.platform != "win32" and not verify_only:
        executable.chmod(
            executable.stat().st_mode
            | stat.S_IXUSR
            | stat.S_IXGRP
            | stat.S_IXOTH
        )
    result = _run(_pmd_command(executable, "--version"), check=False)
    output = (result.stdout + result.stderr).strip()
    if result.returncode != 0 or not re.search(
        rf"\bPMD\s+{re.escape(config['PMD_VERSION'])}\b", output
    ):
        raise ComponentError(
            f"PMD version mismatch: expected {config['PMD_VERSION']}, "
            f"got {output or 'no version output'}. Ensure a Java runtime is installed."
        )
    status = "installed" if changed else "already installed"
    ok(f"PMD {config['PMD_VERSION']} {status}: {executable}")
    return executable


def _picotool_reports(output: str, version: str) -> bool:
    """True when `picotool version` output names the release of VERSION.

    VERSION may carry a build suffix (2.2.0-a4); picotool prints the release
    as `picotool vX.Y.Z`, the same form cmake/jh_rp_sdk_support.cmake reads.
    """
    reported = re.search(r"picotool v(\d+\.\d+\.\d+)", output)
    pinned = re.match(r"\d+\.\d+\.\d+", version)
    return bool(reported and pinned and reported.group(1) == pinned.group(0))


def _picotool_binary_matches(binary: Path, version: str) -> bool:
    result = _run((str(binary), "version"), check=False)
    return result.returncode == 0 and _picotool_reports(
        result.stdout + result.stderr, version
    )


def _picotool_capability_issues(
    binary: Path,
    *,
    require_usb: bool,
    require_signing: bool,
) -> list[str]:
    version = _run((str(binary), "version"), check=False)
    if version.returncode != 0:
        return ["version probe"]
    version_text = (version.stdout + version.stderr).lower()
    issues: list[str] = []
    if require_usb and "without usb support" in version_text:
        issues.append("USB support")

    help_result = _run((str(binary), "help"), check=False)
    if help_result.returncode != 0:
        issues.append("help probe")
        return issues
    help_text = (help_result.stdout + help_result.stderr).lower()
    required_commands = ["load", "verify", "reboot"]
    if require_signing:
        required_commands.append("seal")
    issues.extend(
        f"'{command}' command"
        for command in required_commands
        if not re.search(rf"\b{re.escape(command)}\b", help_text)
    )
    return issues


def _libusb_build_support_available() -> bool:
    return _run(("pkg-config", "--exists", "libusb-1.0"), check=False).returncode == 0


def _managed_build_dir(repo_root: Path, name: str, override: str = "") -> Path:
    """Build directory of a source-built host tool, required below .build/."""
    build = _resolve_component_dir(repo_root, f".build/tools/{name}", override)
    expected_build_root = (repo_root / ".build").resolve(strict=False)
    try:
        build.relative_to(expected_build_root)
    except ValueError as error:
        raise ComponentError(f"{name} build output must be inside {expected_build_root}") from error
    return build


def _cmake_build(source: Path, build: Path, definitions: Sequence[str]) -> None:
    """Configure and build SOURCE from scratch in BUILD."""
    if build.exists():
        _remove_tree(build)
    _run(("cmake", "-S", str(source), "-B", str(build), *definitions))
    _run(("cmake", "--build", str(build), "-j", str(os.cpu_count() or 4)))


def ensure_picotool_linux(
    repo_root: Path,
    *,
    verify_only: bool,
    source_override: str = "",
    build_override: str = "",
    sdk_override: str = "",
    rebuild: bool = False,
) -> Path:
    config_path = repo_root / "third_party/picotool_version.conf"
    config = parse_config(config_path)
    require_values(
        config,
        ("PICOTOOL_REPO", "PICOTOOL_REF", "PICOTOOL_VERSION", "PICOTOOL_DIR"),
        config_path,
    )
    source = _resolve_component_dir(repo_root, config["PICOTOOL_DIR"], source_override)
    changed = sync_git_checkout(
        config["PICOTOOL_REPO"], config["PICOTOOL_REF"], source,
        verify_only=verify_only,
    )
    build = _managed_build_dir(repo_root, "picotool", build_override)
    binary = build / "picotool"
    sdk_config = parse_config(repo_root / "third_party/pico_sdk_version.conf")
    sdk = _resolve_component_dir(
        repo_root, sdk_config["PICO_SDK_DIR"], sdk_override or os.environ.get("PICO_SDK_PATH", "")
    )
    require_usb = _libusb_build_support_available()
    require_signing = (sdk / "lib/mbedtls/library").is_dir()

    if verify_only:
        if not _picotool_binary_matches(binary, config["PICOTOOL_VERSION"]):
            raise ComponentError(
                f"picotool version mismatch or binary missing at {binary}."
            )
        issues = _picotool_capability_issues(
            binary,
            require_usb=require_usb,
            require_signing=require_signing,
        )
        if issues:
            raise ComponentError(
                f"picotool capability mismatch at {binary}: {', '.join(issues)}."
            )
        ok(f"picotool ready: {binary}")
        return binary
    if changed:
        rebuild = True
    if binary.is_file() and not rebuild and _picotool_binary_matches(
        binary, config["PICOTOOL_VERSION"]
    ):
        issues = _picotool_capability_issues(
            binary,
            require_usb=require_usb,
            require_signing=require_signing,
        )
        if not issues:
            ok(f"picotool already built: {binary}")
            return binary
        info(
            "Rebuilding picotool because available dependencies require: "
            + ", ".join(issues)
        )
        rebuild = True
    if not sdk.is_dir():
        raise ComponentError(f"Pico SDK not found at {sdk}. Ensure pico-sdk first.")
    if not require_usb:
        info(
            "libusb-1.0 development files are unavailable; picotool will be built "
            "without USB device access. Install libusb-1.0-0-dev and pkg-config "
            "to enable self-repair on the next run."
        )
    _cmake_build(
        source, build, (f"-DPICO_SDK_PATH={sdk}", "-DCMAKE_BUILD_TYPE=Release")
    )
    if not _picotool_binary_matches(binary, config["PICOTOOL_VERSION"]):
        raise ComponentError(f"Built picotool does not report {config['PICOTOOL_VERSION']}.")
    issues = _picotool_capability_issues(
        binary,
        require_usb=require_usb,
        require_signing=require_signing,
    )
    if issues:
        raise ComponentError(
            f"Built picotool lacks expected capabilities: {', '.join(issues)}."
        )
    ok(f"picotool ready: {binary}")
    return binary


_CPPCHECK_SUPPORT_FILES = ("cfg/std.cfg", "addons/misra.py", "addons/cppcheckdata.py")


def _cppcheck_config(repo_root: Path) -> dict[str, str]:
    config, config_path = supplied_pin(repo_root, "cppcheck")
    require_values(
        config,
        ("CPPCHECK_REPO", "CPPCHECK_REF", "CPPCHECK_VERSION", "CPPCHECK_DIR"),
        config_path,
    )
    return config


def _cppcheck_definitions(build: Path) -> tuple[str, ...]:
    # The build copies cfg/, addons/ and platforms/ into its bin/; FILESDIR
    # points there too, so both cppcheck lookups find the pinned MISRA addon.
    # DISABLE_DMAKE keeps the build from rewriting Makefile in the pinned
    # source checkout, which verification requires to stay clean.
    return (
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DFILESDIR={build / 'bin'}",
        "-DBUILD_GUI=OFF",
        "-DBUILD_TESTS=OFF",
        "-DDISABLE_DMAKE=ON",
    )


def _cppcheck_stamp(config: dict[str, str], build: Path) -> str:
    return "|".join(("cppcheck", config["CPPCHECK_REF"], *_cppcheck_definitions(build)))


def _cppcheck_problem(build: Path, version: str, stamp: str) -> str:
    """Why BUILD does not hold the pinned cppcheck, or "" when it does."""
    binary = build / "bin/cppcheck"
    if not binary.is_file():
        return f"no executable at {binary}"
    result = _run((str(binary), "--version"), check=False)
    reported = (result.stdout + result.stderr).strip()
    if result.returncode != 0 or reported != f"Cppcheck {version}":
        return f"{binary} reports {reported or 'no version'}, expected Cppcheck {version}"
    missing = [name for name in _CPPCHECK_SUPPORT_FILES if not (binary.parent / name).is_file()]
    if missing:
        return f"missing beside {binary}: {', '.join(missing)}"
    stamp_path = build / VERSION_STAMP
    if not stamp_path.is_file() or stamp_path.read_text(encoding="utf-8").strip() != stamp:
        return f"{build} was built from a different pin or configuration"
    return ""


def resolve_cppcheck(repo_root: Path, *, build_override: str = "") -> Path:
    """Return the pinned cppcheck executable, or fail with the repair command."""
    config = _cppcheck_config(repo_root)
    build = _managed_build_dir(repo_root, "cppcheck", build_override)
    problem = _cppcheck_problem(
        build, config["CPPCHECK_VERSION"], _cppcheck_stamp(config, build)
    )
    if problem:
        raise ComponentError(
            f"Pinned cppcheck {config['CPPCHECK_VERSION']} is not ready: {problem}. "
            f"Run {repo_root / 'scripts/ensure_cppcheck.sh'}."
        )
    return build / "bin/cppcheck"


def ensure_cppcheck_linux(
    repo_root: Path,
    *,
    verify_only: bool,
    source_override: str = "",
    build_override: str = "",
    rebuild: bool = False,
) -> Path:
    config = _cppcheck_config(repo_root)
    source = _resolve_component_dir(repo_root, config["CPPCHECK_DIR"], source_override)
    changed = sync_git_checkout(
        config["CPPCHECK_REPO"], config["CPPCHECK_REF"], source,
        verify_only=verify_only,
    )
    if verify_only:
        binary = resolve_cppcheck(repo_root, build_override=build_override)
        ok(f"cppcheck ready: {binary}")
        return binary
    version = config["CPPCHECK_VERSION"]
    build = _managed_build_dir(repo_root, "cppcheck", build_override)
    binary = build / "bin/cppcheck"
    stamp = _cppcheck_stamp(config, build)
    if rebuild or changed:
        reason = "rebuild requested" if rebuild else "checkout changed"
    else:
        reason = _cppcheck_problem(build, version, stamp)
    if not reason:
        ok(f"cppcheck {version} already built: {binary}")
        return binary
    info(f"Building cppcheck {version} ({reason})")
    _cmake_build(source, build, _cppcheck_definitions(build))
    (build / VERSION_STAMP).write_text(stamp + "\n", encoding="utf-8")
    problem = _cppcheck_problem(build, version, stamp)
    if problem:
        raise ComponentError(f"Built cppcheck is not the pinned build: {problem}")
    ok(f"cppcheck {version} ready: {binary}")
    return binary


def _version_tuple(text: str) -> tuple[int, int, int]:
    match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", text)
    if not match:
        return (0, 0, 0)
    return tuple(int(part or 0) for part in match.groups())  # type: ignore[return-value]


def _windows_tool_specs(repo_root: Path) -> list[ToolArchive]:
    path = repo_root / "third_party/windows_tools_version.conf"
    config = parse_config(path)
    prefixes = ("CMAKE", "NINJA", "GNU_ARM", "OPENOCD", "PICOTOOL")
    required = tuple(
        f"WINDOWS_{prefix}_{suffix}"
        for prefix in prefixes
        for suffix in ("VERSION", "URL", "SHA256", "EXECUTABLE")
    )
    require_values(config, required, path)
    checks: dict[str, Callable[[str], bool]] = {
        "cmake": lambda output: (3, 20, 0) <= _version_tuple(output) < (4, 0, 0),
        "ninja": lambda output: _version_tuple(output) > (0, 0, 0),
        "gnu-arm": lambda output: "arm-none-eabi" in output.lower() and _version_tuple(output) > (0, 0, 0),
        "openocd": lambda output: "open on-chip debugger 0.12" in output.lower(),
        "picotool": lambda output: _picotool_reports(
            output, config["WINDOWS_PICOTOOL_VERSION"]
        ),
    }
    names = (
        ("cmake", "CMAKE", ("--version",)),
        ("ninja", "NINJA", ("--version",)),
        ("gnu-arm", "GNU_ARM", ("--version",)),
        ("openocd", "OPENOCD", ("--version",)),
        ("picotool", "PICOTOOL", ("version",)),
    )
    return [
        ToolArchive(
            name,
            config[f"WINDOWS_{prefix}_VERSION"],
            config[f"WINDOWS_{prefix}_URL"],
            config[f"WINDOWS_{prefix}_SHA256"],
            config[f"WINDOWS_{prefix}_EXECUTABLE"],
            version_args,
            checks[name],
            tuple(shlex.split(config.get(f"WINDOWS_{prefix}_EXCLUDE", ""))),
        )
        for name, prefix, version_args in names
    ]


def _tool_output(executable: Path, arguments: Sequence[str]) -> str:
    result = _run((str(executable), *arguments), check=False)
    return (result.stdout + result.stderr).strip()


def _system_tool(spec: ToolArchive) -> Optional[Path]:
    path = shutil.which(spec.executable)
    if not path:
        return None
    executable = Path(path).resolve()
    if not spec.version_check(_tool_output(executable, spec.version_args)):
        return None
    if spec.name == "openocd" and _openocd_scripts_root(executable) is None:
        info(
            f"System OpenOCD at {executable} lacks the required scripts; "
            "falling back to the managed archive."
        )
        return None
    return executable


def _openocd_scripts_root(executable: Path) -> Optional[Path]:
    candidates = (
        executable.parent / "scripts",
        executable.parent.parent / "share/openocd/scripts",
        executable.parent.parent / "scripts",
    )
    required = (
        "board/st_nucleo_g4.cfg",
        "interface/cmsis-dap.cfg",
        "interface/stlink.cfg",
        "target/rp2040.cfg",
        "target/rp2350.cfg",
        "target/stm32g4x.cfg",
    )
    return next(
        (root for root in candidates if all((root / path).is_file() for path in required)),
        None,
    )


def _arm_cstdlib_self_check(compiler: Path, build_root: Path) -> None:
    build_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="jh-arm-self-check-", dir=build_root) as temp_text:
        temp = Path(temp_text)
        source = temp / "cstdlib_probe.cpp"
        output = temp / "cstdlib_probe.o"
        source.write_text("#include <cstdlib>\nint main() { return 0; }\n", encoding="utf-8")
        result = _run(
            (
                str(compiler),
                "-mcpu=cortex-m0plus",
                "-mthumb",
                "-mfloat-abi=soft",
                "-c",
                str(source),
                "-o",
                str(output),
            ),
            check=False,
        )
        if result.returncode != 0 or not output.is_file():
            detail = (result.stderr or result.stdout).strip()
            raise ComponentError(
                "GNU Arm self-check could not compile <cstdlib>. Use a shorter physical "
                f"tools/build root.\n{detail}"
            )


def _verify_sibling_tools(compiler: Path, names: Sequence[str]) -> None:
    missing = [name for name in names if not (compiler.parent / name).is_file()]
    if missing:
        raise ComponentError(
            f"Toolchain beside {compiler} is incomplete; missing: {', '.join(missing)}"
        )


def ensure_windows_tools(
    repo_root: Path,
    tools_root: Path,
    build_root: Path,
    *,
    verify_only: bool,
    prefer_managed: bool,
) -> dict[str, str]:
    if sys.platform != "win32" and os.environ.get("JH_TEST_WINDOWS_TOOLS") != "1":
        raise ComponentError("windows-tools is supported only on native Windows.")
    tools_root = tools_root.resolve(strict=False)
    build_root = build_root.resolve(strict=False)
    tools_root.mkdir(parents=True, exist_ok=True) if not verify_only else None
    resolved: dict[str, str] = {}
    arm_compiler: Optional[Path] = None
    for spec in _windows_tool_specs(repo_root):
        executable = None if prefer_managed else _system_tool(spec)
        source = "system"
        if executable is None:
            destination = tools_root / f"{spec.name}-{spec.version}"
            exclusions_digest = hashlib.sha256(
                "\n".join(spec.excluded_members).encode("utf-8")
            ).hexdigest()
            stamp = f"{spec.name}|{spec.version}|{spec.sha256}|exclude-{exclusions_digest}"
            sync_archive(
                spec.url, spec.sha256, destination,
                verify_only=verify_only, version_stamp=stamp,
                excluded_members=spec.excluded_members,
            )
            executable = _find_executable(destination, spec.executable)
            output = _tool_output(executable, spec.version_args)
            if not spec.version_check(output):
                raise ComponentError(
                    f"{spec.name} version contract failed at {executable}: {output}"
                )
            source = "managed"
        resolved[spec.name] = str(executable)
        info(f"Resolved {spec.name} ({source}): {executable}")
        if spec.name == "gnu-arm":
            arm_compiler = executable.parent / "arm-none-eabi-g++.exe"
            _verify_sibling_tools(
                executable,
                (
                    "arm-none-eabi-g++.exe", "arm-none-eabi-ar.exe",
                    "arm-none-eabi-ranlib.exe", "arm-none-eabi-objcopy.exe",
                    "arm-none-eabi-objdump.exe", "arm-none-eabi-gdb.exe",
                ),
            )
        elif spec.name == "openocd":
            if _openocd_scripts_root(executable) is None:
                raise ComponentError(
                    "Resolved OpenOCD lacks the required CMSIS-DAP, ST-Link, "
                    f"RP2040, RP2350, or STM32G4 scripts: {executable}"
                )
        elif spec.name == "picotool":
            issues = _picotool_capability_issues(
                executable,
                require_usb=True,
                require_signing=True,
            )
            if issues:
                raise ComponentError(
                    "Resolved picotool lacks required capabilities: "
                    + ", ".join(issues)
                )

    # The managed directory is named after the pinned release asset, so the
    # name follows the jhbase pin instead of repeating its GCC major here.
    riscv_stem = supplied_pin(repo_root, "riscv-toolchain")[0]["RISCV_TOOLCHAIN_ASSET_STEM"]
    riscv_dir = tools_root / riscv_stem
    riscv = ensure_riscv_toolchain(
        repo_root, verify_only=verify_only, directory_override=str(riscv_dir)
    )
    resolved["riscv"] = str(riscv)
    _verify_sibling_tools(
        riscv,
        (
            "riscv32-unknown-elf-g++.exe", "riscv32-unknown-elf-ar.exe",
            "riscv32-unknown-elf-ranlib.exe", "riscv32-unknown-elf-objcopy.exe",
            "riscv32-unknown-elf-objdump.exe",
        ),
    )
    if arm_compiler is None or not arm_compiler.is_file():
        raise ComponentError("arm-none-eabi-g++.exe is missing beside the resolved GCC.")
    _arm_cstdlib_self_check(arm_compiler, build_root)
    ok(f"GNU Arm <cstdlib> self-check passed: {arm_compiler}")

    state = tools_root / "resolved-tools.json"
    serialized = json.dumps(resolved, indent=2, sort_keys=True) + "\n"
    if verify_only:
        if not state.is_file() or state.read_text(encoding="utf-8") != serialized:
            raise ComponentError(f"Resolved tools state missing or stale: {state}")
    else:
        if not state.is_file() or state.read_text(encoding="utf-8") != serialized:
            temporary = state.with_name(f".{state.name}.{uuid.uuid4().hex}.tmp")
            temporary.write_text(serialized, encoding="utf-8", newline="\n")
            temporary.replace(state)
    return resolved


def _component_enabled(name: str, arguments: argparse.Namespace) -> bool:
    if name not in (
        "freertos", "pico-sdk", "esp-idf", "picotool", "riscv-toolchain"
    ):
        return True
    if name == "freertos":
        raw_definitions = os.environ.get("EXTRA_HAL_DEFINES", "")
        definitions = [
            item.strip()
            for item in raw_definitions.split(";")
            if item.strip()
        ]
        for definition in definitions:
            normalized = definition.removeprefix("-D")
            if "$<" in normalized:
                raise ComponentError(
                    f"[JH-CFG-VALUE] {normalized} is unsupported; "
                    "generator expressions are not accepted"
                )
            if "HAL_ENABLE_" not in normalized:
                continue
            if re.fullmatch(r"HAL_ENABLE_[A-Z0-9_]+(?:=1)?", normalized):
                continue
            if "HAL_ENABLE_" in normalized:
                raise ComponentError(
                    f"[JH-CFG-VALUE] {normalized} is unsupported; "
                    "use a standalone bare symbol or an explicit value of 1"
                )

        environment_value = os.environ.get("HAL_ENABLE_FREERTOS")
        if environment_value not in (None, "", "1"):
            raise ComponentError(
                "[JH-CFG-VALUE] HAL_ENABLE_FREERTOS="
                f"{environment_value} is unsupported; omit the environment "
                "variable to disable it"
            )
        requested = environment_value == "1" or any(
            item.removeprefix("-D")
            in {"HAL_ENABLE_FREERTOS", "HAL_ENABLE_FREERTOS=1"}
            for item in definitions
        )
        return (
            arguments.force
            or arguments.verify_only
            or arguments.enable
            or requested
        )
    if arguments.force or arguments.verify_only or arguments.enable:
        return True
    environment = {
        "pico-sdk": "JH_ENABLE_PICO_SDK",
        "esp-idf": "JH_ENABLE_ESP_IDF",
        "picotool": "JH_ENABLE_PICOTOOL",
        "riscv-toolchain": "JH_ENABLE_RISCV_TOOLCHAIN",
    }
    return bool(os.environ.get(environment[name]))


def ensure_component(arguments: argparse.Namespace) -> None:
    name = arguments.name
    if not _component_enabled(name, arguments):
        info(f"{name} not requested; skipping component ensure.")
        return
    repo_root = Path(arguments.repo_root).resolve()
    directory = arguments.directory or ""
    if name == "freertos" and arguments.kernel_dir:
        directory = arguments.kernel_dir
    elif name == "freertos" and os.environ.get("JH_FREERTOS_KERNEL_DIR"):
        directory = os.environ["JH_FREERTOS_KERNEL_DIR"]
    if name == "pico-sdk" and arguments.sdk_dir:
        directory = arguments.sdk_dir
    elif name == "pico-sdk" and os.environ.get("JH_PICO_SDK_DIR"):
        directory = os.environ["JH_PICO_SDK_DIR"]
    if name == "esp-idf" and os.environ.get("JH_ESP_IDF_DIR"):
        directory = os.environ["JH_ESP_IDF_DIR"]
    if name == "jhbase" and not directory and os.environ.get("JH_JHBASE_DIR"):
        directory = os.environ["JH_JHBASE_DIR"]
    submodules = None
    if arguments.no_submodules:
        submodules = ""
    elif arguments.with_submodules is not None:
        submodules = arguments.with_submodules
    if name in GIT_COMPONENTS:
        component_directory = ensure_git_component(
            name, repo_root, verify_only=arguments.verify_only,
            directory_override=directory, submodule_override=submodules,
        )
        if name == "esp-idf" and getattr(arguments, "install_sdk_tools", True):
            ensure_esp_idf_tools(
                repo_root,
                component_directory,
                verify_only=arguments.verify_only,
            )
    elif name == "riscv-toolchain":
        ensure_riscv_toolchain(
            repo_root, verify_only=arguments.verify_only,
            directory_override=directory,
        )
    elif name == "cmake-minimum":
        ensure_cmake_minimum(
            repo_root, verify_only=arguments.verify_only,
            directory_override=directory,
        )
    elif name == "pmd":
        ensure_pmd(
            repo_root,
            verify_only=arguments.verify_only,
            directory_override=directory,
        )
    elif name == "picotool":
        if sys.platform == "win32":
            raise ComponentError(
                "Use windows-tools to install the authenticated prebuilt picotool.exe."
            )
        ensure_picotool_linux(
            repo_root,
            verify_only=arguments.verify_only,
            source_override=arguments.picotool_dir or directory,
            build_override=arguments.build_dir or "",
            sdk_override=arguments.sdk_dir or "",
            rebuild=arguments.rebuild,
        )
    elif name == "cppcheck":
        if sys.platform == "win32":
            raise ComponentError("The pinned cppcheck build and its gates are Linux-only.")
        ensure_cppcheck_linux(
            repo_root,
            verify_only=arguments.verify_only,
            source_override=directory,
            build_override=arguments.build_dir or "",
            rebuild=arguments.rebuild,
        )
    else:
        raise ComponentError(f"Unknown component: {name}")


def _common_component_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--enable", "--native", "--build", "--freertos", action="store_true")
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--dir", dest="directory")
    parser.add_argument("--kernel-dir")
    parser.add_argument("--sdk-dir")
    parser.add_argument("--picotool-dir")
    parser.add_argument("--build-dir")
    parser.add_argument("--no-submodules", action="store_true")
    parser.add_argument("--with-submodules")
    parser.add_argument("--rebuild", action="store_true")
    parser.set_defaults(install_sdk_tools=True)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    component = subparsers.add_parser("component", help="ensure one pinned component")
    component.add_argument("name", choices=ALL_COMPONENT_NAMES)
    _common_component_arguments(component)

    sources = subparsers.add_parser(
        "source-components", help="ensure all pinned source checkouts"
    )
    sources.add_argument("--verify-only", action="store_true")
    sources.add_argument("--force", action="store_true")
    sources.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))

    all_parser = subparsers.add_parser("all", help="ensure every pinned component")
    all_parser.add_argument("--verify-only", action="store_true")
    all_parser.add_argument("--force", action="store_true")
    all_parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))

    tool_path = subparsers.add_parser(
        "tool-path", help="print a ready pinned host tool executable"
    )
    tool_path.add_argument("name", choices=("cppcheck",))
    tool_path.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))

    tools = subparsers.add_parser("windows-tools", help="ensure native Windows host tools")
    tools.add_argument("--verify-only", action="store_true")
    tools.add_argument(
        "--prefer-managed",
        action="store_true",
        help="prefer pinned managed archives over compatible system tools",
    )
    tools.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))
    tools.add_argument("--tools-root", required=True)
    tools.add_argument("--build-root", required=True)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)
    try:
        if arguments.command == "component":
            ensure_component(arguments)
        elif arguments.command in ("source-components", "all"):
            names = tuple(GIT_COMPONENTS) if arguments.command == "source-components" else SOURCE_COMPONENT_ORDER
            for name in names:
                component_arguments = argparse.Namespace(
                    name=name,
                    verify_only=arguments.verify_only,
                    force=True,
                    enable=True,
                    repo_root=arguments.repo_root,
                    directory=None,
                    kernel_dir=None,
                    sdk_dir=None,
                    picotool_dir=None,
                    build_dir=None,
                    no_submodules=False,
                    with_submodules=None,
                    rebuild=False,
                    install_sdk_tools=arguments.command != "source-components",
                )
                ensure_component(component_arguments)
            if arguments.command == "source-components":
                print("All managed source components are synchronized.")
            else:
                print("All managed third-party components are synchronized.")
        elif arguments.command == "tool-path":
            print(resolve_cppcheck(Path(arguments.repo_root).resolve()))
        elif arguments.command == "windows-tools":
            ensure_windows_tools(
                Path(arguments.repo_root).resolve(),
                Path(arguments.tools_root).expanduser(),
                Path(arguments.build_root).expanduser(),
                verify_only=arguments.verify_only,
                prefer_managed=arguments.prefer_managed,
            )
        return 0
    except ComponentError as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
