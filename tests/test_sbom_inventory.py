#!/usr/bin/env python3
"""Validate exact provenance and licenses in the generated SBOM inventory."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import generate_sbom  # noqa: E402


class SbomInventoryTests(unittest.TestCase):
    def test_runtime_components_have_verified_versions_and_licenses(self) -> None:
        inventory = json.loads(
            (ROOT / "security/third_party.json").read_text(encoding="utf-8")
        )
        generate_sbom.validate_inventory(inventory)
        self.assertEqual(
            "LicenseRef-JaszczurHAL-Attribution",
            inventory["project"]["license"],
        )
        self.assertTrue((ROOT / inventory["project"]["license_file"]).is_file())

        components = {item["name"]: item for item in inventory["components"]}
        expected = {
            "PubSubClient": "2d228f2f862a95846c65a8518c79f48dfc8f188c",
            "Shared WireGuard/lwIP engine":
                "ba409a040c78bcf86f6c8026ba86c014035b8c63",
            "WireGuard crypto core":
                "ba409a040c78bcf86f6c8026ba86c014035b8c63",
            "SmartTimers": "651b205ab6b6ac64ab08983f2ecf158459c19d16",
        }
        for name, commit in expected.items():
            self.assertEqual(commit, components[name]["commit"])
            self.assertNotIn("unknown", components[name]["version"].lower())
            self.assertTrue(components[name]["licenses"])

    def test_generated_sbom_retains_license_file_and_exact_commits(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-sbom-") as text:
            output = Path(text) / "sbom.json"
            generate_sbom.generate(ROOT / "security/third_party.json", output)
            sbom = json.loads(output.read_text(encoding="utf-8"))
        project = sbom["metadata"]["component"]
        self.assertEqual("LicenseRef-JaszczurHAL-Attribution",
                         project["licenses"][0]["license"]["id"])
        self.assertTrue(any(ref["type"] == "license" and ref["url"] == "LICENSE"
                            for ref in project["externalReferences"]))
        pubsub = next(item for item in sbom["components"]
                      if item["name"] == "PubSubClient")
        properties = {item["name"]: item["value"]
                      for item in pubsub["properties"]}
        self.assertEqual("2d228f2f862a95846c65a8518c79f48dfc8f188c",
                         properties["jaszczurhal:commit"])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
