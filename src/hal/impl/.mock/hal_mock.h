#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "../../hal_gpio.h"
#include "../../hal_system.h"

// ── GPIO ─────────────────────────────────────────────────────────────────────
bool            hal_mock_gpio_get_state(uint8_t pin);
bool            hal_mock_gpio_is_output(uint8_t pin);
hal_gpio_mode_t hal_mock_gpio_get_mode(uint8_t pin);
void            hal_mock_gpio_inject_level(uint8_t pin, bool high);
/** @brief Script successive levels returned by hal_gpio_read() for a pin. */
void            hal_mock_gpio_push_read_sequence(uint8_t pin, const bool *levels, size_t len);
/** @brief Clear a scripted hal_gpio_read() sequence for a pin. */
void            hal_mock_gpio_clear_read_sequence(uint8_t pin);
/** @brief Fire the interrupt callback registered for pin (via hal_gpio_attach_interrupt). */
void            hal_mock_gpio_fire_interrupt(uint8_t pin);
/** @brief Return the GPIO IRQ priority set via hal_gpio_set_irq_priority(). */
hal_irq_priority_t hal_mock_gpio_get_irq_priority(void);

// ── PWM ──────────────────────────────────────────────────────────────────────
uint32_t hal_mock_pwm_get_value(uint8_t pin);
uint8_t  hal_mock_pwm_get_resolution(void);

// ── PWM freq-controlled channels ─────────────────────────────────────────────
#include "../../hal_pwm_freq.h"
int      hal_mock_pwm_freq_get_value(hal_pwm_freq_channel_t ch);
uint32_t hal_mock_pwm_freq_get_frequency(hal_pwm_freq_channel_t ch);
uint8_t  hal_mock_pwm_freq_get_pin(hal_pwm_freq_channel_t ch);

// ── DAC ──────────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_DAC
uint16_t hal_mock_dac_get(uint8_t channel);
bool     hal_mock_dac_is_initialized(uint8_t channel);
#endif

// ── PCNT (pulse counter) ─────────────────────────────────────────────────────
#ifdef HAL_ENABLE_PCNT
#include "../../hal_pcnt.h"
void            hal_mock_pcnt_inject(uint8_t channel, uint32_t pulses);
hal_pcnt_edge_t hal_mock_pcnt_get_edge(uint8_t channel);
uint8_t         hal_mock_pcnt_get_pin(uint8_t channel);
#endif

// ── Timer ─────────────────────────────────────────────────────────────────────
void     hal_mock_timer_advance_us(uint64_t us);
uint64_t hal_mock_timer_get_us(void);
void     hal_mock_timer_reset(void);

// ── System ───────────────────────────────────────────────────────────────────
void     hal_mock_set_millis(uint32_t ms);
void     hal_mock_advance_millis(uint32_t ms);
void     hal_mock_set_micros(uint32_t us);
void     hal_mock_advance_micros(uint32_t us);
bool     hal_mock_watchdog_was_fed(void);
void     hal_mock_watchdog_reset_flag(void);
void     hal_mock_set_caused_reboot(bool val);
void     hal_mock_set_free_heap(uint32_t bytes);
/** @brief Inject a chip temperature value returned by hal_read_chip_temp(). Default: 25.0 °C. */
void     hal_mock_set_chip_temp(float celsius);
/** @brief Return true if hal_enter_bootloader() has been called in the mock backend. */
bool     hal_mock_bootloader_was_requested(void);
/** @brief Clear the mock bootloader request flag. */
void     hal_mock_bootloader_reset_flag(void);
/** @brief Override the 8-byte UID returned by hal_get_device_uid(). */
void     hal_mock_set_device_uid(const uint8_t uid[8]);
/** @brief Restore the default deterministic mock UID. */
void     hal_mock_reset_device_uid(void);
/** @brief Force the return value of hal_in_isr() in the mock backend.
 *
 * Lets unit tests exercise ISR-only code paths (e.g. the deferred
 * debug ring used by hal_deb/hal_derr/hal_derr_limited). Defaults
 * to false; remains stable until changed. */
void     hal_mock_set_in_isr(bool in_isr);

// ── Fault / crash diagnostics ───────────────────────────────────────────────
/** @brief Override the value returned by hal_get_reset_reason(). */
void hal_mock_set_reset_reason(hal_reset_reason_t reason);
/** @brief Stage a fault snapshot to be returned by hal_get_last_fault().
 *         Pass NULL to mark "no fault available". */
