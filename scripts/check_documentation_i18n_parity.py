#!/usr/bin/env python3
"""Validate the layout and structural parity of English/Polish documentation.

`doc/CHANGELOG.md` is explicitly out of translation scope. `doc/HAL_FLAGS.txt`
has no Polish counterpart by design (flag identifiers, not prose).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

PAIRED_DIRECTORIES = (
    ("doc/en", "doc/pl"),
    ("doc/api/en", "doc/api/pl"),
)

PAIRED_FILES = (
    ("README.md", "README.pl.md"),
    ("doc/table_of_contents.md", "doc/table_of_contents.pl.md"),
)

# Root README context may be language-specific when translating it would add no
# useful information for readers of the other language.
ALLOWED_EN_ONLY_HEADINGS = {
    ("README.md", "README.pl.md"): frozenset(
        {"How do you even pronounce this library name?"}
    ),
}

PROJECT_README_DIRECTORIES = (
    "boards",
    "config",
    "examples",
    "src",
    "tests",
    "vscode",
)
PROJECT_README_FILES = (
    ("third_party/README.md", "third_party/README.pl.md"),
)

DOC_ROOT_FILES = {
    "CHANGELOG.md",
    "HAL_FLAGS.txt",
    "table_of_contents.md",
    "table_of_contents.pl.md",
}
DOC_ROOT_DIRECTORIES = {"api", "en", "pl"}
API_SYMBOL_RE = re.compile(r"\b(?:HAL|JH)_[A-Z0-9_]+\b|\b(?:hal|jh)_[a-z0-9_]+\b")
HEADING_RE = re.compile(r"^(#{1,6})\s+(.+?)\s*$", re.MULTILINE)
FENCE_RE = re.compile(r"^```", re.MULTILINE)
PLACEHOLDER_RE = re.compile(r"\b(?:TODO|TBD)\b.*\b(?:translat|tłumacz)", re.IGNORECASE)
FENCED_BLOCK_RE = re.compile(r"^```.*?^```", re.MULTILINE | re.DOTALL)
INLINE_CODE_RE = re.compile(r"`[^`]*`")
BANNED_PROSE_RE = re.compile(
    r"\b(?:canonical|glue|contracts?|kanonicz\w*|klej\w*|kontrakt\w*)\b",
    re.IGNORECASE,
)
POLISH_STYLE_PATTERNS = (
    (re.compile(r"\bkompilacj\w*|\bkompilacyj\w*", re.IGNORECASE), "build"),
    (re.compile(r"\bdyspozytor\w*", re.IGNORECASE), "dispatcher"),
    (re.compile(r"\bsterownik\w*", re.IGNORECASE), "driver"),
    (re.compile(r"\bdostawc\w*", re.IGNORECASE), "provider"),
    (re.compile(r"\bprzepływ\w*\s+pracy\b", re.IGNORECASE), "workflow"),
    (
        re.compile(r"środowisk\w*\s+uruchomieniow\w*", re.IGNORECASE),
        "runtime",
    ),
    (re.compile(r"\bw czasie (?:działania|wykonania)\b", re.IGNORECASE), "runtime"),
    (re.compile(r"\bzakleszcz\w*", re.IGNORECASE), "deadlock"),
    (
        re.compile(
            r"(?:bezpieczn\w*\s+wątk\w*|bezpieczeństw\w*\s+wątk\w*)",
            re.IGNORECASE,
        ),
        "thread safety/thread-safe",
    ),
    (re.compile(r"mechanizm\w*\s+zapasow\w*", re.IGNORECASE), "fallback"),
    (re.compile(r"\baddytyw\w*", re.IGNORECASE), "natural Polish wording"),
)


def _prose(text: str) -> str:
    return INLINE_CODE_RE.sub(" ", FENCED_BLOCK_RE.sub(" ", text))


def _names(directory: Path) -> set[str]:
    if not directory.is_dir():
        return set()
    return {path.name for path in directory.glob("*.md") if path.is_file()}


def _check_pair(
    root: Path,
    en_relative: str,
    pl_relative: str,
    *,
    exact_structure: bool = True,
) -> list[str]:
    en_path, pl_path = root / en_relative, root / pl_relative
    if not en_path.is_file() or not pl_path.is_file():
        return []

    en_text = en_path.read_text(encoding="utf-8")
    pl_text = pl_path.read_text(encoding="utf-8")
    failures: list[str] = []
    en_prose = _prose(en_text)
    pl_prose = _prose(pl_text)

    if PLACEHOLDER_RE.search(pl_text):
        failures.append(f"{pl_relative}: contains an unfinished translation marker")

    for relative, prose in ((en_relative, en_prose), (pl_relative, pl_prose)):
        match = BANNED_PROSE_RE.search(prose)
        if match:
            failures.append(f"{relative}: banned prose term: {match.group(0)}")

    for pattern, preferred in POLISH_STYLE_PATTERNS:
        match = pattern.search(pl_prose)
        if match:
            failures.append(
                f"{pl_relative}: avoid '{match.group(0)}'; prefer {preferred}"
            )

    en_length = len(en_text.strip())
    pl_length = len(pl_text.strip())
    if en_length and not 0.5 <= pl_length / en_length <= 1.8:
        failures.append(
            f"{pl_relative}: suspicious translation size "
            f"({pl_length} bytes versus {en_length} in {en_relative})"
        )

    allowed_en_only_headings = ALLOWED_EN_ONLY_HEADINGS.get(
        (en_relative, pl_relative), frozenset()
    )
    en_headings = [
        len(match.group(1))
        for match in HEADING_RE.finditer(en_text)
        if match.group(2) not in allowed_en_only_headings
    ]
    pl_headings = [len(match.group(1)) for match in HEADING_RE.finditer(pl_text)]
    if en_headings != pl_headings:
        failures.append(f"{pl_relative}: heading structure differs from {en_relative}")

    if exact_structure and len(FENCE_RE.findall(en_text)) != len(
        FENCE_RE.findall(pl_text)
    ):
        failures.append(f"{pl_relative}: fenced-code structure differs from {en_relative}")

    en_symbols = set(API_SYMBOL_RE.findall(en_text))
    pl_symbols = set(API_SYMBOL_RE.findall(pl_text))
    if exact_structure and en_symbols != pl_symbols:
        missing = ", ".join(sorted(en_symbols - pl_symbols)[:8])
        extra = ", ".join(sorted(pl_symbols - en_symbols)[:8])
        details = []
        if missing:
            details.append(f"missing: {missing}")
        if extra:
            details.append(f"extra: {extra}")
        failures.append(
            f"{pl_relative}: HAL/JH symbol set differs from {en_relative} "
            f"({' ; '.join(details)})"
        )

    return failures


def check(root: Path) -> list[str]:
    failures: list[str] = []
    doc_root = root / "doc"

    if not doc_root.is_dir():
        return ["doc/: documentation root is missing"]

    actual_doc_root_files = {
        path.name for path in doc_root.iterdir() if path.is_file()
    }
    actual_doc_root_directories = {
        path.name for path in doc_root.iterdir() if path.is_dir()
    }
    for unexpected in sorted(actual_doc_root_files - DOC_ROOT_FILES):
        failures.append(
            f"doc/{unexpected}: documentation must be placed in doc/en or doc/pl"
        )
    for missing in sorted(DOC_ROOT_FILES - actual_doc_root_files):
        failures.append(f"doc/{missing}: required documentation entry point is missing")
    for unexpected in sorted(actual_doc_root_directories - DOC_ROOT_DIRECTORIES):
        failures.append(f"doc/{unexpected}/: unexpected documentation directory")
    for missing in sorted(DOC_ROOT_DIRECTORIES - actual_doc_root_directories):
        failures.append(f"doc/{missing}/: required documentation directory is missing")

    for en_relative, pl_relative in PAIRED_DIRECTORIES:
        en_dir, pl_dir = root / en_relative, root / pl_relative
        if not en_dir.is_dir():
            failures.append(f"{en_relative}/: English documentation directory is missing")
        if not pl_dir.is_dir():
            failures.append(f"{pl_relative}/: Polish documentation directory is missing")
        en_names, pl_names = _names(en_dir), _names(pl_dir)
        for missing in sorted(en_names - pl_names):
            failures.append(f"{pl_relative}/{missing}: missing Polish translation")
        for orphan in sorted(pl_names - en_names):
            failures.append(
                f"{pl_relative}/{orphan}: Polish file has no English source in {en_relative}"
            )
        for name in sorted(en_names & pl_names):
            failures.extend(
                _check_pair(root, f"{en_relative}/{name}", f"{pl_relative}/{name}")
            )

    for en_relative, pl_relative in PAIRED_FILES:
        en_path, pl_path = root / en_relative, root / pl_relative
        if en_path.is_file() and not pl_path.is_file():
            failures.append(f"{pl_relative}: missing Polish translation")
        if pl_path.is_file() and not en_path.is_file():
            failures.append(f"{pl_relative}: Polish file has no English source {en_relative}")
        failures.extend(_check_pair(root, en_relative, pl_relative))

    readme_pairs = list(PROJECT_README_FILES)
    for directory_relative in PROJECT_README_DIRECTORIES:
        directory = root / directory_relative
        if not directory.is_dir():
            continue
        for en_path in sorted(directory.rglob("README.md")):
            en_relative = en_path.relative_to(root).as_posix()
            pl_relative = en_path.with_name("README.pl.md").relative_to(root).as_posix()
            readme_pairs.append((en_relative, pl_relative))

        for pl_path in sorted(directory.rglob("README.pl.md")):
            en_path = pl_path.with_name("README.md")
            if not en_path.is_file():
                pl_relative = pl_path.relative_to(root).as_posix()
                failures.append(
                    f"{pl_relative}: Polish README has no English README.md"
                )

    for en_relative, pl_relative in readme_pairs:
        en_path, pl_path = root / en_relative, root / pl_relative
        if en_path.is_file() and not pl_path.is_file():
            failures.append(f"{pl_relative}: missing Polish README translation")
            continue
        if pl_path.is_file() and not en_path.is_file():
            failures.append(
                f"{pl_relative}: Polish README has no English source {en_relative}"
            )
            continue
        failures.extend(
            _check_pair(
                root,
                en_relative,
                pl_relative,
                exact_structure=False,
            )
        )

    index_specs = (
        ("doc/table_of_contents.md", "doc/en", "en"),
        ("doc/table_of_contents.md", "doc/api/en", "api/en"),
        ("doc/table_of_contents.pl.md", "doc/pl", "pl"),
        ("doc/table_of_contents.pl.md", "doc/api/pl", "api/pl"),
    )
    for index_relative, directory_relative, link_prefix in index_specs:
        index_path = root / index_relative
        if not index_path.is_file():
            continue
        index_text = index_path.read_text(encoding="utf-8")
        for name in sorted(_names(root / directory_relative)):
            if f"({link_prefix}/{name})" not in index_text:
                failures.append(
                    f"{index_relative}: missing entry for {directory_relative}/{name}"
                )

    readme_indexes = (
        ("README.md", "doc/table_of_contents.md"),
        ("README.pl.md", "doc/table_of_contents.pl.md"),
    )
    for readme_relative, index_relative in readme_indexes:
        readme_path = root / readme_relative
        if readme_path.is_file() and f"({index_relative})" not in readme_path.read_text(
            encoding="utf-8"
        ):
            failures.append(f"{readme_relative}: does not link to {index_relative}")

    return failures


def _parse_repository_root() -> Path:
    parser = argparse.ArgumentParser(
        description="Check the bilingual JaszczurHAL documentation layout."
    )
    parser.add_argument(
        "repository",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        metavar="ROOT",
        help="repository to inspect (defaults to the parent of scripts/)",
    )
    return parser.parse_args().repository.resolve()


def main() -> int:
    root = _parse_repository_root()

    failures = check(root)
    if not failures:
        print("Documentation EN/PL parity check passed.")
        return 0

    report = "\n".join(f"- {failure}" for failure in failures)
    sys.stderr.write(f"Documentation EN/PL parity check failed:\n{report}\n")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
