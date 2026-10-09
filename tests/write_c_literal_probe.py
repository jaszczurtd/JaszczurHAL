#!/usr/bin/env python3
"""Write the C program that checks the literals of hardware_literals.py.

Usage: write_c_literal_probe.py <repository root> <output>...

Every output gets the same source; CTest builds it as C and as C++ with the
host warning policy as errors, so GCC, Clang and MSVC each lex the literals.
The program compares every string literal with its UTF-8 bytes, written as
numbers, and every float/double literal with the bit pattern of its value,
and returns the number of literals that differ.
"""

from __future__ import annotations

from pathlib import Path
import sys

# An escape before a hex digit, before 1 and 9, trigraphs, quotes and
# backslashes, an embedded NUL and characters of two to four UTF-8 bytes.
STRINGS = ["µA", "\x00" + "1", "\x01" + "9", "x\x7f" + "7", "a??=b??/?", 'q"\\', "ż€😀F"]
# Exact decimals, a value just above a binary32 halfway point, subnormals
# and the largest finite values.
FLOATS = [("float", "0.22"), ("float", "-0.22"),
          ("float", "1.0000000596046447753906250000000000000000001"),
          ("float", "1e-45"), ("float", "3.4028235e38"),
          ("double", "0.22"), ("double", "1"), ("double", "4e-324"),
          ("double", "1.7976931348623157e308")]


def source(literals, json_number) -> str:
    lines = ["#include <stdint.h>", "#include <stdio.h>", "#include <string.h>", ""]
    for index, value in enumerate(STRINGS):
        data = ", ".join(str(byte) for byte in value.encode("utf-8"))
        lines.append(f"static const char literal{index}[] = {literals.c_string(value)};")
        lines.append(f"static const unsigned char bytes{index}[] = {{{data}}};")
    for index, (c_type, token) in enumerate(FLOATS):
        value = json_number(token)
        lines.append(f"static const {c_type} number{index} = {literals.c_literal(c_type, value)};")
        lines.append(f"static const uint64_t bits{index} = "
                     f"UINT64_C(0x{literals.ieee_bits(c_type, value)});")
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
        "static int bits_differ(unsigned index, const void *number, size_t size,",
        "                       uint64_t expected) {",
        "  uint32_t narrow = 0u;",
        "  uint64_t wide = 0u;",
        "  if (size == sizeof narrow) {",
        "    memcpy(&narrow, number, size);",
        "    wide = narrow;",
        "  } else {",
        "    memcpy(&wide, number, size);",
        "  }",
        "  if (wide == expected) {",
        "    return 0;",
        "  }",
        '  printf("number %u differs from its bit pattern\\n", index);',
        "  return 1;",
        "}",
        "",
        "int main(void) {",
        "  int failures = 0;",
    ]
    lines += [f"  failures += differs({index}u, literal{index}, sizeof literal{index}, "
              f"bytes{index}, sizeof bytes{index});" for index in range(len(STRINGS))]
    lines += [f"  failures += bits_differ({index}u, &number{index}, sizeof number{index}, "
              f"bits{index});" for index in range(len(FLOATS))]
    lines += ["  return failures;", "}", ""]
    return "\n".join(lines)


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__.splitlines()[2])
    sys.path.insert(0, str(Path(sys.argv[1]) / "scripts"))
    import hardware_literals
    from hardware_schema import JsonNumber

    text = source(hardware_literals, JsonNumber)
    for output in sys.argv[2:]:
        Path(output).write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