void hal_mock_set_last_fault(const hal_fault_info_t *info);
/** @brief Override the value returned by hal_last_boot_was_brownout(). */
void hal_mock_set_brownout_suspected(bool val);
/** @brief Return true if hal_alive_mark() has been called. */
bool hal_mock_alive_was_marked(void);
/** @brief Clear the alive-marked flag captured by the mock. */
void hal_mock_alive_reset_flag(void);
/** @brief Return true if hal_fault_subsystem_init() has been called. */
bool hal_mock_fault_subsystem_was_inited(void);
/** @brief Return true if hal_stack_guard_init() has armed the guard. */
bool hal_mock_stack_guard_is_armed(void);
/** @brief Return true if hal_stack_guard_check() detected corruption.
 *         The mock backend does NOT actually reboot; it only records the
 *         event so tests can observe it. */
bool hal_mock_stack_guard_check_was_triggered(void);
/** @brief Reset all fault-diagnostic mock state to defaults. */
void hal_mock_fault_diagnostics_reset(void);

// ── Serial / Debug ────────────────────────────────────────────────────────────
const char *hal_mock_serial_last_line(void);
const char *hal_mock_deb_last_line(void);
void        hal_mock_serial_reset(void);void        hal_mock_serial_inject_rx(const char *data, int len);

// ── ISR-deferred debug ring (test introspection) ─────────────────────────────
/** @brief Number of records currently pending in the ISR ring. */
size_t   hal_mock_debug_isr_used_slots(void);
/** @brief Total slot capacity of the active ISR ring (effective = cap-1). */
size_t   hal_mock_debug_isr_capacity(void);
/** @brief Number of records dropped due to overflow since last drain. */
uint32_t hal_mock_debug_isr_dropped(void);
/** @brief Reset the ISR ring (head/tail/dropped) without draining records. */
void     hal_mock_debug_isr_reset(void);
/** @brief Swap the ring buffer for a smaller test-owned slot array.
 *
 * Pass slots == NULL (and any cap) to restore the built-in default ring.
 * The caller retains ownership of the slot array; it must outlive any
 * subsequent debug log activity. The ring is reset by this call.
 * @param cap Number of slots; must be >= 2. */
void     hal_mock_debug_isr_set_test_capacity(size_t cap);
/** @brief Restore the built-in default ISR ring. */
void     hal_mock_debug_isr_restore_default_ring(void);
// ── CAN ──────────────────────────────────────────────────────────────────────
#include "../../hal_can.h"
void hal_mock_can_inject(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data);
bool hal_mock_can_get_sent(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data);
void hal_mock_can_reset(hal_can_t h);

// ── ADC ──────────────────────────────────────────────────────────────────────
uint8_t hal_mock_adc_get_resolution(void);
void    hal_mock_adc_inject(uint8_t pin, int value);

// ── SoftwareSerial (swserial) ─────────────────────────────────────────────────
#ifdef HAL_ENABLE_SWSERIAL
#include "../../hal_swserial.h"
/** @brief Inject bytes into the mock software-serial RX buffer. */
void hal_mock_swserial_push(hal_swserial_t h, const uint8_t *data, int len);
/** @brief Reset RX buffer and last-write capture. */
void hal_mock_swserial_reset(hal_swserial_t h);
/** @brief Return the last string written via hal_swserial_write/println. */
const char *hal_mock_swserial_last_write(hal_swserial_t h);
#endif

// ── Hardware UART ─────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_UART
#include "../../hal_uart.h"
/** @brief Inject bytes into the mock hardware-UART RX buffer. */
void hal_mock_uart_push(hal_uart_t h, const uint8_t *data, int len);
/** @brief Reset RX buffer and last-write capture. */
void hal_mock_uart_reset(hal_uart_t h);
/** @brief Return the last string written via hal_uart_write/println. */
const char *hal_mock_uart_last_write(hal_uart_t h);
/** @brief Return the current RX pin stored in the handle. */
uint8_t hal_mock_uart_get_rx_pin(hal_uart_t h);
/** @brief Return the current TX pin stored in the handle. */
uint8_t hal_mock_uart_get_tx_pin(hal_uart_t h);

/** @brief TX observer callback signature for mock UART scripting. */
typedef void (*hal_mock_uart_write_cb_t)(hal_uart_t h, const char *text, void *user);
/** @brief Install a callback fired after every write/println; pass NULL to clear. */
void hal_mock_uart_set_write_callback(hal_uart_t h,
                                      hal_mock_uart_write_cb_t cb,
                                      void *user);
#endif

// ── SPI ───────────────────────────────────────────────────────────────────────
bool    hal_mock_spi_is_initialized(void);
uint8_t hal_mock_spi_get_bus(void);
uint8_t hal_mock_spi_get_rx_pin(void);
uint8_t hal_mock_spi_get_tx_pin(void);
uint8_t hal_mock_spi_get_sck_pin(void);
int     hal_mock_spi_get_lock_depth(uint8_t bus);
bool    hal_mock_spi_transaction_active(uint8_t bus);
uint32_t hal_mock_spi_get_clock_hz(uint8_t bus);
uint8_t hal_mock_spi_get_bit_order(uint8_t bus);
uint8_t hal_mock_spi_get_data_mode(uint8_t bus);
uint32_t hal_mock_spi_get_transfer_count(uint8_t bus);
void    hal_mock_spi_push_rx(uint8_t bus, const uint8_t *data, size_t len);
size_t  hal_mock_spi_get_tx(uint8_t bus, uint8_t *out, size_t max_len);
void    hal_mock_spi_reset(void);

