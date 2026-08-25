#!/usr/bin/env python3
"""Validate the repository-root VS Code static-library workflow."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile


ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT / "scripts"))

import vscode_library_workspace as workspace


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


registry = workspace.load_registry(ROOT)
require(
    set(registry) == {
        "mock",
        "rp2040",
        "rp2350-arm",
        "rp2350-riscv",
        "stm32g474",
    },
    "library workspace target list drifted from the board registry",
)

for relative, expected in workspace.root_vscode_documents(ROOT).items():
    tracked = json.loads((ROOT / relative).read_text(encoding="utf-8"))
    require(
        tracked == expected,
        f"tracked {relative} is stale; run "
        "'python3 scripts/vscode_library_workspace.py sync-vscode'",
    )

tasks = json.loads((ROOT / ".vscode/tasks.json").read_text(encoding="utf-8"))
task_labels = {item["label"] for item in tasks["tasks"]}
binding_labels = {
    item["args"] for item in workspace.root_keybindings_reference()
}
require(
    binding_labels <= task_labels,
    "a repository keybinding does not resolve to a root workspace task",
)
require(
    tasks["inputs"][0]["options"] == workspace.selection_options(registry),
    "VS Code target/board picker drifted from boards/",
)

gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
for ignored in (
    "/.vscode/c_cpp_properties.json",
    "/.vscode/jaszczurhal.library.local.json",
):
    require(ignored in gitignore, f"local VS Code state is tracked: {ignored}")

rp = workspace.profile_from_selection(registry, "rp2040", "pico")
rp_paths = workspace.profile_paths(ROOT, rp)
rp_commands = workspace.build_commands(ROOT, rp, rp_paths)
require(len(rp_commands) == 1, "RP library build gained duplicate entrypoints")
require("--library-only" in rp_commands[0], "RP workspace build is not archive-only")
require("--target" in rp_commands[0], "RP workspace build omitted its target")
require("--board" in rp_commands[0], "RP workspace build omitted its board")

stm32 = workspace.profile_from_selection(
    registry,
    "stm32g474",
    "nucleo-g474re",
)
stm32_paths = workspace.profile_paths(ROOT, stm32)
stm32_commands = workspace.build_commands(ROOT, stm32, stm32_paths)
stm32_helper = Path(stm32_commands[0][0])
require(
    stm32_helper.name == "build_stm32_lib.sh"
    and stm32_helper.parent.name == "scripts",
    "STM32 workspace build bypasses the static-library helper",
)

mock = workspace.profile_from_selection(registry, "mock", "host-mock")
mock_paths = workspace.profile_paths(ROOT, mock)
mock_commands = workspace.build_commands(ROOT, mock, mock_paths)
require(len(mock_commands) == 2, "mock workspace build must configure and build")
require("hal_mock" in mock_commands[1], "mock build does not select hal_mock")

experimental = workspace.parse_selection(
    registry,
    "rp2040:pico-rm2 - Raspberry Pi Pico with PIM730/RM2 (experimental)",
)
require(
    experimental.selection == "rp2040:pico-rm2",
    "GUI selection label was not normalized",
)

with tempfile.TemporaryDirectory(prefix="jh-vscode-library-") as temporary_text:
    temporary = Path(temporary_text)
    (temporary / ".vscode").mkdir(parents=True)
    workspace.save_active_profile(temporary, rp)
    require(
        workspace.load_active_profile(temporary, registry) == rp,
        "local active library profile did not round-trip",
    )

    paths = workspace.profile_paths(temporary, rp)
    paths.build_dir.mkdir(parents=True)
    paths.install_dir.mkdir(parents=True)
    (paths.build_dir / "owned").write_text("build", encoding="utf-8")
    (paths.install_dir / "owned").write_text("install", encoding="utf-8")
    sibling = temporary / ".build/vscode/library/rp2350-arm/pico2"
    sibling.mkdir(parents=True)
    (sibling / "keep").write_text("keep", encoding="utf-8")
    workspace.write_cpp_properties(temporary, rp, paths)
    workspace.clean_profile(temporary, rp)
    require(not paths.build_dir.exists(), "active build directory survived clean")
    require(not paths.install_dir.exists(), "active install directory survived clean")
    require(sibling.is_dir(), "clean removed a different library profile")
    require(
        not (temporary / workspace.CPP_PROPERTIES).exists(),
        "clean retained managed IntelliSense state",
    )

    try:
        workspace.ensure_managed_path(
            temporary,
            temporary / ".build",
            Path(".build"),
        )
    except workspace.WorkspaceError:
        pass
    else:
        raise AssertionError("managed-path guard accepted the whole .build root")

print("VS Code library workspace checks passed")
