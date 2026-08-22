/* This function is called by the compile fixture, but its volatile runtime
 * gate stays false. Calls below retain complete Phase 2/3 service lifecycles
 * through the final linker without touching hardware in CI. */

#include "hal/analog/hal_pcnt.h"
#include "hal/gpio/hal_pwm.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/gpio/hal_rgb_led.h"
#include "hal/i2c/hal_i2c_slave.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_tcp.h"
#include "hal/network/hal_udp.h"
#include "hal/network/hal_wifi.h"
#include "hal/network/http/hal_http_client.h"
#include "hal/network/http/hal_http_files.h"
#include "hal/network/http/hal_http_server.h"
#include "hal/network/mqtt/hal_mqtt.h"
#include "hal/network/ota/hal_ota.h"
#include "hal/network/tls/hal_tls.h"
#include "hal/network/websocket/hal_websocket.h"
#include "hal/network/wireguard/hal_wireguard.h"
#include "hal/system/hal_system.h"
#include "hal/time/hal_time.h"
#include "hal/timers/hal_timer.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stddef.h>
#include <stdint.h>

namespace {

volatile bool s_run_link_probe;

hal_status_t http_handler(const hal_http_request_t *request,
                          hal_http_response_t *response, void *user) {
  (void)user;
  (void)hal_http_request_get_header(request, "X-Link-Probe");
  (void)hal_http_method_to_string(request->method);
  (void)hal_http_response_set_status(response, 200u, "OK");
  (void)hal_http_response_set_content_type(response, "text/plain");
  (void)hal_http_response_set_header(response, "X-Link-Probe", "1");
  (void)hal_http_response_write(response, "probe", 5u);
  return hal_http_response_write_str(response, "\n");
}

hal_status_t file_stat(const char *path, hal_http_file_info_t *out_info,
                       void *user) {
  (void)path;
  (void)user;
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  *out_info = {};
  out_info->exists = true;
  out_info->size = 5u;
  out_info->mtime = 1u;
  return HAL_OK;
}

hal_status_t file_read(const char *path, size_t offset, void *buffer,
                       size_t max_len, size_t *out_len, void *user) {
  (void)path;
  (void)offset;
  (void)buffer;
  (void)max_len;
  (void)user;
  if (out_len == nullptr) {
    return HAL_EINVAL;
  }
  *out_len = 0u;
  return HAL_OK;
}

hal_status_t file_write(const char *path, size_t offset, const void *data,
                        size_t len, bool final, void *user) {
  (void)path;
  (void)offset;
  (void)data;
  (void)len;
  (void) final;
  (void)user;
  return HAL_OK;
}

hal_status_t authorize_upload(const hal_http_request_t *request,
                              hal_http_file_upload_t upload, void *user) {
  (void)request;
  (void)upload;
  (void)user;
  return HAL_OK;
}

void websocket_connect(hal_websocket_client_t client, void *user) {
  (void)client;
  (void)user;
}

void websocket_message(hal_websocket_client_t client,
                       hal_websocket_message_type_t type, const uint8_t *data,
                       size_t len, void *user) {
  (void)client;
  (void)type;
  (void)data;
  (void)len;
  (void)user;
}

void websocket_disconnect(hal_websocket_client_t client, uint16_t close_code,
                          void *user) {
  (void)client;
  (void)close_code;
  (void)user;
}

void mqtt_message(const char *topic, const uint8_t *payload, uint16_t length,
                  void *user) {
  (void)topic;
  (void)payload;
  (void)length;
  (void)user;
}

void ota_start(hal_ota_command_t command, void *user) {
  (void)command;
  (void)user;
}

void ota_end(void *user) { (void)user; }

void ota_progress(uint32_t progress, uint32_t total, void *user) {
  (void)progress;
  (void)total;
  (void)user;
}

void ota_error(hal_ota_error_t error, void *user) {
  (void)error;
  (void)user;
}

int64_t alarm_callback(hal_alarm_id_t alarm_id, void *user) {
  (void)alarm_id;
  (void)user;
  return 0;
}

void timer_callback(hal_timer_t timer, void *user) {
  (void)timer;
  (void)user;
}

} // namespace

