#!/usr/bin/env python3
"""Host-side contract tests for the ESP32-S3 Phase 2 integration."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from source_assertions import (
    source_fragment_position,
    source_has_fragment,
    source_section,
)


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
SCRIPTS = ROOT / "scripts"
FIXTURE = ROOT / "tests" / "hardware" / "esp32s3_phase2"
sys.path.insert(0, str(SCRIPTS))


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


esp_idf = load_module("jh_phase2_build_esp_idf", SCRIPTS / "build_esp_idf.py")
verifier = load_module(
    "jh_esp32s3_phase2_verifier", FIXTURE / "verify_phase2.py"
)


def valid_report() -> dict[str, int | str]:
    return {
        "sequence": 7,
        "target": "esp32s3",
        "board": "waveshare-esp32-s3-zero",
        "core0": 0,
        "core1": 1,
        "task1": 12,
        "system": 1,
        "sync": 1,
        "gpio": 1,
        "irq": 2,
        "irq_isr": 1,
        "adc": 1,
        "adc_low": 120,
        "adc_high": 3900,
        "uart": 1,
        "i2c": 1,
        "i2c_found": 0,
        "spi": 1,
        "timer": 1,
        "timer_count": 3,
        "timer_isr": 1,
        "serial_rx": 1,
        "stack_guard": 1,
        "heap": 185000,
        "temp_centi": 3175,
        "status": "PASS",
    }


def encode_report(report: dict[str, int | str]) -> bytes:
    payload = " ".join(f"{key}={value}" for key, value in report.items())
    return f"I (123) stdout: {verifier.REPORT_PREFIX}{payload}\r\n".encode()


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


class Phase2ReportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.expected = verifier.load_expected_contract(
            "esp32s3", "waveshare-esp32-s3-zero"
        )

    def test_report_parser_and_validator_accept_complete_pass(self) -> None:
        report = verifier.parse_report(encode_report(valid_report()))
        self.assertEqual(report, valid_report())
        verifier.validate_report(report, self.expected)

    def test_parser_ignores_unrelated_or_malformed_lines(self) -> None:
        self.assertIsNone(verifier.parse_report(b"ordinary log output\n"))
        self.assertIsNone(
            verifier.parse_report(b"JH_ESP32_PHASE2 sequence=1 malformed\n")
        )

    def test_validator_rejects_failed_subsystem(self) -> None:
        report = valid_report()
        report["spi"] = 0
        with self.assertRaisesRegex(RuntimeError, "subsystem checks failed"):
            verifier.validate_report(report, self.expected)

    def test_validator_rejects_wrong_task_core(self) -> None:
        report = valid_report()
        report["core1"] = 0
        with self.assertRaisesRegex(RuntimeError, "task affinity mismatch"):
            verifier.validate_report(report, self.expected)

    def test_validator_rejects_insufficient_irq_timer_or_adc_signal(self) -> None:
        invalid = {
            "IRQ": ("irq", 1, "callbacks did not repeat"),
            "timer": ("timer_count", 2, "callbacks did not repeat"),
            "ADC": ("adc_high", valid_report()["adc_low"] + 256, "not distinguishable"),
        }
        for name, (field, value, diagnostic) in invalid.items():
            with self.subTest(name=name):
                report = valid_report()
                report[field] = value
                with self.assertRaisesRegex(RuntimeError, diagnostic):
                    verifier.validate_report(report, self.expected)


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
                "HAL_ENABLE_APP_TASK1",
                "HAL_ENABLE_BLE",
                "HAL_ENABLE_BSD_SOCKETS",
                "HAL_ENABLE_CRC",
                "HAL_ENABLE_CRYPTO",
                "HAL_ENABLE_FREERTOS",
                "HAL_ENABLE_HTTP_CLIENT",
                "HAL_ENABLE_HTTP_FILES",
                "HAL_ENABLE_HTTP_SERVER",
                "HAL_ENABLE_I2C",
                "HAL_ENABLE_I2C_10BIT",
                "HAL_ENABLE_I2C_SLAVE",
                "HAL_ENABLE_MQTT",
                "HAL_ENABLE_NETWORK_CORE",
                "HAL_ENABLE_OTA",
                "HAL_ENABLE_PCNT",
                "HAL_ENABLE_PWM_FREQ",
                "HAL_ENABLE_RGB_LED",
                "HAL_ENABLE_SPI",
                "HAL_ENABLE_STACK_GUARD",
                "HAL_ENABLE_TCP",
                "HAL_ENABLE_TIME",
                "HAL_ENABLE_TLS",
                "HAL_ENABLE_UART",
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

    def test_phase2_fixture_resolves_feature_driven_source_and_dependency_graph(self) -> None:
        model = resolve_model(ROOT, FIXTURE)
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
            cold_project = cold_root / "tests/hardware/esp32s3_phase2"
            cold_project.mkdir(parents=True)
            for name in ("app.cpp", "hal_project_config.h"):
                shutil.copy2(FIXTURE / name, cold_project / name)

            self.assertFalse((cold_root / "third_party/esp-idf").exists())
            model = resolve_model(cold_root, cold_project)
            self.assertIn("src/hal/impl/esp32/hal_adc.cpp", model["integrationSources"])
            self.assertIn("src/hal/impl/esp32/hal_uart.cpp", model["integrationSources"])
            self.assertFalse((cold_root / "third_party/esp-idf").exists())


class Phase2BackendLifecycleTests(unittest.TestCase):
    def test_i2c_slave_events_are_coalesced_without_lossy_isr_queue(self) -> None:
        source = (ROOT / "src/hal/impl/esp32/hal_i2c_slave.cpp").read_text(
            encoding="utf-8"
        )
        self.assertTrue(source_has_fragment(source, "__atomic_fetch_or(&state.pending_events"))
        self.assertTrue(source_has_fragment(source, "xSemaphoreGiveFromISR(state.event_ready"))
        self.assertTrue(source_has_fragment(source, "__atomic_exchange_n(&state.pending_events"))
        self.assertFalse(source_has_fragment(source, "xQueueSendToBackFromISR"))
        worker = source_section(source, "void worker_task(", "void stop_worker(")
        self.assertLess(
            source_fragment_position(worker, "i2c_slave_reset_tx_fifo(state.handle)"),
            source_fragment_position(worker, "write_snapshot(state)"),
        )
        self.assertTrue(source_has_fragment(worker, "bool transmit_pending = false;"))

    def test_i2c_slave_deregister_keeps_valid_isr_context(self) -> None:
        source = (ROOT / "src/hal/impl/esp32/hal_i2c_slave.cpp").read_text(
            encoding="utf-8"
        )
        self.assertTrue(
            source_has_fragment(
                source,
                "i2c_slave_register_event_callbacks(state.handle, &callbacks, &state)",
            )
        )
        self.assertFalse(
            source_has_fragment(
                source,
                "i2c_slave_register_event_callbacks(state.handle, &callbacks, nullptr)",
            )
        )

    def test_i2c_slave_snapshot_preserves_wire_cursor_via_idf_queue(self) -> None:
        source = (ROOT / "src/hal/impl/esp32/hal_i2c_slave.cpp").read_text(
            encoding="utf-8"
        )
        write_call = source_fragment_position(
            source, "const esp_err_t result = i2c_slave_write("
        )
        cursor_update = source_fragment_position(
            source, "state.register_pointer = static_cast<uint16_t>(pointer + written);"
        )
        self.assertLess(write_call, cursor_update)
        self.assertTrue(
            source_has_fragment(source, "state.register_selection_generation == generation")
        )
        self.assertTrue(source_has_fragment(source, "if (written > output)"))
        self.assertTrue(source_has_fragment(source, "if (output == 0u)"))
        self.assertTrue(
            source_has_fragment(source, "static_cast<uint32_t>(output), &written")
        )
        self.assertFalse(
            source_has_fragment(source, "HAL_I2C_SLAVE_REG_MAP_SIZE * 2u")
        )
        self.assertIn("This is the producer cursor", source)
        self.assertIn("accepted but unclocked bytes", source)

        receive = source_section(
            source,
            "bool IRAM_ATTR receive_callback(",
            "bool IRAM_ATTR request_callback(",
        )
        request = source_section(
            source,
            "bool IRAM_ATTR request_callback(",
            "void write_snapshot(",
        )
        self.assertTrue(source_has_fragment(receive, "signal_from_isr(state, kEventResetTx"))
        self.assertTrue(source_has_fragment(request, "signal_from_isr(state, kEventTransmit"))
        self.assertFalse(source_has_fragment(request, "kEventResetTx"))

        public_contract = (
            ROOT / "src/hal/i2c/hal_i2c_slave.h"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "pointer advances for every byte clocked by the",
            public_contract,
        )
        self.assertIn(
            "bytes not yet clocked remain queued across STOP", public_contract
        )
        self.assertIn(
            "local reg_write*() updates to bytes",
            public_contract,
        )

    def test_i2c_slave_driver_lifetime_outlives_worker_and_isr_context(self) -> None:
        source = (ROOT / "src/hal/impl/esp32/hal_i2c_slave.cpp").read_text(
            encoding="utf-8"
        )
        release = source[source_fragment_position(source, "esp_err_t release_slave(") :]
        stop_worker = source_fragment_position(release, "stop_worker(state);")
        delete_driver = source_fragment_position(release, "i2c_del_slave_device(state.handle)")
        clear_driver_handle = source_fragment_position(
            release, "state.handle = nullptr", delete_driver
        )
        delete_event = source_fragment_position(release, "vSemaphoreDelete(state.event_ready)")
        self.assertLess(stop_worker, delete_driver)
        self.assertLess(delete_driver, clear_driver_handle)
        self.assertLess(clear_driver_handle, delete_event)
        self.assertIn("ESP-IDF consumes the device allocation", release)

    def test_ledc_uses_idle_high_for_exact_full_on(self) -> None:
        source = (ROOT / "src/hal/impl/esp32/jh_esp32_ledc.cpp").read_text(
            encoding="utf-8"
        )
        self.assertTrue(source_has_fragment(source, "logical_value >= channel->logical_max"))
        self.assertTrue(source_has_fragment(source, "(ledc_channel_t)channel->channel, 1u"))
        self.assertTrue(source_has_fragment(source, "channel->full_on = full_on"))
        self.assertTrue(source_has_fragment(source, "!full_on && !newly_configured"))
        self.assertTrue(source_has_fragment(source, "ledc_set_duty_and_update("))

    def test_ledc_failed_teardown_retains_hardware_and_logical_ownership(self) -> None:
        ledc = (ROOT / "src/hal/impl/esp32/jh_esp32_ledc.cpp").read_text(
            encoding="utf-8"
        )
        simple = (ROOT / "src/hal/impl/esp32/hal_pwm.cpp").read_text(
            encoding="utf-8"
        )
        frequency = (ROOT / "src/hal/impl/esp32/hal_pwm_freq.cpp").read_text(
            encoding="utf-8"
        )
        release = ledc[source_fragment_position(ledc, "bool jh_esp32_ledc_release(") :]
        deconfigure = source_fragment_position(release, "ledc_channel_config(&config)")
        clear = source_fragment_position(release, "*channel = {};")
        self.assertLess(deconfigure, clear)
        self.assertTrue(
            source_has_fragment(release[:deconfigure], "if (stop_result != ESP_OK)")
        )
        self.assertTrue(
            source_has_fragment(release, "if (ledc_channel_config(&config) != ESP_OK)")
        )
        self.assertTrue(
            source_has_fragment(simple, "if (jh_esp32_ledc_release(channel))")
        )
        self.assertTrue(source_has_fragment(simple, "if (released)"))
        self.assertTrue(
            source_has_fragment(
                frequency, "destroyed = jh_esp32_ledc_release(channel->ledc)"
            )
        )
        self.assertTrue(source_has_fragment(frequency, "if (destroyed)"))

    def test_rmt_teardown_retains_handles_until_successful_delete(self) -> None:
        source = (ROOT / "src/hal/impl/esp32/hal_rgb_led.cpp").read_text(
            encoding="utf-8"
        )
        channel_delete = source_fragment_position(source, "rmt_del_channel(s_channel)")
        channel_clear = source_fragment_position(
            source, "s_channel = nullptr", channel_delete
        )
        encoder_delete = source_fragment_position(source, "rmt_del_encoder(s_encoder)")
        encoder_clear = source_fragment_position(
            source, "s_encoder = nullptr", encoder_delete
        )
        self.assertLess(channel_delete, channel_clear)
        self.assertLess(encoder_delete, encoder_clear)
        self.assertTrue(
            source_has_fragment(
                source, "return jh_esp32_status_from_esp_err(delete_result);"
            )
        )

    def test_fault_init_retries_failed_cross_core_installation(self) -> None:
        source = (ROOT / "src/hal/impl/esp32/jh_esp32_fault.cpp").read_text(
            encoding="utf-8"
        )
        self.assertTrue(source_has_fragment(source, "const esp_err_t result ="))
        self.assertTrue(source_has_fragment(source, "if (result != ESP_OK)"))
        self.assertTrue(source_has_fragment(source, "s_initialized = all_handlers_installed();"))
        self.assertFalse(source_has_fragment(source, "(void)esp_ipc_call_blocking"))

    def test_hardware_fixture_uses_and_releases_dedicated_timer_pool(self) -> None:
        source = (FIXTURE / "app.cpp").read_text(encoding="utf-8")
        self.assertTrue(
            source_has_fragment(source, "s_timer_pool = hal_timer_pool_create_auto(1u);")
        )
        self.assertTrue(source_has_fragment(source, "hal_timer_create(s_timer_pool"))
        self.assertTrue(
            source_has_fragment(source, "hal_timer_pool_destroy(s_timer_pool);")
        )
        self.assertFalse(
            source_has_fragment(source, "hal_timer_pool_create_auto(1u) != nullptr")
        )

    def test_hardware_fixture_requires_implemented_stack_guard(self) -> None:
        source = (FIXTURE / "app.cpp").read_text(encoding="utf-8")
        config = (FIXTURE / "hal_project_config.h").read_text(encoding="utf-8")
        self.assertTrue(source_has_fragment(config, "#define HAL_ENABLE_STACK_GUARD 1"))
        self.assertTrue(source_has_fragment(source, "hal_stack_guard_init_ex()"))
        self.assertFalse(
            source_has_fragment(source, "hal_enter_bootloader() == HAL_EUNSUPPORTED")
        )
        self.assertFalse(
            source_has_fragment(source, "hal_stack_guard_init_ex() == HAL_EUNSUPPORTED")
        )

    def test_compile_fixture_retains_destructive_boot_entry_without_running_it(self) -> None:
        source = (
            ROOT / "tests/fixtures/esp32s3_phase3/link_probe.cpp"
        ).read_text(encoding="utf-8")
        self.assertTrue(source_has_fragment(source, "volatile bool s_run_link_probe;"))
        self.assertTrue(source_has_fragment(source, "if (!s_run_link_probe)"))
        self.assertTrue(source_has_fragment(source, "(void)hal_enter_bootloader();"))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
