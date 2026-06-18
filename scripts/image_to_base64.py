#!/usr/bin/env python3
"""Encode an image file as a C string with Base64 data."""

import argparse
import base64
import re
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Encode an image file to Base64 and emit a C string declaration."
    )
    parser.add_argument("image", help="Path to the input image file")
    parser.add_argument(
        "-o",
        "--output",
        "--otput",
        dest="output",
        help="Write generated text to this file instead of stdout",
    )
    parser.add_argument(
        "-n",
        "--name",
        default="image",
        help="C variable name for the generated string (default: image)",
    )
    parser.add_argument(
        "--line-width",
        type=int,
        default=96,
        help="Maximum Base64 characters per generated string line (default: 96)",
    )
    return parser.parse_args()


def validate_identifier(name: str) -> None:
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) is None:
        raise ValueError(f"invalid C identifier: {name!r}")


def build_declaration(name: str, encoded: str, line_width: int) -> str:
    if line_width <= 0:
        raise ValueError("--line-width must be greater than 0")

    chunks = [encoded[i : i + line_width] for i in range(0, len(encoded), line_width)]
    if not chunks:
        chunks = [""]

    lines = [f"static const char {name}[] ="]
    for index, chunk in enumerate(chunks):
        suffix = ";" if index == len(chunks) - 1 else ""
        lines.append(f'    "{chunk}"{suffix}')

    return "\n".join(lines) + "\n"


def main() -> int:
    args = parse_args()

    try:
        validate_identifier(args.name)

        image_path = Path(args.image)
        data = image_path.read_bytes()
        encoded = base64.b64encode(data).decode("ascii")
        output = build_declaration(args.name, encoded, args.line_width)

        if args.output:
            Path(args.output).write_text(output, encoding="ascii")
        else:
            sys.stdout.write(output)

    except OSError as exc:
        print(f"image_to_base64.py: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(f"image_to_base64.py: {exc}", file=sys.stderr)
        return 2

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
