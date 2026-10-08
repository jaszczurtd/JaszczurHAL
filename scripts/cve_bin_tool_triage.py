#!/usr/bin/env python3
"""Turn security/cve-bin-tool-triage.toml into an OpenVEX triage file.

cve-bin-tool matches a statement to a scanned product by vendor, product and
version, and with --filter-triage drops findings marked not_affected or fixed.
A decision applies only to the SBOM component at the commit it was made for,
and a mitigated decision only until its review date: otherwise the finding is
reported again.
"""

from __future__ import annotations

import argparse
import datetime
import json
import pathlib
import re
import sys
import tomllib


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_TRIAGE = REPO_ROOT / "security" / "cve-bin-tool-triage.toml"
DEFAULT_SBOM = REPO_ROOT / "security" / "sbom.cdx.json"

# OpenVEX justifications for not_affected; fixed takes none.
JUSTIFICATIONS = {
    "component_not_present",
    "vulnerable_code_not_present",
    "vulnerable_code_not_in_execute_path",
    "vulnerable_code_cannot_be_controlled_by_adversary",
    "inline_mitigations_already_exist",
}
FIELDS = {"ids", "component", "commit", "product", "status", "justification",
          "reviewUntil", "reason"}
CVE_ID = re.compile(r"CVE-\d{4}-\d{4,}")
COMMIT = re.compile(r"[0-9a-f]{40}")
# vendor/product as cve-bin-tool reports them (CPE 2.3 characters).
PRODUCT = re.compile(r"[A-Za-z0-9._~-]+/[A-Za-z0-9._~-]+")
REASON = re.compile(r"\d{4}-\d{2}-\d{2}: \S")


def load_entries(path: pathlib.Path) -> list[dict]:
    """Read and check the decision file; raise ValueError on a malformed entry."""
    entries = tomllib.loads(path.read_text(encoding="utf-8")).get("Triage", [])
    seen = set()
    for number, entry in enumerate(entries, 1):
        where = f"{path.name} entry {number}"
        unknown = set(entry) - FIELDS
        missing = {"ids", "component", "commit", "product", "status", "reason"} - set(entry)
        if unknown or missing:
            raise ValueError(f"{where}: unknown {sorted(unknown)}, missing {sorted(missing)}")
        if not entry["ids"] or not all(
                isinstance(i, str) and CVE_ID.fullmatch(i) for i in entry["ids"]):
            raise ValueError(f"{where}: ids must be a nonempty list of CVE IDs")
        if not COMMIT.fullmatch(str(entry["commit"])):
            raise ValueError(f"{where}: commit must be a full lowercase SHA-1")
        if not PRODUCT.fullmatch(str(entry["product"])):
            raise ValueError(f"{where}: product must be vendor/product")
        if not REASON.match(str(entry["reason"])):
            raise ValueError(f"{where}: reason must start with its log date")
        if entry["status"] == "not_affected":
            if entry.get("justification") not in JUSTIFICATIONS:
                raise ValueError(f"{where}: not_affected needs an OpenVEX justification")
        elif entry["status"] == "fixed":
            if "justification" in entry:
                raise ValueError(f"{where}: fixed takes no justification")
        else:
            raise ValueError(f"{where}: status must be not_affected or fixed")
        if "reviewUntil" in entry and not isinstance(entry["reviewUntil"], datetime.date):
            raise ValueError(f"{where}: reviewUntil must be a date")
        for cve in entry["ids"]:
            key = (cve, entry["product"])
            if key in seen:
                raise ValueError(f"{where}: {cve} for {entry['product']} is listed twice")
            seen.add(key)
    return entries


def sbom_components(sbom: dict) -> dict[str, tuple[str, str]]:
    """Map component name to (commit, version cve-bin-tool reads from its purl)."""
    components = {}
    for component in sbom.get("components", []):
        properties = {p.get("name"): p.get("value")
                      for p in component.get("properties", [])}
        purl = component.get("purl", "")
        commit = properties.get("jaszczurhal:commit")
        if commit and "@" in purl:
            components[component["name"]] = (commit, purl.rsplit("@", 1)[1])
    return components


def build_vex(entries: list[dict], sbom: dict, today: datetime.date,
              warn) -> dict:
    """Return the OpenVEX document with every decision that still applies."""
    components = sbom_components(sbom)
    timestamp = f"{today.isoformat()}T00:00:00Z"
    statements = []
    for entry in entries:
        name = entry["component"]
        if name not in components:
            raise ValueError(f"{name}: no such SBOM component with a commit")
        commit, version = components[name]
        ids = ", ".join(entry["ids"])
        if commit != entry["commit"]:
            warn(f"{ids}: decided for {name} {entry['commit'][:12]}, pinned "
                 f"{commit[:12]}; not applied, review it")
            continue
        if "reviewUntil" in entry and entry["reviewUntil"] <= today:
            warn(f"{ids}: review was due on {entry['reviewUntil']}; not applied")
            continue
        for cve in entry["ids"]:
            statement = {
                "vulnerability": {"name": cve},
                "products": [{"@id": f"pkg:generic/{entry['product']}@{version}"}],
                "status": entry["status"],
                "status_notes": entry["reason"],
                "timestamp": timestamp,
            }
            if entry["status"] == "not_affected":
                statement["justification"] = entry["justification"]
            statements.append(statement)
    return {
        "@context": "https://openvex.dev/ns/v0.2.0",
        "@id": "https://github.com/jaszczurtd/JaszczurHAL/security/cve-bin-tool-triage",
        "author": "JaszczurHAL security/cve-bin-tool-triage.toml",
        "role": "Document creator",
        "timestamp": timestamp,
        "version": 1,
        "statements": statements,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--triage", type=pathlib.Path, default=DEFAULT_TRIAGE)
    parser.add_argument("--sbom", type=pathlib.Path, default=DEFAULT_SBOM)
    parser.add_argument("--output", type=pathlib.Path, required=True,
                        help="OpenVEX file; cve-bin-tool needs a .json name")
    parser.add_argument("--today", type=datetime.date.fromisoformat,
                        default=datetime.datetime.now(datetime.timezone.utc).date())
    args = parser.parse_args()

    def warn(message: str) -> None:
        print(f"[WARN] cve-bin-tool triage: {message}", file=sys.stderr)

    try:
        entries = load_entries(args.triage)
        sbom = json.loads(args.sbom.read_text(encoding="utf-8"))
        vex = build_vex(entries, sbom, args.today, warn)
    except (OSError, ValueError, tomllib.TOMLDecodeError) as error:
        print(f"[ERROR] cve-bin-tool triage: {error}", file=sys.stderr)
        return 1
    args.output.write_text(json.dumps(vex, indent=2) + "\n", encoding="utf-8")
    print(f"[INFO] cve-bin-tool triage: {len(vex['statements'])} recorded decisions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
