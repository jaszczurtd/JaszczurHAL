#!/usr/bin/env python3
"""Every HAL header that defines a structure or a union includes
hal/core/hal_layout_check.h, so a #pragma pack opened between two HAL
includes stops the unit at the next HAL header."""

from __future__ import annotations

from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

from repo_root import hal_include_dirs, repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
SOURCE = ROOT / "src"
CHECK = SOURCE / "hal" / "core" / "hal_layout_check.h"
INCLUDE = '#include "hal/core/hal_layout_check.h"'
WORK = ROOT / ".build" / "tests" / "layout-check-headers"
# The upstream CYW43 driver copy is checked against its own checksums; its
# types stay internal to the radio driver.
EXCLUDED = (SOURCE / "hal" / "network" / "cyw43" / "vendor", CHECK)
DEFINITION = re.compile(r"\b(?:struct|union)\b(?:\s+\w+)*\s*\{")


def code(text: str) -> str:
    """The C text without comments and preprocessor lines, whose struct
    bodies (in macros) define nothing where they stand."""
    text = re.sub(r"/\*.*?\*/", lambda match: "\n" * match.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    lines, directive = [], False
    for line in text.splitlines():
        if directive or line.lstrip().startswith("#"):
            directive = line.rstrip().endswith("\\")
            continue
        lines.append(line)
    return "\n".join(lines)


def defines_a_struct(text: str) -> bool:
    return bool(DEFINITION.search(code(text)))


def struct_headers() -> list[Path]:
    return [path for path in sorted(SOURCE.rglob("*.h"))
            if not any(path == item or item in path.parents for item in EXCLUDED)
            and defines_a_struct(path.read_text(encoding="utf-8", errors="replace"))]


class Headers(unittest.TestCase):
    def test_the_scanner_sees_definitions_only(self) -> None:
        self.assertTrue(defines_a_struct("typedef struct {\n  int a;\n} t;\n"))
        self.assertTrue(defines_a_struct("union u { int a; float b; };\n"))
        self.assertTrue(defines_a_struct("typedef struct HAL_PACKED name\n{\n  int a;\n} t;\n"))
        self.assertFalse(defines_a_struct("struct forward;\nvoid f(struct forward *p);\n"))
        self.assertFalse(defines_a_struct("/* struct x { int a; }; */\n// union y { int b; };\n"))
        self.assertFalse(defines_a_struct("#define M(x) \\\n  struct x { int a; }\n"))

    def test_every_struct_header_checks_the_packing(self) -> None:
        headers = struct_headers()
        # Known definitions keep the scan from passing on an empty list.
        for known in ("hal/spi/hal_spi.h", "hal/system/hal_board.h", "sys/socket.h"):
            self.assertIn(SOURCE / known, headers)
        missing = [path.relative_to(ROOT).as_posix() for path in headers
                   if INCLUDE not in path.read_text(encoding="utf-8", errors="replace")]
        self.assertEqual([], missing, f"add {INCLUDE} before the definitions")

    def test_the_check_runs_on_every_inclusion(self) -> None:
        text = CHECK.read_text(encoding="utf-8")
        self.assertNotIn("#pragma once", text)
        self.assertIn("JH_LAYOUT_CHECK(__COUNTER__);", text)


@unittest.skipUnless(shutil.which("cc") and shutil.which("c++"), "the compile checks need a host C and C++ compiler")
class Compile(unittest.TestCase):
    UNIT = ('#include "hal/spi/hal_spi.h"\n{between}#include "hal/serial/hal_uart.h"\n'
            "int main(void) {{ return 0; }}\n")

    def compile(self, name: str, language: str, between: str) -> subprocess.CompletedProcess:
        WORK.mkdir(parents=True, exist_ok=True)
        source = WORK / f"{name}.{'cpp' if language == 'c++' else 'c'}"
        source.write_text(self.UNIT.format(between=between), encoding="utf-8")
        compiler, standard = ("c++", "-std=c++17") if language == "c++" else ("cc", "-std=c11")
        return subprocess.run([compiler, standard, "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                               *(f"-I{path}" for path in hal_include_dirs(ROOT)), "-fsyntax-only", str(source)], capture_output=True, text=True)

    def test_a_pragma_left_open_between_hal_headers_stops_the_unit(self) -> None:
        for language in ("c", "c++"):
            for value in (1, 2, 4):
                with self.subTest(language=language, pack=value):
                    result = self.compile(f"open {value} {language}", language, f"#pragma pack(push, {value})\n")
                    self.assertNotEqual(0, result.returncode)
                    self.assertIn("struct packing changed since the first HAL header", result.stderr)

    def test_a_closed_pragma_compiles(self) -> None:
        between = "#pragma pack(push, 1)\nstruct wire { char a; int b; };\n#pragma pack(pop)\n"
        for language in ("c", "c++"):
            for name, text in (("plain", ""), ("closed", between)):
                with self.subTest(language=language, case=name):
                    result = self.compile(f"{name} {language}", language, text)
                    self.assertEqual(0, result.returncode, result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
