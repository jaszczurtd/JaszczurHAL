#!/usr/bin/env python3
"""Cross-platform manager for pinned JaszczurHAL components and host tools."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import platform
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


PIN_FILE = ".jh-archive-pin"
MANIFEST_FILE = ".jh-content.sha256"
VERSION_STAMP = ".jaszczurhal-component-version"
EXCLUSIONS_FILE = ".jh-archive-exclusions"
SOURCE_COMPONENT_ORDER = (
    "bearssl",
    "cjson",
    "lodepng",
    "jpeg",
    "fatfs",
    "unity",
    "lwip",
    "littlefs",
    "btstack",
    "freertos",
    "pico-sdk",
    "picotool",
    "riscv-toolchain",
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
    version_validator: Optional[Callable[[Path, str], None]] = None


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
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            [str(item) for item in command],
            cwd=str(cwd) if cwd else None,
            check=False,
            capture_output=True,
            text=True,
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
    return _run(("git", "-C", str(directory), *arguments), check=check).stdout.strip()


def _git_head(directory: Path) -> str:
    if not (directory / ".git").exists():
        return ""
    return _git_output(directory, "rev-parse", "HEAD", check=False)


def _clone_pinned(repository: str, reference: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = destination.parent / f".{destination.name}.clone.{uuid.uuid4().hex}"
    _remove_tree(staging)
    try:
        _run(("git", "init", "-q", str(staging)))
        _git_output(staging, "remote", "add", "origin", repository)
        _git_output(staging, "fetch", "--depth", "1", "origin", reference)
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


def _sync_submodules(directory: Path, submodules: Sequence[str], verify_only: bool) -> None:
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
    required_paths: Sequence[str] = (),
) -> bool:
    """Synchronize an exact-ref checkout and return whether it changed."""
    actual = _git_head(destination)
    changed = False
    if actual != reference:
        if verify_only:
            found = actual or "non-git directory"
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

    if clean:
        status_text = _git_output(destination, "status", "--porcelain")
        if status_text:
            if verify_only:
                raise ComponentError(
                    f"Pinned checkout has local changes at {destination} (verify-only)."
                )
            _git_output(destination, "reset", "--hard", reference)
            _git_output(destination, "clean", "-fdx")
            changed = True

    _sync_submodules(destination, submodules, verify_only)
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


GIT_COMPONENTS = {
    spec.name: spec
    for spec in (
        GitComponent(
            "bearssl", "BearSSL", "bearssl_version.conf", "BEARSSL",
            ("LICENSE.txt", "inc/bearssl.h", "src/inner.h", "src/ssl/ssl_client.c"),
        ),
        GitComponent(
            "cjson", "cJSON", "cjson_version.conf", "CJSON",
            ("LICENSE", "cJSON.c", "cJSON.h", "cJSON_Utils.c", "cJSON_Utils.h"),
            clean=True,
        ),
        GitComponent(
            "lodepng", "LodePNG", "lodepng_version.conf", "LODEPNG",
            ("LICENSE", "lodepng.cpp", "lodepng.h"), clean=True,
        ),
        GitComponent(
            "jpeg", "TJpg_Decoder", "jpeg_version.conf", "JPEG",
            ("license.txt", "src/tjpgd.c", "src/tjpgd.h", "src/tjpgdcnf.h"),
            clean=True,
        ),
        GitComponent(
            "fatfs", "FatFs", "fatfs_version.conf", "FATFS",
            (
                "LICENSE.txt", "source/00history.txt", "source/00readme.txt",
                "source/diskio.h", "source/ff.c", "source/ff.h",
                "source/ffsystem.c", "source/ffunicode.c",
            ),
        ),
        GitComponent(
            "unity", "Unity", "unity_version.conf", "UNITY",
            ("LICENSE.txt", "src/unity.c", "src/unity.h", "src/unity_internals.h"),
            clean=True,
        ),
        GitComponent(
            "lwip", "lwIP", "lwip_version.conf", "LWIP",
            ("COPYING", "src/core/init.c", "src/include/lwip/init.h", "src/netif/ethernet.c"),
            version_validator=_version_lwip,
        ),
        GitComponent(
            "littlefs", "littlefs", "littlefs_version.conf", "LITTLEFS",
            ("LICENSE.md", "lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h"),
            version_validator=_version_littlefs,
        ),
        GitComponent(
            "btstack", "BTstack", "btstack_version.conf", "BTSTACK",
            (
                "LICENSE", "src/bluetooth.h", "src/hci.c",
                "src/ble/att_server.c", "platform/embedded/btstack_run_loop_embedded.c",
                "tool/compile_gatt.py",
            ),
        ),
        GitComponent(
            "freertos", "FreeRTOS-Kernel", "freertos_core_version.conf", "FREERTOS_KERNEL",
            (
                "include/FreeRTOS.h", "include/task.h", "include/semphr.h",
                "portable/GCC/ARM_CM4F/port.c", "portable/GCC/ARM_CM4F/portmacro.h",
                "portable/MemMang/heap_4.c", "tasks.c", "queue.c", "list.c",
                "timers.c", "event_groups.c", "stream_buffer.c",
                "portable/ThirdParty/GCC/RP2040/port.c",
                "portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ/non_secure/port.c",
                "portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_RISC-V/port.c",
            ),
            submodules_key="FREERTOS_KERNEL_SUBMODULES",
            version_validator=_version_freertos,
        ),
        GitComponent(
            "pico-sdk", "Pico SDK", "pico_sdk_version.conf", "PICO_SDK",
            (
                "pico_sdk_init.cmake", "pico_sdk_version.cmake",
                "external/pico_sdk_import.cmake",
                "src/rp2_common/hardware_flash/include/hardware/flash.h",
                "src/rp2_common/pico_multicore/include/pico/multicore.h",
                "src/rp2040", "src/rp2350",
            ),
            submodules_key="PICO_SDK_SUBMODULES",
            version_validator=_version_pico,
        ),
    )
}


def _resolve_component_dir(repo_root: Path, configured: str, override: str) -> Path:
    path = Path(override or configured).expanduser()
    if not path.is_absolute():
        path = repo_root / path
    return path.resolve(strict=False)


def ensure_git_component(
    name: str,
    repo_root: Path,
    *,
    verify_only: bool,
    directory_override: str = "",
    submodule_override: Optional[str] = None,
) -> Path:
    spec = GIT_COMPONENTS[name]
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
    if submodule_override is not None:
        submodules_text = submodule_override
    submodules = tuple(shlex.split(submodules_text))
    external = bool(directory_override) and name in ("freertos", "pico-sdk")
    if external:
        actual = _git_head(destination)
        if actual != config[f"{spec.prefix}_REF"]:
            raise ComponentError(
                f"External {spec.label} checkout mismatch at {destination}: expected "
                f"{config[f'{spec.prefix}_REF']}, found {actual or 'non-git directory'}."
            )
        _sync_submodules(destination, submodules, verify_only)
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
            required_paths=spec.required_paths,
        )
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
    config_path = repo_root / "third_party/riscv_toolchain_version.conf"
    config = parse_config(config_path)
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


def _picotool_binary_matches(binary: Path, version: str) -> bool:
    result = _run((str(binary), "version"), check=False)
    return result.returncode == 0 and version in (result.stdout + result.stderr)


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
    build = _resolve_component_dir(repo_root, ".build/tools/picotool", build_override)
    expected_build_root = (repo_root / ".build").resolve(strict=False)
    try:
        build.relative_to(expected_build_root)
    except ValueError as error:
        raise ComponentError(f"picotool build output must be inside {expected_build_root}") from error
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
    if build.exists():
        _remove_tree(build)
    _run(
        (
            "cmake", "-S", str(source), "-B", str(build),
            f"-DPICO_SDK_PATH={sdk}", "-DCMAKE_BUILD_TYPE=Release",
        )
    )
    _run(("cmake", "--build", str(build), "-j", str(os.cpu_count() or 4)))
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
        "picotool": lambda output: "2.2.0" in output,
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

    riscv_dir = tools_root / "riscv-toolchain-15"
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
    if name not in ("freertos", "pico-sdk", "picotool", "riscv-toolchain"):
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
    submodules = None
    if arguments.no_submodules:
        submodules = ""
    elif arguments.with_submodules is not None:
        submodules = arguments.with_submodules
    if name in GIT_COMPONENTS:
        ensure_git_component(
            name, repo_root, verify_only=arguments.verify_only,
            directory_override=directory, submodule_override=submodules,
        )
    elif name == "riscv-toolchain":
        ensure_riscv_toolchain(
            repo_root, verify_only=arguments.verify_only,
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


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    component = subparsers.add_parser("component", help="ensure one pinned component")
    component.add_argument("name", choices=(*GIT_COMPONENTS, "picotool", "riscv-toolchain"))
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
                )
                ensure_component(component_arguments)
            if arguments.command == "source-components":
                print("All managed source components are synchronized.")
            else:
                print("All managed third-party components are synchronized.")
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
