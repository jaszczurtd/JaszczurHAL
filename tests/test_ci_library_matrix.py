#!/usr/bin/env python3
"""Audit GitHub CI quality gates and all-features architecture matrices."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
WORKFLOW = (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
LOCAL_GATE = (ROOT / "runalltests.sh").read_text(encoding="utf-8")
SANITIZER_RUNNER = (ROOT / "scripts" / "run_sanitizer_fuzz.sh").read_text(
    encoding="utf-8"
)

for obsolete_action in ("actions/checkout@v5", "actions/cache@v4"):
    if obsolete_action in WORKFLOW:
        raise AssertionError(f"obsolete GitHub Action remains: {obsolete_action}")
if "actions/checkout@v6" not in WORKFLOW or "actions/cache@v5" not in WORKFLOW:
    raise AssertionError("current Node 24 GitHub Actions are not configured")
if "\npermissions:\n  contents: read\n" not in WORKFLOW:
    raise AssertionError("GitHub Actions token is not restricted to read-only contents")


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

test_job = job("test")
for generated_check in (
    "scripts/sync_generated.py --check",
    "tests/test_tooling_contract.py .",
):
    if generated_check not in test_job:
        raise AssertionError(
            f"Linux CI does not fail early for generated drift: {generated_check}"
        )
for duplicated_check in (
    "scripts/generate_hal_features.py --check",
    "scripts/generate_board_config.py --boards-root boards --check-static",
    "scripts/vscode_library_workspace.py sync-vscode --check",
    "scripts/examples_dispatcher.py check-template",
    "scripts/check_sbom.sh",
):
    if duplicated_check in test_job:
        raise AssertionError(
            f"Linux CI bypasses the shared generated-artifact runner: "
            f"{duplicated_check}"
        )

if "scripts/check_sbom.sh" in job("security-scan"):
    raise AssertionError(
        "security CI repeats the SBOM check already owned by sync_generated.py"
    )
if "needs: test" not in job("security-scan"):
    raise AssertionError("security CI must consume the SBOM verified by the test job")

sanitizer_job = job("sanitizer-fuzz")
shared_sanitizer_command = "./scripts/run_sanitizer_fuzz.sh"
if sanitizer_job.count(shared_sanitizer_command) != 1:
    raise AssertionError("sanitizer CI does not use the shared sanitizer runner once")
local_sanitizer_command = '"${SCRIPT_DIR}/scripts/run_sanitizer_fuzz.sh"'
if LOCAL_GATE.count(local_sanitizer_command) != 2:
    raise AssertionError(
        "the full local flow must check and execute the shared sanitizer runner"
    )
local_sanitizer_gate = LOCAL_GATE.split('header "Gate 3/9', 1)[1].split(
    'header "Gate 4/9', 1
)[0]
if (
    local_sanitizer_command not in local_sanitizer_gate
    or '"${GATE_BUILD_ROOT}/sanitizer-fuzz"' not in local_sanitizer_gate
    or "--check-tools" in local_sanitizer_gate
):
    raise AssertionError("local Gate 3 does not execute the shared sanitizer runner")
for duplicated_fragment in (
    "-DJH_ENABLE_SANITIZERS=ON",
    "-DJH_ENABLE_FUZZING=ON",
    "UBSAN_OPTIONS: print_stacktrace=1:halt_on_error=1",
    "-runs=2000",
):
    if duplicated_fragment in sanitizer_job:
        raise AssertionError(
            f"sanitizer CI duplicates shared runner configuration: {duplicated_fragment}"
        )
for required_fragment in (
    "-DJH_ENABLE_SANITIZERS=ON",
    "-DJH_ENABLE_FUZZING=ON",
    "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1",
    "ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1",
    "fuzz_http_server fuzz_websocket fuzz_http_multipart",
):
    if required_fragment not in SANITIZER_RUNNER:
        raise AssertionError(
            f"shared sanitizer runner is missing: {required_fragment}"
        )

windows_tooling = job("windows-tooling")
if "'tests/test_vscode_library_workspace.py'" not in windows_tooling:
    raise AssertionError(
        "Windows tooling CI does not validate the root VS Code workspace"
    )

for obsolete in (
    "stm32-build",
    "native-pico-build",
    "native-flag-matrix",
    "stm32-cyw43-library",
):
    if f"\n  {obsolete}:\n" in WORKFLOW:
        raise AssertionError(f"obsolete duplicate library job remains: {obsolete}")

print("CI library matrix verified: 4 Linux + 4 Windows builds")
