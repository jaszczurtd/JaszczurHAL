#!/usr/bin/env python3
"""Validate the HAL feature registry and generate production C/CMake artifacts."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from functools import partial
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from typing import Any, Iterable

from codegen_support import (
    atomic_write_text,
    check_generated_outputs,
    load_json_object,
    require_exact_fields,
    validation_error,
    write_generated_outputs,
)
from repository_layout import FEATURE_CMAKE_OUTPUT, FEATURE_HEADER_OUTPUT


GENERATOR_VERSION = 2
SCHEMA_VERSION = 1
SYMBOL_PATTERN = re.compile(r"^HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+$")
DOMAIN_PATTERN = re.compile(r"^[a-z][a-z0-9-]*$")
BUILD_SOURCE_PATTERN = re.compile(
    r"^src/[A-Za-z0-9_./+-]+\.(?:c|cc|cpp|S)$"
)
MANIFEST_DEFINITION_PATTERN = re.compile(
    r"^(?:-D)?(HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)(?:=(.*))?$"
)
HEADER_DEFINITION_PATTERN = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+"
    r"(HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)(.*)$"
)
FRAGMENT_FIELDS = {"$schema", "schemaVersion", "domain", "symbols"}
SYMBOL_FIELDS = {
    "kind",
    "implies",
    "requires",
    "conflicts",
    "buildEffects",
    "note",
}
BUILD_EFFECT_FIELDS = {"featureSources", "portableSources", "dependencies"}
IGNORED_INPUT_PARTS = {".build", ".git", "third_party"}


class RegistryError(ValueError):
    """Feature registry validation failure with actionable context."""


fail = partial(validation_error, RegistryError)
load_json = partial(load_json_object, error_type=RegistryError)
exact_fields = partial(require_exact_fields, error_type=RegistryError)
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
    """Validated feature registry."""

    features: dict[str, Feature]
    digest: str
    schema_digest: str

    def closure(self, name: str) -> tuple[str, ...]:
        resolved: set[str] = set()
        pending = list(self.features[name].implies)
        while pending:
            dependency = pending.pop()
            if dependency in resolved:
                continue
            resolved.add(dependency)
            pending.extend(self.features[dependency].implies)
        return tuple(sorted(resolved))

    def resolve_many(self, requested: Iterable[str]) -> tuple[str, ...]:
        resolved = set(requested)
        pending = list(resolved)
        while pending:
            current = pending.pop()
            for dependency in self.features[current].implies:
                if dependency in resolved:
                    continue
                resolved.add(dependency)
                pending.append(dependency)
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


def normalize_relation(
    path: Path, json_path: str, value: Any
) -> tuple[str, ...]:
    if not isinstance(value, list):
        fail(path, json_path, value, "an array of feature symbols")
    normalized: list[str] = []
    for index, symbol in enumerate(value):
        if not isinstance(symbol, str) or not SYMBOL_PATTERN.fullmatch(symbol):
            fail(
                path,
                f"{json_path}[{index}]",
                symbol,
                "a HAL_ENABLE_* or HAL_DISABLE_* symbol",
            )
        normalized.append(symbol)
    if len(set(normalized)) != len(normalized):
        fail(path, json_path, value, "unique feature symbols")
    return tuple(sorted(normalized))


def normalize_build_sources(
    path: Path, json_path: str, value: Any
) -> tuple[str, ...]:
    if not isinstance(value, list):
        fail(path, json_path, value, "an array of repository source paths")
    normalized: list[str] = []
    for index, source in enumerate(value):
        if not isinstance(source, str) or not BUILD_SOURCE_PATTERN.fullmatch(source):
            fail(
                path,
                f"{json_path}[{index}]",
                source,
                "a source path below src/",
            )
        normalized.append(source)
    if len(set(normalized)) != len(normalized):
        fail(path, json_path, value, "unique repository source paths")
    return tuple(sorted(normalized))


def normalize_build_dependencies(
    path: Path, json_path: str, value: Any, allowed: frozenset[str]
) -> tuple[str, ...]:
    if not isinstance(value, list):
        fail(path, json_path, value, "an array of managed dependency names")
    normalized: list[str] = []
    for index, dependency in enumerate(value):
        if not isinstance(dependency, str) or dependency not in allowed:
            fail(
                path,
                f"{json_path}[{index}]",
                dependency,
                "one of " + ", ".join(sorted(allowed)),
            )
        normalized.append(dependency)
    if len(set(normalized)) != len(normalized):
        fail(path, json_path, value, "unique managed dependency names")
    return tuple(sorted(normalized))


def validate_fragment(
    path: Path,
    document: dict[str, Any],
    managed_dependencies: frozenset[str],
) -> dict[str, Feature]:
    exact_fields(path, "$", document, FRAGMENT_FIELDS, FRAGMENT_FIELDS)
    if document["$schema"] != "../features.schema.json":
        fail(path, "$.$schema", document["$schema"], "'../features.schema.json'")
    schema_version = document["schemaVersion"]
    if type(schema_version) is not int or schema_version != SCHEMA_VERSION:
        fail(path, "$.schemaVersion", schema_version, "the integer 1")
    domain = document["domain"]
    if not isinstance(domain, str) or not DOMAIN_PATTERN.fullmatch(domain):
        fail(path, "$.domain", domain, "a lower-case domain identifier")
    if path.stem != domain:
        fail(path, "$.domain", domain, f"the filename stem {path.stem!r}")
    symbols = document["symbols"]
    if not isinstance(symbols, dict) or not symbols:
        fail(path, "$.symbols", symbols, "a non-empty symbol object")

    result: dict[str, Feature] = {}
    for name, raw_record in symbols.items():
        symbol_path = f"$.symbols.{name}"
        if not isinstance(name, str) or not SYMBOL_PATTERN.fullmatch(name):
            fail(path, symbol_path, name, "a HAL_ENABLE_* or HAL_DISABLE_* symbol")
        record = exact_fields(path, symbol_path, raw_record, set(), SYMBOL_FIELDS)
        kind = record.get("kind", "feature")
        if kind not in {"feature", "derived"}:
            fail(path, f"{symbol_path}.kind", kind, "'feature' or 'derived'")
        note = record.get("note")
        if note is not None and (not isinstance(note, str) or not note.strip()):
            fail(path, f"{symbol_path}.note", note, "a non-empty string")
        raw_build_effects = record.get("buildEffects", {})
        if not isinstance(raw_build_effects, dict):
            fail(
                path,
                f"{symbol_path}.buildEffects",
                raw_build_effects,
                "an object",
            )
        build_effects = exact_fields(
            path,
            f"{symbol_path}.buildEffects",
            raw_build_effects,
            set(),
            BUILD_EFFECT_FIELDS,
        )
        feature_sources = normalize_build_sources(
            path,
            f"{symbol_path}.buildEffects.featureSources",
            build_effects.get("featureSources", []),
        )
        portable_sources = normalize_build_sources(
            path,
            f"{symbol_path}.buildEffects.portableSources",
            build_effects.get("portableSources", []),
        )
        overlap = set(feature_sources) & set(portable_sources)
        if overlap:
            fail(
                path,
                f"{symbol_path}.buildEffects",
                sorted(overlap),
                "disjoint featureSources and portableSources",
            )
        build_dependencies = normalize_build_dependencies(
            path,
            f"{symbol_path}.buildEffects.dependencies",
            build_effects.get("dependencies", []),
            managed_dependencies,
        )
        result[name] = Feature(
            name=name,
            domain=domain,
            kind=kind,
            implies=normalize_relation(
                path, f"{symbol_path}.implies", record.get("implies", [])
            ),
            requires=normalize_relation(
                path, f"{symbol_path}.requires", record.get("requires", [])
            ),
            conflicts=normalize_relation(
                path, f"{symbol_path}.conflicts", record.get("conflicts", [])
            ),
            feature_sources=feature_sources,
            portable_sources=portable_sources,
            build_dependencies=build_dependencies,
            note=note.strip() if note is not None else None,
        )
    return result


def detect_implies_cycles(features: dict[str, Feature]) -> None:
    visiting: list[str] = []
    visited: set[str] = set()

    def visit(name: str) -> None:
        if name in visiting:
            start = visiting.index(name)
            cycle = visiting[start:] + [name]
            raise RegistryError(
                "feature registry: implies cycle: " + " -> ".join(cycle)
            )
        if name in visited:
            return
        visiting.append(name)
        for dependency in features[name].implies:
            visit(dependency)
        visiting.pop()
        visited.add(name)

    for name in sorted(features):
        visit(name)


def normalized_digest(features: dict[str, Feature]) -> str:
    document = {
        "schemaVersion": SCHEMA_VERSION,
        "symbols": {
            name: {
                "domain": feature.domain,
                "kind": feature.kind,
                "implies": list(feature.implies),
                "requires": list(feature.requires),
                "conflicts": list(feature.conflicts),
                "buildEffects": {
                    "featureSources": list(feature.feature_sources),
                    "portableSources": list(feature.portable_sources),
                    "dependencies": list(feature.build_dependencies),
                },
                "note": feature.note,
            }
            for name, feature in sorted(features.items())
        },
    }
    serialized = json.dumps(
        document, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    )
    return hashlib.sha256(serialized.encode("utf-8")).hexdigest()


def json_digest(value: Any) -> str:
    serialized = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    )
    return hashlib.sha256(serialized.encode("utf-8")).hexdigest()


def load_registry(config_root: Path) -> FeatureModel:
    schema_path = config_root / "features.schema.json"
    schema = load_json(schema_path)
    if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        fail(
            schema_path,
            "$.$schema",
            schema.get("$schema"),
            "the JSON Schema 2020-12 URI",
        )
    managed_dependencies_value = (
        schema.get("$defs", {}).get("managedBuildDependency", {}).get("enum")
    )
    if (
        not isinstance(managed_dependencies_value, list)
        or not managed_dependencies_value
        or any(
            not isinstance(value, str) or not DOMAIN_PATTERN.fullmatch(value)
            for value in managed_dependencies_value
        )
        or len(set(managed_dependencies_value)) != len(managed_dependencies_value)
    ):
        fail(
            schema_path,
            "$.$defs.managedBuildDependency.enum",
            managed_dependencies_value,
            "unique lower-case managed dependency names",
        )
    managed_dependencies = frozenset(managed_dependencies_value)
    features_root = config_root / "features"
    try:
        fragments = sorted(features_root.glob("*.json"))
    except OSError as error:
        raise RegistryError(f"{features_root}: cannot list fragments: {error}") from error
    if not fragments:
        raise RegistryError(f"{features_root}: no feature fragments found")

    features: dict[str, Feature] = {}
    owners: dict[str, Path] = {}
    for path in fragments:
        for name, feature in validate_fragment(
            path, load_json(path), managed_dependencies
        ).items():
            if name in features:
                raise RegistryError(
                    f"{path}: $.symbols.{name}: duplicate symbol; "
                    f"first declared in {owners[name]}"
                )
            features[name] = feature
            owners[name] = path

    for name, feature in sorted(features.items()):
        for relation_name, related in (
            ("implies", feature.implies),
            ("requires", feature.requires),
            ("conflicts", feature.conflicts),
        ):
            for target in related:
                if target == name:
                    raise RegistryError(
                        f"{owners[name]}: $.symbols.{name}.{relation_name}: "
                        "a feature cannot reference itself"
                    )
                if target not in features:
                    raise RegistryError(
                        f"{owners[name]}: $.symbols.{name}.{relation_name}: "
                        f"unknown symbol {target!r}"
                    )
        if name.startswith("HAL_DISABLE_") and feature.kind != "feature":
            raise RegistryError(
                f"{owners[name]}: $.symbols.{name}.kind: "
                "HAL_DISABLE_* symbols cannot be derived"
            )
        if name.startswith("HAL_DISABLE_") and (
            feature.feature_sources
            or feature.portable_sources
            or feature.build_dependencies
        ):
            raise RegistryError(
                f"{owners[name]}: $.symbols.{name}.buildEffects: "
                "HAL_DISABLE_* symbols cannot add build inputs"
            )

    detect_implies_cycles(features)
    model = FeatureModel(
        features=features,
        digest=normalized_digest(features),
        schema_digest=json_digest(schema),
    )

    for name, feature in sorted(features.items()):
        for target in feature.conflicts:
            if name not in features[target].conflicts:
                raise RegistryError(
                    f"{owners[name]}: $.symbols.{name}.conflicts: "
                    f"{target} must declare the symmetric conflict"
                )
        active = {name, *model.closure(name)}
        for active_name in sorted(active):
            conflict = active & set(features[active_name].conflicts)
            if conflict:
                target = sorted(conflict)[0]
                raise RegistryError(
                    f"feature registry: {name} resolves to conflicting symbols "
                    f"{active_name} and {target}"
                )
    return model


def render_header(model: FeatureModel) -> str:
    lines = [
        "#ifndef JH_HAL_GENERATED_FEATURE_GRAPH_INCLUDED",
        "#define JH_HAL_GENERATED_FEATURE_GRAPH_INCLUDED 1",
        "/* Generated by generate_hal_features.py; do not edit. */",
        f"#define JH_HAL_FEATURE_SCHEMA_VERSION {SCHEMA_VERSION}",
        f"#define JH_HAL_FEATURE_GENERATOR_VERSION {GENERATOR_VERSION}",
        f'#define JH_HAL_FEATURE_REGISTRY_DIGEST "{model.digest}"',
        f'#define JH_HAL_FEATURE_SCHEMA_DIGEST "{model.schema_digest}"',
        "",
    ]
    for name, feature in sorted(model.features.items()):
        if feature.kind != "derived":
            continue
        lines.extend(
            [
                f"#if defined({name})",
                f'#error "[JH-CFG-DERIVED] {name} cannot be requested directly"',
                "#endif",
                "",
            ]
        )

    for name in sorted(model.features):
        closure = model.closure(name)
        if not closure:
            continue
        lines.append(f"/* Resolved implications of {name}. */")
        lines.append(f"#if defined({name})")
        for dependency in closure:
            lines.extend(
                [
                    f"#if !defined({dependency})",
                    f"#define {dependency} 1",
                    "#endif",
                ]
            )
        lines.extend(["#endif", ""])

    for name, feature in sorted(model.features.items()):
        for requirement in feature.requires:
            lines.extend(
                [
                    f"#if defined({name}) && !defined({requirement})",
                    f'#error "[JH-CFG-REQUIRES] {name} requires {requirement}"',
                    "#endif",
                    "",
                ]
            )
        for conflict in feature.conflicts:
            if name >= conflict:
                continue
            lines.extend(
                [
                    f"#if defined({name}) && defined({conflict})",
                    f'#error "[JH-CFG-CONFLICT] {name} conflicts with {conflict}"',
                    "#endif",
                    "",
                ]
            )
    lines.extend(
        [
            "#endif /* JH_HAL_GENERATED_FEATURE_GRAPH_INCLUDED */",
            "",
            "#if defined(HAL_CONFIG_VERBOSE) &&                                      \\",
            "    !defined(JH_HAL_FEATURE_VERBOSE_REPORT_DEFERRED) &&                 \\",
            "    !defined(JH_HAL_FEATURE_VERBOSE_REPORT_EMITTED)",
            "#define JH_HAL_FEATURE_VERBOSE_REPORT_EMITTED 1",
            "/* Generated report of every active registered feature flag. */",
        ]
    )
    for name in sorted(model.features):
        lines.extend(
            [
                f"#if defined({name})",
                f'#pragma message("HAL_CONFIG: {name}")',
                "#endif",
            ]
        )
    lines.append("#endif /* generated HAL_CONFIG_VERBOSE report */")
    return "\n".join(lines).rstrip() + "\n"


def cmake_set(name: str, values: Iterable[str]) -> list[str]:
    value_list = list(values)
    if not value_list:
        return [f'set({name} "")']
    return [f"set({name}", *(f'    "{value}"' for value in value_list), ")"]


def render_cmake(model: FeatureModel) -> str:
    lines = [
        "# Generated by generate_hal_features.py; do not edit.",
        "include_guard(GLOBAL)",
        "",
        f"set(JH_HAL_FEATURE_SCHEMA_VERSION {SCHEMA_VERSION})",
        f"set(JH_HAL_FEATURE_GENERATOR_VERSION {GENERATOR_VERSION})",
        f'set(JH_HAL_FEATURE_REGISTRY_DIGEST "{model.digest}")',
        f'set(JH_HAL_FEATURE_SCHEMA_DIGEST "{model.schema_digest}")',
        "",
    ]
    lines.extend(cmake_set("JH_HAL_FEATURE_SYMBOLS", sorted(model.features)))
    lines.append("")
    lines.extend(
        cmake_set(
            "JH_HAL_FEATURE_DERIVED_SYMBOLS",
            (
                name
                for name, feature in sorted(model.features.items())
                if feature.kind == "derived"
            ),
        )
    )
    lines.append("")
    for name, feature in sorted(model.features.items()):
        prefix = f"JH_HAL_FEATURE_{name}"
        lines.extend(
            [
                f'set({prefix}_DOMAIN "{feature.domain}")',
                f'set({prefix}_KIND "{feature.kind}")',
            ]
        )
        lines.extend(cmake_set(f"{prefix}_IMPLIES", feature.implies))
        lines.extend(cmake_set(f"{prefix}_TRANSITIVE_IMPLIES", model.closure(name)))
        lines.extend(cmake_set(f"{prefix}_REQUIRES", feature.requires))
        lines.extend(cmake_set(f"{prefix}_CONFLICTS", feature.conflicts))
        if feature.feature_sources:
            lines.extend(
                cmake_set(
                    f"{prefix}_BUILD_EFFECT_FEATURE_SOURCES",
                    feature.feature_sources,
                )
            )
        if feature.portable_sources:
            lines.extend(
                cmake_set(
                    f"{prefix}_BUILD_EFFECT_PORTABLE_SOURCES",
                    feature.portable_sources,
                )
            )
        if feature.build_dependencies:
            lines.extend(
                cmake_set(
                    f"{prefix}_BUILD_EFFECT_DEPENDENCIES",
                    feature.build_dependencies,
                )
            )
        lines.append("")

    lines.extend(
        [
            "function(jh_hal_resolve_features REQUESTED_OUT RESOLVED_OUT)",
            "    if(NOT REQUESTED_OUT OR NOT RESOLVED_OUT)",
            "        message(FATAL_ERROR",
            '            "jh_hal_resolve_features requires requested and resolved output variables")',
            "    endif()",
            "    set(_jh_requested \"\")",
            "    foreach(_jh_raw IN LISTS ARGN)",
            '        string(REGEX REPLACE "^-D" "" _jh_definition "${_jh_raw}")',
            '        if(_jh_definition MATCHES "^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+=0$")',
            "            message(FATAL_ERROR",
            '                "[JH-CFG-VALUE] ${_jh_raw} is unsupported; omit the symbol to disable it")',
            '        elseif(_jh_definition MATCHES "^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+=1$")',
            '            string(REGEX REPLACE "=1$" "" _jh_symbol "${_jh_definition}")',
            '        elseif(_jh_definition MATCHES "^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+$")',
            '            set(_jh_symbol "${_jh_definition}")',
            "        else()",
            "            message(FATAL_ERROR",
            '                "[JH-CFG-VALUE] invalid feature definition ${_jh_raw}")',
            "        endif()",
            "        list(FIND JH_HAL_FEATURE_SYMBOLS \"${_jh_symbol}\" _jh_known)",
            "        if(_jh_known EQUAL -1)",
            "            message(FATAL_ERROR",
            '                "[JH-CFG-UNKNOWN] unknown feature ${_jh_symbol}")',
            "        endif()",
            "        list(FIND JH_HAL_FEATURE_DERIVED_SYMBOLS",
            '            "${_jh_symbol}" _jh_derived)',
            "        if(NOT _jh_derived EQUAL -1)",
            "            message(FATAL_ERROR",
            '                "[JH-CFG-DERIVED] ${_jh_symbol} cannot be requested directly")',
            "        endif()",
            "        list(APPEND _jh_requested \"${_jh_symbol}\")",
            "    endforeach()",
            "    list(REMOVE_DUPLICATES _jh_requested)",
            "    list(SORT _jh_requested)",
            "",
            "    set(_jh_resolved ${_jh_requested})",
            "    set(_jh_pending ${_jh_requested})",
            "    while(_jh_pending)",
            "        list(POP_FRONT _jh_pending _jh_current)",
            '        set(_jh_implies_var "JH_HAL_FEATURE_${_jh_current}_IMPLIES")',
            "        foreach(_jh_dependency IN LISTS ${_jh_implies_var})",
            "            list(FIND _jh_resolved",
            '                "${_jh_dependency}" _jh_dependency_index)',
            "            if(_jh_dependency_index EQUAL -1)",
            "                list(APPEND _jh_resolved \"${_jh_dependency}\")",
            "                list(APPEND _jh_pending \"${_jh_dependency}\")",
            "            endif()",
            "        endforeach()",
            "    endwhile()",
            "    list(SORT _jh_resolved)",
            "",
            "    foreach(_jh_feature IN LISTS _jh_resolved)",
            '        set(_jh_requires_var "JH_HAL_FEATURE_${_jh_feature}_REQUIRES")',
            "        foreach(_jh_required IN LISTS ${_jh_requires_var})",
            "            list(FIND _jh_resolved \"${_jh_required}\" _jh_required_index)",
            "            if(_jh_required_index EQUAL -1)",
            "                message(FATAL_ERROR",
            '                    "[JH-CFG-REQUIRES] ${_jh_feature} requires ${_jh_required}")',
            "            endif()",
            "        endforeach()",
            '        set(_jh_conflicts_var "JH_HAL_FEATURE_${_jh_feature}_CONFLICTS")',
            "        foreach(_jh_conflict IN LISTS ${_jh_conflicts_var})",
            "            list(FIND _jh_resolved \"${_jh_conflict}\" _jh_conflict_index)",
            "            if(NOT _jh_conflict_index EQUAL -1)",
            "                message(FATAL_ERROR",
            '                    "[JH-CFG-CONFLICT] ${_jh_feature} conflicts with ${_jh_conflict}")',
            "            endif()",
            "        endforeach()",
            "    endforeach()",
            "",
            '    set(${REQUESTED_OUT} "${_jh_requested}" PARENT_SCOPE)',
            '    set(${RESOLVED_OUT} "${_jh_resolved}" PARENT_SCOPE)',
            "endfunction()",
            "",
            "function(jh_hal_resolve_build_effects",
            "        FEATURE_SOURCES_OUT PORTABLE_SOURCES_OUT DEPENDENCIES_OUT)",
            "    set(_jh_feature_sources \"\")",
            "    set(_jh_portable_sources \"\")",
            "    set(_jh_dependencies \"\")",
            "    foreach(_jh_feature IN LISTS ARGN)",
            "        list(FIND JH_HAL_FEATURE_SYMBOLS \"${_jh_feature}\" _jh_known)",
            "        if(_jh_known EQUAL -1)",
            "            message(FATAL_ERROR",
            '                "[JH-CFG-UNKNOWN] unknown resolved feature ${_jh_feature}")',
            "        endif()",
            "        set(_jh_feature_sources_var",
            '            "JH_HAL_FEATURE_${_jh_feature}_BUILD_EFFECT_FEATURE_SOURCES")',
            "        set(_jh_portable_sources_var",
            '            "JH_HAL_FEATURE_${_jh_feature}_BUILD_EFFECT_PORTABLE_SOURCES")',
            "        set(_jh_dependencies_var",
            '            "JH_HAL_FEATURE_${_jh_feature}_BUILD_EFFECT_DEPENDENCIES")',
            "        list(APPEND _jh_feature_sources ${${_jh_feature_sources_var}})",
            "        list(APPEND _jh_portable_sources ${${_jh_portable_sources_var}})",
            "        list(APPEND _jh_dependencies ${${_jh_dependencies_var}})",
            "    endforeach()",
            "    list(REMOVE_DUPLICATES _jh_feature_sources)",
            "    list(REMOVE_DUPLICATES _jh_portable_sources)",
            "    list(REMOVE_DUPLICATES _jh_dependencies)",
            "    list(SORT _jh_feature_sources)",
            "    list(SORT _jh_portable_sources)",
            "    list(SORT _jh_dependencies)",
            '    set(${FEATURE_SOURCES_OUT} "${_jh_feature_sources}" PARENT_SCOPE)',
            '    set(${PORTABLE_SOURCES_OUT} "${_jh_portable_sources}" PARENT_SCOPE)',
            '    set(${DEPENDENCIES_OUT} "${_jh_dependencies}" PARENT_SCOPE)',
            "endfunction()",
            "",
        ]
    )
    return "\n".join(lines)


def generated_outputs(model: FeatureModel) -> dict[Path, str]:
    return {
        FEATURE_HEADER_OUTPUT: render_header(model),
        FEATURE_CMAKE_OUTPUT: render_cmake(model),
    }


def write_outputs(output_root: Path, outputs: dict[Path, str]) -> None:
    write_generated_outputs(
        output_root,
        outputs,
        artifact_kind="feature",
        error_type=RegistryError,
        mode=0o644,
    )


def check_outputs(output_root: Path, outputs: dict[Path, str]) -> bool:
    return check_generated_outputs(
        output_root,
        outputs,
        artifact_kind="feature",
        error_type=RegistryError,
        diff_limit=80,
    )


def iter_lint_inputs(root: Path) -> list[Path]:
    if root.is_file():
        return [root]
    if not root.is_dir():
        raise RegistryError(f"{root}: lint input root does not exist")
    inputs: list[Path] = []
    for directory, directory_names, file_names in os.walk(root):
        directory_names[:] = sorted(
            name
            for name in directory_names
            if name not in IGNORED_INPUT_PARTS and not name.startswith("build")
        )
        directory_path = Path(directory)
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


def preprocessor_logical_lines(text: str) -> Iterable[tuple[int, str]]:
    """Yield comment-free preprocessing lines and their source line."""
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    spliced: list[str] = []
    source_lines: list[int] = []
    source_line = 1
    source_offset = 0
    while source_offset < len(text):
        if text.startswith("\\\n", source_offset):
            source_line += 1
            source_offset += 2
            continue
        character = text[source_offset]
        spliced.append(character)
        source_lines.append(source_line)
        if character == "\n":
            source_line += 1
        source_offset += 1
    text = "".join(spliced)

    buffer: list[str] = []
    origin_line: int | None = None
    offset = 0
    quote: str | None = None

    def append(character: str) -> None:
        nonlocal origin_line
        if origin_line is None and not character.isspace():
            origin_line = source_lines[offset]
        buffer.append(character)

    while offset < len(text):
        character = text[offset]
        if quote is not None:
            append(character)
            if character == "\\" and offset + 1 < len(text):
                offset += 1
                append(text[offset])
            elif character == quote:
                quote = None
            elif character == "\n":
                yield origin_line or source_lines[offset], "".join(buffer)
                buffer.clear()
                origin_line = None
                quote = None
            offset += 1
            continue

        if text.startswith("//", offset):
            append(" ")
            newline = text.find("\n", offset + 2)
            offset = len(text) if newline < 0 else newline
            continue
        if text.startswith("/*", offset):
            append(" ")
            block_end = text.find("*/", offset + 2)
            if block_end < 0:
                offset = len(text)
                continue
            offset = block_end + 2
            continue
        if character in {'"', "'"}:
            quote = character
            append(character)
            offset += 1
            continue
        if character == "\n":
            yield origin_line or source_lines[offset], "".join(buffer)
            buffer.clear()
            origin_line = None
            offset += 1
            continue
        append(character)
        offset += 1

    if buffer:
        final_line = source_lines[-1] if source_lines else 1
        yield origin_line or final_line, "".join(buffer)


def lint_header(path: Path, model: FeatureModel) -> list[str]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise RegistryError(f"{path}: cannot read lint input: {error}") from error
    findings: list[str] = []
    conditions: list[tuple[str, str | None]] = []
    for line_number, line in preprocessor_logical_lines(text):
        directive = re.match(
            r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", line
        )
        if directive:
            name = directive.group(1)
            argument = directive.group(2).strip()
            if name == "ifndef" and re.fullmatch(r"[A-Z_][A-Z0-9_]*", argument):
                conditions.append(("ifndef", argument))
            elif name in {"if", "ifdef"}:
                conditions.append(("conditional", None))
            elif name in {"elif", "else"} and conditions:
                conditions[-1] = ("conditional", None)
            elif name == "endif" and conditions:
                conditions.pop()
            continue
        match = HEADER_DEFINITION_PATTERN.match(line)
        if not match:
            continue
        symbol = match.group(1)
        raw_value = match.group(2).strip()
        feature_conditions = [
            item
            for item in conditions
            if item != ("ifndef", "HAL_PROJECT_CONFIG_H")
        ]
        if feature_conditions not in ([], [("ifndef", symbol)]):
            findings.append(
                f"{path}:{line_number}: [JH-CFG-SCOPE] {symbol} must be "
                "unconditional or guarded only by #ifndef of the same symbol"
            )
        findings.extend(
            validate_requested_symbol(
                path,
                str(line_number),
                symbol,
                raw_value if raw_value else None,
                model,
            )
        )
    return findings


def walk_json_strings(value: Any, json_path: str = "$") -> Iterable[tuple[str, str]]:
    if isinstance(value, str):
        yield json_path, value
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from walk_json_strings(item, f"{json_path}[{index}]")
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from walk_json_strings(item, f"{json_path}.{key}")


def lint_manifest_feature_cache(
    path: Path, value: Any, model: FeatureModel, json_path: str = "$"
) -> list[str]:
    findings: list[str] = []
    if isinstance(value, dict):
        if json_path.endswith(".cache"):
            for key, item in value.items():
                if key in {"JH_EXTRA_DEFINES", "EXTRA_HAL_DEFINES"} and isinstance(
                    item, (list, dict)
                ):
                    findings.append(
                        f"{path}:{json_path}.{key}: [JH-CFG-VALUE] expected a "
                        "semicolon-separated scalar, not a JSON array or object"
                    )
                if not SYMBOL_PATTERN.fullmatch(str(key)):
                    continue
                if item is None or isinstance(item, (list, dict)):
                    feature_value = ""
                elif isinstance(item, bool):
                    feature_value = str(item).lower()
                else:
                    feature_value = str(item)
                findings.extend(
                    validate_requested_symbol(
                        path,
                        f"{json_path}.{key}",
                        str(key),
                        feature_value,
                        model,
                    )
                )
        for key, item in value.items():
            findings.extend(
                lint_manifest_feature_cache(
                    path, item, model, f"{json_path}.{key}"
                )
            )
    elif isinstance(value, list):
        for index, item in enumerate(value):
            findings.extend(
                lint_manifest_feature_cache(
                    path, item, model, f"{json_path}[{index}]"
                )
            )
    return findings


def lint_manifest(path: Path, model: FeatureModel) -> list[str]:
    document = load_json(path)
    findings = lint_manifest_feature_cache(path, document, model)
    for json_path, value in walk_json_strings(document):
        definitions = [item.strip() for item in value.split(";") if item.strip()]
        for index, definition in enumerate(definitions):
            definition_surface = (
                json_path.endswith(".JH_EXTRA_DEFINES")
                or json_path.endswith(".EXTRA_HAL_DEFINES")
                or ".extraDefines[" in json_path
            )
            if not definition_surface:
                continue
            match = MANIFEST_DEFINITION_PATTERN.fullmatch(definition)
            if definition_surface and "$<" in definition:
                location = json_path
                if len(definitions) > 1:
                    location = f"{json_path}[cmake-list:{index}]"
                findings.append(
                    f"{path}:{location}: [JH-CFG-VALUE] compile definition "
                    f"{definition!r} uses an unsupported generator expression"
                )
                continue
            if (
                match is None
                and definition_surface
                and (
                    "HAL_ENABLE_" in definition
                    or "HAL_DISABLE_" in definition
                )
            ):
                location = json_path
                if len(definitions) > 1:
                    location = f"{json_path}[cmake-list:{index}]"
                findings.append(
                    f"{path}:{location}: [JH-CFG-VALUE] compile definition "
                    f"{definition!r} embeds HAL_ENABLE_* in an unsupported "
                    "expression"
                )
                continue
            if not match:
                continue
            location = json_path
            if len(definitions) > 1:
                location = f"{json_path}[cmake-list:{index}]"
            findings.extend(
                validate_requested_symbol(
                    path, location, match.group(1), match.group(2), model
                )
            )
    return findings


def lint_inputs(
    roots: list[Path], model: FeatureModel, report_only: bool
) -> bool:
    files: set[Path] = set()
    for root in roots:
        files.update(iter_lint_inputs(root.resolve()))
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


def effective_inputs(roots: list[Path]) -> tuple[list[Path], list[Path]]:
    files: set[Path] = set()
    for root in roots:
        files.update(iter_lint_inputs(root.resolve()))

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


def effective_axes(document: dict[str, Any]) -> list[tuple[str | None, str | None, str | None]]:
    target_names: set[str] = set()
    manifest_target = document.get("target")
    if isinstance(manifest_target, str) and manifest_target:
        target_names.add(manifest_target)

    profiles = document.get("targetProfiles")
    if isinstance(profiles, dict):
        target_names.update(str(name) for name in profiles if str(name))

    example = document.get("example")
    variants: list[dict[str, Any]] = []
    boards: dict[str, Any] = {}
    if isinstance(example, dict):
        targets = example.get("targets")
        if isinstance(targets, list):
            target_names.update(str(item) for item in targets if str(item))
        if isinstance(example.get("boards"), dict):
            boards = example["boards"]
        if isinstance(example.get("variants"), list):
            variants = [
                item for item in example["variants"] if isinstance(item, dict)
            ]
            for variant in variants:
                targets = variant.get("targets")
                if isinstance(targets, list):
                    target_names.update(str(item) for item in targets if str(item))

    targets: list[str | None] = sorted(target_names) if target_names else [None]
    variant_axes: list[tuple[str | None, dict[str, Any] | None]] = [(None, None)]
    for variant in variants:
        variant_id = variant.get("id")
        if isinstance(variant_id, str) and variant_id:
            variant_axes.append((variant_id, variant))

    axes: list[tuple[str | None, str | None, str | None]] = []
    for target in targets:
        board: str | None = None
        board_value = boards.get(target) if target is not None else None
        if isinstance(board_value, str) and board_value:
            board = board_value
        elif target == manifest_target:
            manifest_board = document.get("board")
            if isinstance(manifest_board, str) and manifest_board:
                board = manifest_board
        for variant_id, variant in variant_axes:
            allowed = variant.get("targets") if variant is not None else None
            if (
                target is not None
                and isinstance(allowed, list)
                and target not in [str(item) for item in allowed]
            ):
                continue
            axes.append((target, board, variant_id))
    return sorted(
        axes,
        key=lambda item: tuple(value or "" for value in item),
    )


def cache_json_path(
    document: dict[str, Any],
    target: str | None,
    variant_id: str | None,
    key: str,
) -> str:
    example = document.get("example")
    variants = example.get("variants") if isinstance(example, dict) else None
    if variant_id and isinstance(variants, list):
        for index, variant in enumerate(variants):
            if not isinstance(variant, dict) or variant.get("id") != variant_id:
                continue
            variant_cmake = variant.get("cmake")
            variant_cache = (
                variant_cmake.get("cache")
                if isinstance(variant_cmake, dict)
                else None
            )
            if isinstance(variant_cache, dict) and key in variant_cache:
                return f"$.example.variants[{index}].cmake.cache.{key}"
            if key == "JH_EXTRA_DEFINES":
                extra_defines = variant.get("extraDefines")
                if isinstance(extra_defines, list) and extra_defines:
                    return f"$.example.variants[{index}].extraDefines"
            break

    profiles = document.get("targetProfiles")
    profile = profiles.get(target) if isinstance(profiles, dict) else None
    profile_cmake = profile.get("cmake") if isinstance(profile, dict) else None
    profile_cache = (
        profile_cmake.get("cache") if isinstance(profile_cmake, dict) else None
    )
    if isinstance(profile_cache, dict) and key in profile_cache:
        return f"$.targetProfiles.{target}.cmake.cache.{key}"

    cmake = document.get("cmake")
    cache = cmake.get("cache") if isinstance(cmake, dict) else None
    if isinstance(cache, dict) and key in cache:
        return f"$.cmake.cache.{key}"
    return f"$effective.cmake.cache.{key}"


def collect_header_requests(path: Path, display: str) -> list[FeatureRequest]:
    if not path.exists():
        return []
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise RegistryError(f"{path}: cannot read effective input: {error}") from error
    requests: list[FeatureRequest] = []
    for line_number, line in preprocessor_logical_lines(text):
        match = HEADER_DEFINITION_PATTERN.match(line)
        if not match:
            continue
        raw_value = match.group(2).strip()
        requests.append(
            FeatureRequest(
                symbol=match.group(1),
                value=raw_value if raw_value else None,
                source=f"{display}:{line_number}",
            )
        )
    return requests


def collect_effective_cache_requests(
    config: dict[str, Any],
    document: dict[str, Any],
    manifest_display: str,
    target: str | None,
    variant_id: str | None,
) -> tuple[list[FeatureRequest], list[str]]:
    requests: list[FeatureRequest] = []
    findings: list[str] = []
    cmake = config.get("cmake")
    cache = cmake.get("cache") if isinstance(cmake, dict) else None
    if not isinstance(cache, dict):
        return requests, findings

    for key, item in sorted(cache.items()):
        symbol = str(key)
        if not SYMBOL_PATTERN.fullmatch(symbol):
            continue
        if item is None or isinstance(item, (list, dict)):
            value = ""
        elif isinstance(item, bool):
            value = str(item).lower()
        else:
            value = str(item)
        json_path = cache_json_path(document, target, variant_id, symbol)
        requests.append(
            FeatureRequest(
                symbol=symbol,
                value=value,
                source=f"{manifest_display}:{json_path}",
            )
        )

    for key in ("JH_EXTRA_DEFINES", "EXTRA_HAL_DEFINES"):
        item = cache.get(key)
        if item is None or item == "":
            continue
        if isinstance(item, (list, dict)):
            json_path = cache_json_path(document, target, variant_id, key)
            findings.append(
                f"{manifest_display}:{json_path}: [JH-CFG-VALUE] expected a "
                "semicolon-separated scalar, not a JSON array or object"
            )
            continue
        raw_items = item if isinstance(item, list) else [item]
        token_index = 0
        for raw_item in raw_items:
            for raw_token in str(raw_item).split(";"):
                token = raw_token.strip()
                if not token:
                    continue
                json_path = cache_json_path(document, target, variant_id, key)
                source = f"{manifest_display}:{json_path}[{token_index}]"
                token_index += 1
                if "$<" in token:
                    findings.append(
                        f"{source}: [JH-CFG-VALUE] compile definition "
                        f"{token!r} uses an unsupported generator expression"
                    )
                    continue
                match = MANIFEST_DEFINITION_PATTERN.fullmatch(token)
                if match is None:
                    if "HAL_ENABLE_" in token or "HAL_DISABLE_" in token:
                        findings.append(
                            f"{source}: [JH-CFG-VALUE] compile definition "
                            f"{token!r} embeds a HAL feature in an unsupported "
                            "expression"
                        )
                    continue
                requests.append(
                    FeatureRequest(
                        symbol=match.group(1),
                        value=match.group(2),
                        source=source,
                    )
                )
    return requests, findings


def resolve_feature_requests(
    requests: list[FeatureRequest],
    model: FeatureModel,
    context: str,
) -> tuple[FeatureResolution, list[str]]:
    findings: list[str] = []
    valid: list[FeatureRequest] = []
    for request in requests:
        request_findings = validate_requested_symbol(
            Path(request.source.split(":", 1)[0]),
            request.source.split(":", 1)[1] if ":" in request.source else "$",
            request.symbol,
            request.value,
            model,
        )
        findings.extend(request_findings)
        if (
            request.symbol in model.features
            and request.value in {None, "1"}
            and model.features[request.symbol].kind != "derived"
        ):
            valid.append(request)

    by_symbol: dict[str, list[FeatureRequest]] = {}
    for request in valid:
        by_symbol.setdefault(request.symbol, []).append(request)
    for symbol, entries in sorted(by_symbol.items()):
        sources = sorted({entry.source for entry in entries})
        if len(entries) > 1:
            findings.append(
                f"{context}: [JH-CFG-REDUNDANT] {symbol} is requested "
                f"{len(entries)} times by {', '.join(sources)}"
            )

    requested = tuple(sorted(by_symbol))
    resolved = model.resolve_many(requested)
    resolved_set = set(resolved)
    for symbol in resolved:
        feature = model.features[symbol]
        for requirement in feature.requires:
            if requirement not in resolved_set:
                findings.append(
                    f"{context}: [JH-CFG-REQUIRES] {symbol} requires "
                    f"{requirement}"
                )
        for conflict in feature.conflicts:
            if conflict in resolved_set and symbol < conflict:
                findings.append(
                    f"{context}: [JH-CFG-CONFLICT] {symbol} conflicts with "
                    f"{conflict}"
                )

    provenance = {
        symbol: tuple(sorted({entry.source for entry in entries}))
        for symbol, entries in sorted(by_symbol.items())
    }
    return FeatureResolution(requested, resolved, provenance), findings


def resolve_target_feature_requests(
    requests: list[FeatureRequest],
    model: FeatureModel,
    context: str,
    target: str | None,
    target_descriptor: dict[str, Any] | None,
) -> tuple[FeatureResolution, list[str]]:
    """Resolve explicit requests together with target-mandated features."""
    if target is None or target_descriptor is None:
        return resolve_feature_requests(requests, model, context)

    explicit_symbols = {request.symbol for request in requests}
    required_features = target_descriptor.get("requiredFeatures", [])
    required_requests: list[FeatureRequest] = []
    findings: list[str] = []
    for index, raw_feature in enumerate(required_features):
        symbol = str(raw_feature).removesuffix("=1")
        source = f"target:{target}:requiredFeatures[{index}]"
        disabled = symbol.replace("HAL_ENABLE_", "HAL_DISABLE_", 1)
        if disabled in explicit_symbols:
            findings.append(
                f"{context}: [JH-CFG-TARGET-REQUIRED] {target} requires "
                f"{symbol}; {disabled} cannot be requested"
            )
        if symbol not in explicit_symbols:
            required_requests.append(FeatureRequest(symbol, "1", source))

    resolution, resolution_findings = resolve_feature_requests(
        [*requests, *required_requests], model, context
    )
    findings.extend(resolution_findings)
    provenance = dict(resolution.provenance)
    for index, raw_feature in enumerate(required_features):
        symbol = str(raw_feature).removesuffix("=1")
        source = f"target:{target}:requiredFeatures[{index}]"
        provenance[symbol] = tuple(
            sorted({*provenance.get(symbol, ()), source})
        )
    return (
        FeatureResolution(
            tuple(
                symbol
                for symbol in resolution.requested
                if symbol in explicit_symbols
            ),
            resolution.resolved,
            provenance,
        ),
        findings,
    )


def resolved_features_digest(features: Iterable[str]) -> str:
    normalized = "\n".join(sorted(features))
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()


def effective_matrix_digest(records: list[dict[str, Any]]) -> str:
    contract = [
        {
            "project": record["project"],
            "target": record["target"],
            "board": record["board"],
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
) -> dict[str, Any]:
    """Serialize one effective feature resolution for the lint report."""
    return {
        "project": project,
        "target": target,
        "board": board,
        "variant": variant,
        "requestedFeatures": list(resolution.requested),
        "resolvedFeatures": list(resolution.resolved),
        "resolvedFeaturesDigest": resolved_features_digest(resolution.resolved),
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
) -> bool:
    workflow = load_workflow_runtime()
    findings: list[str] = []
    records: list[dict[str, Any]] = []
    projects, standalone_headers = effective_inputs(roots)
    target_registry = workflow.load_target_registry()
    for project in projects:
        project_display = relative_display(project, roots)
        manifest_path = project / ".vscode/jaszczurhal.project.json"
        try:
            document = workflow.load_json_file(manifest_path)
        except (OSError, ValueError) as error:
            findings.append(f"{project_display}: [JH-CFG-INPUT] {error}")
            continue
        manifest_display = (
            f"{project_display}/.vscode/jaszczurhal.project.json"
            if project_display != "."
            else ".vscode/jaszczurhal.project.json"
        )
        header_display = (
            f"{project_display}/hal_project_config.h"
            if project_display != "."
            else "hal_project_config.h"
        )
        header_requests = collect_header_requests(
            project / "hal_project_config.h", header_display
        )
        for target, board, variant_id in effective_axes(document):
            target_descriptor = None
            if target is not None:
                target_descriptor = target_registry.get(target)
                if not isinstance(target_descriptor, dict):
                    findings.append(
                        f"{project_display} [target={target}]: "
                        f"[JH-CFG-TARGET] unknown target"
                    )
                    continue
                valid_boards = {
                    str(item.get("id"))
                    for item in target_descriptor.get("boards", [])
                    if isinstance(item, dict) and item.get("id")
                }
                if board is not None and board not in valid_boards:
                    findings.append(
                        f"{project_display} [target={target}, board={board}]: "
                        "[JH-CFG-BOARD] board is not registered for the target"
                    )
                    continue
            try:
                config = workflow.load_project_config(
                    project,
                    target_override=target,
                    board_override=board,
                    use_local_state=False,
                )
                workflow.apply_example_variant(config, variant_id)
                workflow.validate_hal_enable_values(config, project)
            except (OSError, ValueError) as error:
                axis = f"target={target or 'default'}, variant={variant_id or 'base'}"
                findings.append(
                    f"{project_display} [{axis}]: [JH-CFG-EFFECTIVE] {error}"
                )
                continue

            active_target = str(config.get("target")) if config.get("target") else None
            active_board = str(config.get("board")) if config.get("board") else None
            axis = (
                f"target={active_target or 'default'}, "
                f"board={active_board or 'default'}, "
                f"variant={variant_id or 'base'}"
            )
            cache_requests, cache_findings = collect_effective_cache_requests(
                config,
                document,
                manifest_display,
                active_target,
                variant_id,
            )
            findings.extend(cache_findings)
            resolution, resolution_findings = resolve_target_feature_requests(
                [*header_requests, *cache_requests],
                model,
                f"{project_display} [{axis}]",
                active_target,
                target_descriptor,
            )
            findings.extend(resolution_findings)
            records.append(
                feature_resolution_record(
                    project_display,
                    active_target,
                    active_board,
                    variant_id,
                    resolution,
                )
            )

    for header_path in standalone_headers:
        project = header_path.parent
        project_display = relative_display(project, roots)
        header_display = relative_display(header_path, roots)
        header_requests = collect_header_requests(header_path, header_display)
        if not header_requests:
            continue
        axis = "target=default, board=default, variant=base"
        resolution, resolution_findings = resolve_feature_requests(
            header_requests,
            model,
            f"{project_display} [{axis}]",
        )
        findings.extend(resolution_findings)
        records.append(
            feature_resolution_record(
                project_display, None, None, None, resolution
            )
        )

    records.sort(
        key=lambda item: (
            item["project"],
            item["target"] or "",
            item["board"] or "",
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
    parser = argparse.ArgumentParser()
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--write", action="store_true")
    modes.add_argument("--check", action="store_true")
    modes.add_argument("--lint", action="store_true")
    parser.add_argument(
        "--config-root", type=Path, default=repository_root / "config"
    )
    parser.add_argument(
        "--output-root", type=Path, default=repository_root
    )
    parser.add_argument("--input-root", type=Path, action="append", default=[])
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--effective", action="store_true")
    parser.add_argument("--resolution-output", type=Path)
    args = parser.parse_args()
    if args.report_only and not args.lint:
        parser.error("--report-only requires --lint")
    if args.input_root and not args.lint:
        parser.error("--input-root requires --lint")
    if args.effective and not args.lint:
        parser.error("--effective requires --lint")
    if args.resolution_output is not None and not args.effective:
        parser.error("--resolution-output requires --effective")
    return args


def main() -> int:
    args = parse_args()
    try:
        model = load_registry(args.config_root.resolve())
        outputs = generated_outputs(model)
        if args.write:
            write_outputs(args.output_root.resolve(), outputs)
            return 0
        if args.check:
            return 0 if check_outputs(args.output_root.resolve(), outputs) else 1
        input_roots = args.input_root or [Path(__file__).resolve().parents[1]]
        raw_valid = lint_inputs(input_roots, model, args.report_only)
        effective_valid = True
        if args.effective:
            effective_valid = lint_effective_inputs(
                input_roots,
                model,
                args.report_only,
                args.resolution_output,
            )
        return 0 if raw_valid and effective_valid else 1
    except RegistryError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
