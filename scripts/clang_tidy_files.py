#!/usr/bin/env python3
"""Prepare a deterministic compile database and emit clang-tidy file regexes."""

import argparse
import functools
import json
import re
import shlex
import subprocess
from pathlib import Path


HOST_UTILS_EXCLUDES = {
    "unity.c",
}

HOST_SHARED_EXCLUDES = {
    "src/hal/impl/shared/frameworks/cjson/cJSON.c",
    "src/hal/impl/shared/frameworks/cjson/cJSON_Utils.c",
    "src/hal/impl/shared/frameworks/filesystem/ff16/ff.c",
    "src/hal/impl/shared/frameworks/filesystem/ff16/ffsystem.c",
    "src/hal/impl/shared/frameworks/filesystem/ff16/ffunicode.c",
    "src/hal/impl/shared/frameworks/jpeg/JPEGDecoder.cpp",
    "src/hal/impl/shared/frameworks/jpeg/picojpeg.c",
    "src/hal/impl/shared/frameworks/lodepng/lodepng.cpp",
}


def _normalise(path: Path) -> str:
    return path.resolve().as_posix()


def _relative_to_root(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def _load_compile_entries(build_dir: Path) -> list[dict]:
    compile_db = build_dir / "compile_commands.json"
    with compile_db.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _entry_arguments(entry: dict) -> list[str]:
    arguments = entry.get("arguments")
    if arguments is not None:
        return list(arguments)
    return shlex.split(entry["command"])


@functools.lru_cache(maxsize=None)
def _compiler_system_includes(
    compiler: str, architecture_flags: tuple[str, ...], language: str
) -> tuple[str, ...]:
    result = subprocess.run(
        [
            compiler,
            *architecture_flags,
            "-E",
            "-v",
            "-x",
            language,
            "/dev/null",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    include_paths: list[str] = []
    in_search_list = False
    for line in result.stderr.splitlines():
        stripped = line.strip()
        if stripped == "#include <...> search starts here:":
            in_search_list = True
            continue
        if stripped == "End of search list.":
            break
        if in_search_list and stripped and not stripped.startswith("("):
            include_paths.append(str(Path(stripped).resolve()))

    if not include_paths:
        raise RuntimeError(f"no system include paths reported by {compiler}")

    # GCC's private stdint.h expects GCC-only __UINT*_C builtins that clang
    # intentionally does not define. Prefer the target C library's public
    # headers before GCC's private include/include-fixed directories, while
    # retaining the original order of C++ standard-library directories.
    cxx_paths = [path for path in include_paths if "/include/c++/" in path]
    target_paths = [
        path
        for path in include_paths
        if "/include/c++/" not in path
        and "/lib/gcc/" not in path
        and not path.endswith("/include-fixed")
    ]
    compiler_paths = [
        path
        for path in include_paths
        if path not in cxx_paths and path not in target_paths
    ]
    return tuple([*cxx_paths, *target_paths, *compiler_paths])


def _prepare_cross_entry(entry: dict, source: Path) -> dict:
    arguments = _entry_arguments(entry)
    compiler_name = Path(arguments[0]).name
    if not compiler_name.startswith("arm-none-eabi-"):
        return entry

    language = "c++" if source.suffix == ".cpp" else "c"
    architecture_flags = tuple(
        argument
        for argument in arguments[1:]
        if argument == "-mthumb"
        or argument.startswith("-mcpu=")
        or argument.startswith("-mfpu=")
        or argument.startswith("-mfloat-abi=")
    )
    include_paths = _compiler_system_includes(
        arguments[0], architecture_flags, language
    )
    prepared = dict(entry)
    prepared.pop("command", None)
    prepared["arguments"] = [
        arguments[0],
        "--target=arm-none-eabi",
        *[
            argument
            for path in include_paths
            for argument in ("-isystem", path)
        ],
        *arguments[1:],
    ]
    return prepared


def _entry_source(entry: dict, build_dir: Path) -> Path:
    source = Path(entry["file"])
    if not source.is_absolute():
        source = Path(entry.get("directory", build_dir)) / source
    return source.resolve()


def _is_c_or_cpp(path: Path) -> bool:
    return path.suffix in {".c", ".cpp"}


def _include_host(path: Path, root: Path) -> bool:
    rel = _relative_to_root(path, root)
    name = path.name

    if not _is_c_or_cpp(path):
        return False
    if rel in HOST_SHARED_EXCLUDES:
        return False
    if rel.startswith("src/hal/hal_"):
        return True
    if rel.startswith("src/hal/impl/shared/"):
        return True
    if rel.startswith("src/utils/") and "/" not in rel[len("src/utils/") :]:
        return name not in HOST_UTILS_EXCLUDES
    return False


def _include_stm32(path: Path, root: Path) -> bool:
    rel = _relative_to_root(path, root)

    if not _is_c_or_cpp(path):
        return False
    if not rel.startswith("src/hal/impl/stm32g474/"):
        return False
    return "/drivers/littlefs/" not in rel


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--repo-root", default=Path.cwd(), type=Path)
    parser.add_argument("--profile", required=True, choices=("host", "stm32"))
    parser.add_argument("--output-compile-db", type=Path)
    args = parser.parse_args()

    include = _include_host if args.profile == "host" else _include_stm32
    selected: dict[Path, dict] = {}
    for entry in _load_compile_entries(args.build_dir):
        path = _entry_source(entry, args.build_dir)
        # Facade tests compile some shared sources with several feature sets.
        # clang-tidy 18 processes every matching command and can leak analyzer
        # state between duplicate entries, producing false va_list findings.
        # Keep one stable command per source for the static-analysis pass.
        if include(path, args.repo_root) and path not in selected:
            normalized_entry = dict(entry)
            normalized_entry["file"] = path.as_posix()
            if args.profile == "stm32":
                normalized_entry = _prepare_cross_entry(normalized_entry, path)
            selected[path] = normalized_entry

    files = sorted(selected, key=lambda path: path.as_posix())

    if args.output_compile_db is not None:
        args.output_compile_db.parent.mkdir(parents=True, exist_ok=True)
        entries = [selected[path] for path in files]
        with args.output_compile_db.open("w", encoding="utf-8") as handle:
            json.dump(entries, handle, indent=2)
            handle.write("\n")

    for path in files:
        print(f"^{re.escape(_normalise(path))}$")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
