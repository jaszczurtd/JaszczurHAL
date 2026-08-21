#!/usr/bin/env python3
"""Validate exact provenance and licenses in the generated SBOM inventory."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import generate_sbom  # noqa: E402


def read_shell_assignments(path: Path) -> dict[str, str]:
    assignments: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        assignments[name] = value
    return assignments


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
            "Semtech SX126x driver":
                "a10c5dfdf89788c6ac805e9fe98889de44175aa2",
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
        self.assertEqual(
            ["BSD-3-Clause-Clear"],
            components["Semtech SX126x driver"]["licenses"],
        )
        self.assertIn(
            "third_party/LICENSE.SX126X",
            components["Semtech SX126x driver"]["paths"],
        )
        self.assertEqual(
            "7.26.0", components["PMD Copy/Paste Detector"]["version"]
        )
        self.assertEqual(
            "development", components["PMD Copy/Paste Detector"]["scope"]
        )
        self.assertIn(
            "third_party/pmd_version.conf",
            components["PMD Copy/Paste Detector"]["paths"],
        )

    def test_esp_idf_tool_inventory_matches_the_pinned_tool_snapshot(self) -> None:
        inventory = json.loads(
            (ROOT / "security/third_party.json").read_text(encoding="utf-8")
        )
        snapshot_path = ROOT / "security/esp_idf_tools.json"
        snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))
        pin = read_shell_assignments(ROOT / "third_party/esp_idf_version.conf")
        components = {item["name"]: item for item in inventory["components"]}
        tool_components = generate_sbom.esp_idf_tool_components(snapshot_path)
        tool_components_by_name = {
            item["name"]: item for item in tool_components
        }

        self.assertEqual(2, snapshot["schemaVersion"])
        self.assertEqual(pin["ESP_IDF_VERSION"], snapshot["espIdf"]["version"])
        self.assertEqual(pin["ESP_IDF_REF"], snapshot["espIdf"]["commit"])
        self.assertEqual(
            pin["ESP_IDF_TARGETS"].split(","), snapshot["espIdf"]["targets"]
        )
        self.assertEqual(pin["ESP_IDF_VERSION"], components["ESP-IDF"]["version"])
        self.assertEqual(pin["ESP_IDF_REF"], components["ESP-IDF"]["commit"])
        self.assertEqual(["Apache-2.0"], components["ESP-IDF"]["licenses"])

        snapshot_tools = snapshot["binaryTools"] + snapshot["pythonTools"]
        snapshot_component_names = {
            item["displayName"] for item in snapshot_tools
        }
        self.assertEqual(snapshot_component_names, set(tool_components_by_name))
        self.assertTrue(snapshot_component_names.isdisjoint(components))
        self.assertFalse(any(
            "security/esp_idf_tools.json" in component.get("paths", [])
            for component in inventory["components"]
        ))
        self.assertTrue(
            {item["purl"] for item in tool_components}.isdisjoint(
                component.get("purl")
                for component in inventory["components"]
                if component.get("purl")
            )
        )
        for tool in snapshot_tools:
            component = tool_components_by_name[tool["displayName"]]
            self.assertEqual("development", component["scope"])
            self.assertEqual(tool["version"], component["version"])
            self.assertEqual([tool["license"]], component["licenses"])
            self.assertEqual(tool["upstream"], component["upstream"])
            self.assertIn("security/esp_idf_tools.json", component["paths"])

        normalization = snapshot["licenseNormalization"]
        for tool in snapshot["binaryTools"]:
            self.assertEqual(
                tool["license"],
                normalization.get(tool["sourceLicense"], tool["sourceLicense"]),
            )

        tools_json = ROOT / "third_party/esp-idf/tools/tools.json"
        if tools_json.is_file():
            self.assertEqual(
                snapshot["espIdf"]["toolsJsonSha256"],
                hashlib.sha256(tools_json.read_bytes()).hexdigest(),
            )
            live_tools = {
                item["name"]: item
                for item in json.loads(tools_json.read_text(encoding="utf-8"))["tools"]
            }
            for tool in snapshot["binaryTools"]:
                live_tool = live_tools[tool["name"]]
                recommended = {
                    item["name"]
                    for item in live_tool["versions"]
                    if item.get("status") == "recommended"
                }
                self.assertIn(tool["version"], recommended)
                self.assertEqual(tool["sourceLicense"], live_tool["license"])
                self.assertEqual(tool["upstream"], live_tool["info_url"])

            requirements_path = (
                ROOT
                / "third_party/esp-idf"
                / snapshot["espIdf"]["pythonRequirementsPath"]
            )
            direct_requirements = set()
            for line in requirements_path.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                name = line.split("[", 1)[0]
                for separator in ("<", ">", "=", "!", "~"):
                    name = name.split(separator, 1)[0]
                direct_requirements.add(name.lower().replace("_", "-"))
            first_party_tools = {
                name
                for name in direct_requirements
                if name == "esptool"
                or name.startswith("esp-")
                or name.startswith("idf-")
                or name in {"pyclang", "freertos-gdb"}
            }
            self.assertEqual(
                first_party_tools,
                {item["name"] for item in snapshot["pythonTools"]},
            )

    def test_generated_sbom_retains_license_file_and_exact_commits(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-sbom-") as text:
            output = Path(text) / "sbom.json"
            generate_sbom.generate(ROOT / "security/third_party.json", output)
            sbom = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                (ROOT / "security/sbom.cdx.json").read_bytes(),
                output.read_bytes(),
            )
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
        components = {item["name"]: item for item in sbom["components"]}
        expected_tools = {
            item["name"]: generate_sbom.make_component(item)
            for item in generate_sbom.esp_idf_tool_components(
                ROOT / "security/esp_idf_tools.json"
            )
        }
        for name, expected in expected_tools.items():
            self.assertEqual(expected, components[name])
        xtensa = next(
            item
            for item in sbom["components"]
            if item["name"] == "Espressif Xtensa GCC toolchain"
        )
        self.assertEqual(
            "GPL-3.0-or-later WITH GCC-exception-3.1",
            xtensa["licenses"][0]["expression"],
        )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