// ── OneWire ───────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_ONEWIRE
#include "../../hal_onewire.h"
/** @brief Force presence response returned by hal_onewire_reset(). */
void     hal_mock_onewire_set_presence(hal_onewire_t h, bool present);
/** @brief Inject byte stream consumed by hal_onewire_read/read_bytes/read_bit. */
void     hal_mock_onewire_inject_read(hal_onewire_t h, const uint8_t *data, int len);
/** @brief Clear queued ROMs used by hal_onewire_search(). */
void     hal_mock_onewire_reset_search_roms(hal_onewire_t h);
/** @brief Append ROM to the search queue. Returns false when queue is full. */
bool     hal_mock_onewire_push_search_rom(hal_onewire_t h, const uint8_t rom[8]);
/** @brief Return the latest byte written via hal_onewire_write/write_bytes. */
uint8_t  hal_mock_onewire_get_last_write(hal_onewire_t h);
/** @brief Return the latest bit written via hal_onewire_write_bit(). */
uint8_t  hal_mock_onewire_get_last_write_bit(hal_onewire_t h);
/** @brief Return true and copy ROM selected by hal_onewire_select(). */
bool     hal_mock_onewire_get_last_selected_rom(hal_onewire_t h, uint8_t out_rom[8]);
/** @brief Return number of hal_onewire_reset() calls. */
uint32_t hal_mock_onewire_get_reset_count(hal_onewire_t h);
/** @brief Return number of hal_onewire_skip() calls. */
uint32_t hal_mock_onewire_get_skip_count(hal_onewire_t h);
/** @brief Return number of hal_onewire_depower() calls. */
uint32_t hal_mock_onewire_get_depower_count(hal_onewire_t h);
/** @brief Return maximal lock depth observed for this handle. */
int      hal_mock_onewire_get_max_lock_depth(hal_onewire_t h);
#endif

// ── RGB LED ───────────────────────────────────────────────────────────────────
#include "../../hal_rgb_led.h"
bool                hal_mock_rgb_led_is_initialized(void);
hal_rgb_led_color_t hal_mock_rgb_led_get_color(void);
uint8_t             hal_mock_rgb_led_get_brightness(void);
hal_rgb_led_pixel_type_t hal_mock_rgb_led_get_pixel_type(void);
uint8_t             hal_mock_rgb_led_get_pin(void);
uint8_t             hal_mock_rgb_led_get_num_pixels(void);
void                hal_mock_rgb_led_reset(void);

// ── Display ───────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_DISPLAY
#include "../../hal_display.h"
/** @brief Reset all mock display state to defaults. */
void         hal_mock_display_reset(void);
/** @brief Return the last string passed to hal_display_print(). */
const char  *hal_mock_display_last_print(void);
/** @brief Return the last string passed to hal_display_println(). */
const char  *hal_mock_display_last_println(void);
/** @brief Return the current font set via hal_display_set_font(). */
hal_font_id_t hal_mock_display_get_font(void);
/** @brief Return the current text colour set via hal_display_set_text_color(). */
uint16_t     hal_mock_display_get_text_color(void);
/** @brief Return the current text size set via hal_display_set_text_size(). */
uint8_t      hal_mock_display_get_text_size(void);
/** @brief Read the current cursor position. */
void         hal_mock_display_get_cursor(int *x, int *y);
/** @brief Read parameters of the last hal_display_fill_rect() call. */
void         hal_mock_display_get_last_fill_rect(int *x, int *y, int *w, int *h, uint16_t *color);
/** @brief Read parameters of the last hal_display_draw_rgb_bitmap() call. */
void         hal_mock_display_get_last_bitmap(int *x, int *y, uint16_t **data, int *w, int *h);
#endif

