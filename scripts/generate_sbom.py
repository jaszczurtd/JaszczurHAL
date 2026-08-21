#!/usr/bin/env python3
"""Generate a deterministic CycloneDX SBOM from reviewed source inventories."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import uuid


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_INVENTORY = REPO_ROOT / "security" / "third_party.json"
DEFAULT_ESP_IDF_TOOLS = REPO_ROOT / "security" / "esp_idf_tools.json"
DEFAULT_OUTPUT = REPO_ROOT / "security" / "sbom.cdx.json"


def read_project_version(repo_root: pathlib.Path, version_file: str) -> str:
    path = repo_root / version_file
    if not path.exists():
        return "unknown"

    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"\s*version\s*=\s*(\S+)\s*$", line)
        if match:
            return match.group(1)
    return "unknown"


def license_entries(licenses: list[str]) -> list[dict[str, object]]:
    entries = []
    for item in licenses:
        if re.search(r"\s(?:AND|OR|WITH)\s", item):
            entries.append({"expression": item})
        elif item.lower() == "unknown" or "style" in item.lower():
            entries.append({"license": {"name": item}})
        else:
            entries.append({"license": {"id": item}})
    return entries


def external_refs(component: dict[str, object]) -> list[dict[str, str]]:
    refs: list[dict[str, str]] = []
    upstream = component.get("upstream")
    if isinstance(upstream, str) and upstream:
        refs.append({"type": "website", "url": upstream})

    for path in component.get("paths", []):
        if isinstance(path, str):
            refs.append({"type": "other", "url": path, "comment": "repository path"})
    return refs


def component_bom_ref(component: dict[str, object]) -> str:
    name = str(component["name"]).lower()
    version = str(component.get("version", "unknown"))
    safe_name = re.sub(r"[^a-z0-9_.-]+", "-", name).strip("-")
    safe_version = re.sub(r"[^A-Za-z0-9_.:+-]+", "-", version).strip("-")
    return f"pkg:{safe_name}@{safe_version}"


def make_component(component: dict[str, object]) -> dict[str, object]:
    item: dict[str, object] = {
        "type": component.get("type", "library"),
        "bom-ref": component_bom_ref(component),
        "name": component["name"],
        "version": component.get("version", "unknown"),
        "scope": component.get("scope", "optional"),
        "licenses": license_entries(list(component.get("licenses", []))),
    }

    supplier = component.get("supplier")
    if isinstance(supplier, str) and supplier:
        item["supplier"] = {"name": supplier}

    purl = component.get("purl")
    if isinstance(purl, str) and purl:
        item["purl"] = purl

    refs = external_refs(component)
    if refs:
        item["externalReferences"] = refs

    properties = []
    for key in ("commit", "notes"):
        value = component.get(key)
        if isinstance(value, str) and value:
            properties.append({"name": f"jaszczurhal:{key}", "value": value})

    flags = component.get("feature_flags", [])
    if isinstance(flags, list) and flags:
        properties.append({
            "name": "jaszczurhal:feature_flags",
            "value": ",".join(str(flag) for flag in flags),
        })

    paths = component.get("paths", [])
    if isinstance(paths, list) and paths:
        properties.append({
            "name": "jaszczurhal:paths",
            "value": ",".join(str(path) for path in paths),
        })

    if properties:
        item["properties"] = properties

    return item


def deterministic_serial(payload: dict[str, object]) -> str:
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    digest = hashlib.sha256(encoded).hexdigest()
    return f"urn:uuid:{uuid.UUID(digest[:32])}"


def validate_inventory(inventory: dict[str, object]) -> None:
    project = inventory["project"]
    if not isinstance(project, dict):
        raise ValueError("project inventory entry must be an object")
    project_license = str(project.get("license", ""))
    if not project_license or "unknown" in project_license.lower():
        raise ValueError("project license must be explicitly identified")
    license_file = project.get("license_file")
    if project_license.startswith("LicenseRef-"):
        if not isinstance(license_file, str) or not license_file:
            raise ValueError("custom project license requires license_file")
        if not (REPO_ROOT / license_file).is_file():
            raise ValueError(f"project license file is missing: {license_file}")

    components = inventory["components"]
    if not isinstance(components, list):
        raise ValueError("components inventory entry must be an array")
    for component in components:
        if not isinstance(component, dict):
            raise ValueError("component inventory entry must be an object")
        name = str(component.get("name", "unnamed component"))
        scope = str(component.get("scope", "optional"))
        version = str(component.get("version", ""))
        licenses = [str(item) for item in component.get("licenses", [])]
        if scope in ("required", "optional"):
            if not version or "unknown" in version.lower():
                raise ValueError(f"{name} has no verified version")
            if not licenses or any("unknown" in item.lower() for item in licenses):
                raise ValueError(f"{name} has no verified license")
        commit = component.get("commit")
        purl = str(component.get("purl", ""))
        if purl.startswith("pkg:github/"):
            if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit):
                raise ValueError(f"{name} GitHub component has no exact commit")
            if not purl.endswith(f"@{commit}"):
                raise ValueError(f"{name} purl does not use its exact commit")


def esp_idf_tool_components(snapshot_path: pathlib.Path) -> list[dict[str, object]]:
    snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))
    if snapshot.get("schemaVersion") != 2:
        raise ValueError("ESP-IDF tool snapshot has an unsupported schema")

    sbom = snapshot.get("sbom")
    if not isinstance(sbom, dict):
        raise ValueError("ESP-IDF tool snapshot has no SBOM metadata")
    scope = sbom.get("scope")
    paths = sbom.get("paths")
    if scope != "development":
        raise ValueError("ESP-IDF tool snapshot SBOM scope must be development")
    if (
        not isinstance(paths, list)
        or not paths
        or not all(isinstance(path, str) and path for path in paths)
        or len(paths) != len(set(paths))
    ):
        raise ValueError("ESP-IDF tool snapshot has invalid SBOM paths")

    components: list[dict[str, object]] = []
    tool_names: set[str] = set()
    component_names: set[str] = set()
    for group_name in ("binaryTools", "pythonTools"):
        defaults = sbom.get(group_name)
        tools = snapshot.get(group_name)
        if not isinstance(defaults, dict):
            raise ValueError(
                f"ESP-IDF tool snapshot has no {group_name} SBOM defaults"
            )
        if not isinstance(tools, list) or not tools:
            raise ValueError(f"ESP-IDF tool snapshot has no {group_name}")

        default_type = defaults.get("type")
        purl_type = defaults.get("purlType")
        default_supplier = defaults.get("supplier")
        if not isinstance(default_type, str) or not default_type:
            raise ValueError(
                f"ESP-IDF tool snapshot {group_name} has no default type"
            )
        if purl_type not in ("generic", "pypi"):
            raise ValueError(
                f"ESP-IDF tool snapshot {group_name} has invalid purl type"
            )
        if default_supplier is not None and (
            not isinstance(default_supplier, str) or not default_supplier
        ):
            raise ValueError(
                f"ESP-IDF tool snapshot {group_name} has invalid supplier"
            )

        for index, tool in enumerate(tools):
            context = f"ESP-IDF tool snapshot {group_name}[{index}]"
            required = (
                "name",
                "displayName",
                "version",
                "license",
                "upstream",
                "notes",
            )
            if not isinstance(tool, dict) or not all(
                isinstance(tool.get(key), str) and tool[key]
                for key in required
            ):
                raise ValueError(f"{context} has incomplete SBOM metadata")

            tool_name = str(tool["name"])
            component_name = str(tool["displayName"])
            if tool_name in tool_names:
                raise ValueError(f"ESP-IDF tool snapshot repeats tool {tool_name!r}")
            if component_name in component_names:
                raise ValueError(
                    f"ESP-IDF tool snapshot repeats component {component_name!r}"
                )
            tool_names.add(tool_name)
            component_names.add(component_name)

            component_type = tool.get("type", default_type)
            supplier = tool.get("supplier", default_supplier)
            if not isinstance(component_type, str) or not component_type:
                raise ValueError(f"{context} has invalid component type")
            if not isinstance(supplier, str) or not supplier:
                raise ValueError(f"{context} has no supplier")

            version = str(tool["version"])
            components.append({
                "name": component_name,
                "version": version,
                "type": component_type,
                "scope": scope,
                "supplier": supplier,
                "licenses": [str(tool["license"])],
                "purl": f"pkg:{purl_type}/{tool_name}@{version}",
                "paths": list(paths),
                "upstream": str(tool["upstream"]),
                "feature_flags": [],
                "notes": str(tool["notes"]),
            })

    return components


def generate(
    inventory_path: pathlib.Path,
    output_path: pathlib.Path,
    esp_idf_tools_path: pathlib.Path = DEFAULT_ESP_IDF_TOOLS,
) -> None:
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    validate_inventory(inventory)
    project = inventory["project"]
    project_version = read_project_version(REPO_ROOT, str(project["version_file"]))

    inventory_components = inventory["components"]
    esp_idf_components = esp_idf_tool_components(esp_idf_tools_path)
    inventory_names = {str(component["name"]) for component in inventory_components}
    duplicate_names = sorted(
        inventory_names.intersection(
            str(component["name"]) for component in esp_idf_components
        )
    )
    if duplicate_names:
        raise ValueError(
            "ESP-IDF tools must be declared only in the tool snapshot; "
            f"third_party.json repeats {duplicate_names!r}"
        )

    components = []
    esp_idf_tools_added = False
    for component in inventory_components:
        components.append(make_component(component))
        if component["name"] == "ESP-IDF":
            components.extend(make_component(item) for item in esp_idf_components)
            esp_idf_tools_added = True
    if not esp_idf_tools_added:
        components.extend(make_component(item) for item in esp_idf_components)
    project_references = [
        {"type": "vcs", "url": str(project["repository"])}
    ]
    license_file = project.get("license_file")
    if isinstance(license_file, str) and license_file:
        project_references.append({
            "type": "license",
            "url": license_file,
            "comment": "repository license text",
        })

    bom: dict[str, object] = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "version": 1,
        "metadata": {
            "tools": [
                {
                    "vendor": "JaszczurHAL",
                    "name": "scripts/generate_sbom.py",
                    "version": "1",
                }
            ],
            "component": {
                "type": "library",
                "bom-ref": "pkg:jaszczurhal",
                "name": project["name"],
                "version": project_version,
                "licenses": license_entries([str(project["license"])]),
                "externalReferences": project_references,
            },
        },
        "components": components,
    }
    bom["serialNumber"] = deterministic_serial(bom)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(bom, indent=2, sort_keys=False, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=pathlib.Path, default=DEFAULT_INVENTORY)
    parser.add_argument(
        "--esp-idf-tools",
        type=pathlib.Path,
        default=DEFAULT_ESP_IDF_TOOLS,
    )
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    try:
        generate(args.inventory, args.output, args.esp_idf_tools)
    except Exception as exc:  # pragma: no cover - script diagnostics
        print(f"generate_sbom.py: {exc}", file=sys.stderr)
        return 1

    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
