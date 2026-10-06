#!/usr/bin/env python3
"""Guard ESP32-S3 integration in existing CI and local gates."""

from __future__ import annotations

from pathlib import Path
import re
import sys


from repo_root import repo_root  # noqa: E402
from source_assertions import shell_function_body  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
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


def require_gamepad_esp_build(body: str, context: str) -> None:
    require(
        re.search(r"build_esp_idf\.py[\"']?\s+build", body) is not None,
        f"{context} does not invoke the production ESP-IDF build action",
    )
    for fragment in (
        "--project",
        "tests/fixtures/esp32_gamepad",
        "--target esp32",
        "--board esp32-devkitc-v4",
        "--clean",
    ):
        require(fragment in body, f"{context} is missing {fragment!r}")


def require_esp_cache(body: str, context: str) -> None:
    for fragment in (
        "uses: actions/cache@v5",
        "third_party/esp-idf",
        "~/.espressif",
        "hashFiles('third_party/esp_idf_version.conf', "
        "'security/esp_idf_tools.json')",
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


def require_failure_diagnostics(body: str, context: str, trigger: str) -> None:
    for fragment in (
        "failure() && !cancelled() &&",
        trigger,
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


WINDOWS_TRIGGER = "steps.esp32_phase3_build.outcome == 'failure'"
LINUX_PHASE3 = ".build/gate/esp-idf/esp32s3-phase3"
LINUX_GAMEPAD = ".build/gate/esp-idf/esp32-gamepad"


def gate_function(name: str) -> str:
    return shell_function_body(QUALITY_GATE, name)


windows = workflow_job("windows-tooling")
linux = workflow_job("test")

# Windows builds the fixture itself; it is the only CI host the gate cannot be.
build_marker = "      - name: Build ESP32-S3 Phase 3 fixture with pinned ESP-IDF\n"
failure_marker = "      - name: Upload ESP32-S3 Phase 3 failure diagnostics\n"
require(
    windows.index(build_marker) < windows.index(failure_marker),
    "windows-tooling failure upload appears before the ESP-IDF build step",
)
windows_build = workflow_step(windows, "Build ESP32-S3 Phase 3 fixture with pinned ESP-IDF")
require_real_esp_build(windows_build, "windows-tooling")
require("id: esp32_phase3_build" in windows_build,
        "windows-tooling ESP-IDF build step has no stable diagnostic id")
require("--jobs" not in windows_build,
        "windows-tooling passes the removed --jobs option to build_esp_idf.py")
require_esp_cache(windows, "windows-tooling")
require_uploaded_images(
    workflow_step(windows, "Upload ESP32-S3 Phase 3 build artifacts"), "windows-tooling")
require_failure_diagnostics(
    workflow_step(windows, "Upload ESP32-S3 Phase 3 failure diagnostics"),
    "windows-tooling", WINDOWS_TRIGGER)
for fragment in (
    "$buildExitCode = $LASTEXITCODE",
    "jh_esp_idf_failure.txt",
    "Get-Content -LiteralPath $failureDiagnostic",
    "Get-Content -LiteralPath $buildLog -Tail 300",
    "ESP-IDF failure diagnostic:",
    "Missing failure diagnostic:",
    "exit $buildExitCode",
):
    require(fragment in windows_build,
            f"windows-tooling failure output is missing {fragment!r}")
require(
    windows_build.index("Get-Content -LiteralPath $buildLog -Tail 300")
    < windows_build.index("Get-Content -LiteralPath $failureDiagnostic"),
    "windows-tooling does not print the failure diagnostic after the log tail",
)
require("$env:GITHUB_WORKSPACE" in windows_build,
        "windows-tooling ESP-IDF output is not rooted in GITHUB_WORKSPACE")

# Linux CI runs the gate's esp-idf stage in the test job, which keeps the
# ESP-IDF cache and publishes what the stage left below .build/gate.
require(
    re.search(r"\./runalltests\.sh --stage \S*\besp-idf\b", linux) is not None,
    "the Linux test job does not run the esp-idf gate stage",
)
require_esp_cache(linux, "Linux test")
phase3_upload = workflow_step(linux, "Upload ESP32-S3 Phase 3 build artifacts")
require_uploaded_images(phase3_upload, "Linux test")
require(f"{LINUX_PHASE3}/" in phase3_upload,
        "Linux test does not publish the esp-idf stage output")
require_failure_diagnostics(
    workflow_step(linux, "Upload ESP32-S3 Phase 3 failure diagnostics"),
    "Linux test", f"hashFiles('{LINUX_PHASE3}/jh_esp_idf_failure.txt')")
require(
    "linux-esp32-gamepad" in workflow_step(linux, "Upload ESP32 Classic gamepad build artifacts"),
    "Linux gamepad artifacts have no stable upload name",
)
require(
    f"hashFiles('{LINUX_GAMEPAD}/jh_esp_idf_failure.txt')"
    in workflow_step(linux, "Upload ESP32 Classic gamepad failure diagnostics"),
    "Linux gamepad failure upload is not tied to its diagnostic",
)

# The esp-idf stage: both fixtures, the all-features library, and failure
# output that shows the build log tail before the runner's diagnostic.
esp_stage = gate_function("stage_esp_idf")
require_real_esp_build(esp_stage, "runalltests.sh esp-idf stage")
require_gamepad_esp_build(esp_stage, "runalltests.sh esp-idf stage")
require("--jobs" not in esp_stage,
        "the esp-idf stage passes the removed --jobs option to build_esp_idf.py")
for fragment in (
    '"${GATE_BUILD_ROOT}/esp-idf/esp32s3-phase3"',
    '"${LOG_ROOT}/jh_esp32s3_phase3.log"',
    '"${GATE_BUILD_ROOT}/esp-idf/esp32-gamepad"',
    '"${LOG_ROOT}/jh_esp32_gamepad.log"',
    "scripts/build_esp32_lib.sh",
    "--target esp32s3",
    "--all-features",
    '"${GATE_BUILD_ROOT}/link-libraries/esp32s3"',
    '"${LOG_ROOT}/jh_esp32s3_link_library.log"',
    "libJaszczurHAL.a",
    "include/generated/jh_board_config.h",
):
    require(fragment in esp_stage, f"the esp-idf stage is missing {fragment!r}")
esp_failure = gate_function("run_esp_build")
for fragment in (
    "jh_esp_idf_failure.txt",
    'tail -n 300 "${output}/build.log"',
    "ESP-IDF failure diagnostic:",
    "Missing failure diagnostic:",
    "exit 1",
):
    require(fragment in esp_failure, f"ESP-IDF failure output is missing {fragment!r}")
require(
    esp_failure.index('tail -n 300 "${output}/build.log"')
    < esp_failure.index("sed -n '1,120p' \"${output}/jh_esp_idf_failure.txt\""),
    "the gate does not print the failure diagnostic after the log tail",
)

require(
    "tests/test_esp32s3_phase1.py" not in windows,
    "windows-tooling must not inspect the ESP32-S3 Phase 1 hardware fixture",
)
require("tests/hardware" not in WORKFLOW, "default CI must not read or build hardware fixtures")
require(
    "build_rp_pico_parity_fixtures.sh" not in QUALITY_GATE,
    "runalltests.sh must not build RP hardware fixtures",
)
for host_test in (
    "tests/test_esp32s3_phase2.py",
    "tests/test_esp32s3_phase3.py",
    "tests/test_esp_ci_contract.py",
):
    require(host_test in windows, f"windows-tooling does not run {host_test}")

jobs = re.findall(r"^  ([a-z0-9-]+):$", WORKFLOW, flags=re.MULTILINE)
require(
    not any("esp" in name for name in jobs),
    f"ESP builds run as gate stages, not as ESP-only CI jobs: {jobs!r}",
)
# The ESP32-S3 library comes from the esp-idf stage, not a library-* stage.
require("esp32s3" not in gate_function("stage_library") and
        not re.search(r"^\s+esp32s3:", QUALITY_GATE, flags=re.MULTILINE),
        "ESP-IDF must not join the library-* stages")

# Every ESP32-S3 example configuration builds in the examples-esp32s3 stage.
examples = gate_function("stage_examples")
for fragment in (
    "scripts/examples_dispatcher.py",
    '--target "$1"',
    '"${LOG_ROOT}/jh_examples_$1_build.log"',
):
    require(fragment in examples, f"the examples stage is missing {fragment!r}")
require(re.search(r"^EXAMPLE_TARGETS=\(.*\besp32s3\b", QUALITY_GATE, flags=re.MULTILINE)
        is not None, "runalltests.sh has no examples-esp32s3 stage")

print("ESP32-S3 Phase 3 CI and local gate integration verified")