// ── WiFi ─────────────────────────────────────────────────────────────────────
#include "../../hal_wifi.h"
/** @brief Reset all mock WiFi state to defaults. */
void        hal_mock_wifi_reset(void);
/** @brief Inject the connected flag returned by hal_wifi_is_connected(). */
void        hal_mock_wifi_set_connected(bool connected);
/** @brief Inject the status value returned by hal_wifi_status(). */
void        hal_mock_wifi_set_status(int status);
/** @brief Inject RSSI value returned by hal_wifi_rssi(). */
void        hal_mock_wifi_set_rssi(int32_t rssi);
/** @brief Inject the local IP string returned by hal_wifi_get_local_ip(). */
void        hal_mock_wifi_set_local_ip(const char *ip);
/** @brief Inject the DNS IP string returned by hal_wifi_get_dns_ip(). */
void        hal_mock_wifi_set_dns_ip(const char *ip);
/** @brief Inject the MAC string returned by hal_wifi_get_mac(). */
void        hal_mock_wifi_set_mac(const char *mac);
/** @brief Inject the ping result returned by hal_wifi_ping(). */
void        hal_mock_wifi_set_ping_result(int result);
/** @brief Return the hostname set by hal_wifi_set_hostname(). */
const char *hal_mock_wifi_get_hostname(void);
/** @brief Return the timeout set by hal_wifi_set_timeout_ms(). */
uint32_t    hal_mock_wifi_get_timeout_ms(void);
/** @brief Inject one WiFi scan result returned by hal_wifi_get_scan_result(). */
bool        hal_mock_wifi_set_scan_result(size_t index,
                                          const char *ssid,
                                          hal_wifi_encryption_t encryption,
                                          const uint8_t bssid[HAL_WIFI_BSSID_LEN],
                                          int32_t channel,
                                          int32_t rssi);

// ── SD logger ───────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_SDLOGGER
#include "../../hal_sdlogger.h"
/** @brief Reset all mock SD logger state to defaults. */
void        hal_mock_sdlogger_reset(void);
/** @brief Control SD.begin() result returned by the mock backend. */
void        hal_mock_sdlogger_set_sd_begin_result(bool result);
/** @brief Control periodic log file open result. */
void        hal_mock_sdlogger_set_log_open_result(bool result);
/** @brief Control crash log file open result. */
void        hal_mock_sdlogger_set_crash_open_result(bool result);
/** @brief Return last periodic log filename. */
const char *hal_mock_sdlogger_log_filename(void);
/** @brief Return last crash log filename. */
const char *hal_mock_sdlogger_crash_filename(void);
/** @brief Return periodic log content flushed by the mock backend. */
const char *hal_mock_sdlogger_log_content(void);
/** @brief Return crash log content written by the mock backend. */
const char *hal_mock_sdlogger_crash_content(void);
/** @brief Return number of periodic log flushes. */
uint32_t    hal_mock_sdlogger_log_flush_count(void);
/** @brief Return number of crash log flushes. */
uint32_t    hal_mock_sdlogger_crash_flush_count(void);
/** @brief Return number of SD.begin() calls. */
uint32_t    hal_mock_sdlogger_sd_begin_count(void);
/** @brief Return true after hal_sdlogger_close() closes the log. */
bool        hal_mock_sdlogger_log_was_closed(void);
/** @brief Return true after hal_sdlogger_crash_close() closes the crash log. */
bool        hal_mock_sdlogger_crash_was_closed(void);
#endif

// ── LittleFS ─────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_LITTLEFS
#include "../../hal_littlefs.h"
/** @brief Reset all mock LittleFS state to defaults. */
void        hal_mock_littlefs_reset(void);
/** @brief Control result returned by hal_littlefs_begin() (default: true). */
void        hal_mock_littlefs_set_begin_result(bool result);
/** @brief Control result returned by hal_littlefs_format() (default: true). */
void        hal_mock_littlefs_set_format_result(bool result);
/** @brief Inject total bytes returned by hal_littlefs_total_bytes(). */
void        hal_mock_littlefs_set_total_bytes(size_t total_bytes);
/** @brief Inject used bytes returned by hal_littlefs_used_bytes(). */
void        hal_mock_littlefs_set_used_bytes(size_t used_bytes);
/** @brief Inject file existence for a path. */
void        hal_mock_littlefs_set_exists(const char *path, bool exists);
#endif

// ── UDP ──────────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_UDP
#include "../../hal_udp.h"
/** @brief Reset all mock UDP state to defaults. */
void        hal_mock_udp_reset(void);
/** @brief Inject one inbound UDP datagram for hal_udp_parse_packet/read. */
void        hal_mock_udp_inject_packet(const char *remote_ip,
									   uint16_t remote_port,
									   const uint8_t *payload,
									   uint16_t len);
/** @brief Control result returned by hal_udp_end_packet() (default: true). */
void        hal_mock_udp_set_end_packet_result(bool result);
/** @brief Return local port set by hal_udp_begin(). */
uint16_t    hal_mock_udp_get_local_port(void);
/** @brief Return destination host captured by hal_udp_begin_packet*(). */
const char *hal_mock_udp_get_last_begin_packet_host(void);
/** @brief Return destination port captured by hal_udp_begin_packet*(). */
uint16_t    hal_mock_udp_get_last_begin_packet_port(void);
/** @brief Return payload captured from hal_udp_write*(). */
const uint8_t *hal_mock_udp_get_last_tx_payload(void);
/** @brief Return payload length captured from hal_udp_write*(). */
uint16_t    hal_mock_udp_get_last_tx_len(void);
/** @brief Return whether hal_udp_end_packet() was called. */
bool        hal_mock_udp_was_end_packet_called(void);
#endif

