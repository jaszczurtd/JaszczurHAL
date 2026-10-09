#!/usr/bin/env python3
"""Write the C program that checks generate_device_defaults.c_string().

Usage: write_c_literal_probe.py <repository root> <output>...

Every output gets the same source; CTest builds it as C and as C++ with the
host warning policy as errors, so GCC, Clang and MSVC each lex the literals.
The program compares every literal with its UTF-8 bytes, written as numbers,
and returns the number of literals that differ.
"""

from __future__ import annotations

from pathlib import Path
import sys

# An escape before a hex digit, before 1 and 9, trigraphs, quotes and
# backslashes, an embedded NUL and characters of two to four UTF-8 bytes.
VALUES = ["µA", "\x00" + "1", "\x01" + "9", "x\x7f" + "7", "a??=b??/?", 'q"\\', "ż€😀F"]


def source(c_string) -> str:
    lines = ["#include <stdio.h>", "#include <string.h>", ""]
    for index, value in enumerate(VALUES):
        data = ", ".join(str(byte) for byte in value.encode("utf-8"))
        lines.append(f"static const char literal{index}[] = {c_string(value)};")
        lines.append(f"static const unsigned char bytes{index}[] = {{{data}}};")
    lines += [
        "",
        "static int differs(unsigned index, const char *literal, size_t size,",
        "                   const unsigned char *bytes, size_t count) {",
        "  if (size == count + 1u && memcmp(literal, bytes, count) == 0) {",
        "    return 0;",
        "  }",
        '  printf("literal %u differs from its UTF-8 bytes\\n", index);',
        "  return 1;",
        "}",
        "",
        "int main(void) {",
        "  int failures = 0;",
    ]
    lines += [f"  failures += differs({index}u, literal{index}, sizeof literal{index}, "
              f"bytes{index}, sizeof bytes{index});" for index in range(len(VALUES))]
    lines += ["  return failures;", "}", ""]
    return "\n".join(lines)


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__.splitlines()[2])
    sys.path.insert(0, str(Path(sys.argv[1]) / "scripts"))
    from generate_device_defaults import c_string

    text = source(c_string)
    for output in sys.argv[2:]:
        Path(output).write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
