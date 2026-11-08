"""The board registry for the HAL scripts that import it: the model `jh board
list` validates and the build configuration `jh board config` generates.

`jh generate boards` writes the checked-in board artifacts and `jh board config`
the files of one build; this module keeps the Python view of both for its
importers."""

from __future__ import annotations

from dataclasses import dataclass
from functools import partial
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from typing import Any

from codegen_support import (
    atomic_write_text,
    require_exact_fields,
    validation_error,
)
import generate_hal_features
import project_config


class DescriptorError(ValueError):
    """Descriptor validation failure with actionable context."""


fail = partial(validation_error, DescriptorError)
exact_fields = partial(require_exact_fields, error_type=DescriptorError)
atomic_write = partial(atomic_write_text, error_type=DescriptorError)

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
FEATURE_PATTERN = re.compile(
    r"^(?:-D)?(HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)(?:=(.*))?$"
)


@dataclass(frozen=True)
class BoardRegistry:
    """One board registry as `jh board list` validated it. The component,
    device-role and CAN tables come from jh, which owns them."""

    targets: dict[str, dict[str, Any]]
    boards: dict[str, dict[str, Any]]
    capabilities: dict[str, Any]
    components: dict[str, dict[str, Any]]
    device_roles: dict[str, dict[str, Any]]
    can_controllers: dict[str, dict[str, Any]]
    bus_device_kind: str


def registry_state(boards_root: Path) -> tuple[Any, ...] | None:
    """Names, sizes and modification times of the registry inputs; None when
    they cannot be listed, and then no result is kept."""
    try:
        inputs = [
            REPOSITORY_ROOT / "config" / "tooling" / "board_components.json",
            boards_root / "capabilities.json",
            *sorted((boards_root / "targets").glob("*.json")),
            *sorted((boards_root / "profiles").glob("*.json")),
        ]
        return tuple(
            (str(path), status.st_size, status.st_mtime_ns)
            for path, status in ((path, path.stat()) for path in inputs)
        )
    except OSError:
        return None


_MODELS: dict[str, tuple[tuple[Any, ...], BoardRegistry]] = {}


def registry_model(boards_root: Path) -> BoardRegistry:
    """The registry below boards_root as `jh board list` validates it, kept
    for this process until its files change; DescriptorError with the jh
    message when jh rejects it."""
    root = Path(boards_root).absolute()
    key = str(root)
    state = registry_state(root)
    cached = _MODELS.get(key)
    if cached is not None and state is not None and cached[0] == state:
        return cached[1]
    returncode, output = project_config.run_jh(
        ["board", "list", f"--root={REPOSITORY_ROOT}", f"--boards-root={root}"],
        DescriptorError,
    )
    if returncode != 0:
        raise DescriptorError(output.get("message", f"jh exit {returncode}"))
    model = BoardRegistry(
        targets={item["id"]: item["descriptor"] for item in output["targets"]},
        boards={item["id"]: item["descriptor"] for item in output["boards"]},
        capabilities=output["capabilities"],
        components={
            item["id"]: {"providers": set(item["providers"]), "slot": item["slot"]}
            for item in output["components"]
        },
        device_roles=output["deviceRoles"],
        can_controllers={
            name: {**item, "targets": set(item["targets"])}
            for name, item in output["canControllers"].items()
        },
        bus_device_kind=output["busDeviceKind"],
    )
    if state is not None:
        _MODELS[key] = (state, model)
    return model


def load_registry(
    boards_root: Path,
) -> tuple[
    dict[str, dict[str, Any]],
    dict[str, dict[str, Any]],
    dict[str, Any],
]:
    """Targets, boards and capabilities of the registry below boards_root."""
    model = registry_model(boards_root)
    return model.targets, model.boards, model.capabilities


def _tables() -> BoardRegistry:
    """The jh tables, read with the registry of this repository."""
    return registry_model(REPOSITORY_ROOT / "boards")


def __getattr__(name: str) -> Any:
    """COMPONENT_REGISTRY and CAN_CONTROLLERS for importers, from jh."""
    if name == "COMPONENT_REGISTRY":
        return _tables().components
    if name == "CAN_CONTROLLERS":
        return _tables().can_controllers
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")

def expand_pin_set(path: Path, json_path: str, value: Any) -> set[Any]:
    pin_set = exact_fields(path, json_path, value, set(), {"ranges", "values"})
    if ("ranges" in pin_set) == ("values" in pin_set):
        fail(path, json_path, value, "exactly one of ranges or values")
    result: set[Any] = set()
    if "ranges" in pin_set:
        ranges = pin_set["ranges"]
        if not isinstance(ranges, list) or not ranges:
            fail(path, f"{json_path}.ranges", ranges, "a non-empty array")
        for index, item in enumerate(ranges):
            item_path = f"{json_path}.ranges[{index}]"
            exact_fields(path, item_path, item, {"first", "last"}, {"first", "last"})
            first, last = item["first"], item["last"]
            if (
                not isinstance(first, int)
                or isinstance(first, bool)
                or not isinstance(last, int)
                or isinstance(last, bool)
                or first < 0
                or last < first
            ):
                fail(path, item_path, item, "a non-negative ordered integer range")
            result.update(range(first, last + 1))
    else:
        values = pin_set["values"]
        if not isinstance(values, list) or not values:
            fail(path, f"{json_path}.values", values, "a non-empty array")
        for item in values:
            if not isinstance(item, (int, str)) or isinstance(item, bool):
                fail(path, f"{json_path}.values", item, "integer or string pin IDs")
            result.add(item)
        if len(result) != len(values):
            fail(path, f"{json_path}.values", values, "unique pin IDs")
    return result