// ── WireGuard ───────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_WIREGUARD
#include "../../hal_wireguard.h"
/** @brief Reset all mock WireGuard state to defaults. */
void        hal_mock_wireguard_reset(void);
/** @brief Control result returned by hal_wireguard_begin*() (default: true). */
void        hal_mock_wireguard_set_begin_result(bool result);
/** @brief Control result returned by hal_wireguard_peer_up() (default: false). */
void        hal_mock_wireguard_set_peer_up_result(bool result);
/** @brief Control result returned by hal_wireguard_kick_handshake*() (default: true). */
void        hal_mock_wireguard_set_kick_result(bool result);
/** @brief Force initialized state observed by hal_wireguard_is_initialized(). */
void        hal_mock_wireguard_set_initialized(bool initialized);
/** @brief Set endpoint values returned by hal_wireguard_peer_up(). */
void        hal_mock_wireguard_set_peer_endpoint(const uint8_t ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t port);
/** @brief Return number of hal_wireguard_peer_up_quick() calls since reset. */
uint32_t    hal_mock_wireguard_get_peer_up_quick_call_count(void);
/** @brief Return last local tunnel IP passed to hal_wireguard_begin*(). */
const uint8_t *hal_mock_wireguard_get_last_local_ip(void);
/** @brief Return last allowed IP passed to hal_wireguard_begin_advanced*(). */
const uint8_t *hal_mock_wireguard_get_last_allowed_ip(void);
/** @brief Return last allowed mask passed to hal_wireguard_begin_advanced*(). */
const uint8_t *hal_mock_wireguard_get_last_allowed_mask(void);
/** @brief Return endpoint host passed to hal_wireguard_begin*(). */
const char *hal_mock_wireguard_get_last_remote_peer_address(void);
/** @brief Return endpoint port passed to hal_wireguard_begin*(). */
uint16_t    hal_mock_wireguard_get_last_remote_peer_port(void);
/** @brief Return true when latest start used hal_wireguard_begin_advanced*(). */
bool        hal_mock_wireguard_was_begin_advanced(void);
/** @brief Return probe IP passed to hal_wireguard_kick_handshake*(). */
const uint8_t *hal_mock_wireguard_get_last_probe_ip(void);
/** @brief Return probe port passed to hal_wireguard_kick_handshake*(). */
uint16_t    hal_mock_wireguard_get_last_probe_port(void);
/** @brief Return min interval passed to hal_wireguard_kick_handshake*(). */
uint32_t    hal_mock_wireguard_get_last_probe_min_interval_ms(void);
#endif

// ── MQTT ─────────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_MQTT
#include "../../hal_mqtt.h"
/** @brief Reset all mock MQTT state to defaults. */
void        hal_mock_mqtt_reset(void);
/** @brief Control whether hal_mqtt_connect*() succeeds (default: true). */
void        hal_mock_mqtt_set_connect_result(bool result);
/** @brief Control value returned by hal_mqtt_loop() (default: true). */
void        hal_mock_mqtt_set_loop_result(bool result);
/** @brief Force connection state returned by hal_mqtt_connected(). */
void        hal_mock_mqtt_set_connected(bool connected);
/** @brief Force state value returned by hal_mqtt_state(). */
void        hal_mock_mqtt_set_state(int state);
/** @brief Queue one inbound MQTT message delivered on next hal_mqtt_loop(). */
void        hal_mock_mqtt_inject_message(const char *topic, const uint8_t *payload, uint16_t length);
/** @brief Return broker host configured via hal_mqtt_set_server(). */
const char *hal_mock_mqtt_get_server_host(void);
/** @brief Return broker port configured via hal_mqtt_set_server(). */
uint16_t    hal_mock_mqtt_get_server_port(void);
/** @brief Return topic captured from last hal_mqtt_publish*(). */
const char *hal_mock_mqtt_get_last_publish_topic(void);
/** @brief Return payload captured from last hal_mqtt_publish*(). */
const uint8_t *hal_mock_mqtt_get_last_publish_payload(void);
/** @brief Return payload length captured from last hal_mqtt_publish*(). */
uint16_t    hal_mock_mqtt_get_last_publish_len(void);
/** @brief Return retained flag captured from last hal_mqtt_publish*(). */
bool        hal_mock_mqtt_get_last_publish_retained(void);
/** @brief Return topic captured from last hal_mqtt_subscribe(). */
const char *hal_mock_mqtt_get_last_subscribe_topic(void);
/** @brief Return qos captured from last hal_mqtt_subscribe(). */
uint8_t     hal_mock_mqtt_get_last_subscribe_qos(void);
/** @brief Return topic captured from last hal_mqtt_unsubscribe(). */
const char *hal_mock_mqtt_get_last_unsubscribe_topic(void);
/** @brief Return keepalive value set by hal_mqtt_set_keepalive(). */
uint16_t    hal_mock_mqtt_get_keepalive(void);
/** @brief Return socket timeout value set by hal_mqtt_set_socket_timeout(). */
uint16_t    hal_mock_mqtt_get_socket_timeout(void);
#endif

