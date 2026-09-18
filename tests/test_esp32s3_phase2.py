#!/usr/bin/env python3
"""Host-side tests for the ESP32-S3 Phase 2 integration."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from source_assertions import source_has_fragment


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


esp_idf = load_module("jh_phase2_build_esp_idf", SCRIPTS / "build_esp_idf.py")


def resolve_model(repo_root: Path, project: Path) -> dict:
    return esp_idf.resolve_build_model(
        repo_root,
        project,
        target="esp32s3",
        board="waveshare-esp32-s3-zero",
        project_name="esp32s3_phase2",
        requested_sources=[],
        features=[],
        definitions=[],
    )


def create_phase2_project(project: Path) -> None:
    project.mkdir(parents=True)
    (project / "app.c").write_text(
        "void app_start(void) {}\n"
        "void app_task0(void) {}\n"
        "void app_task1(void) {}\n",
        encoding="utf-8",
    )
    (project / "hal_project_config.h").write_text(
        "#pragma once\n"
        "#define HAL_ENABLE_APP_TASK1 1\n"
        "#define HAL_ENABLE_I2C 1\n"
        "#define HAL_ENABLE_SPI 1\n"
        "#define HAL_ENABLE_STACK_GUARD 1\n"
        "#define HAL_ENABLE_UART 1\n"
        "#define HAL_FREERTOS_TASK0_CORE 0\n"
        "#define HAL_FREERTOS_TASK1_CORE 1\n"
        "#define HAL_FREERTOS_TASK0_STACK 4096u\n"
        "#define HAL_FREERTOS_TASK1_STACK 4096u\n",
        encoding="utf-8",
    )


class Phase2RegistryAndBuildModelTests(unittest.TestCase):
    def test_registry_preserves_phase2_features_in_phase3_boundary(self) -> None:
        target = json.loads(
            (ROOT / "boards/targets/esp32s3.json").read_text(encoding="utf-8")
        )
        board = json.loads(
            (ROOT / "boards/profiles/waveshare-esp32-s3-zero.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(target["status"], "supported")
        self.assertEqual(board["status"], "supported")
        self.assertEqual(
            target["supportedFeatures"],
            [
                "HAL_ENABLE_A7670",
                "HAL_ENABLE_ADC_SCAN",
                "HAL_ENABLE_ADP5360",
                "HAL_ENABLE_APP_TASK1",
                "HAL_ENABLE_BH1750",
                "HAL_ENABLE_BLE",
                "HAL_ENABLE_BSD_SOCKETS",
                "HAL_ENABLE_CAN",
                "HAL_ENABLE_CELLULAR_MODEM",
                "HAL_ENABLE_CJSON",
                "HAL_ENABLE_COMMAND_ROUTER",
                "HAL_ENABLE_CRC",
                "HAL_ENABLE_CRYPTO",
                "HAL_ENABLE_DACLESS",
                "HAL_ENABLE_DHT",
                "HAL_ENABLE_DIGIPOT",
                "HAL_ENABLE_DISPLAY",
                "HAL_ENABLE_DMA_PWM_AUDIO",
                "HAL_ENABLE_DS18B20",
                "HAL_ENABLE_DS3231",
                "HAL_ENABLE_EXTERNAL_ADC",
                "HAL_ENABLE_FAT",
                "HAL_ENABLE_FREERTOS",
                "HAL_ENABLE_GC9A01",
                "HAL_ENABLE_GPS",
                "HAL_ENABLE_HC595",
                "HAL_ENABLE_HD44780",
                "HAL_ENABLE_HTTP_CLIENT",
                "HAL_ENABLE_HTTP_FILES",
                "HAL_ENABLE_HTTP_SERVER",
                "HAL_ENABLE_I2C",
                "HAL_ENABLE_I2C_10BIT",
                "HAL_ENABLE_I2C_SLAVE",
                "HAL_ENABLE_I2C_SLAVE_SNAPSHOT",
                "HAL_ENABLE_ILI9341",
                "HAL_ENABLE_IRSMALL_DECODER",
                "HAL_ENABLE_JPEG",
                "HAL_ENABLE_JPEG_AS_BASE64",
                "HAL_ENABLE_MAX5395",
                "HAL_ENABLE_MAX6675",
                "HAL_ENABLE_MCP23017",
                "HAL_ENABLE_MCP2515",
                "HAL_ENABLE_MCP251XFD",
                "HAL_ENABLE_MCP3221",
                "HAL_ENABLE_MCP401X",
                "HAL_ENABLE_MCP4725",
                "HAL_ENABLE_MCP9600",
                "HAL_ENABLE_MFRC522",
                "HAL_ENABLE_MQTT",
                "HAL_ENABLE_NETWORK_CORE",
                "HAL_ENABLE_NET_COMMANDS",
                "HAL_ENABLE_NET_CONSOLE",
                "HAL_ENABLE_NOTIFY",
                "HAL_ENABLE_NOTIFY_TELEGRAM",
                "HAL_ENABLE_ONEWIRE",
                "HAL_ENABLE_OTA",
                "HAL_ENABLE_PCA9654E",
                "HAL_ENABLE_PCF8563",
                "HAL_ENABLE_PCF8574",
                "HAL_ENABLE_PCNT",
                "HAL_ENABLE_PGA2311",
                "HAL_ENABLE_PN532",
                "HAL_ENABLE_PNG",
                "HAL_ENABLE_PNG_AS_BASE64",
                "HAL_ENABLE_PULSE_CAPTURE",
                "HAL_ENABLE_PWM_FREQ",
                "HAL_ENABLE_RGB_LED",
                "HAL_ENABLE_RTC",
                "HAL_ENABLE_SERIAL_COMMANDS",
                "HAL_ENABLE_SPI",
                "HAL_ENABLE_SSD1306",
                "HAL_ENABLE_SSD1331",
                "HAL_ENABLE_SSD135X",
                "HAL_ENABLE_SSD16XX",
                "HAL_ENABLE_ST7567",
                "HAL_ENABLE_ST7735",
                "HAL_ENABLE_ST7789",
                "HAL_ENABLE_ST7796S",
                "HAL_ENABLE_STACK_GUARD",
                "HAL_ENABLE_STMPE610",
                "HAL_ENABLE_SWSERIAL",
                "HAL_ENABLE_TCP",
                "HAL_ENABLE_TFT",
                "HAL_ENABLE_THERMOCOUPLE",
                "HAL_ENABLE_TIME",
                "HAL_ENABLE_TLS",
                "HAL_ENABLE_TSC2007",
                "HAL_ENABLE_UART",
                "HAL_ENABLE_UC81XX",
                "HAL_ENABLE_UDP",
                "HAL_ENABLE_WEBSOCKET",
                "HAL_ENABLE_WIFI",
                "HAL_ENABLE_WIREGUARD",
            ],
        )

    def test_selected_board_generation_publishes_phase2_gpio_masks(self) -> None:
        generator = esp_idf.generate_board_config
        targets, boards, capabilities = generator.load_registry(ROOT / "boards")
        with tempfile.TemporaryDirectory(prefix="jh-esp32-phase2-masks-") as text:
            output = Path(text) / "generated"
            generator.generate(
                targets["esp32s3"],
                boards["waveshare-esp32-s3-zero"],
                boards,
                capabilities,
                output,
                [
                    "HAL_ENABLE_APP_TASK1",
                    "HAL_ENABLE_I2C",
                    "HAL_ENABLE_SPI",
                    "HAL_ENABLE_UART",
                ],
            )
            config = (output / "jh_board_config.h").read_text(encoding="utf-8")

        expected_masks = {
            "HAL_TARGET_GPIO_VALID_MASK": "0x0001fffffc3fffff",
            "HAL_TARGET_GPIO_INPUT_ONLY_MASK": "0x0000400000000000",
            "HAL_TARGET_GPIO_ADC_MASK": "0x00000000001ffffe",
            "HAL_BOARD_GPIO_EXPOSED_MASK": "0x000027c00007fffe",
            "HAL_BOARD_GPIO_HARD_RESERVED_MASK": "0x0000003e00180000",
            "HAL_BOARD_GPIO_SOFT_RESERVED_MASK": "0x0000000000200001",
        }
        for name, value in expected_masks.items():
            with self.subTest(name=name):
                self.assertIn(f"#define {name} UINT64_C({value})", config)

    def test_phase2_features_resolve_source_and_dependency_graph(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp32-phase2-project-") as text:
            project = Path(text) / "project"
            create_phase2_project(project)
            model = resolve_model(ROOT, project)
        self.assertEqual(
            model["resolvedFeatures"],
            [
                "HAL_ENABLE_APP_TASK1",
                "HAL_ENABLE_FREERTOS",
                "HAL_ENABLE_I2C",
                "HAL_ENABLE_SPI",
                "HAL_ENABLE_STACK_GUARD",
                "HAL_ENABLE_UART",
            ],
        )
        self.assertEqual(model["componentDependencies"], ["freertos"])

        optional_sources = {
            "src/hal/i2c/hal_i2c.cpp",
            "src/hal/impl/esp32/hal_i2c.cpp",
            "src/hal/spi/hal_spi.cpp",
            "src/hal/spi/hal_spi_device.cpp",
            "src/hal/impl/esp32/hal_spi.cpp",
            "src/hal/impl/esp32/hal_uart.cpp",
        }
        baseline_sources, _, baseline_dependencies = (
            esp_idf.resolve_component_build_inputs(["HAL_ENABLE_FREERTOS"])
        )
        self.assertIn("src/hal/impl/esp32/hal_adc.cpp", baseline_sources)
        self.assertTrue(optional_sources.isdisjoint(baseline_sources))
        self.assertEqual(
            set(model["integrationSources"]) - set(baseline_sources),
            optional_sources,
        )
        self.assertEqual(
            set(model["privateComponentDependencies"])
            - set(baseline_dependencies),
            {"esp_driver_i2c", "esp_driver_spi", "esp_driver_uart"},
        )
        for source in model["integrationSources"]:
            self.assertTrue((ROOT / source).is_file(), source)

    def test_model_resolution_is_hermetic_without_ignored_sdk_checkout(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp32-phase2-cold-") as text:
            cold_root = Path(text)
            shutil.copytree(ROOT / "boards", cold_root / "boards")
            shutil.copytree(ROOT / "config", cold_root / "config")
            cold_project = cold_root / "project"
            create_phase2_project(cold_project)

            self.assertFalse((cold_root / "third_party/esp-idf").exists())
            model = resolve_model(cold_root, cold_project)
            self.assertIn("src/hal/impl/esp32/hal_adc.cpp", model["integrationSources"])
            self.assertIn("src/hal/impl/esp32/hal_uart.cpp", model["integrationSources"])
            self.assertFalse((cold_root / "third_party/esp-idf").exists())


class Phase3FixtureSourceTests(unittest.TestCase):
    """The compile-only fixture is judged by its source: it is never run.

    Backend behaviour is covered by test_esp32_backend_lifecycle, which runs
    the ESP32 backends against a fake ESP-IDF.
    """

    def test_compile_fixture_retains_destructive_boot_entry_without_running_it(self) -> None:
        source = (
            ROOT / "tests/fixtures/esp32s3_phase3/link_probe.cpp"
        ).read_text(encoding="utf-8")
        self.assertTrue(source_has_fragment(source, "volatile bool s_run_link_probe;"))
        self.assertTrue(source_has_fragment(source, "if (!s_run_link_probe)"))
        self.assertTrue(source_has_fragment(source, "(void)hal_enter_bootloader();"))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