void jh_phase3_link_probe(void) {
  if (!s_run_link_probe) {
    return;
  }

  hal_i2c_slave_init(8u, 9u, 0x42u);
  hal_i2c_slave_reg_write8(0u, 0x12u);
  hal_i2c_slave_reg_write16(1u, 0x3456u);
  (void)hal_i2c_slave_reg_read8(0u);
  (void)hal_i2c_slave_reg_read16(1u);
  (void)hal_i2c_slave_get_address();
  (void)hal_i2c_slave_get_transaction_count();
  hal_i2c_slave_deinit();
  (void)hal_pcnt_is_supported();
  (void)hal_pcnt_channel_count();
  (void)hal_pcnt_init_ex(0u, 17u, HAL_PCNT_EDGE_RISING);
  uint32_t pulse_count = 0u;
  (void)hal_pcnt_read_ex(0u, &pulse_count);
  (void)hal_pcnt_reset(0u);
  (void)hal_pcnt_read_and_reset_ex(0u, &pulse_count);
  hal_pwm_set_resolution(12u);
  (void)hal_pwm_is_pin_supported(18u);
  hal_pwm_write(18u, 0u);
  hal_pwm_write(18u, 4095u);
  hal_pwm_freq_channel_t pwm = hal_pwm_freq_create(18u, 1000u, 4095u);
  (void)hal_pwm_freq_source_clock_hz(18u);
  hal_pwm_freq_write(pwm, 4095);
  hal_pwm_freq_stop(pwm);
  hal_pwm_freq_destroy(pwm);
  (void)hal_rgb_led_init_ex(21u, 1u, HAL_RGB_LED_PIXEL_GRB_KHZ800);
  hal_rgb_led_set_brightness(30u);
  (void)hal_rgb_led_set_color(HAL_RGB_LED_BLUE);
  (void)hal_rgb_led_off();
  hal_timer_pool_t pool = hal_timer_pool_create_auto(1u);
  hal_alarm_id_t alarm =
      hal_timer_pool_add_alarm_us(pool, 1000u, alarm_callback, nullptr, false);
  (void)hal_timer_pool_cancel_alarm(pool, alarm);
  hal_timer_t timer = nullptr;
  (void)hal_timer_create(pool, 1000u, true, timer_callback, nullptr, &timer);
  (void)hal_timer_start(timer);
  (void)hal_timer_pause(timer);
  (void)hal_timer_resume(timer);
  (void)hal_timer_set_period_us(timer, 2000u, true);
  uint32_t timer_period_us = 0u;
  int64_t timer_remaining_us = 0;
  (void)hal_timer_get_period_us(timer, &timer_period_us);
  (void)hal_timer_get_state(timer);
  (void)hal_timer_get_remaining_us(timer, &timer_remaining_us);
  (void)hal_timer_stop(timer);
  (void)hal_timer_destroy(timer);
  hal_timer_pool_destroy(pool);
  (void)hal_stack_guard_init_ex();
  (void)hal_stack_guard_init();
  hal_stack_guard_check();
  hal_fault_subsystem_init();
  hal_fault_info_t fault = {};
  (void)hal_get_last_fault_ex(&fault);
  hal_clear_last_fault();
  (void)hal_enter_bootloader();

  (void)hal_wifi_set_mode_ex(HAL_WIFI_MODE_OFF);
  (void)hal_net_get_capabilities();
  hal_tcp_socket_t tcp = nullptr;
  (void)hal_tcp_socket_open_ex(&tcp);
  hal_tcp_socket_close(tcp);
  hal_udp_socket_t udp = nullptr;
  (void)hal_udp_socket_open_ex(&udp);
  hal_udp_socket_close(udp);

  const int descriptor = socket(AF_INET, SOCK_STREAM, 0);
  if (descriptor >= 0) {
    (void)close(descriptor);
  }
  struct addrinfo *addresses = nullptr;
  (void)getaddrinfo("localhost", nullptr, nullptr, &addresses);
  freeaddrinfo(addresses);
  uint8_t ipv4[4] = {};
  (void)inet_pton(AF_INET, "127.0.0.1", ipv4);

  const uint8_t trust_dn[] = {0x30u};
  const uint8_t trust_modulus[] = {0x01u};
  const uint8_t trust_exponent[] = {0x03u};
  const uint8_t server_key_pin[32] = {};
  hal_tls_trust_anchor_t trust_anchor = {};
  trust_anchor.subject_dn = trust_dn;
  trust_anchor.subject_dn_length = sizeof(trust_dn);
  trust_anchor.key_type = HAL_TLS_TRUST_KEY_RSA;
  trust_anchor.key.rsa.modulus = trust_modulus;
  trust_anchor.key.rsa.modulus_length = sizeof(trust_modulus);
  trust_anchor.key.rsa.exponent = trust_exponent;
  trust_anchor.key.rsa.exponent_length = sizeof(trust_exponent);
  hal_tls_security_config_t tls_security = {};
  tls_security.trust_anchors = &trust_anchor;
  tls_security.trust_anchor_count = 1u;
  tls_security.get_time = hal_tls_default_time;
  tls_security.get_entropy = hal_tls_default_entropy;
  tls_security.server_public_key_sha256 = server_key_pin;
  hal_tls_trust_anchor_storage_t trust_storage = {};
  (void)hal_tls_trust_anchor_from_der_ex(trust_dn, sizeof(trust_dn),
                                         &trust_storage);

  hal_tls_client_config_t tls_config = {};
  (void)hal_tls_client_config_init(&tls_config);
  hal_tls_client_t tls_client = nullptr;
  (void)hal_tls_client_create_ex(&tls_config, &tls_client);
  (void)hal_tls_client_configure_server_ex(tls_client, "localhost", 443u);
  (void)hal_tls_client_configure_security_ex(tls_client, &tls_security);
  (void)hal_tls_client_connect_ex(tls_client);
  (void)hal_tls_client_poll_ex(tls_client);
  uint8_t tls_buffer[8] = {};
  size_t tls_transferred = 0u;
  (void)hal_tls_client_read_ex(tls_client, tls_buffer, sizeof(tls_buffer),
                               &tls_transferred);
  (void)hal_tls_client_write_ex(tls_client, tls_buffer, sizeof(tls_buffer),
                                &tls_transferred);
  hal_tls_state_t tls_state = HAL_TLS_STATE_CREATED;
  (void)hal_tls_client_get_state_ex(tls_client, &tls_state);
  hal_status_t tls_status = HAL_NONE;
  int32_t provider_error = 0;
  (void)hal_tls_client_get_last_error_ex(tls_client, &tls_status,
                                         &provider_error);
  (void)hal_tls_client_shutdown_ex(tls_client);
  (void)hal_tls_client_cancel_ex(tls_client);
  (void)hal_tls_client_close_ex(tls_client);

  hal_http_client_request_t request = {};
  (void)hal_http_client_request_init(&request);
  request.transport = HAL_HTTP_CLIENT_TRANSPORT_TLS;
  request.host = "localhost";
  request.port = 443u;
  request.method = "GET";
  request.path = "/";
  request.tls_security = &tls_security;
  uint8_t response_body[16] = {};
  hal_http_client_response_t response = {};
  (void)hal_http_client_perform_ex(&request, response_body,
                                   sizeof(response_body), &response);

  hal_http_server_clear_routes();
  (void)hal_http_server_route(HAL_HTTP_METHOD_GET, "/", http_handler, nullptr);
  (void)hal_http_server_route_prefix(HAL_HTTP_METHOD_GET, "/probe",
                                     http_handler, nullptr);
  (void)hal_http_server_start(8080u);
  hal_http_server_poll();
  (void)hal_http_server_is_running();

  hal_http_files_config_t files = {};
  files.url_prefix = "/files";
  files.fs_root = "/www";
  files.upload_path = "/upload";
  files.enable_upload = true;
  files.stat = file_stat;
  files.read = file_read;
  files.write = file_write;
  files.authorize_upload = authorize_upload;
  (void)hal_http_files_mount(&files);
  (void)hal_http_files_content_type_for_path("/index.html");
  hal_http_file_info_t file_info = {};
  file_info.exists = true;
  file_info.size = 5u;
  file_info.mtime = 1u;
  char etag[HAL_HTTP_FILES_ETAG_MAX] = {};
  (void)hal_http_files_make_etag("/index.html", &file_info, etag, sizeof(etag));
  hal_http_files_clear();
  hal_http_server_stop();
  hal_http_server_clear_routes();

  hal_websocket_callbacks_t websocket = {};
  websocket.on_connect = websocket_connect;
  websocket.on_message = websocket_message;
  websocket.on_disconnect = websocket_disconnect;
  (void)hal_websocket_server_set_callbacks(&websocket, nullptr);
  (void)hal_websocket_server_start(8081u, "/ws");
  hal_websocket_server_poll();
  (void)hal_websocket_server_is_running();
  (void)hal_websocket_client_count();
  (void)hal_websocket_client_is_connected(0u);
  const uint8_t websocket_payload[] = {'o', 'k'};
  (void)hal_websocket_send(0u, HAL_WEBSOCKET_MESSAGE_BINARY, websocket_payload,
                           sizeof(websocket_payload));
  (void)hal_websocket_send_text(0u, "probe");
  size_t websocket_sent = 0u;
  (void)hal_websocket_broadcast(HAL_WEBSOCKET_MESSAGE_BINARY, websocket_payload,
                                sizeof(websocket_payload), &websocket_sent);
  (void)hal_websocket_broadcast_text("probe", &websocket_sent);
  (void)hal_websocket_close(0u, 1000u);
  hal_websocket_server_stop();
  (void)hal_websocket_server_set_callbacks(nullptr, nullptr);

  (void)hal_mqtt_set_server_ex("localhost", 8883u);
  (void)hal_mqtt_set_callback_ex(mqtt_message, nullptr);
  (void)hal_mqtt_set_keepalive_ex(30u);
  (void)hal_mqtt_set_socket_timeout_ex(1u);
  (void)hal_mqtt_set_buffer_size_ex(256u);
  (void)hal_mqtt_configure_tls_ex(&tls_security);
  (void)hal_mqtt_connect_ex("phase3-probe");
  (void)hal_mqtt_connect_auth_ex("phase3-probe", "user", "password");
  (void)hal_mqtt_loop_ex();
  const uint8_t mqtt_payload[] = {'o', 'k'};
  (void)hal_mqtt_publish_ex("probe/raw", mqtt_payload, sizeof(mqtt_payload),
                            false);
  (void)hal_mqtt_publish_str_ex("probe/text", "probe", false);
  (void)hal_mqtt_subscribe_ex("probe/#", 0u);
  (void)hal_mqtt_unsubscribe_ex("probe/#");
  (void)hal_mqtt_get_buffer_size();
  (void)hal_mqtt_connected();
  (void)hal_mqtt_state();
  hal_mqtt_disconnect();
  (void)hal_mqtt_disable_tls_ex();

  (void)hal_time_set_timezone("UTC0");
  (void)hal_time_sync_ntp_ex("pool.ntp.org", "time.google.com");
  (void)hal_time_set_unix_ex(1'700'000'000u, 0u, HAL_TIME_SOURCE_MANUAL);
  (void)hal_time_unix();
  (void)hal_time_is_synced(1'600'000'000u);
  hal_time_status_t time_status = {};
  (void)hal_time_get_status_ex(&time_status);
  struct tm local_time = {};
  (void)hal_time_get_local(&local_time);
  char formatted_time[32] = {};
  (void)hal_time_format_local(formatted_time, sizeof(formatted_time),
                              "%Y-%m-%dT%H:%M:%S");

  (void)hal_ota_set_port(8266u);
  (void)hal_ota_set_hostname("phase3-probe");
  (void)hal_ota_set_password("probe");
  (void)hal_ota_on_start(ota_start, nullptr);
  (void)hal_ota_on_end(ota_end, nullptr);
  (void)hal_ota_on_progress(ota_progress, nullptr);
  (void)hal_ota_on_error(ota_error, nullptr);
  (void)hal_ota_begin();
  hal_ota_handle();
  (void)hal_ota_is_started();
  hal_ota_boot_info_t boot = {};
  (void)hal_ota_get_boot_info_ex(&boot);
  (void)hal_ota_confirm_boot_ex();

  const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS] = {10u, 0u, 0u, 2u};
  const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS] = {};
  const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS] = {};
  uint8_t parsed_ip[HAL_WIREGUARD_IPV4_OCTETS] = {};
  (void)hal_wireguard_parse_ipv4_ex("10.0.0.2", parsed_ip);
  (void)hal_wireguard_begin_advanced_ex(local_ip, "private-key", "localhost",
                                        "public-key", 51820u, allowed_ip,
                                        allowed_mask);
  (void)hal_wireguard_is_initialized();
  char peer_ip[HAL_WIREGUARD_IP_STR_LEN] = {};
  uint16_t peer_port = 0u;
  bool peer_up = false;
  (void)hal_wireguard_peer_up_ex(peer_ip, sizeof(peer_ip), &peer_port,
                                 &peer_up);
  (void)hal_wireguard_peer_up_quick_ex(&peer_up);
  (void)hal_wireguard_kick_handshake_ex(local_ip, 9u, 1000u);
  hal_wireguard_end();
}