// ── OTA ──────────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_OTA
#include "../../hal_ota.h"
/** @brief Reset all mock OTA state to defaults. */
void        hal_mock_ota_reset(void);
/** @brief Control result returned by hal_ota_begin() (default: true). */
void        hal_mock_ota_set_begin_result(bool result);
/** @brief Inject OTA start event consumed on next hal_ota_handle(). */
void        hal_mock_ota_inject_start(hal_ota_command_t command);
/** @brief Inject OTA end event consumed on next hal_ota_handle(). */
void        hal_mock_ota_inject_end(void);
/** @brief Inject OTA progress event consumed on next hal_ota_handle(). */
void        hal_mock_ota_inject_progress(uint32_t progress, uint32_t total);
/** @brief Inject OTA error event consumed on next hal_ota_handle(). */
void        hal_mock_ota_inject_error(hal_ota_error_t error);
/** @brief Return currently configured OTA port. */
uint16_t    hal_mock_ota_get_port(void);
/** @brief Return currently configured OTA hostname. */
const char *hal_mock_ota_get_hostname(void);
/** @brief Return currently configured OTA password. */
const char *hal_mock_ota_get_password(void);
/** @brief Return number of hal_ota_handle() calls. */
uint32_t    hal_mock_ota_get_handle_count(void);
#endif

// ── Time / NTP ──────────────────────────────────────────────────────────────
#include "../../hal_time.h"
/** @brief Reset all mock time state to defaults. */
void        hal_mock_time_reset(void);
/** @brief Inject Unix timestamp returned by hal_time_unix(). */
void        hal_mock_time_set_unix(uint64_t unix_time);
/** @brief Inject local-time breakdown returned by hal_time_get_local(). */
void        hal_mock_time_set_local(const struct tm *tm_local);
/** @brief Return the timezone string set by hal_time_set_timezone(). */
const char *hal_mock_time_get_timezone(void);
/** @brief Return the primary NTP server set by hal_time_sync_ntp(). */
const char *hal_mock_time_get_ntp_primary(void);
/** @brief Return the secondary NTP server set by hal_time_sync_ntp(). */
const char *hal_mock_time_get_ntp_secondary(void);

// ── Thermocouple ──────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_THERMOCOUPLE
#include "../../hal_thermocouple.h"
/** @brief Inject the hot-junction temperature returned by hal_thermocouple_read(). */
void hal_mock_thermocouple_set_temp(hal_thermocouple_t h, float temp);
/** @brief Inject the cold-junction temperature returned by hal_thermocouple_read_ambient(). */
void hal_mock_thermocouple_set_ambient(hal_thermocouple_t h, float temp);
/** @brief Inject the raw ADC value returned by hal_thermocouple_read_adc_raw(). */
void hal_mock_thermocouple_set_adc_raw(hal_thermocouple_t h, int32_t raw);
/** @brief Inject the status byte returned by hal_thermocouple_get_status(). */
void hal_mock_thermocouple_set_status(hal_thermocouple_t h, uint8_t status);
#endif

// ── DS18B20 ───────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_DS18B20
#include "../../hal_ds18b20.h"
/** @brief Inject value consumed by the next successful conversion completion. */
void hal_mock_ds18b20_set_next_temp(hal_ds18b20_t h, float temp_c);
/** @brief Control presence result used by init/request checks. */
void hal_mock_ds18b20_set_presence(hal_ds18b20_t h, bool present);
/** @brief Control CRC result of the scratchpad read at conversion completion. */
void hal_mock_ds18b20_set_crc_ok(hal_ds18b20_t h, bool ok);
/** @brief Return number of successful hal_ds18b20_request() starts. */
uint32_t hal_mock_ds18b20_get_request_count(hal_ds18b20_t h);
#endif

// ── RTC ─────────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_RTC
#include "../../hal_rtc.h"
/** @brief Replace current mock RTC date-time payload. */
void hal_mock_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt);
/** @brief Inject clock integrity state returned by hal_rtc_get_clock_integrity(). */
void hal_mock_rtc_set_clock_integrity(hal_rtc_t h, bool ok);
/** @brief Inject event flags consumed by hal_rtc_get_and_clear_flags(). */
void hal_mock_rtc_set_flags(hal_rtc_t h, uint8_t flags);
#endif

