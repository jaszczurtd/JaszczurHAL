#!/usr/bin/env python3
"""Guard the STM32G474 host-compiler sanity configuration used by analysis."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
HOST_SANITY_FLAG = "-DJH_STM32_HOST_SANITY=ON"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


recipe = (ROOT / "stm32_lib" / "CMakeLists.txt").read_text(encoding="utf-8")
require(
    "option(JH_STM32_HOST_SANITY" in recipe,
    "stm32_lib does not declare the host-compiler sanity option",
)
require(
    'set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH' in recipe,
    "stm32_lib no longer pins its toolchain into the CMake cache",
)
require(
    "-DJH_STM32_HOST_SANITY=ON for the host-compiler analysis build." in recipe,
    "the missing-toolchain error does not name the host-compiler mode",
)

# Every caller configuring stm32_lib must state its mode: a cross toolchain for
# firmware, or the host-compiler analysis mode.
for relative in ("runalltests.sh", ".github/workflows/ci.yml"):
    lines = (ROOT / relative).read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines):
        if "-S stm32_lib" not in line:
            continue
        # Follow shell and PowerShell continuations so the whole flag list is read.
        window = [line]
        cursor = index
        while cursor + 1 < len(lines) and window[-1].rstrip().endswith(("\\", "`")):
            cursor += 1
            window.append(lines[cursor])
        text = "\n".join(window)
        require(
            "-DCMAKE_TOOLCHAIN_FILE" in text or HOST_SANITY_FLAG in text,
            f"{relative}:{index + 1} configures stm32_lib without declaring its mode",
        )

if sys.platform == "win32":
    print("host-compiler STM32 configure probe: skipped on native Windows")
    raise SystemExit(0)

cmake = shutil.which("cmake")
require(cmake is not None, "cmake is required to verify the host sanity configure")

# Board-profile generation only writes below a managed build tree.
scratch = ROOT / ".build" / "tests" / f"stm32-host-sanity-{os.getpid()}"
accepted = scratch / "with-flag"
rejected = scratch / "without-flag"
try:
    with_flag = subprocess.run(
        [cmake, "-S", str(ROOT / "stm32_lib"), "-B", str(accepted), HOST_SANITY_FLAG],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        with_flag.returncode == 0,
        "host-compiler STM32 sanity configure failed:\n"
        f"{with_flag.stdout}\n{with_flag.stderr}",
    )
    require(
        "host-compiler sanity build" in with_flag.stdout,
        "host-compiler STM32 configure does not report its mode",
    )

    without_flag = subprocess.run(
        [cmake, "-S", str(ROOT / "stm32_lib"), "-B", str(rejected)],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        without_flag.returncode != 0,
        "stm32_lib accepted a firmware configure without a cross toolchain",
    )
    require(
        "CMAKE_TOOLCHAIN_FILE is required for stm32_lib" in without_flag.stderr,
        "missing-toolchain diagnostic changed",
    )
finally:
    shutil.rmtree(scratch, ignore_errors=True)

print("STM32 host-compiler sanity configuration verified")
