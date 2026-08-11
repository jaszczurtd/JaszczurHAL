#!/usr/bin/env python3
"""Keep GitHub CI at one all-features build per OS and architecture."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
WORKFLOW = (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")


def job(name: str) -> str:
    marker = f"\n  {name}:\n"
    if marker not in WORKFLOW:
        raise AssertionError(f"CI job is missing: {name}")
    body = WORKFLOW.split(marker, 1)[1]
    next_job = re.search(r"\n  [a-z0-9-]+:\n", body)
    return body[: next_job.start()] if next_job else body


expected = [
    ("rp2040", "picow"),
    ("rp2350-arm", "pico2w"),
    ("rp2350-riscv", "pico2"),
    ("stm32g474", "nucleo-g474re-pim730"),
]

for job_name in ("windows-static-library", "linux-static-library"):
    body = job(job_name)
    rows = re.findall(
        r"^          - target: ([^\s]+)\n(?:            platform: [^\n]+\n)?"
        r"            board: ([^\s]+)$",
        body,
        flags=re.MULTILINE,
    )
    if rows != expected:
        raise AssertionError(
            f"{job_name} must contain exactly the four production architectures; "
            f"got {rows!r}"
        )

if "-DJH_ENABLE_ALL_FEATURES=ON" not in job("windows-static-library"):
    raise AssertionError("Windows architecture matrix does not enable all features")
if "--all-features" not in job("linux-static-library"):
    raise AssertionError("Linux architecture matrix does not enable all features")

for obsolete in (
    "stm32-build",
    "native-pico-build",
    "native-flag-matrix",
    "stm32-cyw43-library",
):
    if f"\n  {obsolete}:\n" in WORKFLOW:
        raise AssertionError(f"obsolete duplicate library job remains: {obsolete}")

print("CI library matrix verified: 4 Linux + 4 Windows builds")