// ── GPS ───────────────────────────────────────────────────────────────────

/** @brief Inject latitude and longitude into the mock GPS. */
void hal_mock_gps_set_location(double lat, double lng);
/** @brief Set the mock location-valid flag (returned by hal_gps_location_is_valid()). */
void hal_mock_gps_set_valid(bool valid);
/** @brief Set the mock location-updated flag (returned by hal_gps_location_is_updated()). */
void hal_mock_gps_set_updated(bool updated);
/** @brief Set the mock location age in milliseconds (returned by hal_gps_location_age()). */
void hal_mock_gps_set_age(uint32_t age_ms);
/** @brief Set the mock ground speed in km/h (returned by hal_gps_speed_kmph()). */
void hal_mock_gps_set_speed(double kmph);
/** @brief Set the mock GPS date fields. */
void hal_mock_gps_set_date(int year, int month, int day);
/** @brief Set the mock GPS time fields (UTC). */
void hal_mock_gps_set_time(int hour, int minute, int second);
/** @brief Set the mock GPS altitude (metres). */
void hal_mock_gps_set_altitude_m(double altitude_m);
/** @brief Set the mock GPS course over ground (degrees). */
void hal_mock_gps_set_course_deg(double course_deg);
/** @brief Set the mock GPS dilution-of-precision values. */
void hal_mock_gps_set_dop(double hdop, double vdop, double pdop);
/** @brief Set the mock GPS satellite counts (used / in view). */
void hal_mock_gps_set_satellites(uint32_t used, uint8_t in_view);
/** @brief Set the mock GPS fix quality (GGA) and fix mode (GSA). */
void hal_mock_gps_set_fix(uint8_t quality, uint8_t mode);
/** @brief Set the mock GPS horizontal accuracy (metres). */
void hal_mock_gps_set_horizontal_accuracy_m(double accuracy_m);
/** @brief Reset all mock GPS state to zero / invalid. */
void hal_mock_gps_reset(void);

// ── EEPROM ───────────────────────────────────────────────────────────────────
#ifdef HAL_ENABLE_EEPROM
#include "../../hal_eeprom.h"
/** @brief Read a byte directly from the mock EEPROM backing store. */
uint8_t           hal_mock_eeprom_get_byte(uint16_t addr);
/** @brief Return the EEPROM type set by hal_eeprom_init(). */
hal_eeprom_type_t hal_mock_eeprom_get_type(void);
/** @brief Return true if hal_eeprom_commit() has been called since the last reset. */
bool              hal_mock_eeprom_was_committed(void);
/** @brief Clear the committed flag (allows re-checking in tests). */
void              hal_mock_eeprom_clear_committed_flag(void);
/** @brief Return number of byte writes performed since last reset/counter clear. */
uint32_t          hal_mock_eeprom_get_write_count(void);
/** @brief Clear EEPROM byte-write counter. */
void              hal_mock_eeprom_clear_write_count(void);
/** @brief Reset all mock EEPROM state (memory, type, committed flag) to defaults. */
void              hal_mock_eeprom_reset(void);
#endif

// ── I2C ──────────────────────────────────────────────────────────────────────

/**
 * @brief Pre-load the receive buffer used by hal_i2c_read().
 * @param data Pointer to the byte array to inject.
 * @param len  Number of bytes (clamped to MOCK_I2C_BUF_SIZE = 256).
 */
