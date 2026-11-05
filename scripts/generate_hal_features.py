#!/usr/bin/env python3
"""The HAL feature registry for its Python importers, read from `jh feature
list` and resolved by `jh feature resolve`, and the lint of feature requests;
`jh generate features` writes and checks the generated artifacts."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from functools import partial
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from typing import Any, Iterable

from codegen_support import atomic_write_text, load_json_object
import device_tree_project
import project_config


SCHEMA_VERSION = 1
SYMBOL_PATTERN = re.compile(r"^HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+$")
HEADER_FEATURE_PREFIX = re.compile(r"HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]")
IGNORED_INPUT_PARTS = {".build", ".git", "third_party"}
# Device-test firmware is never read by the default checks (AGENTS.md); the
# opt-in hardware-fixture checks lint it with --include-hardware-fixtures.
HARDWARE_FIXTURE_DIR = ("tests", "hardware")


class RegistryError(ValueError):
    """Feature registry validation failure with actionable context."""


load_json = partial(load_json_object, error_type=RegistryError)
atomic_write = partial(
    atomic_write_text, error_type=RegistryError, mode=0o644
)


@dataclass(frozen=True)
class Feature:
    """Normalized feature record."""

    name: str
    domain: str
    kind: str
    implies: tuple[str, ...]
    requires: tuple[str, ...]
    conflicts: tuple[str, ...]
    feature_sources: tuple[str, ...]
    portable_sources: tuple[str, ...]
    build_dependencies: tuple[str, ...]
    note: str | None


@dataclass(frozen=True)
class FeatureModel:
    """Validated feature registry of one configuration root."""

    features: dict[str, Feature]
    digest: str
    schema_digest: str
    config_root: str = field(compare=False, repr=False)
    closures: dict[str, tuple[str, ...]] = field(compare=False, repr=False)
    # Resolutions jh gave for this registry, by their arguments.
    resolutions: dict[Any, Any] = field(
        default_factory=dict, compare=False, repr=False
    )

    def closure(self, name: str) -> tuple[str, ...]:
        return self.closures[name]

    def resolve_many(self, requested: Iterable[str]) -> tuple[str, ...]:
        resolved = set(requested)
        for name in list(resolved):
            resolved.update(self.closures[name])
        return tuple(sorted(resolved))

    def resolve_build_effects(
        self, resolved: Iterable[str]
    ) -> tuple[tuple[str, ...], tuple[str, ...], tuple[str, ...]]:
        feature_sources: list[str] = []
        portable_sources: list[str] = []
        dependencies: list[str] = []
        for name in sorted(set(resolved)):
            feature = self.features[name]
            feature_sources.extend(feature.feature_sources)
            portable_sources.extend(feature.portable_sources)
            dependencies.extend(feature.build_dependencies)
        return (
            tuple(sorted(set(feature_sources))),
            tuple(sorted(set(portable_sources))),
            tuple(sorted(set(dependencies))),
        )


@dataclass(frozen=True)
class FeatureRequest:
    """One direct feature request and its effective source."""

    symbol: str
    value: str | None
    source: str


@dataclass(frozen=True)
class FeatureResolution:
    """Deterministic result for one effective project configuration."""

    requested: tuple[str, ...]
    resolved: tuple[str, ...]
    provenance: dict[str, tuple[str, ...]]
    digest: str


def _jh_result(arguments: list[str], accepted: tuple[int, ...] = (0,)) -> dict[str, Any]:
    """The JSON result of one jh feature command; RegistryError with the jh
    message for any other exit code."""
    returncode, output = project_config.run_jh(arguments, RegistryError)
    if returncode not in accepted:
        raise RegistryError(output.get("message", f"jh exit {returncode}"))
    return output


def _registry_state(config_root: Path) -> tuple[Any, ...] | None:
    """Names, sizes and modification times of the registry files; None when
    they cannot be listed, and then no result is kept."""
    try:
        schema = (config_root / "features.schema.json").stat()
        with os.scandir(config_root / "features") as entries:
            fragments = sorted(
                (entry.name, entry.stat().st_size, entry.stat().st_mtime_ns)
                for entry in entries
            )
    except OSError:
        return None
    return (schema.st_size, schema.st_mtime_ns, tuple(fragments))


_MODELS: dict[str, tuple[tuple[Any, ...], FeatureModel]] = {}


def load_registry(config_root: Path) -> FeatureModel:
    """The registry below config_root as `jh feature list` validates it, kept
    for this process until its files change; RegistryError when jh rejects
    it."""
    root = Path(config_root)
    key = str(root.absolute())
    state = _registry_state(root)
    cached = _MODELS.get(key)
    if cached is not None and state is not None and cached[0] == state:
        return cached[1]
    output = _jh_result(["feature", "list", f"--config-root={root}"])
    model = FeatureModel(
        features={
            item["name"]: Feature(
                name=item["name"],
                domain=item["domain"],
                kind=item["kind"],
                implies=tuple(item["implies"]),
                requires=tuple(item["requires"]),
                conflicts=tuple(item["conflicts"]),
                feature_sources=tuple(item["featureSources"]),
                portable_sources=tuple(item["portableSources"]),
                build_dependencies=tuple(item["dependencies"]),
                note=item["note"],
            )
            for item in output["features"]
        },
        digest=output["registryDigest"],
        schema_digest=output["registrySchemaDigest"],
        config_root=key,
        closures={item["name"]: tuple(item["closure"]) for item in output["features"]},
    )
    if state is not None:
        _MODELS[key] = (state, model)
    return model


def is_hardware_fixture_dir(path: Path) -> bool:
    return tuple(path.parts[-2:]) == HARDWARE_FIXTURE_DIR


def iter_lint_inputs(root: Path, include_hardware: bool = False) -> list[Path]:
    if root.is_file():
        return [root]
    if not root.is_dir():
        raise RegistryError(f"{root}: lint input root does not exist")
    inputs: list[Path] = []
    for directory, directory_names, file_names in os.walk(root):
        directory_path = Path(directory)
        directory_names[:] = sorted(
            name
            for name in directory_names
            if name not in IGNORED_INPUT_PARTS
            and not name.startswith("build")
            and (include_hardware
                 or not is_hardware_fixture_dir(directory_path / name))
        )
        for name in sorted(file_names):
            if name in {"hal_project_config.h", "jaszczurhal.project.json"}:
                inputs.append(directory_path / name)
    return sorted(inputs)


def validate_requested_symbol(
    path: Path,
    location: str,
    symbol: str,
    value: str | None,
    model: FeatureModel,
) -> list[str]:
    findings: list[str] = []
    prefix = f"{path}:{location}"
    if symbol not in model.features:
        findings.append(f"{prefix}: [JH-CFG-UNKNOWN] unknown feature {symbol}")
        return findings
    if value not in {None, "1"}:
        if value == "0":
            findings.append(
                f"{prefix}: [JH-CFG-VALUE] {symbol}=0 is unsupported; "
                "omit the symbol to disable it"
            )
        else:
            findings.append(
                f"{prefix}: [JH-CFG-VALUE] {symbol} has unsupported value {value!r}"
            )
    if model.features[symbol].kind == "derived":
        findings.append(
            f"{prefix}: [JH-CFG-DERIVED] {symbol} cannot be requested directly"
        )
    return findings


def lint_header(path: Path, model: FeatureModel) -> list[str]:
    """Check every feature the header can request, in any branch and in any
    variant, and that the project configuration reader accepts the header."""
    try:
        config = project_config.read_project_config(path)
    except project_config.ProjectConfigError as error:
        return [str(error)]
    findings: list[str] = []
    # Every #define of a HAL feature in the header, in any branch.
    for item in config.file_directives(path):
        if item.kind != "define" or not HEADER_FEATURE_PREFIX.match(item.name):
            continue
        findings.extend(
            validate_requested_symbol(
                path,
                str(item.line),
                item.name,
                item.text if item.text else None,
                model,
            )
        )
    for variant in config.variants:
        for definition in variant.definitions:
            symbol, separator, value = definition.partition("=")
            if SYMBOL_PATTERN.fullmatch(symbol):
                findings.extend(
                    validate_requested_symbol(
                        path,
                        f"{project_config.VARIANTS_MACRO}({variant.id})",
                        symbol,
                        value if separator else None,
                        model,
                    )
                )
    return findings


def lint_manifest(path: Path, model: FeatureModel) -> list[str]:
    """A manifest holds no project configuration; report what it carries."""
    del model
    return load_workflow_runtime().manifest_configuration_findings(
        load_json(path), str(path)
    )


def lint_inputs(
    roots: list[Path],
    model: FeatureModel,
    report_only: bool,
    include_hardware: bool = False,
) -> bool:
    files: set[Path] = set()
    for root in roots:
        files.update(iter_lint_inputs(root.resolve(), include_hardware))
    findings: list[str] = []
    for path in sorted(files):
        if path.name == "hal_project_config.h":
            findings.extend(lint_header(path, model))
        else:
            findings.extend(lint_manifest(path, model))
    for finding in sorted(findings):
        label = "warning" if report_only else "error"
        print(f"{label}: {finding}", file=sys.stderr)
    if findings:
        print(
            f"linted {len(files)} feature inputs: {len(findings)} finding(s)",
            file=sys.stderr,
        )
    else:
        print(f"linted {len(files)} feature inputs: no findings")
    return report_only or not findings


def effective_inputs(
    roots: list[Path], include_hardware: bool = False
) -> tuple[list[Path], list[Path]]:
    files: set[Path] = set()
    for root in roots:
        files.update(iter_lint_inputs(root.resolve(), include_hardware))

    projects: set[Path] = set()
    for path in files:
        if (
            path.name == "jaszczurhal.project.json"
            and path.parent.name == ".vscode"
        ):
            projects.add(path.parent.parent)

    standalone_headers = {
        path
        for path in files
        if path.name == "hal_project_config.h" and path.parent not in projects
    }
    return sorted(projects), sorted(standalone_headers)


def relative_display(path: Path, roots: list[Path]) -> str:
    candidates: list[tuple[int, str]] = []
    for root in roots:
        resolved = root.resolve()
        base = resolved if resolved.is_dir() else resolved.parent
        try:
            relative = path.resolve().relative_to(base)
        except ValueError:
            continue
        display = relative.as_posix() or "."
        candidates.append((len(relative.parts), display))
    if candidates:
        return min(candidates)[1]
    return path.resolve().as_posix()


def load_workflow_runtime() -> Any:
    repository_root = Path(__file__).resolve().parents[1]
    if str(repository_root) not in sys.path:
        sys.path.insert(0, str(repository_root))
    from vscode.runtime import jh_vscode

    return jh_vscode


def display_source(source: str, roots: list[Path]) -> str:
    """Show a request source inside the linted roots relative to them."""
    location, separator, detail = source.rpartition(":")
    if separator and Path(location).is_absolute():
        return f"{relative_display(Path(location), roots)}:{detail}"
    return source


def _resolve(
    requests: list[FeatureRequest],
    model: FeatureModel,
    context: str,
    target: str | None,
    required: tuple[str, ...],
) -> tuple[FeatureResolution, list[str]]:
    """The resolution and findings `jh feature resolve` gives, once per
    distinct request list for a registry."""
    key = (tuple(requests), context, target, required)
    cached = model.resolutions.get(key)
    if cached is None:
        arguments = [
            "feature",
            "resolve",
            f"--config-root={model.config_root}",
            f"--context={context}",
        ]
        if target is not None:
            arguments.append(f"--target={target}")
            arguments += [f"--required-feature={item}" for item in required]
        # Every source repeats a header path: as arguments they outgrow the
        # 32767 characters of a Windows command line.
        listed = {
            "requests": [
                {
                    "request": request.symbol
                    + ("" if request.value is None else f"={request.value}"),
                    "source": request.source,
                }
                for request in requests
            ]
        }
        with tempfile.TemporaryDirectory(prefix="jh-resolve-") as directory:
            request_file = Path(directory) / "requests.json"
            request_file.write_text(json.dumps(listed), encoding="utf-8")
            output = _jh_result([*arguments, f"--file={request_file}"], (0, 1))
        cached = (
            FeatureResolution(
                tuple(output["requestedFeatures"]),
                tuple(output["resolvedFeatures"]),
                {symbol: tuple(sources) for symbol, sources in output["provenance"].items()},
                output["resolvedFeaturesDigest"],
            ),
            tuple(output["findings"]),
        )
        model.resolutions[key] = cached
    return cached[0], list(cached[1])


def resolve_feature_requests(
    requests: list[FeatureRequest],
    model: FeatureModel,
    context: str,
) -> tuple[FeatureResolution, list[str]]:
    return _resolve(requests, model, context, None, ())


def resolve_target_feature_requests(
    requests: list[FeatureRequest],
    model: FeatureModel,
    context: str,
    target: str | None,
    target_descriptor: dict[str, Any] | None,
) -> tuple[FeatureResolution, list[str]]:
    """Resolve explicit requests together with target-mandated features."""
    if target is None or target_descriptor is None:
        return _resolve(requests, model, context, None, ())
    required = tuple(
        str(item) for item in target_descriptor.get("requiredFeatures", [])
    )
    return _resolve(requests, model, context, target, required)


def effective_matrix_digest(records: list[dict[str, Any]]) -> str:
    contract = [
        {
            "project": record["project"],
            "target": record["target"],
            "board": record["board"],
            # Two assemblies of one board are two configurations.
            **({"assembly": record["assembly"]} if record.get("assembly") else {}),
            "variant": record["variant"],
            "requestedFeatures": record["requestedFeatures"],
            "resolvedFeatures": record["resolvedFeatures"],
        }
        for record in records
    ]
    serialized = json.dumps(
        contract, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    )
    return hashlib.sha256(serialized.encode("utf-8")).hexdigest()


def feature_resolution_record(
    project: str,
    target: str | None,
    board: str | None,
    variant: str | None,
    resolution: FeatureResolution,
    assembly: str | None = None,
) -> dict[str, Any]:
    """Serialize one effective feature resolution for the lint report; a
    device tree build also names its assembly."""
    return {
        "project": project,
        "target": target,
        "board": board,
        **({"assembly": assembly} if assembly else {}),
        "variant": variant,
        "requestedFeatures": list(resolution.requested),
        "resolvedFeatures": list(resolution.resolved),
        "resolvedFeaturesDigest": resolution.digest,
        "provenance": {
            symbol: list(sources)
            for symbol, sources in resolution.provenance.items()
        },
    }


def write_resolution_report(
    path: Path, model: FeatureModel, records: list[dict[str, Any]]
) -> None:
    document = {
        "schemaVersion": 1,
        "registrySchemaVersion": SCHEMA_VERSION,
        "registryDigest": model.digest,
        "registrySchemaDigest": model.schema_digest,
        "matrixDigest": effective_matrix_digest(records),
        "configurations": records,
    }
    content = json.dumps(document, indent=2, sort_keys=True) + "\n"
    atomic_write(path, content)
    print(f"wrote effective feature resolution for {len(records)} configurations to {path}")


def lint_effective_inputs(
    roots: list[Path],
    model: FeatureModel,
    report_only: bool,
    resolution_output: Path | None,
    include_hardware: bool = False,
) -> bool:
    """Resolve every build each project configures, the way jh-vscode and
    CMake do: targets and variants from hal_project_config.h, boards from the
    manifest."""
    workflow = load_workflow_runtime()
    targets = project_config.load_targets()
    findings: list[str] = []
    records: list[dict[str, Any]] = []
    projects, standalone_headers = effective_inputs(roots, include_hardware)

    def record(
        project_display: str,
        build: project_config.BuildConfig,
        board: str | None,
    ) -> None:
        resolution, resolution_findings = project_config.resolve_build_features(
            build, model
        )
        findings.extend(resolution_findings)
        resolution = FeatureResolution(
            resolution.requested,
            resolution.resolved,
            {
                symbol: tuple(display_source(source, roots) for source in sources)
                for symbol, sources in resolution.provenance.items()
            },
            resolution.digest,
        )
        records.append(
            feature_resolution_record(
                project_display,
                build.target.id or None,
                board,
                build.variant.id if build.variant else None,
                resolution,
                build.assembly,
            )
        )

    for project in projects:
        project_display = relative_display(project, roots)
        manifest_path = project / ".vscode/jaszczurhal.project.json"
        try:
            builds = workflow.project_builds(
                project, workflow.load_json_file(manifest_path)
            )
        except (OSError, ValueError) as error:
            findings.append(f"{project_display}: [JH-CFG-EFFECTIVE] {error}")
            continue
        for available in builds:
            target = available.target.id
            variant_id = available.variant.id if available.variant else None
            try:
                config = workflow.load_project_config(
                    project, target_override=target or None, use_local_state=False,
                    assembly_override=available.assembly,
                )
                workflow.apply_variant(config, variant_id)
                build = workflow.project_build(config, project)
            except (OSError, ValueError) as error:
                findings.append(
                    f"{project_display} [{available.label()}]: [JH-CFG-EFFECTIVE] {error}"
                )
                continue
            record(project_display, build, str(config["board"]) if config.get("board") else None)

    for header_path in standalone_headers:
        project = header_path.parent
        project_display = relative_display(project, roots)
        try:
            builds = project_config.available_builds(project, targets, [""])
        except ValueError as error:
            findings.append(f"{project_display}: [JH-CFG-EFFECTIVE] {error}")
            continue
        boards = (
            {item["name"]: item["board"] for item in device_tree_project.assemblies(project)}
            if device_tree_project.device_tree_path(project) is not None
            else {}
        )
        for build in builds:
            if build.requested_features():
                record(project_display, build, boards.get(build.assembly))

    records.sort(
        key=lambda item: (
            item["project"],
            item["target"] or "",
            item["board"] or "",
            item.get("assembly") or "",
            item["variant"] or "",
        )
    )
    if resolution_output is not None:
        write_resolution_report(resolution_output.resolve(), model, records)
    for finding in sorted(set(findings)):
        label = "warning" if report_only else "error"
        print(f"{label}: {finding}", file=sys.stderr)
    if findings:
        print(
            f"linted {len(records)} effective feature configurations: "
            f"{len(set(findings))} finding(s)",
            file=sys.stderr,
        )
    else:
        print(
            f"linted {len(records)} effective feature configurations: no findings"
        )
    return report_only or not findings


def parse_args() -> argparse.Namespace:
    repository_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(
        description="Lint feature requests against the HAL feature registry; "
        "jh generate features writes and checks the generated artifacts."
    )
    parser.add_argument("--lint", action="store_true", required=True)
    parser.add_argument(
        "--config-root", type=Path, default=repository_root / "config"
    )
    parser.add_argument("--input-root", type=Path, action="append", default=[])
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--effective", action="store_true")
    parser.add_argument("--resolution-output", type=Path)
    parser.add_argument(
        "--include-hardware-fixtures",
        action="store_true",
        help="also lint tests/hardware (opt-in hardware-fixture checks only)",
    )
    args = parser.parse_args()
    if args.resolution_output is not None and not args.effective:
        parser.error("--resolution-output requires --effective")
    return args


def main() -> int:
    args = parse_args()
    try:
        model = load_registry(args.config_root.resolve())
        input_roots = args.input_root or [Path(__file__).resolve().parents[1]]
        raw_valid = lint_inputs(
            input_roots, model, args.report_only, args.include_hardware_fixtures
        )
        effective_valid = True
        if args.effective:
            effective_valid = lint_effective_inputs(
                input_roots,
                model,
                args.report_only,
                args.resolution_output,
                args.include_hardware_fixtures,
            )
        return 0 if raw_valid and effective_valid else 1
    except RegistryError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
