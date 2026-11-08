#!/usr/bin/env python3
"""Read a project's hal_project_config.h the way the C preprocessor does.

The header is the single description of a project's build configuration:
HAL features, values the build reads, and named build variants declared with
``JH_PROJECT_VARIANTS(X)``. This adapter serves the remaining Python tools;
CMake reads the same configuration directly with ``jh config cmake``.

The evaluators are ``jh config inspect`` and ``jh config build``: this module
turns their results into the objects below; tools/jh/README.md describes the
supported directives, the int64_t arithmetic with range checks and the limits.
A condition may depend only on what the build passes to the compiler before
the header (target selector, extra and variant definitions) and on the
header's own macros; a macro the build reads that depends on anything else is
reported by ``uncertain``.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from typing import Any, Iterable, Mapping, Sequence

HEADER_NAME = "hal_project_config.h"
VARIANTS_MACRO = "JH_PROJECT_VARIANTS"
TARGETS_MACRO = "JH_PROJECT_TARGETS"
TARGET_SELECTORS = (
    "HAL_TARGET_RP2040",
    "HAL_TARGET_RP2350_ARM",
    "HAL_TARGET_RP2350_RISCV",
    "HAL_TARGET_STM32G474",
    "HAL_TARGET_ESP32",
    "HAL_TARGET_ESP32_S3",
    "HAL_TARGET_MOCK",
)
FEATURE_PATTERN = re.compile(r"HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+")


class ProjectConfigError(ValueError):
    """Header the reader cannot evaluate, with a [JH-CFG-*] diagnostic."""


class ConfigurationRejected(ProjectConfigError):
    """The header stops this build on ``#error``."""


@dataclass(frozen=True)
class Macro:
    name: str
    body: str
    parameters: tuple[str, ...] | None
    source: str

    @property
    def function_like(self) -> bool:
        return self.parameters is not None


@dataclass(frozen=True)
class ProjectVariant:
    id: str
    description: str
    definitions: tuple[str, ...]
    source: str


@dataclass(frozen=True)
class Directive:
    """One ``#define`` or ``#undef`` of a file the reader read, in any branch."""

    path: Path
    line: int
    kind: str
    name: str
    text: str


@dataclass(frozen=True)
class MacroValue:
    """What the build reads of one macro, as jh computed it."""

    state: str
    integer: int | None = None
    unresolved: frozenset[str] = frozenset()
    error: str = ""
    status: str = ""


@dataclass
class ProjectConfig:
    """Result of evaluating the header for one set of predefined macros."""

    macros: dict[str, Macro]
    variants: tuple[ProjectVariant, ...]
    files: tuple[Path, ...]
    error: tuple[str, str] | None = None
    dependencies: dict[str, frozenset[str]] = field(default_factory=dict)
    targets: tuple[str, ...] = ()
    values: dict[str, MacroValue] = field(default_factory=dict)
    directives: tuple[Directive, ...] = ()

    def defined(self, name: str) -> bool:
        return name in self.macros

    def value(self, name: str) -> str | None:
        macro = self.macros.get(name)
        return None if macro is None else macro.body

    def _computed(self, name: str) -> MacroValue:
        value = self.values.get(name, MacroValue("none"))
        if value.state == "error":
            # An invalid literal or a jh limit, as int() of Python fails.
            raise ValueError(value.error)
        return value

    def integer(self, name: str) -> int | None:
        """Integer value of an object-like macro, or None when it has none, uses
        a name this configuration does not define or leaves the int64_t range."""
        return self._computed(name).integer

    def out_of_range(self, name: str) -> bool:
        """The value of ``name`` is an integer outside int64_t."""
        return self._computed(name).state == "outOfRange"

    def limit_error(self, name: str) -> str:
        """The jh diagnostic when computing the value of ``name`` exceeds a
        limit of jh, otherwise empty; integer() and unresolved() then raise."""
        value = self.values.get(name, MacroValue("none"))
        return value.error if value.state == "error" and value.status == "HAL_EOVERFLOW" else ""

    def uncertain(self, name: str) -> frozenset[str]:
        """Identifiers the reader cannot see that decide whether or how
        ``name`` is defined; empty when the result is exact."""
        return self.dependencies.get(name, frozenset())

    def unresolved(self, name: str) -> frozenset[str]:
        """Names the value of ``name`` uses that this configuration cannot
        resolve: names it does not define (SDK macros, C symbols), function-like
        macros, and the unknown names deciding a macro the value uses. The
        compiler computes such a value later, so the reader has none."""
        return self._computed(name).unresolved

    def features(self) -> list[Macro]:
        """HAL feature definitions made by the header itself."""
        return [
            macro
            for name, macro in self.macros.items()
            if FEATURE_PATTERN.fullmatch(name) and macro.source != "predefined"
        ]

    def file_directives(self, path: Path) -> list[Directive]:
        """``#define``/``#undef`` of one file read, active or not."""
        path = path.resolve()
        return [item for item in self.directives if item.path == path]


