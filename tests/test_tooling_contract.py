#!/usr/bin/env python3
"""Verify shared tooling manifests and their language-specific projections."""

from __future__ import annotations

from pathlib import Path
import sys
import unittest


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

import component_manager
import examples_dispatcher
import generate_board_config
import repository_layout
from tooling_contract import ToolingContractError, load_tooling_contract


class ToolingContractTests(unittest.TestCase):
    def test_loader_rejects_non_local_contract_names(self) -> None:
        for name in ("", "artifacts", "../artifacts.json", "/artifacts.json"):
            with self.subTest(name=name), self.assertRaises(ToolingContractError):
                load_tooling_contract(name)

    def test_artifact_contract_is_shared_with_python_and_windows(self) -> None:
        document = load_tooling_contract("artifacts.json")
        archive = document["archiveMetadata"]
        generated = document["generatedArtifacts"]

        self.assertEqual(repository_layout.ARCHIVE_PIN_FILE, archive["pinFile"])
        self.assertEqual(
            repository_layout.COMPONENT_VERSION_STAMP, archive["versionStamp"]
        )
        self.assertEqual(
            repository_layout.FEATURE_OUTPUTS,
            (Path(generated["featureHeader"]), Path(generated["featureCmake"])),
        )
        bootstrap = (ROOT / "runmefirst.ps1").read_text(encoding="utf-8")
        self.assertIn("config\\tooling\\artifacts.json", bootstrap)
        self.assertIn("$ComponentVersionStamp", bootstrap)
        self.assertNotIn("'.jaszczurhal-component-version'", bootstrap)

    def test_managed_component_catalog_matches_manager_and_launchers(self) -> None:
        document = load_tooling_contract("managed_components.json")
        git_ids = tuple(item["id"] for item in document["gitComponents"])
        tool_ids = tuple(document["toolComponents"])
        launchers = document["launchers"]

        self.assertEqual(set(git_ids), set(component_manager.GIT_COMPONENTS))
        self.assertEqual(tool_ids, component_manager.TOOL_COMPONENTS)
        self.assertEqual(
            tuple(document["defaultOrder"]), component_manager.SOURCE_COMPONENT_ORDER
        )
        self.assertEqual(set(git_ids) | set(tool_ids), set(launchers))
        for component, launcher in launchers.items():
            with self.subTest(component=component):
                self.assertEqual(
                    launcher, component_manager.COMPONENT_LAUNCHERS[component]
                )
                self.assertTrue((SCRIPTS / launcher).is_file())

    def test_example_catalog_matches_dispatcher(self) -> None:
        document = load_tooling_contract("examples.json")
        self.assertEqual(set(document), {"schemaVersion", "examples"})
        self.assertTrue(all("covers" not in item for item in document["examples"]))
        self.assertEqual(document["examples"], examples_dispatcher.EXAMPLES)

    def test_board_component_catalog_matches_python_and_generated_cmake(self) -> None:
        document = load_tooling_contract("board_components.json")
        identifiers = tuple(item["id"] for item in document["components"])
        self.assertEqual(identifiers, tuple(generate_board_config.COMPONENT_REGISTRY))

        generated_path = ROOT / repository_layout.BOARD_COMPONENTS_CMAKE_OUTPUT
        self.assertEqual(
            generated_path.read_text(encoding="utf-8"),
            generate_board_config.render_board_components_cmake(),
        )
        cmake_entrypoint = (ROOT / "cmake/jh_board_components.cmake").read_text(
            encoding="utf-8"
        )
        self.assertIn("generated/jh_board_components_registry.cmake", cmake_entrypoint)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
