/* The internal HAL headers can be used from C: every function they declare
 * links from this C translation unit (the headers give them C linkage). The
 * target-only headers are linked from C in their own backend tests
 * (test_esp32_backend_lifecycle, test_stm32_pwm_clock,
 * test_jh_littlefs_lfs_provider). */
#include "hal/analog/digipot/hal_digipot_ops.h"
#include "hal/analog/jh_pulse_capture_backend.h"
#include "hal/audio/pga2311/pga2311_driver.h"
#include "hal/commands/jh_command_router_internal.h"
#include "hal/display/hal_display_internal.h"
#include "hal/i2c/hal_i2c_internal.h"
#include "hal/network/tls/BearSSL/jh_bearssl_bsd_io.h"
#include "hal/network/tls/BearSSL/jh_bearssl_engine.h"
#include "hal/network/tls/BearSSL/jh_bearssl_hal_tcp_io.h"
#include "hal/network/tls/BearSSL/jh_bearssl_provider.h"
#include "hal/network/wireguard/hal_wireguard_internal.h"
#include "hal/radio/jh_lora_link_frame.h"
#include "hal/radio/jh_lora_modem.h"
#include "hal/radio/jh_lora_radio_internal.h"
#include "hal/rtc/jh_rtc_provider.h"
#include "hal/serial/hal_uart_internal.h"
#include "hal/spi/hal_spi_internal.h"
#include "hal/storage/hal_sdlogger_internal.h"
#include "hal/storage/jh_littlefs_provider.h"
#include "hal/temperature/jh_thermocouple_provider.h"

#include "c_link_probe.h"

/* Each group links only where the HAL defines it: the same feature flags as
 * the header and the source (the BSD socket transport is not built on
 * Windows). */