void    hal_mock_i2c_inject_rx(const uint8_t *data, int len);
/** @brief Pre-load receive buffer for selected I2C mock bus (0 = Wire, 1 = Wire1). */
void    hal_mock_i2c_inject_rx_bus(uint8_t bus, const uint8_t *data, int len);
/** @brief Return the 7-bit address of the last hal_i2c_begin_transmission() call. */
uint8_t hal_mock_i2c_get_last_addr(void);
/** @brief Return the last transmission address for selected I2C mock bus. */
uint8_t hal_mock_i2c_get_last_addr_bus(uint8_t bus);
/** @brief Return the current lock depth for selected I2C mock bus. */
int     hal_mock_i2c_get_lock_depth_bus(uint8_t bus);
/** @brief Return lock depth for default I2C bus (0 = Wire). */
int     hal_mock_i2c_get_lock_depth(void);
/** @brief Return lock depth observed at the byte-read point inside hal_i2c_read_byte_bus(). */
int     hal_mock_i2c_get_read_byte_lock_depth_bus(uint8_t bus);
/** @brief Return lock depth observed at the byte-read point for default I2C bus. */
int     hal_mock_i2c_get_read_byte_lock_depth(void);
/** @brief Return true when selected I2C mock bus is marked initialized. */
bool    hal_mock_i2c_is_initialized_bus(uint8_t bus);
/** @brief Return initialized flag for default I2C bus (0 = Wire). */
bool    hal_mock_i2c_is_initialized(void);
/** @brief Return the last configured I2C clock for selected I2C mock bus. */
uint32_t hal_mock_i2c_get_clock_hz_bus(uint8_t bus);
/** @brief Return the last configured I2C clock for default I2C bus. */
uint32_t hal_mock_i2c_get_clock_hz(void);
/** @brief Control the value returned by hal_i2c_is_busy() (default: false). */
void    hal_mock_i2c_set_busy(bool busy);
/** @brief Control busy state for selected I2C mock bus (0 = Wire, 1 = Wire1). */
void    hal_mock_i2c_set_busy_bus(uint8_t bus, bool busy);
/** @brief Return how many times hal_i2c_bus_clear() was called on bus 0. */
uint32_t hal_mock_i2c_get_bus_clear_count(void);
/** @brief Return how many times hal_i2c_bus_clear_bus() was called on the given bus. */
uint32_t hal_mock_i2c_get_bus_clear_count_bus(uint8_t bus);
/** @brief Clear the captured write-frame log on bus 0. */
void hal_mock_i2c_reset_write_log(void);
/** @brief Clear the captured write-frame log on the given bus. */
void hal_mock_i2c_reset_write_log_bus(uint8_t bus);
/** @brief Number of write frames (begin..end) captured on bus 0 since reset. */
int hal_mock_i2c_get_write_frame_count(void);
/** @brief Number of write frames captured on the given bus since reset. */
int hal_mock_i2c_get_write_frame_count_bus(uint8_t bus);
/**
 * @brief Copy a captured write frame (bus 0) into @p out.
 * @return frame length in bytes, or -1 if @p index is out of range.
 */
int hal_mock_i2c_get_write_frame(int index, uint8_t *out, int max);
/** @brief Copy a captured write frame on the given bus into @p out. */
int hal_mock_i2c_get_write_frame_bus(uint8_t bus, int index, uint8_t *out, int max);

// ── I2C Slave ────────────────────────────────────────────────────────────────
#include "../../hal_i2c_slave.h"

/** @brief Return true if I2C slave on default bus is initialized. */
bool    hal_mock_i2c_slave_is_initialized(void);
/** @brief Return true if I2C slave on selected bus is initialized. */
bool    hal_mock_i2c_slave_is_initialized_bus(uint8_t bus);
/** @brief Read a register directly from the mock slave map (bus 0). */
uint8_t hal_mock_i2c_slave_get_reg(uint8_t reg);
/** @brief Read a register directly from the mock slave map (selected bus). */
uint8_t hal_mock_i2c_slave_get_reg_bus(uint8_t bus, uint8_t reg);
/** @brief Write a register directly into the mock slave map (bus 0). */
void    hal_mock_i2c_slave_set_reg(uint8_t reg, uint8_t value);
/** @brief Write a register directly into the mock slave map (selected bus). */
void    hal_mock_i2c_slave_set_reg_bus(uint8_t bus, uint8_t reg, uint8_t value);
/** @brief Return the current register pointer (bus 0). */
uint8_t hal_mock_i2c_slave_get_reg_ptr(void);
/** @brief Return the current register pointer (selected bus). */
uint8_t hal_mock_i2c_slave_get_reg_ptr_bus(uint8_t bus);
/** @brief Simulate a master-write: first byte = reg pointer, rest = data. */
void    hal_mock_i2c_slave_simulate_receive(const uint8_t *data, int len);
/** @brief Simulate a master-write on selected bus. */
void    hal_mock_i2c_slave_simulate_receive_bus(uint8_t bus, const uint8_t *data, int len);
/** @brief Simulate a master-read starting from current reg pointer. Returns bytes copied. */
int     hal_mock_i2c_slave_simulate_request(uint8_t *out_buf, int max_len);
/** @brief Simulate a master-read on selected bus. */
int     hal_mock_i2c_slave_simulate_request_bus(uint8_t bus, uint8_t *out_buf, int max_len);

// ── External ADC (ADS1115) ───────────────────────────────────────────────────

/** @brief Inject a raw 16-bit ADC result for the given channel (0–3). */
void  hal_mock_ext_adc_inject_raw(uint8_t channel, int16_t value);
/** @brief Inject a pre-scaled float result for the given channel (0–3). */
void  hal_mock_ext_adc_inject_scaled(uint8_t channel, float value);
/** @brief Return the adc_range value set by hal_ext_adc_init(). */
float hal_mock_ext_adc_get_range(void);
