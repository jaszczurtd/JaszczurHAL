#!/usr/bin/env python3
"""Test the production ESP-IDF dispatcher and Phase 0 compatibility wrapper."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


esp_idf = load_module("jh_build_esp_idf", SCRIPTS / "build_esp_idf.py")
phase0 = load_module(
    "jh_build_esp_idf_phase0", SCRIPTS / "build_esp_idf_phase0.py"
)
PIN = esp_idf._pin_config(ROOT)


def supply_chain_contract():
    return esp_idf.load_supply_chain_contract(
        ROOT,
        idf_version=PIN["ESP_IDF_VERSION"],
        idf_commit=PIN["ESP_IDF_REF"],
        idf_target="esp32s3",
    )


def binary_tools_document(contract):
    """Build a checkout-independent tools.json shape for unit tests."""
    tools = []
    for entry in contract["binaryTools"]:
        tool = {
            "name": entry["name"],
            "install": "always",
            "supported_targets": contract["snapshot"]["espIdf"]["targets"],
            "versions": [
                {"name": entry["version"], "status": "recommended"}
            ],
            "license": entry["sourceLicense"],
            "info_url": entry["upstream"],
        }
        if entry["name"] == "xtensa-esp-elf":
            tool.update(
                {
                    "version_cmd": ["xtensa-esp-elf-gcc", "--version"],
                    "version_regex": r"(esp-[0-9.]+_[0-9A-Za-z]+)",
                }
            )
        tools.append(tool)
    return {"tools": tools}


class EnvironmentTests(unittest.TestCase):
    def test_export_parser_expands_posix_path(self) -> None:
        environment = esp_idf.parse_exported_environment(
            "IDF_PYTHON_ENV_PATH=/idf/python\nPATH=/idf/tools:$PATH\n",
            {"PATH": "/usr/bin", "KEEP": "value"},
            platform="linux",
        )
        self.assertEqual(environment["PATH"], "/idf/tools:/usr/bin")
        self.assertEqual(environment["KEEP"], "value")
        self.assertEqual(
            esp_idf.idf_python(environment, platform="linux"),
            Path("/idf/python/bin/python"),
        )

    def test_export_parser_expands_windows_path(self) -> None:
        environment = esp_idf.parse_exported_environment(
            "IDF_PYTHON_ENV_PATH=C:\\idf-python\n"
            "PATH=C:\\idf-tools;%PATH%\n",
            {"PATH": "C:\\Windows"},
            platform="win32",
        )
        self.assertEqual(environment["PATH"], "C:\\idf-tools;C:\\Windows")
        self.assertEqual(
            esp_idf.idf_python(environment, platform="win32"),
            Path("C:\\idf-python") / "Scripts" / "python.exe",
        )


class ProjectModelTests(unittest.TestCase):
    def test_discovers_root_and_nested_src_sources(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-project-") as text:
            project = Path(text)
            (project / "app.c").write_text("void app_start(void) {}\n")
            (project / "src").mkdir()
            (project / "src/worker.cpp").write_text("void worker() {}\n")
            (project / ".build/ignored").mkdir(parents=True)
            (project / ".build/ignored/stale.c").write_text("stale\n")
            sources = esp_idf.resolve_project_sources(project, [])
            self.assertEqual(
                [
                    esp_idf._relative_to_project(path, project)
                    for path in sources
                ],
                ["app.c", "src/worker.cpp"],
            )

    def test_rejects_source_outside_project(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-root-") as text:
            root = Path(text)
            project = root / "project"
            project.mkdir()
            outside = root / "outside.c"
            outside.write_text("void outside(void) {}\n")
            with self.assertRaisesRegex(esp_idf.EspIdfError, "escapes"):
                esp_idf.resolve_project_sources(project, [str(outside)])

    def test_model_resolves_supported_target_required_feature(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-project-") as text:
            project = Path(text)
            (project / "app.c").write_text("void app_start(void) {}\n")
            (project / "hal_project_config.h").write_text(
                "#pragma once\n#define HAL_ENABLE_FREERTOS\n",
                encoding="utf-8",
            )
            model = esp_idf.resolve_build_model(
                ROOT,
                project,
                target="esp32s3",
                board="waveshare-esp32-s3-zero",
                project_name="firmware",
                requested_sources=[],
                features=[],
                definitions=["HAL_DEBUG_DEFAULT_BAUD=115200u"],
            )
            self.assertEqual(model["requestedFeatures"], ["HAL_ENABLE_FREERTOS"])
            self.assertEqual(model["resolvedFeatures"], ["HAL_ENABLE_FREERTOS"])
            self.assertNotIn("HAL_ENABLE_FREERTOS", model["compileDefinitions"])
            self.assertIn("HAL_PROVIDE_APP_ENTRY", model["compileDefinitions"])
            self.assertIn("HAL_DEBUG_DEFAULT_BAUD=115200u", model["compileDefinitions"])

    def test_model_rejects_registered_but_unsupported_feature(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-project-") as text:
            project = Path(text)
            (project / "app.c").write_text("void app_start(void) {}\n")
            with self.assertRaisesRegex(
                esp_idf.EspIdfError,
                r"\[JH-CFG-UNSUPPORTED\].*HAL_ENABLE_DAC",
            ):
                esp_idf.resolve_build_model(
                    ROOT,
                    project,
                    target="esp32s3",
                    board="waveshare-esp32-s3-zero",
                    project_name="firmware",
                    requested_sources=[],
                    features=["HAL_ENABLE_DAC"],
                    definitions=[],
                )

    def test_model_rejects_unsupported_resolved_dependency(self) -> None:
        with self.assertRaisesRegex(
            esp_idf.EspIdfError,
            r"\[JH-CFG-UNSUPPORTED\].*resolved=HAL_ENABLE_UART",
        ):
            esp_idf.validate_supported_features(
                {
                    "id": "esp32s3",
                    "supportedFeatures": ["HAL_ENABLE_FREERTOS"],
                },
                ["HAL_ENABLE_FREERTOS"],
                ["HAL_ENABLE_FREERTOS", "HAL_ENABLE_UART"],
            )

    def test_original_esp32_gamepad_selects_bluedroid_hid_inputs(self) -> None:
        project = ROOT / "tests/fixtures/esp32_gamepad"
        model = esp_idf.resolve_build_model(
            ROOT,
            project,
            target="esp32",
            board="esp32-devkitc-v4",
            project_name="esp32_gamepad",
            requested_sources=[],
            features=[],
            definitions=[],
        )
        self.assertEqual(
            model["resolvedFeatures"],
            [
                "HAL_ENABLE_BLUETOOTH_CLASSIC",
                "HAL_ENABLE_BLUETOOTH_GAMEPAD",
                "HAL_ENABLE_BLUETOOTH_HID_HOST",
                "HAL_ENABLE_CRC",
                "HAL_ENABLE_FREERTOS",
            ],
        )
        self.assertTrue(
            {
                "src/hal/bluetooth/hal_gamepad.cpp",
                "src/hal/bluetooth/hal_bluetooth_classic.cpp",
                "src/hal/bluetooth/hal_bluetooth_hid_host.cpp",
                "src/hal/bluetooth/jh_bluetooth_classic_bond_codec.c",
                "src/hal/bluetooth/jh_bluetooth_gamepad_parser.c",
                "src/hal/impl/esp32/jh_esp32_nvs_runtime.cpp",
                "src/hal/impl/esp32/jh_bluetooth_classic_bluedroid_backend.c",
            }.issubset(model["integrationSources"])
        )
        self.assertTrue(
            {"bt", "esp_hid", "nvs_flash"}.issubset(
                model["privateComponentDependencies"]
            )
        )
        defaults = esp_idf._render_sdkconfig_defaults(model)
        for symbol in (
            "CONFIG_BT_BLUEDROID_ENABLED=y",
            "CONFIG_BT_CLASSIC_ENABLED=y",
            "CONFIG_BT_HID_HOST_ENABLED=y",
            "CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y",
        ):
            self.assertIn(symbol, defaults)

    def test_esp32_bluetooth_transport_boundaries_are_enforced(self) -> None:
        project = ROOT / "tests/fixtures/esp32_gamepad"
        with self.assertRaisesRegex(
            esp_idf.EspIdfError, r"\[JH-CFG-UNSUPPORTED\].*HAL_ENABLE_BLE"
        ):
            esp_idf.resolve_build_model(
                ROOT,
                project,
                target="esp32",
                board="esp32-devkitc-v4",
                project_name="esp32_gamepad",
                requested_sources=[],
                features=["HAL_ENABLE_BLE"],
                definitions=[],
            )
        with self.assertRaisesRegex(
            esp_idf.EspIdfError,
            r"\[JH-CFG-UNSUPPORTED\].*HAL_ENABLE_BLUETOOTH_GAMEPAD",
        ):
            esp_idf.resolve_build_model(
                ROOT,
                ROOT / "tests/fixtures/esp32s3_phase3",
                target="esp32s3",
                board="waveshare-esp32-s3-zero",
                project_name="esp32s3_phase3",
                requested_sources=[],
                features=["HAL_ENABLE_BLUETOOTH_GAMEPAD"],
                definitions=[],
            )

    def test_build_output_defaults_below_project_dot_build(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-root-") as text:
            root = Path(text)
            project = root / "project"
            project.mkdir()
            expected = (
                project
                / ".build/esp-idf/esp32s3/waveshare-esp32-s3-zero"
            )
            self.assertEqual(
                esp_idf.resolve_build_dir(
                    root,
                    project,
                    "",
                    "esp32s3",
                    "waveshare-esp32-s3-zero",
                ),
                expected.resolve(),
            )
            with self.assertRaises(esp_idf.EspIdfError):
                esp_idf.resolve_build_dir(
                    root,
                    project,
                    str(root / "outside"),
                    "esp32s3",
                    "waveshare-esp32-s3-zero",
                )


class ArtifactTests(unittest.TestCase):
    def setUp(self) -> None:
        freshness = mock.patch.object(esp_idf, "validate_ninja_freshness")
        self.addCleanup(freshness.stop)
        self.validate_ninja_freshness = freshness.start()

    def _fixture(
        self,
        root: Path,
        *,
        config_header: str | None = None,
        sdkconfig_defaults: str | None = None,
    ):
        project = root / "project"
        project.mkdir()
        app = project / "app.c"
        app.write_text(
            "void app_start(void) {}\nvoid app_task0(void) {}\n",
            encoding="utf-8",
        )
        if config_header is not None:
            (project / "hal_project_config.h").write_text(
                config_header, encoding="utf-8"
            )
        if sdkconfig_defaults is not None:
            (project / "sdkconfig.defaults").write_text(
                sdkconfig_defaults, encoding="utf-8"
            )
        model = esp_idf.resolve_build_model(
            ROOT,
            project,
            target="esp32s3",
            board="waveshare-esp32-s3-zero",
            project_name="firmware",
            requested_sources=[],
            features=[],
            definitions=[],
        )
        build_dir = (
            project / ".build/esp-idf/esp32s3/waveshare-esp32-s3-zero"
        )
        esp_idf.materialize_build_inputs(ROOT, project, build_dir, model)
        contract = supply_chain_contract()
        esptool = next(
            item
            for item in contract["pythonTools"]
            if item["name"] == "esptool"
        )
        compiler_tool = next(
            item
            for item in contract["binaryTools"]
            if item["name"] == "xtensa-esp-elf"
        )
        toolchain = {
            "schemaVersion": 3,
            "cCompiler": {
                "name": "xtensa-esp32s3-elf-gcc",
                "version": (
                    "xtensa-esp-elf-gcc (crosstool-NG "
                    "esp-15.2.0_20251204) 15.2.0"
                ),
                "tool": "xtensa-esp-elf",
                "toolVersion": compiler_tool["version"],
            },
            "cmake": {"name": "cmake", "version": "3.28.3"},
            "ninja": {"name": "ninja", "version": "1.11.1"},
            "idfPython": {"name": "python", "version": "3.12.3"},
            "esptool": {"name": "esptool", "version": esptool["version"]},
            "espIdfTools": {
                "path": "tools/tools.json",
                "sha256": contract["snapshot"]["espIdf"][
                    "toolsJsonSha256"
                ],
            },
            "supplyChain": contract["provenance"],
        }
        (
            build_dir
            / esp_idf.GENERATED_DIR_NAME
            / esp_idf.TOOLCHAIN_PROVENANCE_NAME
        ).write_text(json.dumps(toolchain), encoding="utf-8")
        files = {
            "firmware.elf": b"elf",
            "firmware.map": b"map",
            "firmware.bin": b"application",
            "sdkconfig": (
                b"CONFIG_PARTITION_TABLE_SINGLE_APP=y\n"
                b"CONFIG_PARTITION_TABLE_OFFSET=0x8000\n"
            ),
            "CMakeCache.txt": b"CMAKE_GENERATOR:INTERNAL=Ninja\n",
            "build.log": b"build complete",
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
                    "project_name": "firmware",
                    "app_elf": "firmware.elf",
                    "app_bin": "firmware.bin",
                    "build_components": ["esp_system", "jaszczurhal", "main"],
                }
            ),
            encoding="utf-8",
        )
        generated = build_dir / esp_idf.GENERATED_DIR_NAME
        compile_sources = [
            app,
            *(ROOT / source for source in model["integrationSources"]),
            generated / "jh_link_contract_reference.c",
            generated / "jh_link_contract_definition.c",
        ]
        (build_dir / "compile_commands.json").write_text(
            json.dumps(
                [
                    {"file": str(source), "command": "compiler -c"}
                    for source in compile_sources
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
                        "0x10000": "firmware.bin",
                    },
                }
            ),
            encoding="utf-8",
        )
        return project, build_dir, model

    def test_manifest_is_relocatable_and_complete(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-artifacts-") as text:
            project, build_dir, model = self._fixture(Path(text))
            manifest = esp_idf.validate_artifacts(
                ROOT,
                project,
                build_dir,
                model,
                idf_version=PIN["ESP_IDF_VERSION"],
                idf_commit=PIN["ESP_IDF_REF"],
            )
            self.assertEqual(
                [item["offset"] for item in manifest["flashImages"]],
                ["0x0", "0x8000", "0x10000"],
            )
            self.assertEqual(manifest["board"], "waveshare-esp32-s3-zero")
            self.assertEqual(manifest["toolchain"]["esptool"]["version"], "5.3.1")
            self.assertEqual(
                manifest["toolchain"]["supplyChain"]["snapshot"],
                {
                    "path": "security/esp_idf_tools.json",
                    "schemaVersion": 2,
                    "sha256": esp_idf._sha256(
                        ROOT / "security/esp_idf_tools.json"
                    ),
                },
            )
            self.assertEqual(
                len(manifest["toolchain"]["supplyChain"]["binaryTools"]),
                len(supply_chain_contract()["binaryTools"]),
            )
            self.assertEqual(
                len(manifest["toolchain"]["supplyChain"]["pythonTools"]),
                len(supply_chain_contract()["pythonTools"]),
            )
            self.assertEqual(
                manifest["configuration"]["partitionTable"],
                {
                    "profile": "single-app",
                    "path": "partition_table/partition-table.bin",
                    "offset": "0x8000",
                    "sha256": esp_idf._sha256(
                        build_dir / "partition_table/partition-table.bin"
                    ),
                },
            )
            self.assertEqual(
                manifest["configuration"]["sdkconfigSha256"],
                esp_idf._sha256(build_dir / "sdkconfig"),
            )
            serialized = (build_dir / esp_idf.MANIFEST_NAME).read_text(
                encoding="utf-8"
            )
            self.assertNotIn(str(project), serialized)
            self.assertEqual(
                manifest["integration"]["projectSources"], ["app.c"]
            )
            project_config = json.loads(
                (
                    build_dir
                    / esp_idf.GENERATED_DIR_NAME
                    / esp_idf.PROJECT_CONFIG_JSON_NAME
                ).read_text(encoding="utf-8")
            )
            self.assertEqual(project_config["schemaVersion"], 2)
            self.assertEqual(
                project_config["projectSourceSha256"],
                {"app.c": esp_idf._sha256(project / "app.c")},
            )
            self.assertEqual(project_config["projectIncludeDirs"], ["."])
            self.assertIsNone(project_config["projectConfigHeader"])
            self.validate_ninja_freshness.assert_called_once_with(
                build_dir.resolve(), "firmware", manifest["toolchain"], None
            )

    def test_manifest_accepts_equivalent_canonical_path_aliases(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-artifacts-") as text:
            root = Path(text)
            alias_anchor = root / "path-alias"
            alias_anchor.mkdir()
            aliased_root = alias_anchor / ".."
            project, build_dir, model = self._fixture(
                aliased_root,
                sdkconfig_defaults="CONFIG_COMPILER_OPTIMIZATION_DEBUG=y\n",
            )

            manifest = esp_idf.validate_artifacts(
                ROOT,
                project,
                build_dir.resolve(),
                model,
                idf_version=PIN["ESP_IDF_VERSION"],
                idf_commit=PIN["ESP_IDF_REF"],
            )

            self.assertEqual(
                manifest["integration"]["projectSources"], ["app.c"]
            )
            self.assertEqual(
                manifest["configuration"]["sdkconfigSha256"],
                esp_idf._sha256(build_dir.resolve() / "sdkconfig"),
            )

    def test_manifest_rejects_incomplete_flash_set(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-artifacts-") as text:
            project, build_dir, model = self._fixture(Path(text))
            (build_dir / "flasher_args.json").write_text(
                json.dumps(
                    {
                        "extra_esptool_args": {"chip": "esp32s3"},
                        "flash_settings": {"flash_size": "4MB"},
                        "flash_files": {"0x10000": "firmware.bin"},
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(esp_idf.EspIdfError, "required images"):
                esp_idf.validate_artifacts(
                    ROOT,
                    project,
                    build_dir,
                    model,
                    idf_version=PIN["ESP_IDF_VERSION"],
                    idf_commit=PIN["ESP_IDF_REF"],
                    write_manifest=False,
                )

    def test_manifest_rejects_build_warning(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-artifacts-") as text:
            project, build_dir, model = self._fixture(Path(text))
            (build_dir / "build.log").write_text(
                "compiler warning: unsafe setting\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(esp_idf.EspIdfError, "diagnostics"):
                esp_idf.validate_artifacts(
                    ROOT,
                    project,
                    build_dir,
                    model,
                    idf_version=PIN["ESP_IDF_VERSION"],
                    idf_commit=PIN["ESP_IDF_REF"],
                    write_manifest=False,
                )

    def test_manifest_rejects_partition_offset_mismatch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-artifacts-") as text:
            project, build_dir, model = self._fixture(Path(text))
            (build_dir / "sdkconfig").write_text(
                "CONFIG_PARTITION_TABLE_SINGLE_APP=y\n"
                "CONFIG_PARTITION_TABLE_OFFSET=0x9000\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                esp_idf.EspIdfError, "offset differs from sdkconfig"
            ):
                esp_idf.validate_artifacts(
                    ROOT,
                    project,
                    build_dir,
                    model,
                    idf_version=PIN["ESP_IDF_VERSION"],
                    idf_commit=PIN["ESP_IDF_REF"],
                    write_manifest=False,
                )

    def test_manifest_rejects_current_board_contract_drift(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-artifacts-") as text:
            project, build_dir, model = self._fixture(Path(text))
            drifted_models = []

            psram_drift = copy.deepcopy(model)
            psram_drift["boardDescriptor"]["memory"]["psram"][
                "sizeBytes"
            ] = 4 * 1024 * 1024
            drifted_models.append(psram_drift)

            capability_drift = copy.deepcopy(model)
            capability_drift["boardDescriptor"]["capabilities"][
                "native-wifi"
            ]["present"] = False
            drifted_models.append(capability_drift)

            for drifted in drifted_models:
                with self.subTest(drifted=drifted["boardDescriptor"]):
                    with self.assertRaisesRegex(
                        esp_idf.EspIdfError,
                        "board contract differs from the current",
                    ):
                        esp_idf.validate_artifacts(
                            ROOT,
                            project,
                            build_dir,
                            drifted,
                            idf_version=PIN["ESP_IDF_VERSION"],
                            idf_commit=PIN["ESP_IDF_REF"],
                            write_manifest=False,
                        )

    def test_manifest_rejects_project_input_content_drift(self) -> None:
        inputs = (
            ("app.c", None, None, "void app_start(void) { for (;;) {} }\n"),
            (
                "hal_project_config.h",
                "#pragma once\n#define PROJECT_VALUE 1\n",
                None,
                "#pragma once\n#define PROJECT_VALUE 2\n",
            ),
            (
                "sdkconfig.defaults",
                None,
                "CONFIG_COMPILER_OPTIMIZATION_DEBUG=y\n",
                "CONFIG_COMPILER_OPTIMIZATION_SIZE=y\n",
            ),
        )
        for relative, header, defaults, replacement in inputs:
            with self.subTest(relative=relative):
                with tempfile.TemporaryDirectory(
                    prefix="jh-esp-artifacts-"
                ) as text:
                    project, build_dir, model = self._fixture(
                        Path(text),
                        config_header=header,
                        sdkconfig_defaults=defaults,
                    )
                    (project / relative).write_text(replacement, encoding="utf-8")
                    with self.assertRaisesRegex(
                        esp_idf.EspIdfError,
                        "Generated project config .*Sha256 mismatch|"
                        "projectConfigHeader mismatch",
                    ):
                        esp_idf.validate_artifacts(
                            ROOT,
                            project,
                            build_dir,
                            model,
                            idf_version=PIN["ESP_IDF_VERSION"],
                            idf_commit=PIN["ESP_IDF_REF"],
                            write_manifest=False,
                        )

    def test_manifest_rejects_project_config_path_drift(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-artifacts-") as text:
            project, build_dir, model = self._fixture(
                Path(text),
                sdkconfig_defaults="CONFIG_COMPILER_OPTIMIZATION_DEBUG=y\n",
            )
            (project / "sdkconfig.defaults").unlink()
            with self.assertRaisesRegex(
                esp_idf.EspIdfError, "project CMake config differs"
            ):
                esp_idf.validate_artifacts(
                    ROOT,
                    project,
                    build_dir,
                    model,
                    idf_version=PIN["ESP_IDF_VERSION"],
                    idf_commit=PIN["ESP_IDF_REF"],
                    write_manifest=False,
                )

        with tempfile.TemporaryDirectory(prefix="jh-esp-artifacts-") as text:
            project, build_dir, model = self._fixture(Path(text))
            config_path = (
                build_dir
                / esp_idf.GENERATED_DIR_NAME
                / esp_idf.PROJECT_CONFIG_JSON_NAME
            )
            project_config = json.loads(config_path.read_text(encoding="utf-8"))
            project_config["projectIncludeDirs"] = []
            config_path.write_text(json.dumps(project_config), encoding="utf-8")
            with self.assertRaisesRegex(
                esp_idf.EspIdfError, "projectIncludeDirs mismatch"
            ):
                esp_idf.validate_artifacts(
                    ROOT,
                    project,
                    build_dir,
                    model,
                    idf_version=PIN["ESP_IDF_VERSION"],
                    idf_commit=PIN["ESP_IDF_REF"],
                    write_manifest=False,
                )

    def test_manifest_rejects_supply_chain_provenance_drift(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-artifacts-") as text:
            project, build_dir, model = self._fixture(Path(text))
            provenance_path = (
                build_dir
                / esp_idf.GENERATED_DIR_NAME
                / esp_idf.TOOLCHAIN_PROVENANCE_NAME
            )
            provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
            provenance["supplyChain"]["pythonTools"][0]["version"] = "0.0.0"
            provenance_path.write_text(json.dumps(provenance), encoding="utf-8")
            with self.assertRaisesRegex(
                esp_idf.EspIdfError, "differs from.*supply-chain snapshot"
            ):
                esp_idf.validate_artifacts(
                    ROOT,
                    project,
                    build_dir,
                    model,
                    idf_version=PIN["ESP_IDF_VERSION"],
                    idf_commit=PIN["ESP_IDF_REF"],
                    write_manifest=False,
                )


class NinjaFreshnessTests(unittest.TestCase):
    def _fixture(
        self, root: Path, executable: str = "ninja"
    ) -> tuple[Path, dict[str, object]]:
        build_dir = root / "build"
        build_dir.mkdir()
        ninja = root / "tools" / executable
        ninja.parent.mkdir()
        ninja.write_text("test executable placeholder\n", encoding="utf-8")
        (build_dir / "CMakeCache.txt").write_text(
            "CMAKE_GENERATOR:INTERNAL=Ninja\n"
            f"CMAKE_MAKE_PROGRAM:FILEPATH={ninja}\n",
            encoding="utf-8",
        )
        return build_dir, {"ninja": {"name": ninja.name, "version": "1.12.1"}}

    def test_ninja_dry_run_accepts_only_a_clean_elf_graph(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-ninja-") as text:
            build_dir, provenance = self._fixture(Path(text))
            clean = mock.Mock(
                returncode=0,
                stdout="ninja: no work to do.\r\n",
            )
            environment = {"PATH": "/idf/tools", "JH_TEST_SENTINEL": "yes"}
            with mock.patch.object(
                esp_idf.subprocess, "run", return_value=clean
            ) as run:
                esp_idf.validate_ninja_freshness(
                    build_dir, "firmware", provenance, environment
                )
            self.assertEqual(
                run.call_args.args[0],
                [str(Path(text) / "tools/ninja"), "-n", "firmware.elf"],
            )
            self.assertEqual(run.call_args.kwargs["cwd"], build_dir)
            self.assertEqual(
                run.call_args.kwargs["env"]["JH_ESP_IDF_GENERATED_DIR"],
                str(build_dir / esp_idf.GENERATED_DIR_NAME),
            )
            self.assertEqual(
                run.call_args.kwargs["env"]["JH_ESP_IDF_PROJECT_CONFIG"],
                str(
                    build_dir
                    / esp_idf.GENERATED_DIR_NAME
                    / esp_idf.PROJECT_CONFIG_CMAKE_NAME
                ),
            )
            self.assertEqual(
                run.call_args.kwargs["env"]["JH_TEST_SENTINEL"], "yes"
            )
            self.assertIs(
                run.call_args.kwargs["stderr"], esp_idf.subprocess.STDOUT
            )
            self.assertEqual(run.call_args.kwargs["encoding"], "utf-8")
            self.assertEqual(run.call_args.kwargs["errors"], "replace")

            pending = mock.Mock(
                returncode=0,
                stdout="[1/1] compiler -c project/include/changed.h\n",
            )
            with mock.patch.object(
                esp_idf.subprocess, "run", return_value=pending
            ):
                with self.assertRaisesRegex(
                    esp_idf.EspIdfError, "Ninja reports pending work"
                ):
                    esp_idf.validate_ninja_freshness(
                        build_dir, "firmware", provenance
                    )

    def test_ninja_dry_run_accepts_windows_executable_identity(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-ninja-win-") as text:
            build_dir, provenance = self._fixture(Path(text), "ninja.exe")
            clean = mock.Mock(returncode=0, stdout="ninja: no work to do.\r\n")
            with mock.patch.object(
                esp_idf.subprocess, "run", return_value=clean
            ) as run:
                esp_idf.validate_ninja_freshness(
                    build_dir, "firmware", provenance, {"PATH": "C:\\idf"}
                )
            self.assertEqual(
                run.call_args.args[0][0], str(Path(text) / "tools/ninja.exe")
            )


class SupplyChainContractTests(unittest.TestCase):
    def test_snapshot_is_bound_to_pin_target_and_normalized_licenses(self) -> None:
        contract = supply_chain_contract()
        gcc = next(
            item
            for item in contract["binaryTools"]
            if item["name"] == "xtensa-esp-elf"
        )
        self.assertEqual(gcc["sourceLicense"], "GPL-3.0-with-GCC-exception")
        self.assertEqual(
            gcc["license"],
            "GPL-3.0-or-later WITH GCC-exception-3.1",
        )
        with self.assertRaisesRegex(esp_idf.EspIdfError, "version mismatch"):
            esp_idf.load_supply_chain_contract(
                ROOT,
                idf_version="0.0.0",
                idf_commit=PIN["ESP_IDF_REF"],
                idf_target="esp32s3",
            )
        with self.assertRaisesRegex(esp_idf.EspIdfError, "commit mismatch"):
            esp_idf.load_supply_chain_contract(
                ROOT,
                idf_version=PIN["ESP_IDF_VERSION"],
                idf_commit="0" * 40,
                idf_target="esp32s3",
            )
        with self.assertRaisesRegex(esp_idf.EspIdfError, "does not cover target"):
            esp_idf.load_supply_chain_contract(
                ROOT,
                idf_version=PIN["ESP_IDF_VERSION"],
                idf_commit=PIN["ESP_IDF_REF"],
                idf_target="esp32c6",
            )

    def test_binary_contract_checks_recommendation_license_and_upstream(self) -> None:
        contract = supply_chain_contract()
        tools_document = binary_tools_document(contract)
        esp_idf.validate_binary_tools_contract(tools_document, contract)
        tool_index = next(
            index
            for index, item in enumerate(tools_document["tools"])
            if item["name"] == "xtensa-esp-elf"
        )

        version_drift = copy.deepcopy(tools_document)
        version_drift["tools"][tool_index]["versions"][0]["status"] = "supported"
        with self.assertRaisesRegex(
            esp_idf.EspIdfError, "recommended version drift"
        ):
            esp_idf.validate_binary_tools_contract(version_drift, contract)

        license_drift = copy.deepcopy(tools_document)
        license_drift["tools"][tool_index]["license"] = "MIT"
        with self.assertRaisesRegex(esp_idf.EspIdfError, "source license drift"):
            esp_idf.validate_binary_tools_contract(license_drift, contract)

        upstream_drift = copy.deepcopy(tools_document)
        upstream_drift["tools"][tool_index]["info_url"] = "https://invalid.test"
        with self.assertRaisesRegex(esp_idf.EspIdfError, "upstream drift"):
            esp_idf.validate_binary_tools_contract(upstream_drift, contract)

        incomplete_snapshot = copy.deepcopy(contract)
        incomplete_snapshot["binaryTools"] = [
            item
            for item in incomplete_snapshot["binaryTools"]
            if item["name"] != "esp-rom-elfs"
        ]
        with self.assertRaisesRegex(
            esp_idf.EspIdfError, "binary tool coverage drift"
        ):
            esp_idf.validate_binary_tools_contract(
                tools_document, incomplete_snapshot
            )

    def test_executed_compiler_is_bound_to_binary_tool_contract(self) -> None:
        contract = supply_chain_contract()
        tools_document = binary_tools_document(contract)
        compiler = Path("xtensa-esp32s3-elf-gcc")
        banner = (
            "xtensa-esp-elf-gcc (crosstool-NG "
            "esp-15.2.0_20251204) 15.2.0"
        )
        self.assertEqual(
            esp_idf.validate_compiler_contract(
                compiler, banner, tools_document, contract
            ),
            ("xtensa-esp-elf", "esp-15.2.0_20251204"),
        )
        with self.assertRaisesRegex(
            esp_idf.EspIdfError, "Executed C compiler version drift"
        ):
            esp_idf.validate_compiler_contract(
                compiler,
                banner.replace("esp-15.2.0_20251204", "esp-14.2.0_unknown"),
                tools_document,
                contract,
            )

    def test_tools_json_digest_drift_is_rejected(self) -> None:
        contract = supply_chain_contract()
        with tempfile.TemporaryDirectory(prefix="jh-esp-tools-drift-") as text:
            idf_dir = Path(text)
            (idf_dir / "tools").mkdir()
            (idf_dir / "tools/tools.json").write_text("{}\n", encoding="utf-8")
            with self.assertRaisesRegex(esp_idf.EspIdfError, "digest drift"):
                esp_idf.verify_esp_idf_tools_contract(idf_dir, contract)

    def test_tools_json_digest_accepts_crlf_but_rejects_content_drift(self) -> None:
        contract = supply_chain_contract()
        tools_document = binary_tools_document(contract)
        content = (
            json.dumps(tools_document, indent=2, sort_keys=True) + "\n"
        ).encode("utf-8")
        with tempfile.TemporaryDirectory(prefix="jh-esp-tools-crlf-") as text:
            idf_dir = Path(text)
            tools_path = idf_dir / "tools/tools.json"
            tools_path.parent.mkdir()
            tools_path.write_bytes(content)
            expected_digest = esp_idf._sha256(tools_path)
            fixture_contract = copy.deepcopy(contract)
            fixture_contract["snapshot"]["espIdf"]["toolsJsonSha256"] = (
                expected_digest
            )

            tools_path.write_bytes(content.replace(b"\n", b"\r\n"))
            self.assertEqual(
                esp_idf._normalized_lf_sha256(tools_path), expected_digest
            )
            self.assertEqual(
                esp_idf.verify_esp_idf_tools_contract(
                    idf_dir, fixture_contract
                ),
                tools_document,
            )

            tools_path.write_bytes(
                content.replace(b'"tools"', b'"changed"', 1).replace(
                    b"\n", b"\r\n"
                )
            )
            with self.assertRaisesRegex(esp_idf.EspIdfError, "digest drift"):
                esp_idf.verify_esp_idf_tools_contract(
                    idf_dir, fixture_contract
                )

    def test_all_snapshot_python_distributions_are_checked(self) -> None:
        contract = supply_chain_contract()
        versions = {
            item["name"]: item["version"] for item in contract["pythonTools"]
        }
        completed = mock.Mock(
            returncode=0,
            stdout=json.dumps({"missing": [], "versions": versions}),
            stderr="",
        )
        with mock.patch.object(
            esp_idf.subprocess, "run", return_value=completed
        ) as run:
            installed = esp_idf.collect_python_tools(
                Path("/idf/python"), {}, contract
            )
        queried_names = json.loads(run.call_args.args[0][-1])
        self.assertEqual(queried_names, list(versions))
        self.assertEqual(installed, contract["provenance"]["pythonTools"])

        drifted_versions = dict(versions)
        drifted_versions["esptool"] = "0.0.0"
        completed.stdout = json.dumps(
            {"missing": [], "versions": drifted_versions}
        )
        with mock.patch.object(esp_idf.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(esp_idf.EspIdfError, "version drift"):
                esp_idf.collect_python_tools(Path("/idf/python"), {}, contract)


class CommandContractTests(unittest.TestCase):
    def _run_mocked_main(self, root: Path, action: str):
        project = root / "project"
        build_dir = root / ".build/esp-idf/firmware"
        environment = {
            "IDF_PATH": str(root / "idf"),
            "IDF_PYTHON_ENV_PATH": str(root / "python"),
            "PATH": "/idf/tools",
        }
        model = {
            "target": "esp32s3",
            "board": "waveshare-esp32-s3-zero",
            "idfTarget": "esp32s3",
        }
        manifest = {
            "target": model["target"],
            "board": model["board"],
            "flashImages": [{"path": "firmware.bin"}],
        }
        arguments = [
            action,
            "--project",
            str(project),
            "--repo-root",
            str(root),
            "--output",
            str(build_dir),
        ]
        if action == "flash":
            arguments.extend(("--port", "/dev/ttyACM0"))
        with (
            mock.patch.object(
                esp_idf, "resolve_project_dir", return_value=project
            ),
            mock.patch.object(
                esp_idf, "normalize_project_name", return_value="firmware"
            ),
            mock.patch.object(
                esp_idf, "resolve_build_model", return_value=model
            ),
            mock.patch.object(
                esp_idf, "resolve_build_dir", return_value=build_dir
            ),
            mock.patch.object(
                esp_idf,
                "_pin_config",
                return_value={
                    "ESP_IDF_TARGETS": "esp32s3",
                    "ESP_IDF_VERSION": "6.0.2",
                    "ESP_IDF_REF": "a" * 40,
                },
            ),
            mock.patch.object(esp_idf, "prepare_feature_dependencies"),
            mock.patch.object(esp_idf, "materialize_build_inputs"),
            mock.patch.object(
                esp_idf, "prepare_sdk", return_value=root / "idf"
            ),
            mock.patch.object(
                esp_idf,
                "exported_environment",
                return_value=environment,
            ),
            mock.patch.object(esp_idf, "run_idf") as run_idf,
            mock.patch.object(esp_idf, "_validate_clean_build_log"),
            mock.patch.object(esp_idf, "collect_toolchain_provenance"),
            mock.patch.object(
                esp_idf, "validate_artifacts", return_value=manifest
            ) as validate,
            mock.patch("builtins.print"),
        ):
            result = esp_idf.main(arguments)
        return result, environment, run_idf, validate

    def test_main_passes_exported_environment_to_artifact_validation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-main-") as text:
            result, environment, run_idf, validate = self._run_mocked_main(
                Path(text), "build"
            )
            self.assertEqual(result, 0)
            self.assertIs(run_idf.call_args.args[4], environment)
            self.assertIs(
                validate.call_args.kwargs["build_environment"], environment
            )

    def test_main_exports_environment_before_pre_flash_validation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-main-") as text:
            result, environment, run_idf, validate = self._run_mocked_main(
                Path(text), "flash"
            )
            self.assertEqual(result, 0)
            self.assertEqual(validate.call_count, 2)
            for call in validate.call_args_list:
                self.assertIs(call.kwargs["build_environment"], environment)
            self.assertIs(run_idf.call_args.args[4], environment)
            self.assertEqual(run_idf.call_args.args[5], "flash")

    def test_run_idf_preserves_merged_output_and_failure_log(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-run-") as text:
            root = Path(text)
            build_dir = root / "build"
            completed = mock.Mock(returncode=7, stdout="configure\ncompile\n")
            environment = {
                "IDF_PYTHON_ENV_PATH": str(root / "python"),
                "PATH": "/idf/tools",
            }
            with mock.patch.object(
                esp_idf.subprocess, "run", return_value=completed
            ) as run, mock.patch("builtins.print") as output:
                with self.assertRaisesRegex(
                    esp_idf.EspIdfError, "failed with exit code 7"
                ):
                    esp_idf.run_idf(
                        root,
                        build_dir,
                        root / "idf",
                        "esp32s3",
                        environment,
                        "build",
                    )

            self.assertEqual(
                (build_dir / esp_idf.LOG_NAME).read_text(encoding="utf-8"),
                "configure\ncompile\n",
            )
            self.assertIs(run.call_args.kwargs["stdout"], subprocess.PIPE)
            self.assertIs(run.call_args.kwargs["stderr"], subprocess.STDOUT)
            self.assertEqual(run.call_args.kwargs["encoding"], "utf-8")
            self.assertEqual(run.call_args.kwargs["errors"], "replace")
            self.assertEqual(
                run.call_args.kwargs["env"]["JH_ESP_IDF_GENERATED_DIR"],
                str(build_dir / esp_idf.GENERATED_DIR_NAME),
            )
            output.assert_called_once_with(
                "configure\ncompile\n", end="", flush=True
            )

    def test_run_idf_start_failure_is_persisted(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-run-") as text:
            root = Path(text)
            build_dir = root / "build"
            environment = {"IDF_PYTHON_ENV_PATH": str(root / "python")}
            with mock.patch.object(
                esp_idf.subprocess,
                "run",
                side_effect=OSError("executable unavailable"),
            ), mock.patch("builtins.print"):
                with self.assertRaisesRegex(
                    esp_idf.EspIdfError, "Cannot start ESP-IDF build"
                ):
                    esp_idf.run_idf(
                        root,
                        build_dir,
                        root / "idf",
                        "esp32s3",
                        environment,
                        "build",
                    )
            self.assertIn(
                "executable unavailable",
                (build_dir / esp_idf.LOG_NAME).read_text(encoding="utf-8"),
            )

    def test_failure_report_records_stage(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-failure-") as text:
            build_dir = Path(text) / "build"
            with mock.patch("builtins.print") as output:
                esp_idf._report_failure(
                    "artifact-validation", ValueError("stale graph"), build_dir
                )
            self.assertEqual(
                (
                    build_dir / esp_idf.FAILURE_DIAGNOSTIC_NAME
                ).read_text(encoding="utf-8"),
                "build_esp_idf.py: stage=artifact-validation failed: "
                "stale graph\n",
            )
            output.assert_called_once_with(
                "build_esp_idf.py: stage=artifact-validation failed: "
                "stale graph",
                file=sys.stderr,
                flush=True,
            )

    def test_failure_report_does_not_mask_diagnostic_write_error(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-failure-") as text:
            build_dir = Path(text) / "build"
            with mock.patch.object(
                Path,
                "write_text",
                side_effect=UnicodeError("encoding failure"),
            ), mock.patch("builtins.print") as output:
                esp_idf._report_failure(
                    "artifact-validation", ValueError("stale graph"), build_dir
                )

            self.assertEqual(output.call_count, 2)
            self.assertEqual(
                output.call_args_list[0],
                mock.call(
                    "build_esp_idf.py: stage=artifact-validation failed: "
                    "stale graph",
                    file=sys.stderr,
                    flush=True,
                ),
            )
            self.assertIn(
                "cannot persist failure diagnostic: encoding failure",
                output.call_args_list[1].args[0],
            )

    def test_collects_tool_versions_without_publishing_absolute_paths(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp-tools-") as text:
            root = Path(text)
            idf_dir = root / "idf"
            build_dir = root / "build"
            compiler = root / "toolchain/xtensa-esp32s3-elf-gcc"
            compiler.parent.mkdir(parents=True)
            compiler.write_text("tool", encoding="utf-8")
            cmake = root / "tools/cmake"
            ninja = root / "tools/ninja"
            cmake.parent.mkdir(parents=True)
            cmake.write_text("tool", encoding="utf-8")
            ninja.write_text("tool", encoding="utf-8")
            (idf_dir / "tools").mkdir(parents=True)
            tools_document = binary_tools_document(supply_chain_contract())
            tools_path = idf_dir / "tools/tools.json"
            tools_content = (
                json.dumps(tools_document, sort_keys=True) + "\n"
            ).encode("utf-8")
            tools_path.write_bytes(tools_content)
            build_dir.mkdir()
            (build_dir / esp_idf.GENERATED_DIR_NAME).mkdir(parents=True)
            (build_dir / "project_description.json").write_text(
                json.dumps({"c_compiler": str(compiler)}), encoding="utf-8"
            )
            (build_dir / "CMakeCache.txt").write_text(
                "CMAKE_GENERATOR:INTERNAL=Ninja\n"
                f"CMAKE_COMMAND:INTERNAL={cmake}\n"
                f"CMAKE_MAKE_PROGRAM:FILEPATH={ninja}\n",
                encoding="utf-8",
            )
            environment = {"IDF_PYTHON_ENV_PATH": str(root / "python")}
            contract = supply_chain_contract()
            fixture_contract = copy.deepcopy(contract)
            fixture_contract["snapshot"]["espIdf"]["toolsJsonSha256"] = (
                esp_idf._sha256(tools_path)
            )
            tools_path.write_bytes(tools_content.replace(b"\n", b"\r\n"))
            python_versions = {
                item["name"]: item["version"]
                for item in contract["pythonTools"]
            }
            completed = [
                mock.Mock(
                    returncode=0,
                    stdout=(
                        "xtensa-esp-elf-gcc (crosstool-NG "
                        "esp-15.2.0_20251204) 15.2.0\n"
                    ),
                    stderr="",
                ),
                mock.Mock(
                    returncode=0,
                    stdout="cmake version 3.28.3\n",
                    stderr="",
                ),
                mock.Mock(
                    returncode=0,
                    stdout="1.11.1\n",
                    stderr="",
                ),
                mock.Mock(
                    returncode=0,
                    stdout="Python 3.12.3\n",
                    stderr="",
                ),
                mock.Mock(
                    returncode=0,
                    stdout="esptool v5.3.1\n",
                    stderr="",
                ),
                mock.Mock(
                    returncode=0,
                    stdout=json.dumps(
                        {"missing": [], "versions": python_versions}
                    ),
                    stderr="",
                ),
            ]
            with mock.patch.object(
                esp_idf, "load_supply_chain_contract", return_value=fixture_contract
            ), mock.patch.object(esp_idf.subprocess, "run", side_effect=completed):
                provenance = esp_idf.collect_toolchain_provenance(
                    idf_dir,
                    environment,
                    build_dir,
                    repo_root=ROOT,
                    idf_version=PIN["ESP_IDF_VERSION"],
                    idf_commit=PIN["ESP_IDF_REF"],
                    idf_target="esp32s3",
                )
            serialized = json.dumps(provenance)
            self.assertNotIn(str(root), serialized)
            self.assertNotIn(str(ROOT), serialized)
            self.assertEqual(provenance["esptool"]["version"], "5.3.1")
            self.assertEqual(provenance["cmake"]["version"], "3.28.3")
            self.assertEqual(provenance["ninja"]["version"], "1.11.1")
            self.assertEqual(provenance["idfPython"]["version"], "3.12.3")
            self.assertEqual(
                provenance["cCompiler"]["toolVersion"],
                "esp-15.2.0_20251204",
            )
            self.assertEqual(
                provenance["espIdfTools"]["sha256"],
                fixture_contract["snapshot"]["espIdf"]["toolsJsonSha256"],
            )
            self.assertEqual(
                provenance["supplyChain"], contract["provenance"]
            )

    def test_idf_commands_expose_build_and_port_qualified_flash(self) -> None:
        environment = {"IDF_PYTHON_ENV_PATH": "/idf/python"}
        build = esp_idf.idf_command(
            Path("/idf"),
            environment,
            Path("/template"),
            Path("/build"),
            "esp32s3",
            "build",
        )
        flash = esp_idf.idf_command(
            Path("/idf"),
            environment,
            Path("/template"),
            Path("/build"),
            "esp32s3",
            "flash",
            port="/dev/ttyACM0",
        )
        self.assertEqual(build[-1], "build")
        self.assertEqual(flash[-3:], ["-p", "/dev/ttyACM0", "flash"])
        self.assertIn("CMAKE_EXPORT_COMPILE_COMMANDS=ON", build)

    def test_phase0_wrapper_only_forwards_to_production_runner(self) -> None:
        fixture = ROOT / "tests/fixtures/esp_idf_phase0"
        self.assertFalse((fixture / "CMakeLists.txt").exists())
        self.assertFalse((fixture / "main/CMakeLists.txt").exists())
        self.assertFalse((fixture / "sdkconfig.defaults").exists())
        with mock.patch.object(phase0.build_esp_idf, "main", return_value=0) as main:
            result = phase0.main(
                ["--repo-root", str(ROOT), "--artifacts-only"]
            )
        self.assertEqual(result, 0)
        forwarded = main.call_args.args[0]
        self.assertEqual(forwarded[0], "artifacts")
        self.assertIn("main/phase0_main.c", forwarded)
        self.assertIn("jh_esp_idf_phase0", forwarded)

    def test_templates_use_neutral_generated_contract(self) -> None:
        component = (
            ROOT / "cmake/esp-idf/components/jaszczurhal/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        project = (ROOT / "cmake/esp-idf/project/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        main = (ROOT / "cmake/esp-idf/project/main/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        combined = component + project + main
        self.assertNotIn("PHASE0", combined)
        self.assertIn("JH_ESP_IDF_PROJECT_CONFIG", combined)
        self.assertIn("JH_ESP_IDF_GENERATED_DIR", combined)
        self.assertIn("JH_ESP_IDF_COMPONENT_SOURCES", component)
        self.assertIn("JH_ESP_IDF_COMPONENT_PRIV_REQUIRES", component)
        self.assertIn("jh_link_contract_reference.c", main)
        self.assertIn("jaszczurhal esp_system esp_psram spi_flash", main)

    def test_generic_cmake_dispatcher_delegates_esp_idf_before_project(self) -> None:
        dispatcher = (
            ROOT / "cmake/jh_firmware_project/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        self.assertIn('JH_TARGET STREQUAL "esp32s3"', dispatcher)
        self.assertIn("scripts/build_esp_idf.py build --project", dispatcher)
        self.assertIn("ESP-IDF pre-project bootstrap", dispatcher)

        with tempfile.TemporaryDirectory(prefix="jh-esp-dispatch-") as text:
            project = Path(text)
            (project / "app.c").write_text("void app_start(void) {}\n")
            completed = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(ROOT / "cmake/jh_firmware_project"),
                    "-B",
                    str(project / ".build/dispatcher"),
                    "-DJH_TARGET=esp32s3",
                    "-DJH_BOARD=waveshare-esp32-s3-zero",
                    f"-DJH_PROJECT_DIR={project}",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            output = completed.stdout + completed.stderr
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("ESP-IDF pre-project bootstrap", output)
            self.assertNotIn("Unsupported JH_TARGET='esp32s3'", output)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
