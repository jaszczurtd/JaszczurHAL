#!/usr/bin/env python3
"""Run the pinned PMD CPD policy over JaszczurHAL-owned C/C++ sources."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET


SOURCE_EXTENSIONS = frozenset({".c", ".cc", ".cpp", ".cxx"})
MINIMUM_TOKENS = 100
COVERAGE_SCOPE_ORDER = (
    "mock",
    "rp2040",
    "stm32g474",
    "shared",
    "portable-other",
)
PRODUCTION_EXCLUDED_PREFIXES = (
    "src/hal/generated/",
    "src/hal/impl/shared/frameworks/PubSubClient/",
    "src/hal/impl/shared/frameworks/smart_timers/",
    "src/hal/impl/shared/frameworks/wireguard/",
)
PRODUCTION_EXCLUDED_FILES = frozenset({"src/utils/unity.c"})


class CpdError(RuntimeError):
    """The CPD tool or its report did not satisfy the gate contract."""


@dataclass(frozen=True)
class Occurrence:
    path: str
    line: int
    begin_token: int
    end_token: int


@dataclass(frozen=True)
class Duplication:
    tokens: int
    occurrences: tuple[Occurrence, ...]


@dataclass
class CpdReport:
    groups: list[Duplication]
    file_tokens: dict[str, int]


@dataclass(frozen=True)
class Coverage:
    scope: str
    total_tokens: int
    duplicated_tokens: int

    @property
    def percentage(self) -> float:
        if self.total_tokens == 0:
            return 0.0
        return 100.0 * self.duplicated_tokens / self.total_tokens


def _relative_text(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def _is_production_excluded(relative: str) -> bool:
    return relative in PRODUCTION_EXCLUDED_FILES or relative.startswith(
        PRODUCTION_EXCLUDED_PREFIXES
    )


def collect_sources(
    repo_root: Path, roots: tuple[str, ...], *, production: bool
) -> list[Path]:
    sources: list[Path] = []
    for root_text in roots:
        root = repo_root / root_text
        if not root.is_dir():
            raise CpdError(f"CPD source root is missing: {root}")
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_EXTENSIONS:
                continue
            relative = _relative_text(path, repo_root)
            if production and _is_production_excluded(relative):
                continue
            sources.append(path.resolve())
    return sorted(set(sources), key=lambda path: _relative_text(path, repo_root))


def resolve_pmd(repo_root: Path, override: str = "") -> Path:
    if override:
        candidate = Path(override).expanduser().resolve()
        if not candidate.is_file():
            raise CpdError(f"PMD executable is missing: {candidate}")
        return candidate
    executable = "pmd.bat" if sys.platform == "win32" else "pmd"
    root = repo_root / "third_party/pmd"
    matches = sorted(path for path in root.rglob(executable) if path.is_file())
    if len(matches) != 1:
        raise CpdError(
            f"Expected one managed {executable} below {root}, found {len(matches)}. "
            "Run scripts/ensure_pmd.sh --force."
        )
    return matches[0].resolve()


def _pmd_command(executable: Path, *arguments: str) -> list[str]:
    if sys.platform == "win32":
        return ["cmd.exe", "/d", "/c", str(executable), *arguments]
    return [str(executable), *arguments]


def _write_file_list(path: Path, sources: list[Path]) -> None:
    if not sources:
        raise CpdError(f"CPD source list would be empty: {path}")
    path.write_text(
        "".join(f"{source}\n" for source in sources),
        encoding="utf-8",
        newline="\n",
    )


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def parse_report(report: Path, repo_root: Path) -> CpdReport:
    try:
        root = ET.parse(report).getroot()
    except (OSError, ET.ParseError) as error:
        raise CpdError(f"Could not parse CPD report {report}: {error}") from error

    file_tokens: dict[str, int] = {}
    for element in root:
        if _local_name(element.tag) != "file":
            continue
        raw_path = element.attrib.get("path", "")
        try:
            total_tokens = int(element.attrib["totalNumberOfTokens"])
        except (KeyError, ValueError) as error:
            raise CpdError(f"Malformed file summary in {report}") from error
        path = Path(raw_path)
        display = (
            _relative_text(path, repo_root) if path.is_absolute() else path.as_posix()
        )
        if not display or total_tokens < 0 or display in file_tokens:
            raise CpdError(f"Invalid file summary in {report}: {display}")
        file_tokens[display] = total_tokens

    groups: list[Duplication] = []
    for element in root.iter():
        if _local_name(element.tag) != "duplication":
            continue
        try:
            tokens = int(element.attrib["tokens"])
        except (KeyError, ValueError) as error:
            raise CpdError(f"Malformed duplication entry in {report}") from error
        occurrences: list[Occurrence] = []
        for child in element:
            if _local_name(child.tag) != "file":
                continue
            raw_path = child.attrib.get("path", "")
            try:
                line = int(child.attrib.get("line", "0"))
                begin_token = int(child.attrib["begintoken"])
                end_token = int(child.attrib["endtoken"])
            except (KeyError, ValueError) as error:
                raise CpdError(f"Malformed file entry in {report}") from error
            path = Path(raw_path)
            display = (
                _relative_text(path, repo_root)
                if path.is_absolute()
                else path.as_posix()
            )
            if (
                display not in file_tokens
                or line < 1
                or begin_token < 0
                or end_token < begin_token
            ):
                raise CpdError(f"Invalid file entry in {report}: {display}")
            occurrences.append(
                Occurrence(display, line, begin_token, end_token)
            )
        if len(occurrences) < 2:
            raise CpdError(f"Duplication in {report} has fewer than two occurrences")
        groups.append(Duplication(tokens, tuple(occurrences)))
    return CpdReport(
        sorted(
            groups,
            key=lambda group: (
                -group.tokens,
                tuple((item.path, item.line) for item in group.occurrences),
            ),
        ),
        file_tokens,
    )


def run_scan(
    pmd: Path,
    repo_root: Path,
    output_dir: Path,
    label: str,
    sources: list[Path],
    minimum_tokens: int,
) -> CpdReport:
    file_list = output_dir / f"{label}_sources.txt"
    report = output_dir / f"{label}_cpd.xml"
    _write_file_list(file_list, sources)
    command = _pmd_command(
        pmd,
        "cpd",
        "--minimum-tokens",
        str(minimum_tokens),
        "--language",
        "cpp",
        "--file-list",
        str(file_list),
        "--format",
        "xml",
        "--report-file",
        str(report),
        "--relativize-paths-with",
        str(repo_root),
        "--no-fail-on-violation",
    )
    environment = os.environ.copy()
    environment.setdefault("PMD_JAVA_OPTS", "-Xmx512m")
    result = subprocess.run(
        command,
        cwd=repo_root,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise CpdError(
            f"PMD CPD failed for {label} with exit code {result.returncode}"
            + (f":\n{detail}" if detail else "")
        )
    if not report.is_file():
        raise CpdError(f"PMD CPD did not create its report: {report}")
    return parse_report(report, repo_root)


def _coverage_scope(path: str) -> str:
    if path.startswith("src/hal/impl/.mock/"):
        return "mock"
    if path.startswith("src/hal/impl/rp2040/"):
        return "rp2040"
    if path.startswith("src/hal/impl/stm32g474/"):
        return "stm32g474"
    if path.startswith("src/hal/impl/shared/"):
        return "shared"
    return "portable-other"


def _covered_tokens(ranges: list[tuple[int, int]]) -> int:
    if not ranges:
        return 0
    ordered = sorted(ranges)
    begin, end = ordered[0]
    total = 0
    for next_begin, next_end in ordered[1:]:
        if next_begin <= end + 1:
            end = max(end, next_end)
            continue
        total += end - begin + 1
        begin, end = next_begin, next_end
    return total + end - begin + 1


def duplicate_coverage(report: CpdReport) -> list[Coverage]:
    ranges_by_file: dict[str, list[tuple[int, int]]] = {}
    for group in report.groups:
        for occurrence in group.occurrences:
            ranges_by_file.setdefault(occurrence.path, []).append(
                (occurrence.begin_token, occurrence.end_token)
            )

    totals = {scope: [0, 0] for scope in COVERAGE_SCOPE_ORDER}
    for path, total_tokens in report.file_tokens.items():
        scope = _coverage_scope(path)
        totals[scope][0] += total_tokens
        totals[scope][1] += _covered_tokens(ranges_by_file.get(path, []))

    coverage = [
        Coverage(scope, totals[scope][0], totals[scope][1])
        for scope in COVERAGE_SCOPE_ORDER
        if totals[scope][0] > 0
    ]
    coverage.append(
        Coverage(
            "global",
            sum(item.total_tokens for item in coverage),
            sum(item.duplicated_tokens for item in coverage),
        )
    )
    return coverage


def _print_group(group: Duplication) -> None:
    locations = " <-> ".join(
        f"{item.path}:{item.line}" for item in group.occurrences
    )
    print(f"  {group.tokens:4d} tokens: {locations}")


def _print_scope(name: str, report: CpdReport, minimum_tokens: int) -> None:
    print(
        f"CPD {name}: {len(report.groups)} blocking duplicate group(s) at >= "
        f"{minimum_tokens} tokens."
    )
    for group in report.groups:
        _print_group(group)
    print(f"CPD {name} duplicate-token coverage:")
    for item in duplicate_coverage(report):
        print(
            f"  {item.scope:15s} {item.percentage:7.3f}% "
            f"({item.duplicated_tokens}/{item.total_tokens} tokens)"
        )


def build_parser() -> argparse.ArgumentParser:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root)
    parser.add_argument("--output-dir", type=Path, default=repo_root / ".build/gate/cpd")
    parser.add_argument("--pmd", default="")
    parser.add_argument("--minimum-tokens", type=int, default=MINIMUM_TOKENS)
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    repo_root = arguments.repo_root.resolve()
    output_dir = arguments.output_dir.resolve()
    if arguments.minimum_tokens < 1:
        print(
            "run_cpd.py: minimum token threshold must be positive",
            file=sys.stderr,
        )
        return 2
    output_dir.mkdir(parents=True, exist_ok=True)
    try:
        pmd = resolve_pmd(repo_root, arguments.pmd)
        production_sources = collect_sources(repo_root, ("src",), production=True)
        test_sources = collect_sources(
            repo_root, ("tests", "examples"), production=False
        )
        print(
            f"CPD scope: {len(production_sources)} production and "
            f"{len(test_sources)} test/example implementation files."
        )
        production_report = run_scan(
            pmd,
            repo_root,
            output_dir,
            "production",
            production_sources,
            arguments.minimum_tokens,
        )
        test_report = run_scan(
            pmd,
            repo_root,
            output_dir,
            "tests_examples",
            test_sources,
            arguments.minimum_tokens,
        )
    except CpdError as error:
        print(f"run_cpd.py: {error}", file=sys.stderr)
        return 2

    _print_scope("production", production_report, arguments.minimum_tokens)
    _print_scope("tests/examples", test_report, arguments.minimum_tokens)
    total_groups = len(production_report.groups) + len(test_report.groups)
    if total_groups:
        sys.stdout.flush()
        print(
            f"CPD gate failed with {total_groups} duplicate group(s); "
            f"inspect XML reports in {output_dir}",
            file=sys.stderr,
        )
        return 1
    print(f"CPD gate passed with zero duplicate groups; XML reports: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
