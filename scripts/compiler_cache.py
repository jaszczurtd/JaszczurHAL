#!/usr/bin/env python3
"""Shared compiler cache configuration for clean gate and example builds."""

from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import os
from pathlib import Path
import shlex
import sys
import tempfile

from codegen_support import atomic_write_text
import component_manager


def compiler_cache_environment(repo_root: Path, label: str) -> tuple[Path, dict[str, str]]:
    """Verify the pinned tool and isolate its settings from the caller's cache."""
    executable = component_manager.ensure_ccache(repo_root, verify_only=True)
    if os.name == "nt":
        cache_root = Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData/Local")
    else:
        cache_root = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache")
    cache_dir = Path(os.environ.get("JH_COMPILER_CACHE_DIR") or
                     cache_root / "jaszczurhal/ccache").expanduser().resolve()
    log_dir = repo_root / ".build/compiler-cache"
    log_dir.mkdir(parents=True, exist_ok=True)
    config = log_dir / "ccache.conf"
    # ccache matches base_dir as text: keep the caller's spelling, without
    # expanding symlinks or Windows 8.3 names into another one.
    base_dir = Path(os.path.abspath(repo_root)).as_posix()
    if "\n" in base_dir or "\r" in base_dir:
        raise ValueError("Compiler cache checkout path contains a newline")
    atomic_write_text(config, "\n".join((
        "compiler_check = content",
        # __has_include in the HAL configuration hooks must notice new headers.
        "direct_mode = false",
        "hash_dir = false",
        f"base_dir = {base_dir.replace('$', '$$')}",
        "max_size = 5G",
        "namespace = jaszczurhal",
        "",
    )), error_type=ValueError)
    with tempfile.NamedTemporaryFile(dir=log_dir, prefix=f"{label}-", suffix=".log", delete=False) as log:
        stats = log.name
    # Sloppiness, ignored headers or a caller's config must not weaken checks.
    environment = {key: value for key, value in os.environ.items() if not key.startswith("CCACHE_")}
    environment.update({
        "PATH": os.pathsep.join((str(executable.parent), environment.get("PATH", ""))),
        "CCACHE_CONFIGPATH": str(config),
        "CCACHE_DIR": str(cache_dir),
        "CCACHE_STATSLOG": stats,
        # CMake's launcher also reaches IDF's bootloader ExternalProject.
        "IDF_CCACHE_ENABLE": "0",
    })
    for language in ("C", "CXX"):
        environment[f"CMAKE_{language}_COMPILER_LAUNCHER"] = str(executable)
    print(f"compiler cache: {cache_dir} (statistics: {stats})", file=sys.stderr, flush=True)
    return executable, environment


def main() -> int:
    """Emit quoted Bash assignments; the gate applies them in a stage subshell."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label")
    args = parser.parse_args()
    try:
        # Component verification logs are diagnostics, not shell assignments.
        with redirect_stdout(sys.stderr):
            _, environment = compiler_cache_environment(Path(__file__).resolve().parents[1], args.label)
    except (ValueError, RuntimeError, OSError) as error:
        print(f"error: {error}; run scripts/ensure_ccache.sh", file=sys.stderr)
        return 1
    for key in os.environ.keys() - environment.keys():
        print(f"unset -- {shlex.quote(key)}")
    for key, value in environment.items():
        if os.environ.get(key) != value:
            print(f"export {shlex.quote(key)}={shlex.quote(value)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