def board_device_owner(device_id: str) -> str:
    """Reservation owner of a board device: board.<device-id in kebab case>."""
    return "board." + re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "-", device_id).lower()


def normalize_features(features: list[str]) -> list[str]:
    normalized: set[str] = set()
    for feature in features:
        value = feature.strip()
        match = FEATURE_PATTERN.fullmatch(value)
        if match is None:
            raise DescriptorError(
                f"feature {feature!r}: expected a HAL_ENABLE_* or "
                "HAL_DISABLE_* symbol, optionally followed by =1"
            )
        explicit_value = match.group(2)
        if explicit_value is not None and explicit_value != "1":
            raise DescriptorError(
                f"[JH-CFG-VALUE] feature {feature!r}: HAL feature symbols "
                "accept only a bare macro or an explicit value of 1"
            )
        normalized.add(f"{match.group(1)}=1")
    ordered = sorted(normalized)
    return ordered


def validate_definitions(definitions: list[str]) -> None:
    for definition in definitions:
        value = definition.strip()
        if "$<" in value:
            raise DescriptorError(
                f"[JH-CFG-VALUE] compile definition {definition!r}: "
                "generator expressions are not supported"
            )
        normalized = value.removeprefix("-D")
        if not normalized.startswith(("HAL_ENABLE_", "HAL_DISABLE_")):
            if "HAL_ENABLE_" in normalized or "HAL_DISABLE_" in normalized:
                raise DescriptorError(
                    f"[JH-CFG-VALUE] compile definition {definition!r}: "
                    "HAL feature symbols must be standalone bare macros or "
                    "have an explicit value of 1"
                )
            continue
        match = FEATURE_PATTERN.fullmatch(value)
        if match is None or (match.group(2) is not None and match.group(2) != "1"):
            raise DescriptorError(
                f"[JH-CFG-VALUE] compile definition {definition!r}: "
                "HAL feature symbols accept only a bare macro or an explicit "
                "value of 1"
            )


def resolve_features(
    features: list[str], target: dict[str, Any]
) -> tuple[list[str], list[str], dict[str, list[str]], str]:
    """Requested and resolved features, their provenance and the digest of the
    resolved set."""
    normalized = normalize_features(features)
    required = normalize_features(target.get("requiredFeatures", []))
    requested_symbols = {item.removesuffix("=1") for item in normalized}
    for definition in required:
        symbol = definition.removesuffix("=1")
        disabled = symbol.replace("HAL_ENABLE_", "HAL_DISABLE_", 1)
        if disabled in requested_symbols:
            raise DescriptorError(
                f"[JH-CFG-TARGET-REQUIRED] {target['id']} requires {symbol}; "
                f"{disabled} cannot be requested"
            )
    requests = [
        generate_hal_features.FeatureRequest(
            symbol=definition.removesuffix("=1"),
            value="1",
            source=f"command-line:--requested-feature[{index}]",
        )
        for index, definition in enumerate(normalized)
    ]
    requests.extend(
        generate_hal_features.FeatureRequest(
            symbol=definition.removesuffix("=1"),
            value="1",
            source=f"target:{target['id']}:requiredFeatures[{index}]",
        )
        for index, definition in enumerate(required)
        if definition.removesuffix("=1") not in requested_symbols
    )
    try:
        model = generate_hal_features.load_registry(
            Path(__file__).resolve().parents[1] / "config"
        )
        resolution, findings = generate_hal_features.resolve_feature_requests(
            requests, model, "board configuration"
        )
    except generate_hal_features.RegistryError as error:
        raise DescriptorError(str(error)) from error
    if findings:
        raise DescriptorError("\n".join(findings))
    requested = [definition.removesuffix("=1") for definition in normalized]
    provenance = {
        symbol: list(sources) for symbol, sources in resolution.provenance.items()
    }
    for index, definition in enumerate(required):
        symbol = definition.removesuffix("=1")
        source = f"target:{target['id']}:requiredFeatures[{index}]"
        provenance[symbol] = sorted({*provenance.get(symbol, []), source})
    return requested, list(resolution.resolved), provenance, resolution.digest


def board_compile_definitions(
    target: dict[str, Any], board: dict[str, Any]
) -> list[str]:
    components = set(target["components"]) | set(board["components"])
    definitions: list[str] = []
    if "cyw43-pico-pio" in components:
        definitions.extend(
            [
                "HAL_NETWORK_BACKEND_CYW43",
                "HAL_CYW43_BUS_PICO_PIO",
                "HAL_CYW43_STACK_LWIP",
                "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u",
            ]
        )
    if "stm32g474-hse-24mhz" in components:
        definitions.append("HAL_STM32G474_CLOCK_HSE_160MHZ")
    if "cyw43-stm32-gspi" in components:
        definitions.extend(
            [
                "HAL_NETWORK_BACKEND_CYW43",
                "HAL_CYW43_BUS_STM32_GSPI",
                "HAL_CYW43_STACK_LWIP",
                "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u",
            ]
        )
    return definitions


