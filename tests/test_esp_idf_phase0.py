#!/usr/bin/env python3
"""Test the isolated ESP-IDF Phase 0 build and artifact contract."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
MODULE_PATH = ROOT / "scripts/build_esp_idf_phase0.py"
SPEC = importlib.util.spec_from_file_location("jh_esp_idf_phase0", MODULE_PATH)
assert SPEC and SPEC.loader
phase0 = importlib.util.module_from_spec(SPEC)
sys.path.insert(0, str(ROOT / "scripts"))
sys.modules[SPEC.name] = phase0
SPEC.loader.exec_module(phase0)


class EnvironmentTests(unittest.TestCase):
    def test_export_parser_expands_posix_path(self) -> None:
        environment = phase0.parse_exported_environment(
            "IDF_PYTHON_ENV_PATH=/idf/python\nPATH=/idf/tools:$PATH\n",
            {"PATH": "/usr/bin", "KEEP": "value"},
            platform="linux",
        )
        self.assertEqual(environment["PATH"], "/idf/tools:/usr/bin")
        self.assertEqual(environment["KEEP"], "value")
        self.assertEqual(
            phase0.idf_python(environment, platform="linux"),
            Path("/idf/python/bin/python"),
        )

    def test_export_parser_expands_windows_path(self) -> None:
        environment = phase0.parse_exported_environment(
            "IDF_PYTHON_ENV_PATH=C:\\idf-python\n"
            "PATH=C:\\idf-tools;%PATH%\n",
            {"PATH": "C:\\Windows"},
            platform="win32",
        )
        self.assertEqual(
            environment["PATH"], "C:\\idf-tools;C:\\Windows"
        )
        self.assertEqual(
            phase0.idf_python(environment, platform="win32"),
            Path("C:\\idf-python") / "Scripts" / "python.exe",
        )


class ArtifactTests(unittest.TestCase):
    def _fixture(self, build_dir: Path) -> None:
        files = {
            "jh_esp_idf_phase0.elf": b"elf",
            "jh_esp_idf_phase0.map": b"map",
            "jh_esp_idf_phase0.bin": b"application",
            "sdkconfig": b"config",
            "build.log": b"build log",
            "bootloader/bootloader.bin": b"bootloader",
            "partition_table/partition-table.bin": b"partition",
        }
        for name, data in files.items():
            path = build_dir / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (build_dir / "project_description.json").write_text(
            json.dumps(
                {
                    "target": "esp32s3",
                    "project_name": "jh_esp_idf_phase0",
                    "app_elf": "jh_esp_idf_phase0.elf",
                    "app_bin": "jh_esp_idf_phase0.bin",
                    "build_components": ["esp_system", "jaszczurhal", "main"],
                }
            ),
            encoding="utf-8",
        )
        (build_dir / "compile_commands.json").write_text(
            json.dumps(
                [
                    {"file": "/project/main/phase0_main.c", "command": "cc"},
                    {
                        "file": "/jh/src/hal/core/jh_handle_pool.cpp",
                        "command": "c++",
                    },
                    {
                        "file": (
                            "/build/generated/jaszczurhal/"
                            "jh_link_contract_definition.c"
                        ),
                        "command": "cc",
                    },
                ]
            ),
            encoding="utf-8",
        )
        (build_dir / "flasher_args.json").write_text(
            json.dumps(
                {
                    "extra_esptool_args": {"chip": "esp32s3"},
                    "flash_settings": {"flash_size": "4MB"},
                    "flash_files": {
                        "0x0": "bootloader/bootloader.bin",
                        "0x8000": "partition_table/partition-table.bin",
                        "0x10000": "jh_esp_idf_phase0.bin",
                    }
                }
            ),
            encoding="utf-8",
        )
        phase0.generate_component_config(
            build_dir,
            target="esp32s3",
            idf_version="6.0.2",
            idf_commit="a" * 40,
        )

    def test_artifact_manifest_is_relocatable_and_complete(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-phase0-") as text:
            build_dir = Path(text)
            self._fixture(build_dir)
            manifest = phase0.validate_artifacts(
                build_dir,
                target="esp32s3",
                idf_version="6.0.2",
                idf_commit="a" * 40,
            )
            self.assertEqual(
                [item["offset"] for item in manifest["flashImages"]],
                ["0x0", "0x8000", "0x10000"],
            )
            self.assertEqual(
                manifest["espIdf"],
                {"version": "6.0.2", "commit": "a" * 40},
            )
            serialized = (build_dir / phase0.MANIFEST_NAME).read_text(
                encoding="utf-8"
            )
            self.assertNotIn(str(build_dir), serialized)
            self.assertEqual(
                manifest["integration"]["component"], "jaszczurhal"
            )

    def test_artifact_manifest_rejects_incomplete_flash_set(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-phase0-") as text:
            build_dir = Path(text)
            self._fixture(build_dir)
            (build_dir / "flasher_args.json").write_text(
                json.dumps(
                    {
                        "extra_esptool_args": {"chip": "esp32s3"},
                        "flash_settings": {"flash_size": "4MB"},
                        "flash_files": {
                            "0x10000": "jh_esp_idf_phase0.bin"
                        },
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                phase0.Phase0Error, "missing required images"
            ):
                phase0.validate_artifacts(
                    build_dir,
                    target="esp32s3",
                    idf_version="6.0.2",
                    idf_commit="a" * 40,
                    write_manifest=False,
                )

    def test_artifact_manifest_rejects_missing_hal_component(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-phase0-") as text:
            build_dir = Path(text)
            self._fixture(build_dir)
            description_path = build_dir / "project_description.json"
            description = json.loads(
                description_path.read_text(encoding="utf-8")
            )
            description["build_components"].remove("jaszczurhal")
            description_path.write_text(
                json.dumps(description), encoding="utf-8"
            )
            with self.assertRaisesRegex(
                phase0.Phase0Error, "does not include the jaszczurhal component"
            ):
                phase0.validate_artifacts(
                    build_dir,
                    target="esp32s3",
                    idf_version="6.0.2",
                    idf_commit="a" * 40,
                    write_manifest=False,
                )

    def test_build_output_must_be_below_dot_build(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-phase0-root-") as text:
            root = Path(text)
            expected = root / ".build/esp-idf/phase0/esp32s3"
            self.assertEqual(
                phase0.resolve_build_dir(root, "", "esp32s3"),
                expected.resolve(),
            )
            with self.assertRaises(phase0.Phase0Error):
                phase0.resolve_build_dir(root, "outside", "esp32s3")


class ProjectFixtureTests(unittest.TestCase):
    def test_component_config_rejects_invalid_provenance(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-phase0-") as text:
            with self.assertRaises(phase0.Phase0Error):
                phase0.generate_component_config(
                    Path(text),
                    target="esp32-s3",
                    idf_version="6.0.2",
                    idf_commit="not-a-commit",
                )

    def test_fixture_uses_standard_esp_idf_component_flow(self) -> None:
        fixture = ROOT / "tests/fixtures/esp_idf_phase0"
        cmake = (fixture / "CMakeLists.txt").read_text(encoding="utf-8")
        main_cmake = (fixture / "main/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        main = (fixture / "main/phase0_main.c").read_text(encoding="utf-8")
        component = (
            ROOT / "cmake/esp-idf/components/jaszczurhal/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        self.assertIn("tools/cmake/project.cmake", cmake)
        self.assertIn("idf_build_set_property(MINIMAL_BUILD ON)", cmake)
        self.assertIn('IDF_TARGET STREQUAL "esp32s3"', cmake)
        self.assertIn("EXTRA_COMPONENT_DIRS", cmake)
        self.assertIn("REQUIRES jaszczurhal", main_cmake)
        self.assertIn("jh_handle_pool.cpp", component)
        self.assertIn("ENV{JH_PHASE0_GENERATED_DIR}", component)
        self.assertIn(
            'file(TO_CMAKE_PATH "$ENV{JH_PHASE0_GENERATED_DIR}"', component
        )
        self.assertIn("void app_main(void)", main)
        self.assertIn("JH_BOARD_CONTRACT_SYMBOL", main)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