static const volatile c_link_probe_fn_t k_functions[] = {
#ifdef HAL_ENABLE_MCP401X
    C_LINK_PROBE_FN(hal_digipot_mcp401x_ops),
#endif
#ifdef HAL_ENABLE_MAX5395
    C_LINK_PROBE_FN(hal_digipot_max5395_ops),
#endif
#ifdef HAL_ENABLE_PULSE_CAPTURE
    C_LINK_PROBE_FN(jh_pulse_capture_start),
    C_LINK_PROBE_FN(jh_pulse_capture_stop),
    C_LINK_PROBE_FN(jh_pulse_capture_next),
#endif
#ifdef HAL_ENABLE_PGA2311
    C_LINK_PROBE_FN(hal_pga2311_driver_validate_config),
    C_LINK_PROBE_FN(hal_pga2311_driver_init_pins),
    C_LINK_PROBE_FN(hal_pga2311_driver_set_hw_mute),
    C_LINK_PROBE_FN(hal_pga2311_driver_write_codes),
#endif
#ifdef HAL_ENABLE_COMMAND_ROUTER
    C_LINK_PROBE_FN(jh_command_router_register_erased),
    C_LINK_PROBE_FN(jh_command_name_valid),
    C_LINK_PROBE_FN(jh_command_source_valid),
    C_LINK_PROBE_FN(jh_command_encoding_valid),
#endif
    C_LINK_PROBE_FN(jh_hal_display_get_dimensions),
#ifdef HAL_ENABLE_I2C
    C_LINK_PROBE_FN(jh_hal_i2c_bus_is_initialized),
#endif
#ifdef HAL_ENABLE_I2C_10BIT
    C_LINK_PROBE_FN(jh_hal_i2c_bus_is_10bit),
#endif
#if defined(HAL_ENABLE_TLS) && defined(HAL_ENABLE_BSD_SOCKETS)
    C_LINK_PROBE_FN(jh_bearssl_bsd_transport_init),
    C_LINK_PROBE_FN(jh_bearssl_bsd_io_init),
    C_LINK_PROBE_FN(jh_bearssl_bsd_read),
    C_LINK_PROBE_FN(jh_bearssl_bsd_write),
    C_LINK_PROBE_FN(jh_bearssl_blocking_io_init),
#endif
#ifdef HAL_ENABLE_TLS
    C_LINK_PROBE_FN(jh_bearssl_engine_poll_with_ops),
    C_LINK_PROBE_FN(jh_bearssl_engine_poll_for_read_with_ops),
    C_LINK_PROBE_FN(jh_bearssl_engine_poll),
    C_LINK_PROBE_FN(jh_bearssl_engine_poll_for_read),
    C_LINK_PROBE_FN(jh_bearssl_hal_tcp_transport_init),
    C_LINK_PROBE_FN(jh_bearssl_client_allocate),
    C_LINK_PROBE_FN(jh_bearssl_client_release),
    C_LINK_PROBE_FN(jh_bearssl_client_init),
    C_LINK_PROBE_FN(jh_bearssl_error_to_hal),
    C_LINK_PROBE_FN(jh_bearssl_verify_server_key_pin),
    C_LINK_PROBE_FN(jh_bearssl_provider_source_revision),
#endif
#ifdef HAL_ENABLE_WIREGUARD
    C_LINK_PROBE_FN(jh_hal_wireguard_begin_provider),
    C_LINK_PROBE_FN(jh_hal_wireguard_end_provider),
    C_LINK_PROBE_FN(jh_hal_wireguard_is_initialized_provider),
    C_LINK_PROBE_FN(jh_hal_wireguard_peer_up_provider),
    C_LINK_PROBE_FN(jh_hal_wireguard_note_quick_check),
    C_LINK_PROBE_FN(jh_hal_wireguard_kick_provider),
#endif
#ifdef HAL_ENABLE_LORA_LINK
    C_LINK_PROBE_FN(jh_lora_link_frame_payload_capacity),
    C_LINK_PROBE_FN(jh_lora_link_frame_encode),
    C_LINK_PROBE_FN(jh_lora_link_frame_decode),
#endif
#ifdef HAL_ENABLE_LORA
    C_LINK_PROBE_FN(jh_lora_modem_config_valid),
    C_LINK_PROBE_FN(jh_lora_modem_time_on_air),
    C_LINK_PROBE_FN(jh_lora_radio_default_provider),
    C_LINK_PROBE_FN(jh_lora_radio_context_lock),
    C_LINK_PROBE_FN(jh_lora_radio_context_unlock),
    C_LINK_PROBE_FN(jh_lora_radio_describe_capabilities),
    C_LINK_PROBE_FN(jh_lora_radio_set_provider_for_test),
#endif
#ifdef HAL_ENABLE_RTC
    C_LINK_PROBE_FN(jh_rtc_provider_get_ops),
    C_LINK_PROBE_FN(jh_rtc_i2c_provider_get_ops),
    C_LINK_PROBE_FN(jh_rtc_mock_provider_set_datetime),
    C_LINK_PROBE_FN(jh_rtc_mock_provider_set_clock_integrity),
    C_LINK_PROBE_FN(jh_rtc_mock_provider_set_flags),
    C_LINK_PROBE_FN(jh_rtc_mock_provider_fire_wakeup),
#endif
#ifdef HAL_ENABLE_UART
    C_LINK_PROBE_FN(jh_hal_uart_validate_config_for_target),
    C_LINK_PROBE_FN(jh_hal_uart_create_for_target),
    C_LINK_PROBE_FN(jh_hal_uart_set_pin_for_target),
#endif
#ifdef HAL_ENABLE_SPI
    C_LINK_PROBE_FN(jh_hal_spi_transfer16_provider),
    C_LINK_PROBE_FN(jh_hal_spi_write_provider),
    C_LINK_PROBE_FN(jh_hal_spi_transfer_txrx_generic),
#endif
#ifdef HAL_ENABLE_SDLOGGER
    C_LINK_PROBE_FN(jh_sdlogger_make_log_filename),
    C_LINK_PROBE_FN(jh_sdlogger_make_crash_filename),
    C_LINK_PROBE_FN(jh_sdlogger_append_crash_context),
#endif
#ifdef HAL_ENABLE_LITTLEFS
    C_LINK_PROBE_FN(jh_littlefs_provider_get),
    C_LINK_PROBE_FN(jh_littlefs_mock_reset_facade),
#endif
#ifdef HAL_ENABLE_THERMOCOUPLE
    C_LINK_PROBE_FN(jh_thermocouple_provider_get),
    C_LINK_PROBE_FN(jh_thermocouple_provider_visit_context),
#endif
};

int main(void) { return C_LINK_PROBE_ALL(k_functions) ? 0 : 1; }
