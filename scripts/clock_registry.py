#!/usr/bin/env python3
"""The closed clock-tree registry as `jh clock list` reads it.

config/hardware/clocks/<target>.json lists the trees a target supports.
`jh clock check` checks the registry against its backends and the boards;
this module gives hardware_resolver.py and generate_hardware_config.py the
trees, the board component behind each STM32G474 tree and why a board cannot
run a tree, all from jh.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import generate_board_config
import project_config

REPO_ROOT = Path(__file__).resolve().parents[1]
CLOCKS = REPO_ROOT / "config" / "hardware" / "clocks"
BOARDS = REPO_ROOT / "boards"


class ClockError(ValueError):
    """A clock registry or board registry that jh rejects."""


@dataclass(frozen=True)
class ClockModel:
    """One `jh clock list` result: the registry by target, the board
    component behind each STM32G474 tree and the source problems of every
    board, compatible target and tree."""

    registry: dict[str, dict[str, Any]]
    tree_components: dict[str, str | None]
    board_sources: dict[tuple[str, str, str], list[str]]


def _state(directory: Path, boards_root: Path) -> tuple[Any, ...] | None:
    """The board registry inputs and the clock files with their sizes and
    modification times; None when they cannot be listed."""
    boards = generate_board_config.registry_state(boards_root)
    try:
        clocks = tuple(
            (str(path), status.st_size, status.st_mtime_ns)
            for path, status in ((path, path.stat()) for path in sorted(directory.glob("*.json")))
        )
    except OSError:
        return None
    return None if boards is None else (boards, clocks)


_MODELS: dict[tuple[str, str], tuple[tuple[Any, ...], ClockModel]] = {}


def clock_model(directory: Path = CLOCKS, boards_root: Path = BOARDS) -> ClockModel:
    """The registry of directory with the boards of boards_root, kept for
    this process until its files change; ClockError with the jh message when
    jh rejects either."""
    clocks, boards = Path(directory).absolute(), Path(boards_root).absolute()
    key = (str(clocks), str(boards))
    state = _state(clocks, boards)
    cached = _MODELS.get(key)
    if cached is not None and state is not None and cached[0] == state:
        return cached[1]
    returncode, output = project_config.run_jh(
        ["clock", "list", f"--root={REPO_ROOT}", f"--boards-root={boards}", f"--clocks={clocks}"],
        ClockError,
    )
    if returncode != 0:
        raise ClockError(output.get("message", f"jh exit {returncode}"))
    model = ClockModel(
        registry={item["id"]: item["registry"] for item in output["targets"]},
        tree_components=output["stm32g474TreeComponents"],
        board_sources={(item["board"], item["target"], item["tree"]):
                       [error["message"] for error in item["errors"]]
                       for item in output["boardSources"]},
    )
    if state is not None:
        _MODELS[key] = (state, model)
    return model


def load_registry(directory: Path = CLOCKS, boards_root: Path = BOARDS) -> dict[str, dict[str, Any]]:
    """The registry by target."""
    return clock_model(directory, boards_root).registry


def board_sources_satisfy(board_id: str, target_id: str, tree: str,
                          boards_root: Path = BOARDS) -> list[str]:
    """Why the board cannot run the tree of the target; empty when it can."""
    return clock_model(CLOCKS, boards_root).board_sources[(board_id, target_id, tree)]


def __getattr__(name: str) -> Any:
    """STM32G474_TREE_COMPONENTS for importers, from jh."""
    if name == "STM32G474_TREE_COMPONENTS":
        return clock_model().tree_components
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
