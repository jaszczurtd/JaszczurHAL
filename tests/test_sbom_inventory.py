#!/usr/bin/env python3
"""Validate exact provenance and licenses in the generated SBOM inventory."""

from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))
import generate_sbom  # noqa: E402
import build_esp_idf  # noqa: E402
import component_manager  # noqa: E402
from component_manager import JHBASE_PINS, parse_config, supplied_pin  # noqa: E402


class SbomInventoryTests(unittest.TestCase):
    def test_runtime_components_have_verified_versions_and_licenses(self) -> None:
        inventory = generate_sbom.load_inventory(ROOT / "security/third_party.json")
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
            ["Apache-2.0"], components["Bluedroid SBC codec"]["licenses"]
        )
        self.assertIn(
            "third_party/LICENSE.BLUEDROID-SBC",
            components["Bluedroid SBC codec"]["paths"],
        )
        self.assertEqual(
            "7.26.0", components["PMD Copy/Paste Detector"]["version"]
        )
        self.assertEqual(
            "development", components["PMD Copy/Paste Detector"]["scope"]
        )
        self.assertIn(
            "third_party/jhbase/third_party/pmd_version.conf",
            components["PMD Copy/Paste Detector"]["paths"],
        )

    def test_cyw43_vendor_pin_matches_inventory_and_provenance(self) -> None:
        inventory = generate_sbom.load_inventory(ROOT / "security/third_party.json")
        pin = parse_config(ROOT / "third_party/cyw43_driver_version.conf")
        components = {item["name"]: item for item in inventory["components"]}

        driver = components["cyw43-driver"]
        self.assertEqual(pin["CYW43_DRIVER_REF"], driver["commit"])
        self.assertEqual(pin["CYW43_DRIVER_VERSION"], driver["version"])
        self.assertTrue(driver["purl"].endswith(f"@{pin['CYW43_DRIVER_REF']}"))
        self.assertIn(
            "third_party/cyw43_driver_version.conf", driver["paths"]
        )
        self.assertEqual(
            [
                "LicenseRef-CYW43-GeorgeRobotics-NonCommercial"
                " OR LicenseRef-CYW43-RaspberryPi-Devices"
            ],
            driver["licenses"],
        )

        firmware = components["CYW43xx SoC firmware"]
        self.assertEqual("firmware", firmware["type"])
        self.assertEqual(driver["licenses"], firmware["licenses"])

        shared_bus = components["CYW43 Bluetooth shared-bus port"]
        self.assertEqual(pin["CYW43_SHARED_BUS_REF"], shared_bus["commit"])
        self.assertEqual(
            pin["CYW43_SHARED_BUS_REF"],
            components["Raspberry Pi Pico SDK"]["commit"],
        )

        # The human-readable provenance note must not drift away from the pin.
        upstream_note = (
            ROOT / "src/hal/network/cyw43/UPSTREAM.md"
        ).read_text(encoding="utf-8")
        self.assertIn(pin["CYW43_DRIVER_REF"], upstream_note)
        self.assertIn(pin["CYW43_SHARED_BUS_REF"], upstream_note)

        # test_cyw43_dependency_boundary verifies the checksums themselves; the
        # pin only has to point at a manifest that is actually there.
        self.assertTrue((ROOT / pin["CYW43_DRIVER_MANIFEST"]).is_file())
        self.assertTrue((ROOT / pin["CYW43_DRIVER_DIR"]).is_dir())

    def test_pico_sdk_and_picotool_pins_match_inventory(self) -> None:
        inventory = generate_sbom.load_inventory(ROOT / "security/third_party.json")
        components = {item["name"]: item for item in inventory["components"]}
        for name, conf, prefix, repository in (
            ("Raspberry Pi Pico SDK", "pico_sdk_version.conf", "PICO_SDK", "pico-sdk"),
            ("picotool", "picotool_version.conf", "PICOTOOL", "picotool"),
        ):
            with self.subTest(component=name):
                pin = parse_config(ROOT / "third_party" / conf)
                component = components[name]
                self.assertEqual(pin[f"{prefix}_REF"], component["commit"])
                self.assertEqual(pin[f"{prefix}_VERSION"], component["version"])
                self.assertEqual(
                    f"pkg:github/raspberrypi/{repository}@{pin[f'{prefix}_REF']}",
                    component["purl"],
                )
                self.assertIn(f"third_party/{conf}", component["paths"])

    def test_cppcheck_pin_matches_inventory(self) -> None:
        inventory = generate_sbom.load_inventory(ROOT / "security/third_party.json")
        component = {item["name"]: item for item in inventory["components"]}["Cppcheck"]
        pin = supplied_pin(ROOT, "cppcheck")[0]
        self.assertEqual(pin["CPPCHECK_REF"], component["commit"])
        self.assertEqual(pin["CPPCHECK_VERSION"], component["version"])
        # CVE scanners compare the purl version with affected ranges; a commit
        # there matches every range of an unrelated product named cppcheck.
        self.assertEqual(f"pkg:generic/cppcheck@{pin['CPPCHECK_VERSION']}", component["purl"])
        self.assertIn("third_party/jhbase/third_party/cppcheck_version.conf", component["paths"])

    def test_jhbase_pin_and_supplied_tool_pins_match_inventory(self) -> None:
        inventory = generate_sbom.load_inventory(ROOT / "security/third_party.json")
        components = {item["name"]: item for item in inventory["components"]}
        pin = parse_config(ROOT / "third_party/jhbase_version.conf")
        jhbase = components["jhbase"]
        self.assertEqual(pin["JHBASE_REF"], jhbase["commit"])
        self.assertEqual(pin["JHBASE_VERSION"], jhbase["version"])
        self.assertEqual(f"pkg:github/jaszczurtd/jhbase@{pin['JHBASE_REF']}", jhbase["purl"])
        self.assertEqual(["LicenseRef-JaszczurHAL-Attribution"], jhbase["licenses"])
        self.assertIn("third_party/jhbase_version.conf", jhbase["paths"])
        # Every tool jhbase pins names that pin file, not a copy, as its provenance.
        names = {
            "unity": "Unity",
            "pmd": "PMD Copy/Paste Detector",
            "cppcheck": "Cppcheck",
            "riscv-toolchain": "Raspberry Pi RISC-V toolchain",
            "cmake-minimum": "CMake (oldest supported)",
        }
        for component, supplied in JHBASE_PINS.items():
            with self.subTest(component=component):
                paths = components[names[component]]["paths"]
                self.assertIn(f"third_party/jhbase/third_party/{supplied.config}", paths)
                self.assertNotIn(f"third_party/{supplied.config}", paths)

    def test_esp_idf_tool_inventory_matches_the_pinned_tool_snapshot(self) -> None:
        inventory = generate_sbom.load_inventory(ROOT / "security/third_party.json")
        snapshot_path = ROOT / "security/esp_idf_tools.json"
        snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))
        pin = parse_config(ROOT / "third_party/esp_idf_version.conf")
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
                build_esp_idf._normalized_lf_sha256(tools_json),
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
        btstack = components["BTstack"]
        self.assertEqual(
            "LicenseRef-BTstack-NonCommercial OR "
            "LicenseRef-BTstack-RaspberryPi-Products",
            btstack["licenses"][0]["expression"],
        )
        btstack_paths = {
            ref["url"]
            for ref in btstack["externalReferences"]
            if ref["type"] == "other"
        }
        self.assertIn("third_party/LICENSE.BTstack", btstack_paths)
        self.assertIn("src/hal/bluetooth/LICENSE.RP", btstack_paths)
        bluedroid = components["Bluedroid SBC codec"]
        self.assertEqual(
            "Apache-2.0", bluedroid["licenses"][0]["license"]["id"]
        )
        bluedroid_paths = {
            ref["url"]
            for ref in bluedroid["externalReferences"]
            if ref["type"] == "other"
        }
        self.assertIn("third_party/LICENSE.BLUEDROID-SBC", bluedroid_paths)
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

    def test_cjson_pin_and_release_match_inventory(self) -> None:
        pin = parse_config(ROOT / "third_party/cjson_version.conf")
        inventory = generate_sbom.load_inventory(ROOT / "security/third_party.json")
        cjson = next(item for item in inventory["components"]
                     if item["name"] == "cJSON")
        self.assertEqual(pin["CJSON_REF"], cjson["commit"])
        self.assertEqual(pin["CJSON_VERSION"], cjson["version"])
        # The pin is v1.7.19 plus later fixes; advisories end at v1.7.19.
        self.assertEqual(
            {"version": "1.7.19",
             "commit": "c859b25da02955fef659d658b8f324b5cde87be3"},
            cjson["release"],
        )

    def test_pinned_entries_derive_from_their_pin_files(self) -> None:
        raw = json.loads((ROOT / "security/third_party.json").read_text(encoding="utf-8"))
        pinned = [item for item in raw["components"] if "pin" in item]
        self.assertGreaterEqual(len(pinned), 20)
        referenced = set()
        for item in pinned:
            with self.subTest(component=item["name"]):
                for field in generate_sbom.PIN_FIELDS:
                    if field in item:
                        self.assertRegex(item[field], generate_sbom.PLACEHOLDER.pattern)
                pin = item["pin"]
                referenced.add(pin.get("file") or f"component:{pin['component']}")
                expanded = generate_sbom.expand_pin(item, ROOT)
                self.assertNotIn("pin", expanded)
                self.assertFalse(generate_sbom.PLACEHOLDER.search(expanded["version"]))
                if "commit" in expanded:
                    self.assertRegex(expanded["commit"], r"^[0-9a-f]{40}$")
        # Every managed component's pin is the source of at least one entry.
        for name, spec in component_manager.GIT_COMPONENTS.items():
            expected = f"component:{name}" if name in JHBASE_PINS else f"third_party/{spec.config}"
            self.assertIn(expected, referenced, name)
        for name in JHBASE_PINS:
            self.assertIn(f"component:{name}", referenced, name)
        self.assertIn("third_party/picotool_version.conf", referenced)
        # The SBOM names the pin file an entry was read from, repository-relative.
        unity = generate_sbom.expand_pin(
            next(item for item in pinned if item["name"] == "Unity"), ROOT
        )
        self.assertEqual("third_party/jhbase/third_party/unity_version.conf", unity["pin_file"])
        self.assertEqual(parse_config(supplied_pin(ROOT, "unity")[1])["UNITY_REF"], unity["commit"])

    def test_literal_copies_of_pinned_values_are_rejected(self) -> None:
        raw = json.loads((ROOT / "security/third_party.json").read_text(encoding="utf-8"))
        sdk = next(item for item in raw["components"] if item["name"] == "Raspberry Pi Pico SDK")
        copied = dict(sdk, commit="0" * 40)
        with self.assertRaisesRegex(ValueError, "derived from third_party/pico_sdk_version.conf"):
            generate_sbom.expand_pin(copied, ROOT)
        unknown = dict(sdk, version="{PICO_SDK_NO_SUCH_KEY}")
        with self.assertRaisesRegex(ValueError, "does not define"):
            generate_sbom.expand_pin(unknown, ROOT)
        unpinned = {key: value for key, value in sdk.items() if key != "pin"}
        with self.assertRaisesRegex(ValueError, "without a pin"):
            generate_sbom.expand_pin(unpinned, ROOT)
        bad_pin = dict(sdk, pin={"file": "third_party/missing_version.conf"})
        with self.assertRaisesRegex(ValueError, "missing"):
            generate_sbom.expand_pin(bad_pin, ROOT)

    def test_release_becomes_the_sbom_pedigree(self) -> None:
        component = {
            "name": "Lib",
            "version": "1.0",
            "commit": "a" * 40,
            "licenses": ["MIT"],
            "purl": f"pkg:github/owner/lib@{'a' * 40}",
            "release": {"version": "1.0", "commit": "b" * 40},
        }
        item = generate_sbom.make_component(component)
        self.assertEqual(f"pkg:github/owner/lib@{'a' * 40}", item["purl"])
        self.assertEqual(
            [{"type": "library", "name": "Lib", "version": "1.0",
              "purl": f"pkg:github/owner/lib@{'b' * 40}"}],
            item["pedigree"]["ancestors"],
        )
        del component["release"]
        self.assertNotIn("pedigree", generate_sbom.make_component(component))

    def test_inventory_rejects_unusable_releases(self) -> None:
        inventory = generate_sbom.load_inventory(ROOT / "security/third_party.json")
        cases = {
            "pinned to its release": lambda item: item["release"].update(
                commit=item["commit"]),
            "no exact commit": lambda item: item["release"].update(
                commit=item["release"]["commit"][:7]),
            "needs a version": lambda item: item["release"].update(version=""),
            "needs a GitHub purl": lambda item: item.update(
                purl="pkg:generic/cjson@1.7.19"),
        }
        for message, change in cases.items():
            with self.subTest(message):
                broken = json.loads(json.dumps(inventory))
                change(next(item for item in broken["components"]
                            if item["name"] == "cJSON"))
                with self.assertRaisesRegex(ValueError, message):
                    generate_sbom.validate_inventory(broken)

    def test_sbom_check_detects_missing_and_stale_output(self) -> None:
        inventory = ROOT / "security/third_party.json"
        tools = ROOT / "security/esp_idf_tools.json"
        with tempfile.TemporaryDirectory(prefix="jh-sbom-check-test-") as text:
            output = Path(text) / "sbom.cdx.json"
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(
                io.StringIO()
            ):
                self.assertFalse(generate_sbom.check(inventory, output, tools))

            generate_sbom.generate(inventory, output, tools)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(generate_sbom.check(inventory, output, tools))

            output.write_text("{}\n", encoding="utf-8")
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertFalse(generate_sbom.check(inventory, output, tools))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
