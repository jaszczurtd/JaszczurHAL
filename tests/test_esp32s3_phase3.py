#!/usr/bin/env python3
"""Host-side contracts for the complete ESP32-S3 Phase 3 build graph."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
SCRIPTS = ROOT / "scripts"
FIXTURE = ROOT / "tests" / "fixtures" / "esp32s3_phase3"
sys.path.insert(0, str(SCRIPTS))


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


esp_idf = load_module("jh_phase3_build_esp_idf", SCRIPTS / "build_esp_idf.py")


def resolve_model(repo_root: Path, project: Path) -> dict:
    return esp_idf.resolve_build_model(
        repo_root,
        project,
        target="esp32s3",
        board="waveshare-esp32-s3-zero",
        project_name="esp32s3_phase3",
        requested_sources=[],
        features=[],
        definitions=[],
    )


EXPECTED_RESOLVED = [
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
]


class Phase3RegistryAndBuildTests(unittest.TestCase):
    def test_target_allowlist_is_the_exact_supported_feature_closure(self) -> None:
        target_path = ROOT / "boards" / "targets" / "esp32s3.json"
        target = json.loads(target_path.read_text(encoding="utf-8"))
        self.assertEqual(target["supportedFeatures"], EXPECTED_RESOLVED)
        esp_idf.generate_board_config.validate_target(target_path, target)

    def test_fixture_resolves_all_phase3_features(self) -> None:
        model = resolve_model(ROOT, FIXTURE)
        self.assertEqual(model["resolvedFeatures"], EXPECTED_RESOLVED)
        self.assertNotIn("HAL_ENABLE_NETWORK_CORE", model["requestedFeatures"])
        self.assertEqual(model["componentDependencies"], ["freertos"])
        for source in model["integrationSources"]:
            self.assertTrue((ROOT / source).is_file(), source)

    def test_missing_phase2_backends_are_in_the_feature_graph(self) -> None:
        model = resolve_model(ROOT, FIXTURE)
        sources = set(model["integrationSources"])
        dependencies = set(model["privateComponentDependencies"])
        self.assertTrue(
            {
                "src/hal/impl/esp32/hal_i2c_slave.cpp",
                "src/hal/impl/esp32/hal_pcnt.cpp",
                "src/hal/impl/esp32/hal_pwm.cpp",
                "src/hal/impl/esp32/hal_pwm_freq.cpp",
                "src/hal/gpio/hal_rgb_led.cpp",
                "src/hal/gpio/neopixel/jh_neopixel.cpp",
                "src/hal/impl/esp32/hal_rgb_led.cpp",
                "src/hal/impl/esp32/jh_esp32_fault.cpp",
                "src/hal/impl/esp32/jh_esp32_ledc.cpp",
            }
            <= sources
        )
        self.assertTrue(
            {
                "esp_driver_i2c",
                "esp_driver_ledc",
                "esp_driver_pcnt",
                "esp_driver_rmt",
            }
            <= dependencies
        )

    def test_native_network_graph_avoids_public_bsd_symbol_adapter(self) -> None:
        model = resolve_model(ROOT, FIXTURE)
        sources = set(model["integrationSources"])
        dependencies = set(model["privateComponentDependencies"])
        self.assertTrue(
            {
                "src/hal/impl/esp32/esp32_network_backend.cpp",
                "src/hal/impl/esp32/esp32_network_sockets.cpp",
                "src/hal/impl/esp32/esp32_lwip_extension_port.cpp",
                "src/hal/network/hal_net.cpp",
                "src/hal/network/hal_tcp.cpp",
                "src/hal/network/hal_udp.cpp",
                "src/hal/network/hal_wifi.cpp",
            }
            <= sources
        )
        self.assertNotIn(
            "src/hal/network/adapters/bsd/hal_bsd_sockets.cpp", sources
        )
        self.assertNotIn(
            "src/hal/network/jh_public_network_backend_adapter.cpp", sources
        )
        self.assertTrue(
            {"esp_event", "esp_netif", "esp_wifi", "lwip", "nvs_flash"}
            <= dependencies
        )

    def test_services_tls_ota_and_wireguard_are_linked(self) -> None:
        model = resolve_model(ROOT, FIXTURE)
        sources = set(model["integrationSources"])
        self.assertTrue(
            {
                "src/hal/impl/esp32/esp32_secure_random.cpp",
                "src/hal/impl/esp32/hal_ota.cpp",
                "src/hal/impl/esp32/hal_time.cpp",
                "src/hal/network/http/hal_http_client.cpp",
                "src/hal/network/http/hal_http_server.cpp",
                "src/hal/network/mqtt/hal_mqtt.cpp",
                "src/hal/network/ota/jh_ota_protocol.cpp",
                "src/hal/network/tls/BearSSL/jh_bearssl_provider.cpp",
                "src/hal/network/websocket/hal_websocket.cpp",
                "src/hal/network/wireguard/core/wireguardif.c",
            }
            <= sources
        )
        self.assertIn("jh_bearssl", model["privateComponentDependencies"])
        component = (
            ROOT / "cmake/esp-idf/components/jh_bearssl/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        self.assertIn("jh_bearssl_source_manifest", component)
        self.assertIn("idf_component_register", component)

        wireguard_sources, _, _ = esp_idf.resolve_component_build_inputs(
            [
                "HAL_ENABLE_FREERTOS",
                "HAL_ENABLE_NETWORK_CORE",
                "HAL_ENABLE_UDP",
                "HAL_ENABLE_WIFI",
                "HAL_ENABLE_WIREGUARD",
            ]
        )
        self.assertIn(
            "src/hal/impl/esp32/esp32_secure_random.cpp", wireguard_sources
        )

    def test_ota_mutex_allocation_failure_is_fail_closed(self) -> None:
        source = (ROOT / "src/hal/impl/esp32/hal_ota.cpp").read_text(
            encoding="utf-8"
        )
        self.assertNotIn(
            "(void)jh_hal_mutex_create_once(&s_ota.mutex)", source
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool ensure_mutex\(\)\s*\{\s*return "
                r"jh_hal_mutex_create_once\(&s_ota\.mutex\) != nullptr;",
                re.MULTILINE,
            ),
        )
        guarded_returns = {
            "bool hal_ota_set_port(uint16_t port)": "return false;",
            "bool hal_ota_set_hostname(const char *hostname)": "return false;",
            "bool hal_ota_set_password(const char *password)": "return false;",
            "bool hal_ota_on_start(hal_ota_on_start_callback_t callback, void *user)": "return false;",
            "bool hal_ota_on_end(hal_ota_on_end_callback_t callback, void *user)": "return false;",
            "bool hal_ota_on_progress(hal_ota_on_progress_callback_t callback, void *user)": "return false;",
            "bool hal_ota_on_error(hal_ota_on_error_callback_t callback, void *user)": "return false;",
            "bool hal_ota_begin(void)": "return false;",
            "void hal_ota_handle(void)": "return;",
            "bool hal_ota_is_started(void)": "return false;",
            "hal_status_t hal_ota_confirm_boot_ex(void)": "return HAL_ENOMEM;",
            "hal_status_t hal_ota_get_boot_info_ex(hal_ota_boot_info_t *out_info)": "return HAL_ENOMEM;",
        }
        for signature, failure_return in guarded_returns.items():
            start = source.index(signature)
            guard = source.index("if (!ensure_mutex())", start, start + 600)
            self.assertIn(failure_return, source[guard : guard + 100], signature)

    def test_sdkconfig_enforces_network_ota_and_stack_contracts(self) -> None:
        defaults = esp_idf._render_sdkconfig_defaults(resolve_model(ROOT, FIXTURE))
        for line in (
            "CONFIG_LWIP_MAX_SOCKETS=16",
            "CONFIG_LWIP_TCPIP_CORE_LOCKING=y",
            "CONFIG_PARTITION_TABLE_TWO_OTA_LARGE=y",
            "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
            "CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y",
            "CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y",
        ):
            self.assertIn(line, defaults)
        self.assertNotIn("CONFIG_PARTITION_TABLE_SINGLE_APP=y", defaults)

        stack_guard = (
            ROOT / "src/hal/impl/esp32/hal_system.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY", stack_guard)
        self.assertIn("CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK", stack_guard)

    def test_default_timer_handle_has_release_acquire_publication(self) -> None:
        timer_backend = (
            ROOT / "src/hal/impl/esp32/hal_timer.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "__atomic_load_n(&s_default_pool.timer, __ATOMIC_ACQUIRE)",
            timer_backend,
        )
        self.assertIn(
            "__atomic_store_n(&pool.timer, timer, __ATOMIC_RELEASE)",
            timer_backend,
        )

    def test_managed_timer_cross_context_fields_use_atomic_helpers(self) -> None:
        managed_timer = (
            ROOT / "src/hal/timers/hal_timer_ext.cpp"
        ).read_text(encoding="utf-8")
        cross_context_access = re.compile(
            r"\b(?:t|timer)->(?:state|alarm_id|next_fire_us|period_us)\b"
        )
        for line_number, line in enumerate(managed_timer.splitlines(), start=1):
            if cross_context_access.search(line):
                self.assertIn("__atomic_", line, f"non-atomic access at {line_number}")

    def test_phase3_mutex_allocation_is_never_ignored(self) -> None:
        sources = (
            "src/hal/network/hal_wifi.cpp",
            "src/hal/network/hal_tcp.cpp",
            "src/hal/network/hal_udp.cpp",
            "src/hal/network/tls/hal_tls.cpp",
            "src/hal/network/mqtt/hal_mqtt.cpp",
            "src/hal/network/wireguard/hal_wireguard_provider.cpp",
            "src/hal/network/adapters/bsd/hal_bsd_sockets.cpp",
            "src/hal/time/hal_time_ntp.cpp",
            "src/hal/impl/esp32/esp32_network_backend.cpp",
            "src/hal/impl/esp32/esp32_network_sockets.cpp",
        )
        for relative in sources:
            source = (ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn("(void)jh_hal_mutex_create_once", source, relative)
            self.assertIn("HAL_ENOMEM", source, relative)

    def test_rp_timer_uses_stable_dispatch_before_managed_callback(self) -> None:
        source = (ROOT / "src/hal/impl/rp2040/hal_timer.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("timer_dispatch_callback", source)
        self.assertIn("cancelled_id", source)
        self.assertIn("entry->firing", source)
        self.assertIn("__get_current_exception() == 0u", source)

    def test_rp_timer_reused_id_cannot_match_stale_cancellation(self) -> None:
        source = (ROOT / "src/hal/impl/rp2040/hal_timer.cpp").read_text(
            encoding="utf-8"
        )
        add_start = source.index("hal_timer_pool_add_alarm_us_ex(")
        add_end = source.index("bool hal_timer_pool_cancel_alarm(", add_start)
        add_alarm = source[add_start:add_end]
        publish_begin = add_alarm.index(
            "__atomic_add_fetch(&dispatch->publishing"
        )
        sdk_add = add_alarm.index("alarm_pool_add_alarm_in_us(")
        clear_cancellation = add_alarm.index(
            "__atomic_store_n(&entry.cancelled_id, HAL_ALARM_INVALID"
        )
        publish_id = add_alarm.index("__atomic_store_n(&entry.active_id")
        publish_end = add_alarm.index(
            "__atomic_sub_fetch(&dispatch->publishing", publish_id
        )
        self.assertLess(publish_begin, sdk_add)
        self.assertLess(sdk_add, clear_cancellation)
        self.assertLess(clear_cancellation, publish_id)
        self.assertLess(publish_id, publish_end)

        callback_start = source.index("static int64_t timer_dispatch_callback(")
        callback_end = source.index("static inline void timer_store_result(")
        callback = source[callback_start:callback_end]
        self.assertIn("__atomic_load_n(&dispatch->publishing", callback)
        self.assertIn("publishing != 0u ||", callback)

    def test_wifi_underlay_transition_is_transactional(self) -> None:
        facade = (ROOT / "src/hal/network/hal_wifi.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("quiesce_persistent_services", facade)
        self.assertRegex(
            facade,
            re.compile(
                r"const hal_status_t status = ops->set_mode\(mode\);\s*"
                r"if \(status == HAL_OK && mode == HAL_WIFI_MODE_OFF\) \{\s*"
                r"reset_transport_handles\(\);",
                re.MULTILINE,
            ),
        )

        backend = (
            ROOT / "src/hal/impl/esp32/esp32_network_backend.cpp"
        ).read_text(encoding="utf-8")
        attach = backend.index("esp_netif_attach_wifi_station(s_station_netif)")
        published = backend.index("s_station_attached = true", attach)
        self.assertLess(attach, published)

    def test_tls_dependency_is_synchronized_only_when_selected(self) -> None:
        model = resolve_model(ROOT, FIXTURE)
        with mock.patch.object(
            esp_idf.component_manager, "ensure_git_component"
        ) as ensure:
            esp_idf.prepare_feature_dependencies(ROOT, model, verify_only=False)
        ensure.assert_called_once_with("bearssl", ROOT, verify_only=False)

        baseline = dict(model)
        baseline["resolvedFeatures"] = ["HAL_ENABLE_FREERTOS"]
        with mock.patch.object(
            esp_idf.component_manager, "ensure_git_component"
        ) as ensure:
            esp_idf.prepare_feature_dependencies(ROOT, baseline, verify_only=True)
        ensure.assert_not_called()

    def test_model_resolution_is_hermetic_without_sdk_or_bearssl_checkout(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-esp32-phase3-cold-") as text:
            cold_root = Path(text)
            shutil.copytree(ROOT / "boards", cold_root / "boards")
            shutil.copytree(ROOT / "config", cold_root / "config")
            cold_project = cold_root / "tests/fixtures/esp32s3_phase3"
            cold_project.mkdir(parents=True)
            for name in ("app.cpp", "hal_project_config.h"):
                shutil.copy2(FIXTURE / name, cold_project / name)

            model = resolve_model(cold_root, cold_project)
            self.assertEqual(model["resolvedFeatures"], EXPECTED_RESOLVED)
            self.assertFalse((cold_root / "third_party/esp-idf").exists())
            self.assertFalse((cold_root / "third_party/BearSSL").exists())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
