#!/usr/bin/env python3
"""Linux CI runs the stages of runalltests.sh, each once, and nothing else;
the local-only stages run only on the developer's host."""

from __future__ import annotations

from collections import Counter
import re
import subprocess
import sys

import yaml

from repo_root import repo_root  # noqa: E402
from source_assertions import shell_function_body  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
WORKFLOW = yaml.safe_load(
    (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
)
INSTALL = "./scripts/install_host_tools.sh"
GATE = re.compile(r'\./runalltests\.sh --stage (?:"([^"]+)"|(\S+))')
# A step that only hands a value to a later step, e.g. a cache key.
OUTPUT_ONLY = re.compile(r'echo "[a-z_]+=[^"]*" >> "\$GITHUB_OUTPUT"')
ACTIONS = {"actions/checkout@v6", "actions/cache@v5", "actions/upload-artifact@v6"}

# Written out here, independent of runalltests.sh and the workflow, so that
# dropping a stage or an architecture from both still fails. Change these only
# together with a deliberate change of coverage.
EXPECTED_LIBRARIES = [
    "rp2040:picow",
    "rp2350-arm:pico2w",
    "rp2350-riscv:pico2",
    "stm32g474:nucleo-g474re-pim730",
]
EXPECTED_LOCAL_ONLY = ["examples-rp2040", "examples-stm32g474", "examples-esp32s3"]
EXPECTED_STAGES = [
    "tools", "repository", "host", "sanitizer-fuzz", "memcheck", "cppcheck",
    "clang-tidy", "cpd", "stm32", "rp", "esp-idf",
    *(f"library-{entry.split(':')[0]}" for entry in EXPECTED_LIBRARIES),
    *EXPECTED_LOCAL_ONLY,
    "security",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def gate(*arguments: str) -> list[str]:
    result = subprocess.run(
        ["bash", str(ROOT / "runalltests.sh"), *arguments],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.split()


def expand(value: str, job: dict) -> list[str]:
    """``value`` with ``${{ matrix.<key> }}`` replaced by each matrix entry."""
    match = re.search(r"\$\{\{ matrix\.(\w+) \}\}", value)
    if match is None:
        return [value]
    entries = job.get("strategy", {}).get("matrix", {}).get(match.group(1))
    require(isinstance(entries, list), f"matrix.{match.group(1)} is not a list")
    return [value.replace(match.group(0), str(entry)) for entry in entries]


stages = gate("--list-stages")
require(stages == EXPECTED_STAGES,
        f"runalltests.sh stages changed: {stages}, expected {EXPECTED_STAGES}")
# The example builds stay local (user decision, 2026-10-06).
local_only = gate("--list-local-stages")
require(local_only == EXPECTED_LOCAL_ONLY,
        f"local-only stages must be the example builds, got {local_only}")
ci_stages = [stage for stage in stages if stage not in local_only]
require(WORKFLOW.get("permissions") == {"contents": "read"},
        "the GitHub token is not limited to reading contents")

run_stages: Counter[str] = Counter()
linux_images: set[str] = set()
for name, job in WORKFLOW["jobs"].items():
    runner = job["runs-on"]
    if runner.startswith("windows-"):
        continue
    require(runner.startswith("ubuntu-"), f"{name}: unexpected runner {runner}")
    linux_images.add(runner)
    gate_steps = 0
    for step in job["steps"]:
        if "uses" in step:
            require(step["uses"] in ACTIONS, f"{name}: unexpected action {step['uses']}")
            continue
        command = step["run"].strip()
        if command == INSTALL or OUTPUT_ONLY.fullmatch(command):
            continue
        match = GATE.fullmatch(command)
        require(
            match is not None,
            f"{name}: a Linux job runs '{command}' instead of a runalltests.sh "
            "stage; move the check into a stage",
        )
        gate_steps += 1
        for value in expand(match.group(1) or match.group(2), job):
            run_stages.update(value.split(","))
    require(gate_steps == 1, f"{name}: expected one runalltests.sh step, got {gate_steps}")

# The security scan covers ESP-IDF, so CI runs it in the job of the esp-idf
# stage, after it.
security_job = [
    value.split(",")
    for name, job in WORKFLOW["jobs"].items()
    if not job["runs-on"].startswith("windows-")
    for step in job["steps"] if "run" in step and GATE.fullmatch(step["run"].strip())
    for value in expand((GATE.fullmatch(step["run"].strip()).group(1)
                         or GATE.fullmatch(step["run"].strip()).group(2)), job)
    if "security" in value.split(",")
]
require(
    len(security_job) == 1 and "esp-idf" in security_job[0]
    and security_job[0].index("esp-idf") < security_job[0].index("security"),
    f"security must run after esp-idf in the same CI job, got {security_job}",
)

# One Linux image, the base the local gate runs on (runmefirst.sh).
require(linux_images == {"ubuntu-24.04"}, f"Linux CI images: {sorted(linux_images)}")
require(
    sorted(run_stages) == sorted(ci_stages) and set(run_stages.values()) == {1},
    "Linux CI must run every runalltests.sh stage except the local-only ones "
    f"exactly once; missing {sorted(set(ci_stages) - set(run_stages))}, "
    f"local-only or unknown {sorted(set(run_stages) - set(ci_stages))}, "
    f"repeated {sorted(stage for stage, count in run_stages.items() if count > 1)}",
)

for name, job in WORKFLOW["jobs"].items():
    for step in job["steps"]:
        require(
            step.get("uses") not in {"actions/checkout@v5", "actions/cache@v4"},
            f"{name}: obsolete GitHub Action {step.get('uses')}",
        )
for obsolete in (
    "stm32-build", "native-pico-build", "native-flag-matrix", "stm32-cyw43-library",
    "sanitizer-fuzz", "memcheck", "static-analysis", "security-scan",
    "linux-static-library",
):
    require(obsolete not in WORKFLOW["jobs"], f"obsolete CI job remains: {obsolete}")

# Every production architecture with its board, in the library-* stages and
# in the Windows library matrix, with every feature like the stage.
libraries = gate("--list-libraries")
require(libraries == EXPECTED_LIBRARIES,
        f"runalltests.sh library stages build {libraries}, expected {EXPECTED_LIBRARIES}")
windows_library = WORKFLOW["jobs"]["windows-static-library"]
require(
    [f"{row['target']}:{row['board']}"
     for row in windows_library["strategy"]["matrix"]["include"]] == EXPECTED_LIBRARIES,
    f"windows-static-library must build exactly {EXPECTED_LIBRARIES}",
)
windows_library_text = yaml.safe_dump(windows_library)
require("-DJH_ENABLE_ALL_FEATURES=ON" in windows_library_text,
        "the Windows library matrix does not enable all features")
require("tests/test_vscode_library_workspace.py" in yaml.safe_dump(WORKFLOW["jobs"]["windows-tooling"]),
        "Windows tooling CI does not validate the root VS Code workspace")

# What the stages run.
LOCAL_GATE = (ROOT / "runalltests.sh").read_text(encoding="utf-8")
SANITIZER_RUNNER = (ROOT / "scripts" / "run_sanitizer_fuzz.sh").read_text(encoding="utf-8")


def stage_body(function: str) -> str:
    return shell_function_body(LOCAL_GATE, function)


require("--all-features" in stage_body("build_library"),
        "the library stages do not enable all features")
require('-e "${SCRIPT_DIR}/third_party/esp-idf/.git"' in stage_body("stage_security"),
        "the security stage runs without ESP-IDF")

repository = stage_body("stage_repository")
require("scripts/sync_generated.py --check" in repository,
        "the repository stage does not fail on generated drift")
for bypass in (
    "scripts/generate_hal_features.py --check",
    "scripts/generate_board_config.py --boards-root boards --check-static",
    "scripts/vscode_library_workspace.py sync-vscode --check",
    "scripts/examples_dispatcher.py check-template",
    "scripts/check_sbom.sh",
):
    require(bypass not in LOCAL_GATE,
            f"runalltests.sh bypasses the shared generated-artifact runner: {bypass}")
for line in LOCAL_GATE.splitlines():
    if "sync_generated.py --write" in line:
        require(line.lstrip().startswith(("#", "fail ")),
                f"runalltests.sh rewrites generated files: {line.strip()}")
# Later jobs scan and build what the test job verified (generated files, SBOM).
for name, job in WORKFLOW["jobs"].items():
    if job["runs-on"].startswith("ubuntu-") and name != "test":
        require(job.get("needs") == "test", f"{name} does not wait for the test job")

# clang-tidy keeps stderr and fails on any diagnostic, in all four databases.
tidy = stage_body("run_tidy_pass")
require("run-clang-tidy " in tidy and "2>&1" in tidy,
        "clang-tidy standard error is not captured")
require(":[0-9]+:[0-9]+: (warning|error):" in stage_body("stage_clang_tidy"),
        "the clang-tidy stage does not reject concrete diagnostics")
passes = re.findall(r'^\s+run_tidy_pass "[^"]+" "\$\{(\w+)\}" (\w+) ',
                    stage_body("stage_clang_tidy"), flags=re.MULTILINE)
require(
    sorted(passes) == sorted([
        ("HOST_BUILD", "host"), ("plain_host", "host"),
        ("STM32_ARM_BUILD", "stm32"), ("STM32_HOST_BUILD", "stm32"),
    ]),
    f"clang-tidy must check both host databases and both STM32 ones, got {passes}",
)
require(
    re.search(r'"\$\{STM32_HOST_BUILD\}" -S link_libraries/stm32_lib \\\n\s+'
              r"-DJH_STM32_HOST_SANITY=ON", LOCAL_GATE) is not None,
    "STM32_HOST_BUILD is not the host-compiler STM32 database",
)

# --commit verifies the copied components before it hands over to the selected
# stages, so --commit --stage rp cannot build locally edited sources.
commit_mode = stage_body("run_on_commit")
handover = commit_mode.index('exec "${worktree}/runalltests.sh"')
for verification in (
    '"${worktree}/third_party/update_components.sh" --verify-only',
    'ensure_git_component("esp-idf", Path(sys.argv[1]), verify_only=True)',
):
    require(verification in commit_mode and commit_mode.index(verification) < handover,
            f"--commit hands over to the stages before {verification!r}")

# One shared sanitizer runner: the tools stage checks its Clang through
# install_host_tools.sh --check, the sanitizer-fuzz stage runs it.
HOST_TOOLS = (ROOT / "scripts" / "install_host_tools.sh").read_text(encoding="utf-8")
require('"${SCRIPT_DIR}/scripts/install_host_tools.sh" --check' in stage_body("stage_tools")
        and '"${SCRIPT_DIR}/run_sanitizer_fuzz.sh" --check-tools' in HOST_TOOLS,
        "the tools stage does not check the sanitizer tools")
sanitizer = stage_body("stage_sanitizer_fuzz")
require('"${SCRIPT_DIR}/scripts/run_sanitizer_fuzz.sh"' in sanitizer
        and '"${GATE_BUILD_ROOT}/sanitizer-fuzz"' in sanitizer,
        "the sanitizer-fuzz stage does not run the shared runner below .build/gate")
for fragment in (
    "-DJH_ENABLE_SANITIZERS=ON",
    "-DJH_ENABLE_FUZZING=ON",
    "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1",
    "ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1",
    "fuzz_http_server fuzz_websocket fuzz_http_multipart",
    "-DJH_ENABLE_THREAD_SANITIZER=ON",
    "TSAN_OPTIONS=halt_on_error=1",
):
    require(fragment in SANITIZER_RUNNER, f"shared sanitizer runner is missing: {fragment}")

print(f"Linux CI runs {len(ci_stages)} of {len(stages)} gate stages once; "
      f"{len(local_only)} run only locally")
