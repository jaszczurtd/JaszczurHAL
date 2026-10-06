#!/usr/bin/env python3
"""Validate the managed build artifact layout used by automated workflows."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


from repo_root import repo_root  # noqa: E402
from source_assertions import shell_function_body  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
BUILD_ROOT = ROOT / ".build"
sys.path.insert(0, str(ROOT / "scripts"))
import examples_dispatcher


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


for example_dir in examples_dispatcher.selected_example_dirs([]):
    manifest_path = example_dir / ".vscode" / "jaszczurhal.project.json"
    manifest = load_json(manifest_path)
    expected = f"${{jhRoot}}/.build/examples/{example_dir.name}"
    require(
        manifest.get("buildDir") == expected,
        f"{example_dir.name}: buildDir escapes the central .build tree",
    )
    require(
        manifest.get("cmakeBuildDir") == "${buildDir}/cmake",
        f"{example_dir.name}: CMake output is not below buildDir",
    )
    cache = manifest.get("cmake", {}).get("cache", {})
    require(
        cache.get("JH_ARTIFACT_DIR") == "${buildDir}",
        f"{example_dir.name}: final artifacts do not follow buildDir",
    )

from board_registry import tooling_target_registry

tooling_registry = tooling_target_registry(ROOT)
for target in ("rp2040", "rp2350-arm", "rp2350-riscv"):
    descriptor = tooling_registry[target]
    require(
        "JH_PICOTOOL_EXECUTABLE" not in descriptor.get("cache", {}),
        f"{target}: registry hardcodes a host-specific picotool executable",
    )

for script in (
    "build_rp_pico_lib.sh",
    "build_stm32_lib.sh",
    "build_esp32_lib.sh",
):
    text = (ROOT / "scripts" / script).read_text(encoding="utf-8")
    require(
        "jh_resolve_build_output" in text,
        f"{script}: output path is not constrained by the shared helper",
    )

quality_gate = (ROOT / "runalltests.sh").read_text(encoding="utf-8")
require(
    'LOG_ROOT="${GATE_BUILD_ROOT}/logs"' in quality_gate,
    "runalltests.sh logs are not below .build/gate",
)
require(
    'PYTHONPYCACHEPREFIX="${BUILD_ROOT}/python-cache"' in quality_gate,
    "runalltests.sh Python cache is not below .build",
)
host_tools = (ROOT / "scripts" / "install_host_tools.sh").read_text(encoding="utf-8")
require(
    "cmake ninja g++ gcc make" in host_tools
    and '"${SCRIPT_DIR}/scripts/install_host_tools.sh" --check' in quality_gate,
    "the tools stage does not verify the default Ninja generator",
)
require(
    'scripts/run_cpd.py --output-dir "${GATE_BUILD_ROOT}/cpd"' in quality_gate,
    "runalltests.sh does not keep CPD reports below .build/gate",
)
memcheck_gate = shell_function_body(quality_gate, "stage_memcheck")
require(
    'ctest --test-dir "${HOST_BUILD}" -N' in memcheck_gate
    and "-L '^memcheck$'" in memcheck_gate
    and "-LE '^no_memcheck$'" not in memcheck_gate
    and '-R "${memcheck_regex}"' not in memcheck_gate,
    "runalltests.sh memcheck does not select every labelled native test",
)
require(
    '| tee "${LOG_ROOT}/jh_memcheck.log"' in memcheck_gate
    and "| grep -E '(^[0-9]|Memory|passed|failed|Defects)'"
    not in memcheck_gate,
    "runalltests.sh memcheck progress is filtered or not logged live",
)
tests_cmake = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
generated_runner = (ROOT / "scripts" / "sync_generated.py").read_text(
    encoding="utf-8"
)
pre_commit_hook = (ROOT / ".githooks" / "pre-commit").read_text(
    encoding="utf-8"
)
require(
    "DIRECTORY PROPERTY TESTS" in tests_cmake
    and "DIRECTORY PROPERTY BUILDSYSTEM_TARGETS" in tests_cmake
    and 'APPEND PROPERTY LABELS memcheck' in tests_cmake
    and "no_memcheck" not in tests_cmake,
    "native CTest executables are not automatically labelled for memcheck",
)
# The gate only checks generated files, as CI does; rewriting them stays a
# separate command.
require(
    'scripts/sync_generated.py --check --report-file "${GENERATED_REPORT}"'
    in quality_gate
    and "--check-generated" not in quality_gate,
    "runalltests.sh does not verify generated artifacts strictly",
)
require(
    'done < "${GENERATED_REPORT}"' in quality_gate,
    "runalltests.sh does not include generated changes in its final summary",
)
require(
    '("scripts/generate_sbom.py",)' in generated_runner
    and '("scripts/generate_sbom.py", "--check")' in generated_runner,
    "shared generated-artifact runner does not refresh and verify the SBOM",
)
require(
    'python3 "$staged_tree/scripts/sync_generated.py" --check; then' in pre_commit_hook
    and "python3 scripts/sync_generated.py --write" in pre_commit_hook
    and "Commit blocked: generated artifacts are missing or stale."
    in pre_commit_hook,
    "pre-commit hook does not block stale generated artifacts with repair guidance",
)


def hook_scenarios(work: Path) -> None:
    """Run the real pre-commit hook in a scratch repository, with a
    clang-format that appends a marker line and a generated-artifact check
    that wants generated.txt equal to config.txt and an unformatted layout.h
    in the tree it runs from."""
    repo = work / "repo"
    (repo / ".githooks").mkdir(parents=True)
    shutil.copy2(ROOT / ".githooks" / "pre-commit", repo / ".githooks" / "pre-commit")
    (repo / "scripts").mkdir()
    # Like the real runner, the stub lists the tree through git; it must see
    # the commit's index there, not an empty one.
    (repo / "scripts" / "sync_generated.py").write_text(
        "import pathlib, subprocess, sys\n"
        "root = pathlib.Path(__file__).resolve().parents[1]\n"
        "cached = subprocess.run(['git', '-C', str(root), 'ls-files', '--cached'],\n"
        "                        capture_output=True, text=True).stdout.split()\n"
        "if 'scripts/sync_generated.py' not in cached:\n"
        "    sys.exit('the staged tree does not see the commit index')\n"
        "layout = root / 'layout.h'\n"
        "if layout.exists() and 'formatted' in layout.read_text():\n"
        "    sys.exit('layout.h changed by formatting')\n"
        "config, generated = root / 'config.txt', root / 'generated.txt'\n"
        "sys.exit(config.exists() and (not generated.exists()"
        " or generated.read_text() != config.read_text()))\n",
        encoding="utf-8",
    )
    tools = work / "bin"
    tools.mkdir()
    formatter = tools / "clang-format"
    formatter.write_text(
        '#!/bin/sh\n[ "$1" = -i ] || exit 2\n'
        'grep -q "// formatted" "$2" || echo "// formatted" >> "$2"\n',
        encoding="utf-8",
    )
    formatter.chmod(0o755)
    environment = {**os.environ, "PATH": f"{tools}{os.pathsep}{os.environ['PATH']}"}

    def git(*arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["git", "-c", "user.name=t", "-c", "user.email=t@t", *arguments],
            cwd=repo, env=environment, capture_output=True, text=True, check=False,
        )

    def stage(name: str, data: bytes) -> None:
        (repo / name).write_bytes(data)
        require(git("add", name).returncode == 0, f"scratch repository: git add {name}")

    def commit() -> subprocess.CompletedProcess[str]:
        return git("commit", "-q", "-m", "feat: probe")

    for arguments in (("init", "-q"), ("config", "core.hooksPath", ".githooks"),
                      ("add", "scripts")):
        require(git(*arguments).returncode == 0, f"scratch repository: git {arguments}")
    require(commit().returncode == 0, "the first commit was refused")

    # The hook formats and normalizes the staged files and commits the result.
    stage("config.h", b"#define A 1\n")
    stage("notes.md", "one\r\nit\u2019s \u201cquoted\u201d \u2014 done\u2026  \r\n".encode())
    formatted = commit()
    require(formatted.returncode == 0, f"the hook refused a fixable commit:\n{formatted.stderr}")
    require(git("show", "HEAD:config.h").stdout == "#define A 1\n// formatted\n",
            "the hook did not commit the formatted C file")
    require(git("show", "HEAD:notes.md").stdout == "one\nit's \"quoted\" - done...\n",
            "the hook did not normalize line ends, blanks and punctuation")

    # The generator check reads the files after formatting, so a formatting
    # change a generator rejects blocks the commit.
    stage("layout.h", b"#define LAYOUT 1\n")
    reformatted = commit()
    require(
        reformatted.returncode != 0
        and "generated artifacts are missing or stale" in reformatted.stderr,
        "pre-commit hook checks generated artifacts before formatting the commit:\n"
        f"{reformatted.stderr}",
    )
    require(git("rm", "-q", "--cached", "layout.h").returncode == 0, "git rm layout.h")
    (repo / "layout.h").unlink()

    # Generators check the staged tree: a staged input with its regenerated
    # output left unstaged blocks the commit, though the working tree is fresh.
    stage("config.txt", b"a\n")
    stage("generated.txt", b"a\n")
    require(commit().returncode == 0, "a fresh generated pair was refused")
    stage("config.txt", b"b\n")
    (repo / "generated.txt").write_bytes(b"b\n")
    stale = commit()
    require(
        stale.returncode != 0 and "generated artifacts are missing or stale" in stale.stderr,
        f"pre-commit hook accepted a stale staged generated file:\n{stale.stderr}",
    )
    stage("generated.txt", b"b\n")
    require(commit().returncode == 0, "the fully staged generated pair was refused")
    # A commit naming paths uses a temporary index; the same rule holds.
    (repo / "config.txt").write_bytes(b"c\n")
    (repo / "generated.txt").write_bytes(b"c\n")
    partial = git("commit", "-q", "-m", "feat: probe", "config.txt")
    require(
        partial.returncode != 0 and "generated artifacts are missing or stale" in partial.stderr,
        f"pre-commit hook accepted a path commit without its generated file:\n{partial.stderr}",
    )
    require(git("commit", "-q", "-m", "feat: probe", "config.txt", "generated.txt").returncode == 0,
            "a path commit with both files was refused")
    require(not (repo / ".build" / "pre-commit" / "staged").exists(),
            "the hook left its staged-tree export behind")


with tempfile.TemporaryDirectory() as hook_work:
    hook_scenarios(Path(hook_work))
for duplicated_generator in (
    "scripts/generate_hal_features.py --write",
    "scripts/generate_board_config.py --boards-root boards --write-static",
    "scripts/examples_dispatcher.py generate-template",
    "scripts/examples_dispatcher.py generate",
    "scripts/vscode_library_workspace.py sync-vscode",
    "scripts/generate_sbom.py",
):
    require(
        duplicated_generator not in quality_gate,
        f"runalltests.sh bypasses the shared generated-artifact runner: "
        f"{duplicated_generator}",
    )
require(
    "/tmp/jh_" not in quality_gate,
    "runalltests.sh still writes logs outside .build",
)

# Every CMake build tree inside the repository must sit below a .build
# directory; the configure-time guard cannot remove what a refused configure
# already wrote.
stray_build_trees = []
for directory, subdirectories, files in os.walk(ROOT):
    subdirectories[:] = [
        name
        for name in subdirectories
        if name not in {".build", ".git", "third_party"}
    ]
    if "CMakeCache.txt" in files:
        stray_build_trees.append(Path(directory).relative_to(ROOT).as_posix())
require(
    not stray_build_trees,
    "CMake build trees outside .build/: "
    + ", ".join(sorted(stray_build_trees))
    + "; delete them and configure into .build/<name>",
)

sanitizer_runner = (ROOT / "scripts" / "run_sanitizer_fuzz.sh").read_text(
    encoding="utf-8"
)
require(
    '"${REPO_ROOT}/.build/"*' in sanitizer_runner
    and 'cmake -E remove_directory "${BUILD_DIR}"' in sanitizer_runner,
    "sanitizer runner does not constrain its clean build below .build",
)
require(
    '"${GATE_BUILD_ROOT}/sanitizer-fuzz"' in quality_gate
    and '"${LOG_ROOT}/jh_sanitizer_fuzz.log"' in quality_gate,
    "runalltests.sh sanitizer outputs are not below .build/gate",
)

helper = ROOT / "scripts" / "lib" / "build_artifacts.sh"
accepted = subprocess.run(
    [
        "bash",
        "-c",
        'source "$1"; jh_resolve_build_output "$2" ".build/custom/test" unused',
        "bash",
        str(helper),
        str(ROOT),
    ],
    check=False,
    capture_output=True,
    text=True,
)
require(accepted.returncode == 0, "shared helper rejected a .build output")
require(
    Path(accepted.stdout.strip()) == BUILD_ROOT / "custom" / "test",
    "shared helper resolved an unexpected managed output",
)
rejected = subprocess.run(
    [
        "bash",
        "-c",
        'source "$1"; jh_resolve_build_output "$2" "build_legacy" unused',
        "bash",
        str(helper),
        str(ROOT),
    ],
    check=False,
    capture_output=True,
    text=True,
)
require(rejected.returncode != 0, "shared helper accepted output outside .build")

for probe in (
    "test_target_selection.cmake",
    "test_board_selection.cmake",
    "test_network_backend_selection.cmake",
):
    text = (ROOT / "tests" / probe).read_text(encoding="utf-8")
    require(
        "jh_test_artifact_dir" in text,
        f"{probe}: compiler probes are not redirected below .build",
    )
    require(
        "CMAKE_CURRENT_BINARY_DIR" not in text,
        f"{probe}: script-mode output still depends on the caller directory",
    )

target_artifacts = {
    "cmake/targets/rp-pico.cmake": ("firmware.elf", "firmware.bin", "firmware.uf2", "firmware.hex", "firmware.map"),
    "cmake/targets/stm32g474.cmake": ("firmware.elf", "firmware.bin", "firmware.hex", "firmware.map"),
}
for recipe, artifacts in target_artifacts.items():
    text = (ROOT / recipe).read_text(encoding="utf-8")
    for artifact in artifacts:
        require(
            artifact in text,
            f"{recipe}: managed artifact layout omits {artifact}",
        )

stm32_linker_script = (ROOT / "link_libraries/stm32_lib" / "STM32G474RETx_FLASH.ld").read_text(
    encoding="utf-8"
)
for section in (".preinit_array", ".init_array"):
    require(
        f"{section} (READONLY)" in stm32_linker_script,
        f"STM32 linker script leaves {section} writable in the executable segment",
    )
stm32_recipe = (ROOT / "link_libraries/stm32_lib" / "jh_stm32g474_firmware.cmake").read_text(
    encoding="utf-8"
)
require(
    'LINK_DEPENDS "${_ldscript}"' in stm32_recipe,
    "STM32 firmware does not relink when its linker script changes",
)
require(
    "-Wl,-u,_printf_float" in stm32_recipe,
    "STM32 firmware does not enable newlib-nano floating-point formatting",
)
stm32_library = (ROOT / "link_libraries/stm32_lib" / "CMakeLists.txt").read_text(encoding="utf-8")
require(
    'OUTPUT_ROOT "${CMAKE_BINARY_DIR}"' in stm32_library,
    "STM32 static-library generator is not scoped to its CMake build tree",
)
require(
    'set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH' in stm32_library,
    "STM32 static-library configuration does not consume its toolchain cache value",
)

gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
require("build_*/" not in gitignore, ".gitignore hides legacy root build directories")
require("*.o" not in gitignore, ".gitignore hides misplaced object files")
require(BUILD_ROOT.name == ".build", "invalid managed build root")
