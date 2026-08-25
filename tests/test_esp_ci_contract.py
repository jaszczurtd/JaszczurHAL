#!/usr/bin/env python3
"""Guard ESP32-S3 integration in existing CI and local gates."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
WORKFLOW = (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
QUALITY_GATE = (ROOT / "runalltests.sh").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def workflow_job(name: str) -> str:
    marker = f"\n  {name}:\n"
    require(marker in WORKFLOW, f"CI job is missing: {name}")
    body = WORKFLOW.split(marker, 1)[1]
    next_job = re.search(r"\n  [a-z0-9-]+:\n", body)
    return body[: next_job.start()] if next_job else body


def workflow_step(body: str, name: str) -> str:
    marker = f"      - name: {name}\n"
    require(marker in body, f"CI step is missing: {name}")
    step = body.split(marker, 1)[1]
    next_step = re.search(r"\n      - (?:name|uses):", step)
    return step[: next_step.start()] if next_step else step


def require_real_esp_build(body: str, context: str) -> None:
    require(
        re.search(r"build_esp_idf\.py[\"']?\s+build", body) is not None,
        f"{context} does not invoke the production ESP-IDF build action",
    )
    for fragment in (
        "--project",
        "tests/fixtures/esp32s3_phase3",
        "--target esp32s3",
        "--board waveshare-esp32-s3-zero",
        "--clean",
    ):
        require(fragment in body, f"{context} is missing {fragment!r}")
    require(
        "build_esp_idf_phase0.py" not in body,
        f"{context} still invokes the Phase 0 compatibility wrapper",
    )


def require_esp_cache(body: str, context: str) -> None:
    for fragment in (
        "uses: actions/cache@v5",
        "third_party/esp-idf",
        "~/.espressif",
        "hashFiles('third_party/esp_idf_version.conf')",
    ):
        require(fragment in body, f"{context} ESP-IDF cache is missing {fragment!r}")
    require(
        "uses: actions/cache@v4" not in body,
        f"{context} still uses the obsolete Node 20 cache action",
    )


def require_uploaded_images(body: str, context: str) -> None:
    for fragment in (
        "jh_esp_idf_artifacts.json",
        "build.log",
        "bootloader/bootloader.bin",
        "partition_table/partition-table.bin",
        "ota_data_initial.bin",
        "esp32s3_phase3.bin",
        "if-no-files-found: error",
        "include-hidden-files: true",
    ):
        require(fragment in body, f"{context} artifact upload is missing {fragment!r}")


def require_failure_diagnostics(body: str, context: str) -> None:
    for fragment in (
        "failure() && !cancelled() &&",
        "steps.esp32_phase3_build.outcome == 'failure'",
        "uses: actions/upload-artifact@v6",
        "jh_esp_idf_failure.txt",
        "build.log",
        "generated/jaszczurhal/",
        "CMakeCache.txt",
        "CMakeConfigureLog.yaml",
        "project_description.json",
        "compile_commands.json",
        "flasher_args.json",
        "sdkconfig",
        "if-no-files-found: warn",
        "include-hidden-files: true",
    ):
        require(
            fragment in body,
            f"{context} failure diagnostics are missing {fragment!r}",
        )


windows = workflow_job("windows-tooling")
linux = workflow_job("test")
for body, context in ((windows, "windows-tooling"), (linux, "Linux test")):
    build_marker = (
        "      - name: Build ESP32-S3 Phase 3 fixture with pinned ESP-IDF\n"
    )
    failure_marker = (
        "      - name: Upload ESP32-S3 Phase 3 failure diagnostics\n"
    )
    require(
        body.index(build_marker) < body.index(failure_marker),
        f"{context} failure upload appears before the ESP-IDF build step",
    )
    build_step = workflow_step(
        body, "Build ESP32-S3 Phase 3 fixture with pinned ESP-IDF"
    )
    require_real_esp_build(build_step, context)
    require(
        "id: esp32_phase3_build" in build_step,
        f"{context} ESP-IDF build step has no stable diagnostic id",
    )
    require(
        "--jobs" not in build_step,
        f"{context} passes the removed --jobs option to build_esp_idf.py",
    )
    require_esp_cache(body, context)
    upload_step = workflow_step(body, "Upload ESP32-S3 Phase 3 build artifacts")
    require_uploaded_images(upload_step, context)
    failure_step = workflow_step(
        body, "Upload ESP32-S3 Phase 3 failure diagnostics"
    )
    require_failure_diagnostics(failure_step, context)

windows_build = workflow_step(
    windows, "Build ESP32-S3 Phase 3 fixture with pinned ESP-IDF"
)
for fragment in (
    "$buildExitCode = $LASTEXITCODE",
    "jh_esp_idf_failure.txt",
    "Get-Content -LiteralPath $failureDiagnostic",
    "Get-Content -LiteralPath $buildLog -Tail 300",
    "ESP-IDF failure diagnostic:",
    "Missing failure diagnostic:",
    "exit $buildExitCode",
):
    require(
        fragment in windows_build,
        f"windows-tooling failure output is missing {fragment!r}",
    )
require(
    windows_build.index("Get-Content -LiteralPath $buildLog -Tail 300")
    < windows_build.index("Get-Content -LiteralPath $failureDiagnostic"),
    "windows-tooling does not print the failure diagnostic after the log tail",
)

linux_build = workflow_step(
    linux, "Build ESP32-S3 Phase 3 fixture with pinned ESP-IDF"
)
for fragment in (
    "build_exit=0",
    "|| build_exit=$?",
    "jh_esp_idf_failure.txt",
    'tail -n 300 "${output}/build.log"',
    "ESP-IDF failure diagnostic:",
    "Missing failure diagnostic:",
    'exit "${build_exit}"',
):
    require(
        fragment in linux_build,
        f"Linux test failure output is missing {fragment!r}",
    )
require(
    linux_build.index('tail -n 300 "${output}/build.log"')
    < linux_build.index(
        "sed -n '1,120p' \"${output}/jh_esp_idf_failure.txt\""
    ),
    "Linux test does not print the failure diagnostic after the log tail",
)

require(
    "$env:GITHUB_WORKSPACE" in windows_build,
    "windows-tooling ESP-IDF output is not rooted in GITHUB_WORKSPACE",
)
require(
    "${GITHUB_WORKSPACE}/.build/ci/esp-idf/esp32s3-phase3" in linux_build,
    "Linux test ESP-IDF output is not rooted in GITHUB_WORKSPACE",
)

require(
    "tests/test_esp32s3_phase1.py" in windows,
    "windows-tooling does not run the ESP32-S3 Phase 1 host contract test",
)
require(
    "tests/test_esp32s3_phase2.py" in windows,
    "windows-tooling does not run the ESP32-S3 Phase 2 host contract test",
)
require(
    "tests/test_esp32s3_phase3.py" in windows,
    "windows-tooling does not run the ESP32-S3 Phase 3 host contract test",
)
require(
    "tests/test_esp_ci_contract.py" in windows,
    "windows-tooling does not guard its ESP-IDF CI contract",
)

jobs = re.findall(r"^  ([a-z0-9-]+):$", WORKFLOW, flags=re.MULTILINE)
require(
    not any("esp" in name for name in jobs),
    f"ESP integration must reuse existing CI jobs, found: {jobs!r}",
)
require(
    "esp32s3" not in workflow_job("linux-static-library"),
    "ESP-IDF must not be added to the static-library matrix",
)

gate7 = QUALITY_GATE.split("# GATE 7:", 1)[1].split("# GATE 8:", 1)[0]
gate7_esp = gate7.split(
    'info "Building the ESP32-S3 Phase 3 fixture with pinned ESP-IDF..."', 1
)[1].split(
    'pass "ESP32-S3 Phase 3 fixture produced a validated multi-image ESP-IDF build."',
    1,
)[0]
require_real_esp_build(gate7_esp, "runalltests.sh Gate 7")
require(
    "--jobs" not in gate7_esp,
    "Gate 7 passes the removed --jobs option to build_esp_idf.py",
)
require(
    '"${GATE_BUILD_ROOT}/esp-idf/esp32s3-phase3"' in gate7,
    "Gate 7 ESP-IDF output is not below .build/gate",
)
require(
    '"${LOG_ROOT}/jh_esp32s3_phase3.log"' in gate7,
    "Gate 7 ESP-IDF command is not captured below .build/gate/logs",
)

print("ESP32-S3 Phase 3 CI and local gate integration verified")
