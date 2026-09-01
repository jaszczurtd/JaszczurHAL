#!/usr/bin/env python3
"""Verify release version metadata and optional tag ancestry."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


class ReleaseError(RuntimeError):
    """Raised when tracked release metadata is inconsistent."""


def project_version(root: Path) -> str:
    match = re.fullmatch(
        r"version=([^\s]+)\n?", (root / "VERSION").read_text(encoding="utf-8")
    )
    if not match:
        raise ReleaseError("VERSION must contain exactly version=<value>")
    return match.group(1)


def sbom_version(root: Path) -> str:
    sbom = json.loads((root / "security/sbom.cdx.json").read_text(encoding="utf-8"))
    try:
        return str(sbom["metadata"]["component"]["version"])
    except (KeyError, TypeError) as error:
        raise ReleaseError("security/sbom.cdx.json has no project version") from error


def git(root: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *arguments], cwd=root, check=False, capture_output=True, text=True
    )


def check(root: Path, tag: str = "", release_ref: str = "") -> str:
    version = project_version(root)
    observed = {
        "security/sbom.cdx.json": sbom_version(root),
    }
    for source, value in observed.items():
        if value != version:
            raise ReleaseError(
                f"{source} version {value!r} does not match VERSION {version!r}"
            )

    if bool(tag) != bool(release_ref):
        raise ReleaseError("--tag and --release-ref must be supplied together")
    if tag:
        if tag != version:
            raise ReleaseError(f"tag {tag!r} does not match VERSION {version!r}")
        tag_commit = git(root, "rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}")
        if tag_commit.returncode != 0:
            raise ReleaseError(f"tag {tag!r} is not present in the checkout")
        release_commit = git(root, "rev-parse", "--verify", f"{release_ref}^{{commit}}")
        if release_commit.returncode != 0:
            raise ReleaseError(f"release ref {release_ref!r} is not present")
        ancestry = git(
            root,
            "merge-base",
            "--is-ancestor",
            tag_commit.stdout.strip(),
            release_commit.stdout.strip(),
        )
        if ancestry.returncode != 0:
            raise ReleaseError(
                f"tag {tag!r} is not an ancestor of release ref {release_ref!r}"
            )
    return version


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).parents[1])
    parser.add_argument("--tag", default="")
    parser.add_argument("--release-ref", default="")
    arguments = parser.parse_args()
    try:
        version = check(
            arguments.repo_root.resolve(), arguments.tag, arguments.release_ref
        )
    except (OSError, ValueError, ReleaseError) as error:
        print(f"check_release_metadata.py: {error}", file=sys.stderr)
        return 1
    print(f"Release metadata is consistent for {version}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
