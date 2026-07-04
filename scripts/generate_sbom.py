#!/usr/bin/env python3
"""Generate a deterministic CycloneDX SBOM from security/third_party.json."""

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


def license_entries(licenses: list[str]) -> list[dict[str, dict[str, str]]]:
    entries = []
    for item in licenses:
        if item.lower() == "unknown" or "style" in item.lower():
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


def generate(inventory_path: pathlib.Path, output_path: pathlib.Path) -> None:
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    project = inventory["project"]
    project_version = read_project_version(REPO_ROOT, str(project["version_file"]))

    components = [make_component(component) for component in inventory["components"]]
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
                "externalReferences": [
                    {"type": "vcs", "url": str(project["repository"])}
                ],
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
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    try:
        generate(args.inventory, args.output)
    except Exception as exc:  # pragma: no cover - script diagnostics
        print(f"generate_sbom.py: {exc}", file=sys.stderr)
        return 1

    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