def _config_from(result: dict[str, Any]) -> ProjectConfig:
    macros = {
        item["name"]: Macro(
            item["name"], item["body"],
            tuple(item["parameters"]) if item["functionLike"] else None, item["source"],
        )
        for item in result["macros"]
    }
    values = {
        item["name"]: MacroValue(
            item["integerState"], item.get("integer"), frozenset(item["unresolved"]),
            item.get("integerError", {}).get("message", ""),
            item.get("integerError", {}).get("status", ""),
        )
        for item in result["macros"]
    }
    error = result["error"]
    return ProjectConfig(
        macros,
        tuple(
            ProjectVariant(item["id"], item["description"], tuple(item["definitions"]), item["source"])
            for item in result["variants"]
        ),
        tuple(Path(item) for item in result["files"]),
        None if error is None else (error["message"], error["source"]),
        {item["name"]: frozenset(item["unknown"]) for item in result["dependencies"]},
        tuple(result["targets"]),
        values,
        _directives(result),
    )


def run_jh(
    arguments: list[str], error_type: type[Exception] = ProjectConfigError
) -> tuple[int, dict[str, Any]]:
    """The exit code and JSON result of one jh command; error_type when it
    gives no result."""
    result = subprocess.run([jh_executable(), *arguments, "--json"], capture_output=True, check=False)
    try:
        output = json.loads(result.stdout.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise error_type(
            f"[JH-CFG-SCOPE] jh {' '.join(arguments[:2])} gave no result (exit {result.returncode}): "
            f"{result.stderr.decode('utf-8', 'replace').strip()}"
        ) from error
    return result.returncode, output


def _run_jh(arguments: list[str]) -> dict[str, Any]:
    """The JSON result of one jh command; a rejection raises like the reader."""
    returncode, output = run_jh(arguments)
    if returncode != 0:
        if output.get("status") == "HAL_EPROTO":
            raise ValueError(output.get("message", "invalid input"))
        raise ProjectConfigError(output.get("message", f"jh exit {returncode}"))
    return output


def _directives(result: dict[str, Any]) -> tuple[Directive, ...]:
    return tuple(
        Directive(Path(item["file"]), item["line"], item["directive"], item["name"], item["text"])
        for item in result["directives"]
    )


def file_directives(path: Path) -> list[Directive]:
    """``#define``/``#undef`` of one file, active or not, without evaluating
    it, so also of a header the reader rejects; empty for a missing file."""
    return list(_directives(_run_jh(["config", "directives", "--file", str(path)])))


def read_project_config(
    header: Path | None, predefined: Iterable[str] = (), preinclude: Path | None = None
) -> ProjectConfig:
    """Evaluate ``header`` with the given -D definitions (``NAME`` or
    ``NAME=VALUE``). ``preinclude`` is read first, as the hook loads the
    generated jh_hardware.h before the project header. A missing header is an
    empty configuration. A header the reader rejects raises
    ProjectConfigError with its [JH-CFG-*] diagnostic; invalid UTF-8 or an
    invalid literal raises ValueError."""
    arguments = ["config", "inspect"]
    if header is not None:
        arguments += ["--header", str(header)]
    arguments += [f"--define={item}" for item in predefined]
    if preinclude is not None:
        arguments += ["--preinclude", str(preinclude)]
    return _config_from(_run_jh(arguments))


# ── Project builds ───────────────────────────────────────────────────────────

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


def _same_checkout(root: str) -> bool:
    try:
        return Path(root).resolve() == REPOSITORY_ROOT.resolve()
    except OSError:
        return False


def jh_executable(cmake: str = "cmake", environment: Mapping[str, str] | None = None) -> str:
    """The jh host CLI of this checkout: JH_EXECUTABLE of the environment when
    it names a file and JH_EXECUTABLE_ROOT, if set, is this checkout;
    otherwise what scripts/jh_bootstrap.cmake publishes, building the tool
    when its inputs changed. The result goes into the environment of this
    process with JH_EXECUTABLE_ROOT, so the processes it starts for this
    checkout ask the bootstrap no more, while a copy of the checkout prepares
    its own. Firmware configure only verifies the tool, so the entries that
    run CMake prepare it with this."""
    environment = os.environ if environment is None else environment
    given = environment.get("JH_EXECUTABLE", "")
    root = environment.get("JH_EXECUTABLE_ROOT", "")
    if given and Path(given).is_file() and (not root or _same_checkout(root)):
        return given
    with tempfile.TemporaryDirectory(prefix="jh-bootstrap-") as directory:
        result_file = Path(directory) / "result.txt"
        try:
            result = subprocess.run(
                [cmake, f"-DJH_ROOT={REPOSITORY_ROOT}", f"-DJH_RESULT_FILE={result_file}",
                 "-P", str(REPOSITORY_ROOT / "scripts" / "jh_bootstrap.cmake")],
                env=dict(environment), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", check=False,
            )
        except OSError as error:
            raise ProjectConfigError(
                f"[JH-CFG-SCOPE] cannot run {cmake} to prepare the jh host CLI: {error}"
            ) from error
        if result.returncode != 0 or not result_file.is_file():
            raise ProjectConfigError(
                f"[JH-CFG-SCOPE] cannot prepare the jh host CLI:\n{result.stdout.strip()}"
            )
        executable = result_file.read_text(encoding="utf-8").splitlines()[0]
    os.environ["JH_EXECUTABLE"] = executable
    os.environ["JH_EXECUTABLE_ROOT"] = str(REPOSITORY_ROOT)
    return executable


@dataclass(frozen=True)
class TargetFacts:
    id: str
    selector: str
    required_features: tuple[str, ...]


def load_targets(repo_root: Path = REPOSITORY_ROOT) -> dict[str, TargetFacts]:
    """Target selectors and required features from the board registry."""
    from generate_board_config import load_registry

    targets, _, _ = load_registry(repo_root / "boards")
    return {
        target_id: TargetFacts(
            target_id,
            target["hal"]["targetSelector"],
            tuple(target.get("requiredFeatures") or ()),
        )
        for target_id, target in targets.items()
    }


@dataclass(frozen=True)
class BuildConfig:
    """One project configuration: the header evaluated for a target and an
    optional variant, as the compiler sees it."""

    header: Path | None
    target: TargetFacts
    variant: ProjectVariant | None
    extra_definitions: tuple[str, ...]
    config: ProjectConfig
    # From ``jh config build``: each effective definition with its source,
    # the requested feature names and the feature requests (symbol, value or
    # None, source). evaluate_build() supplies them; a BuildConfig cannot be
    # built without them.
    sources: tuple[tuple[str, str], ...]
    requested: tuple[str, ...]
    requests: tuple[tuple[str, str | None, str], ...]
    # The assembly of a device tree build, whose jh_hardware.h the header
    # was read with.
    assembly: str | None = None

    @property
    def definitions(self) -> tuple[str, ...]:
        """-D values the build passes before the header: extra definitions,
        then the variant's."""
        return (*self.extra_definitions, *(self.variant.definitions if self.variant else ()))

    def definition_sources(self) -> list[tuple[str, str]]:
        """Each definition with the place that asked for it. A feature
        definition the header removes with #undef is left out: the compiler
        sees the header's final state."""
        return list(self.sources)

    def requested_features(self) -> list[str]:
        """Features the project asks for: the header's own and the ones the
        variant or extra definitions name. Target-required features are added
        by the feature resolver."""
        return list(self.requested)

    def label(self) -> str:
        target = self.target.id or "default"
        assembly = f", assembly={self.assembly}" if self.assembly else ""
        return f"target={target}{assembly}, variant={self.variant.id if self.variant else 'base'}"


def project_target_ids(config: ProjectConfig, targets: dict[str, TargetFacts]) -> list[str]:
    """Registry IDs of the targets the header declares."""
    by_selector = {facts.selector: target_id for target_id, facts in targets.items()}
    return [by_selector[selector] for selector in config.targets if selector in by_selector]


def target_declarations(header: Path) -> list[int]:
    """Lines defining JH_PROJECT_TARGETS, active or not; a device tree takes
    the targets from its assemblies, so any definition is rejected there."""
    return [item.line for item in file_directives(header)
            if item.kind == "define" and item.name == TARGETS_MACRO]


def evaluate_build(
    config_dir: Path | None,
    target: TargetFacts,
    variant_id: str | None = None,
    extra_definitions: Sequence[str] = (),
    hardware_header: Path | None = None,
) -> BuildConfig:
    """Evaluate ``config_dir/hal_project_config.h`` for one build.

    ``extra_definitions`` are -D values a static library build passes; a
    project build passes none. Without ``config_dir`` (a library build with
    no project header) only the definitions count. Raises when the target is
    not declared, the variant is unknown or the header stops on ``#error``.
    """
    arguments = ["config", "build", f"--target={target.id}", f"--selector={target.selector}"]
    if config_dir is not None:
        arguments += ["--header", str(config_dir / HEADER_NAME)]
    if variant_id:
        arguments += ["--variant", variant_id]
    if hardware_header is not None:
        arguments += ["--hardware-header", str(hardware_header)]
    arguments += [f"--required-feature={item}" for item in target.required_features]
    arguments += [f"--define={item}" for item in extra_definitions]
    returncode, result = run_jh(arguments)
    if returncode != 0:
        message = result.get("message", f"jh exit {returncode}")
        if result.get("configurationRejected"):
            raise ConfigurationRejected(message)
        if result.get("status") == "HAL_EPROTO":
            raise ValueError(message)
        raise ProjectConfigError(message)
    selected = result["variant"]
    variant = None if selected is None else ProjectVariant(
        selected["id"], selected["description"], tuple(selected["definitions"]), selected["source"]
    )
    return BuildConfig(
        None if config_dir is None else config_dir / HEADER_NAME,
        target, variant, tuple(extra_definitions), _config_from(result["config"]),
        sources=tuple((item["definition"], item["source"]) for item in result["definitionSources"]),
        requested=tuple(result["requestedFeatures"]),
        requests=tuple((item["symbol"], item["value"], item["source"])
                       for item in result["featureRequests"]),
    )


def available_builds(
    config_dir: Path, targets: dict[str, TargetFacts], candidate_targets: Iterable[str]
) -> list[BuildConfig]:
    """Every build the project configures: the declared targets (or the given
    candidates when the header declares none; "" builds with no target) times
    the base and each variant, without the builds that stop on ``#error``.
    With device_tree.json the assemblies replace the targets."""
    import device_tree_project

    if device_tree_project.device_tree_path(config_dir) is not None:
        return device_tree_project.builds(config_dir, targets)
    declarations = read_project_config(config_dir / HEADER_NAME)
    target_ids = project_target_ids(declarations, targets) or list(candidate_targets)
    builds: list[BuildConfig] = []
    for target_id in target_ids:
        if target_id and target_id not in targets:
            raise ProjectConfigError(f"[JH-CFG-TARGET] unknown target {target_id!r}")
        facts = targets[target_id] if target_id else TargetFacts("", "", ())
        for variant_id in [None, *(item.id for item in declarations.variants)]:
            try:
                builds.append(evaluate_build(config_dir, facts, variant_id))
            except ConfigurationRejected:
                continue
    return builds


def resolve_build_features(build: BuildConfig, model: Any) -> tuple[Any, list[str]]:
    """Resolve the features one build requests against the feature registry
    ``model``: the header's own, the ones its definitions name and the
    target's required ones. Returns the resolution and its findings."""
    import generate_hal_features

    requests = [
        generate_hal_features.FeatureRequest(*request) for request in build.requests
    ]
    return generate_hal_features.resolve_target_feature_requests(
        requests,
        model,
        f"{build.header or HEADER_NAME} [{build.label()}]",
        build.target.id or None,
        {"requiredFeatures": list(build.target.required_features)},
    )


def main(argv: Sequence[str] | None = None) -> int:
    import sys
    arguments = sys.argv[1:] if argv is None else list(argv)
    return subprocess.run(
        [jh_executable(), "config", *arguments, "--root", str(REPOSITORY_ROOT)],
        check=False,
    ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
