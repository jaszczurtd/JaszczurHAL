#!/usr/bin/env python3
"""The HAL feature registry for its Python importers, read from `jh feature
list` and resolved by `jh feature resolve`; --lint runs `jh feature lint`.
`jh generate features` writes and checks the
generated artifacts."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import sys
import tempfile
from typing import Any, Iterable

import project_config


class RegistryError(ValueError):
    """Feature registry validation failure with actionable context."""


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


def lint_inputs(
    roots: list[Path],
    config_root: str,
    report_only: bool,
    include_hardware: bool = False,
) -> dict[str, Any]:
    """`jh feature lint` of the roots: the inputs it read and its findings.
    Device-test firmware (tests/hardware) is left out unless
    include_hardware, for the opt-in hardware-fixture checks (AGENTS.md)."""
    arguments = [
        "feature",
        "lint",
        f"--config-root={config_root}",
        *(f"--input-root={root}" for root in roots),
    ]
    if report_only:
        arguments.append("--report-only")
    if include_hardware:
        arguments.append("--include-hardware-fixtures")
    return _jh_result(arguments, (0, 1))


def report(findings: list[str], count: int, noun: str, report_only: bool) -> bool:
    """Print findings the way `jh feature lint` does; whether the lint passes."""
    label = "warning" if report_only else "error"
    for finding in findings:
        print(f"{label}: {finding}", file=sys.stderr)
    if findings:
        print(f"linted {count} {noun}: {len(findings)} finding(s)", file=sys.stderr)
    else:
        print(f"linted {count} {noun}: no findings")
    return report_only or not findings


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


def effective_lint(
    roots: list[Path], config_root: str, report_only: bool,
    resolution_output: Path | None, include_hardware: bool,
) -> dict[str, Any]:
    arguments = [
        "feature", "lint", "--effective",
        f"--root={project_config.REPOSITORY_ROOT}",
        f"--config-root={config_root}",
        *(f"--input-root={root}" for root in roots),
    ]
    if report_only:
        arguments.append("--report-only")
    if resolution_output is not None:
        arguments.append(f"--resolution-output={resolution_output}")
    if include_hardware:
        arguments.append("--include-hardware-fixtures")
    return _jh_result(arguments, (0, 1))


def report_effective(result: dict[str, Any], output: Path | None, report_only: bool) -> bool:
    count = len(result["effectiveReport"]["configurations"])
    if output is not None:
        print(f"wrote effective feature resolution for {count} configurations to {output.resolve()}")
    return report(result["effectiveFindings"], count,
                  "effective feature configurations", report_only)


def lint_effective_inputs(
    roots: list[Path], model: FeatureModel, report_only: bool,
    resolution_output: Path | None, include_hardware: bool = False,
    inputs: list[Path] | None = None,
) -> bool:
    """Compatibility entry for importers; the native batch owns the matrix."""
    result = effective_lint(roots, model.config_root, report_only,
                            resolution_output, include_hardware)
    return report_effective(result, resolution_output, report_only)


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
        config_root = args.config_root.resolve()
        input_roots = args.input_root or [Path(__file__).resolve().parents[1]]
        if args.effective:
            raw = effective_lint(input_roots, str(config_root), args.report_only,
                                 args.resolution_output, args.include_hardware_fixtures)
        else:
            raw = lint_inputs(input_roots, str(config_root), args.report_only,
                              args.include_hardware_fixtures)
        raw_valid = report(
            raw["findings"], len(raw["inputs"]), "feature inputs", args.report_only
        )
        effective_valid = True
        if args.effective:
            effective_valid = report_effective(raw, args.resolution_output, args.report_only)
        return 0 if raw_valid and effective_valid else 1
    except RegistryError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