OUTPUT_NAMES = (
    "jh_board_config.cmake",
    "jh_board_config.h",
    "jh_link_contract.h",
    "jh_link_contract_definition.c",
    "jh_link_contract_reference.c",
    "jh_board_resolved.json",
    "generation.d",
)


def generator_modules() -> list[str]:
    """The modules of this directory the process loaded: inputs of the files
    a Python caller writes."""
    scripts_dir = Path(__file__).resolve().parent
    return sorted(
        {
            str(Path(module.__file__).resolve())
            for module in list(sys.modules.values())
            if getattr(module, "__file__", None)
            and Path(module.__file__).resolve().parent == scripts_dir
        }
    )


def generate(
    target: dict[str, Any],
    board: dict[str, Any],
    boards: dict[str, dict[str, Any]],
    capabilities: dict[str, Any],
    output_dir: Path,
    requested_feature_inputs: list[str],
    boards_root: Path | None = None,
) -> None:
    outputs = build_outputs(
        target, board, boards, capabilities, requested_feature_inputs, boards_root
    )
    try:
        (output_dir / "jh_board_registry.h").unlink()
    except FileNotFoundError:
        pass
    for name, content in outputs.items():
        atomic_write(output_dir / name, content)


def build_outputs(
    target: dict[str, Any],
    board: dict[str, Any],
    boards: dict[str, dict[str, Any]],
    capabilities: dict[str, Any],
    requested_feature_inputs: list[str],
    boards_root: Path | None = None,
    *,
    header_definitions: list[str] | None = None,
    cmake_definitions: list[str] | None = None,
    components: list[str] | None = None,
    contract_hash: str | None = None,
    header_prelude: list[str] = (),
    generation_extra: list[Path] = (),
) -> dict[str, str]:
    """Text of every per-build output, from `jh board config` of the registry
    below boards_root. An assembly passes the board it resolved and the values
    it owns: the definitions its header carries, those still passed to the
    compiler, its components and its digest for the link signature. Without
    them the outputs are the board's. boards and capabilities are the
    registry's; jh reads them itself."""
    del boards, capabilities
    assembly: dict[str, Any] = {
        "board": board,
        "headerPrelude": list(header_prelude),
        "generationExtra": [
            *generator_modules(),
            *(str(path) for path in generation_extra),
        ],
    }
    if header_definitions is not None:
        assembly["headerDefinitions"] = list(header_definitions)
    if cmake_definitions is not None:
        assembly["cmakeDefinitions"] = list(cmake_definitions)
    if components is not None:
        assembly["components"] = list(components)
    if contract_hash:
        assembly["contractHash"] = contract_hash
    root = Path(boards_root or REPOSITORY_ROOT / "boards").absolute()
    # The board and the sources of an assembly outgrow a Windows command line.
    with tempfile.TemporaryDirectory(prefix="jh-board-") as directory:
        assembly_file = Path(directory) / "assembly.json"
        assembly_file.write_text(json.dumps(assembly), encoding="utf-8")
        returncode, output = project_config.run_jh(
            [
                "board",
                "config",
                f"--root={REPOSITORY_ROOT}",
                f"--boards-root={root}",
                f"--target={target['id']}",
                f"--board={board['id']}",
                f"--file={assembly_file}",
                *(f"--feature={item}" for item in requested_feature_inputs),
            ],
            DescriptorError,
        )
    if returncode != 0:
        raise DescriptorError(output.get("message", f"jh exit {returncode}"))
    return {name: output["files"][name] for name in OUTPUT_NAMES}


def require_managed_output(
    output_dir: Path, output_root: Path | None, repository_root: Path
) -> Path:
    """The resolved output directory, which must lie below a .build directory
    or the runtime-managed build root."""
    output_dir = output_dir.resolve()
    managed_root = (
        output_root.resolve()
        if output_root is not None
        else repository_root / ".build"
    )
    host_build_root = os.environ.get("JH_MANAGED_BUILD_ROOT")
    matches_host_build_root = False
    if host_build_root:
        normalized_managed = os.path.normcase(str(managed_root))
        normalized_host = os.path.normcase(str(Path(host_build_root).resolve()))
        try:
            matches_host_build_root = (
                os.path.commonpath((normalized_managed, normalized_host))
                == normalized_host
            )
        except ValueError:
            matches_host_build_root = False
    if ".build" not in managed_root.parts and not matches_host_build_root:
        raise DescriptorError(
            f"output root must be a .build directory, one of its "
            f"descendants, or the runtime-managed build root; got {managed_root}"
        )
    if output_dir != managed_root and managed_root not in output_dir.parents:
        raise DescriptorError(
            f"output directory must be below {managed_root}, got {output_dir}"
        )
    return output_dir
