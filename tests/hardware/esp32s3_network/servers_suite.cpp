/**
 * @file servers_suite.cpp
 * @brief Basic suite, server part: HTTP routes and files, WebSocket, network
 *        commands, the net console and IPv6 sockets, probed by the host.
 *
 * The board starts every server, announces `check=serve` and serves until the
 * host sends `RESTART` (stop and start everything again, then announce
 * `check=serve phase=2`), `STOP` (stop with the host's clients still
 * connected) and `DONE`.
 */
#include "net_test_support.h"

#if !defined(JH_NET_TEST_SERVICES)

#include <hal/network/hal_udp.h>
#include <hal/network/http/hal_http_files.h>
#include <hal/network/http/hal_http_server.h>
#include <hal/network/net_commands/hal_net_commands.h>
#include <hal/network/net_console/hal_net_console.h>
#include <hal/network/websocket/hal_websocket.h>
#include <hal/system/hal_system.h>

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint16_t kHttpPort = 18195u;
constexpr uint16_t kWebSocketPort = 18196u;
constexpr uint16_t kIpv6UdpPort = 18199u;
constexpr uint16_t kIpv6TcpPort = 18200u;
constexpr uint16_t kConsolePort = 18201u;
constexpr const char *kConsolePassword = "jh-esp-console";
constexpr const char *kFilePath = "/www/hello.txt";
constexpr const char *kFileBody = "JH-ESP-FILE\n";
constexpr const char *kUploadPath = "/www/upload.txt";
constexpr uint32_t kServeTimeoutMs = 240000u;

struct Counters {
  unsigned http;
  unsigned ws_messages;
  unsigned commands;
  unsigned console_lines;
  unsigned ipv6_udp;
  unsigned ipv6_tcp;
};

Counters s_counters;
char s_upload[192];
size_t s_upload_length = 0u;
bool s_has_upload = false;
hal_udp_socket_t s_ipv6_udp = nullptr;
hal_tcp_listener_t s_ipv6_listener = nullptr;
hal_tcp_socket_t s_ipv6_client = nullptr;

hal_status_t route_page(const hal_http_request_t *request,
                        hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;
  ++s_counters.http;
  const hal_status_t status =
      hal_http_response_set_content_type(response, "text/plain");
  return status == HAL_OK
             ? hal_http_response_write_str(response, "JH-ESP-HTTP-OK")
             : status;
}

hal_status_t route_echo(const hal_http_request_t *request,
                        hal_http_response_t *response, void *user) {
  (void)user;
  ++s_counters.http;
  hal_status_t status = hal_http_response_set_status(response, 201u, "Created");
  if (status == HAL_OK) {
    status =
        hal_http_response_write(response, request->body, request->body_len);
  }
  return status;
}

hal_status_t route_fail(const hal_http_request_t *, hal_http_response_t *,
                        void *) {
  ++s_counters.http;
  return HAL_EIO;
}

const char *file_body(const char *path, size_t *out_length) {
  if (strcmp(path, kFilePath) == 0) {
    *out_length = strlen(kFileBody);
    return kFileBody;
  }
  if (strcmp(path, kUploadPath) == 0 && s_has_upload) {
    *out_length = s_upload_length;
    return s_upload;
  }
  return nullptr;
}

hal_status_t file_stat(const char *path, hal_http_file_info_t *out_info,
                       void *) {
  size_t length = 0u;
  *out_info = {};
  if (file_body(path, &length) == nullptr) {
    return HAL_ENOENT;
  }
  out_info->exists = true;
  out_info->size = length;
  out_info->mtime = s_has_upload ? 2u : 1u;
  out_info->content_type = "text/plain";
  return HAL_OK;
}

hal_status_t file_read(const char *path, size_t offset, void *buffer,
                       size_t max_len, size_t *out_len, void *) {
  size_t length = 0u;
  const char *body = file_body(path, &length);
  if (body == nullptr) {
    return HAL_ENOENT;
  }
  const size_t count =
      offset >= length
          ? 0u
          : (length - offset < max_len ? length - offset : max_len);
  memcpy(buffer, body + offset, count);
  *out_len = count;
  return HAL_OK;
}

hal_status_t file_write(const char *path, size_t offset, const void *data,
                        size_t len, bool final, void *) {
  (void) final;
  if (strcmp(path, kUploadPath) != 0 || offset + len > sizeof(s_upload)) {
    return HAL_EOVERFLOW;
  }
  memcpy(s_upload + offset, data, len);
  s_upload_length = offset + len;
  s_has_upload = true;
  return HAL_OK;
}

// Uploads carry the token header; anything else is refused with 403.
hal_status_t file_authorize(const hal_http_request_t *request,
                            hal_http_file_upload_t, void *) {
  const char *token = hal_http_request_get_header(request, "X-JH-Token");
  return token != nullptr && strcmp(token, "jh-esp-upload") == 0 ? HAL_OK
                                                                 : HAL_EPERM;
}

hal_status_t command_ping(const hal_net_command_request_t *,
                          hal_net_command_response_t *response, void *) {
  ++s_counters.commands;
  return hal_net_command_response_write_str(response, "pong");
}

void websocket_message(hal_websocket_client_t client,
                       hal_websocket_message_type_t type, const uint8_t *data,
                       size_t length, void *user) {
  (void)user;
  if (type == HAL_WEBSOCKET_MESSAGE_TEXT && length > 0u && data[0] == '{') {
    (void)hal_net_commands_handle_websocket_message(
        client, type, data, length, HAL_NET_COMMANDS_FORMAT_JSON);
    return;
  }
  if (type == HAL_WEBSOCKET_MESSAGE_TEXT && length == 13u &&
      memcmp(data, "JH-ESP-WS-REQ", 13u) == 0) {
    ++s_counters.ws_messages;
    (void)hal_websocket_send_text(client, "JH-ESP-WS-OK");
  }
}

hal_status_t console_line(hal_net_console_client_t client, const char *line,
                          void *) {
  ++s_counters.console_lines;
  return strcmp(line, "ping") == 0
             ? hal_net_console_write_text_to(client, "JH-ESP-CONSOLE-OK\r\n")
             : HAL_EINVAL;
}

hal_net_endpoint_t any_ipv6(uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET6;
  endpoint.addr_len = HAL_NET_IPV6_ADDR_LEN;
  endpoint.port = port;
  return endpoint;
}

hal_status_t start_ipv6(void) {
  const hal_net_endpoint_t udp_local = any_ipv6(kIpv6UdpPort);
  hal_status_t status = hal_udp_socket_open_ex(&s_ipv6_udp);
  if (status == HAL_OK) {
    status = hal_udp_socket_bind_ex(s_ipv6_udp, &udp_local);
  }
  const hal_net_endpoint_t tcp_local = any_ipv6(kIpv6TcpPort);
  if (status == HAL_OK) {
    status = hal_tcp_listener_open_ex(&s_ipv6_listener);
  }
  if (status == HAL_OK) {
    status = hal_tcp_listener_bind_ex(s_ipv6_listener, &tcp_local);
  }
  if (status == HAL_OK) {
    status = hal_tcp_listener_listen_ex(s_ipv6_listener, 1u);
  }
  return status;
}

void stop_ipv6(void) {
  hal_tcp_socket_close(s_ipv6_client);
  hal_tcp_listener_close(s_ipv6_listener);
  hal_udp_socket_close(s_ipv6_udp);
  s_ipv6_client = nullptr;
  s_ipv6_listener = nullptr;
  s_ipv6_udp = nullptr;
}

// Echo one IPv6 datagram and one IPv6 TCP chunk per poll.
void poll_ipv6(void) {
  uint8_t buffer[64];
  hal_net_endpoint_t sender = {};
  size_t received = 0u;
  if (s_ipv6_udp != nullptr &&
      hal_udp_socket_recvfrom_ex(s_ipv6_udp, buffer, sizeof(buffer), &sender,
                                 0u, &received) == HAL_OK &&
      received > 0u && sender.family == HAL_NET_AF_INET6) {
    size_t sent = 0u;
    if (hal_udp_socket_sendto_ex(s_ipv6_udp, buffer, received, &sender,
                                 &sent) == HAL_OK) {
      ++s_counters.ipv6_udp;
    }
  }
  if (s_ipv6_client == nullptr && s_ipv6_listener != nullptr) {
    hal_net_endpoint_t remote = {};
    if (hal_tcp_listener_accept_ex(s_ipv6_listener, &remote, 0u,
                                   &s_ipv6_client) != HAL_OK ||
        remote.family != HAL_NET_AF_INET6) {
      hal_tcp_socket_close(s_ipv6_client);
      s_ipv6_client = nullptr;
    }
  }
  if (s_ipv6_client != nullptr) {
    const hal_status_t status = hal_tcp_socket_recv_ex(
        s_ipv6_client, buffer, sizeof(buffer), 0u, &received);
    if (status == HAL_OK && received > 0u) {
      size_t sent = 0u;
      if (hal_tcp_socket_send_ex(s_ipv6_client, buffer, received, &sent) ==
          HAL_OK) {
        ++s_counters.ipv6_tcp;
      }
    } else if (status != HAL_ETIMEOUT && status != HAL_EAGAIN) {
      hal_tcp_socket_close(s_ipv6_client);
      s_ipv6_client = nullptr;
    }
  }
}

hal_status_t start_servers(void) {
  hal_net_endpoint_t http_local = {};
  hal_net_endpoint_t ws_local = {};
  hal_status_t status = HAL_OK;
  if (!net_test_endpoint(g_net_test_local_ip, kHttpPort, &http_local) ||
      !net_test_endpoint(g_net_test_local_ip, kWebSocketPort, &ws_local)) {
    status = HAL_EINVAL;
  }
  const hal_http_method_t page_methods[] = {HAL_HTTP_METHOD_GET,
                                            HAL_HTTP_METHOD_HEAD};
  for (hal_http_method_t method : page_methods) {
    if (status == HAL_OK) {
      status = hal_http_server_route(method, "/jh-net", route_page, nullptr);
    }
  }
  if (status == HAL_OK) {
    status = hal_http_server_route(HAL_HTTP_METHOD_POST, "/echo", route_echo,
                                   nullptr);
  }
  if (status == HAL_OK) {
    status = hal_http_server_route(HAL_HTTP_METHOD_GET, "/fail", route_fail,
                                   nullptr);
  }
  hal_http_files_config_t files = {};
  files.url_prefix = "/fs";
  files.fs_root = "/www";
  files.enable_upload = true;
  files.stat = file_stat;
  files.read = file_read;
  files.write = file_write;
  files.authorize_upload = file_authorize;
  if (status == HAL_OK) {
    status = hal_http_files_mount(&files);
  }
  if (status == HAL_OK) {
    status = hal_net_commands_register("ping", command_ping, nullptr);
  }
  if (status == HAL_OK) {
    status = hal_net_commands_register_http_route("/cmd",
                                                  HAL_NET_COMMANDS_FORMAT_JSON);
  }
  if (status == HAL_OK) {
    status = hal_http_server_start_ex(&http_local);
  }
  hal_websocket_callbacks_t callbacks = {};
  callbacks.on_message = websocket_message;
  if (status == HAL_OK) {
    status = hal_websocket_server_set_callbacks(&callbacks, nullptr);
  }
  if (status == HAL_OK) {
    status = hal_websocket_server_start_ex(&ws_local, "/ws");
  }
  if (status == HAL_OK) {
    status = hal_net_console_set_callbacks(nullptr, console_line, nullptr);
  }
  if (status == HAL_OK) {
    status = hal_net_console_start(kConsolePort, kConsolePassword);
  }
  if (status == HAL_OK) {
    status = start_ipv6();
  }
  return status;
}

void stop_servers(void) {
  stop_ipv6();
  hal_net_console_stop();
  hal_websocket_server_stop();
  hal_http_server_stop();
  hal_http_server_clear_routes();
  hal_http_files_clear();
  (void)hal_net_commands_clear();
}

void announce(unsigned phase) {
  char mac[24] = {};
  (void)hal_wifi_get_mac_ex(mac, sizeof(mac));
  char line[192];
  (void)snprintf(line, sizeof(line),
                 "check=serve phase=%u ip=%s mac=%s http=%u ws=%u console=%u "
                 "ipv6_udp=%u ipv6_tcp=%u",
                 phase, g_net_test_local_ip, mac, (unsigned)kHttpPort,
                 (unsigned)kWebSocketPort, (unsigned)kConsolePort,
                 (unsigned)kIpv6UdpPort, (unsigned)kIpv6TcpPort);
  net_test_announce(line);
}

// Serve until the host sends @p command; false on timeout.
bool serve_until(const char *command) {
  const uint32_t start = hal_millis();
  while (hal_millis() - start < kServeTimeoutMs) {
    hal_http_server_poll();
    hal_websocket_server_poll();
    hal_net_console_poll();
    poll_ipv6();
    const char *line = net_test_read_line();
    if (line != nullptr && strcmp(line, command) == 0) {
      return true;
    }
    hal_delay_ms(1u);
  }
  return false;
}

void report_counters(const char *check, bool served) {
  char extra[128];
  (void)snprintf(extra, sizeof(extra),
                 "http=%u ws=%u commands=%u console=%u ipv6_udp=%u ipv6_tcp=%u",
                 s_counters.http, s_counters.ws_messages, s_counters.commands,
                 s_counters.console_lines, s_counters.ipv6_udp,
                 s_counters.ipv6_tcp);
  net_test_report(check,
                  served && s_counters.http > 0u &&
                      s_counters.ws_messages > 0u && s_counters.commands > 0u &&
                      s_counters.console_lines > 0u,
                  served ? HAL_OK : HAL_ETIMEOUT, extra);
}

} // namespace

void net_test_run_servers(void) {
  hal_status_t status = start_servers();
  net_test_report("servers_start", status == HAL_OK, status, "");
  if (status != HAL_OK) {
    stop_servers();
    return;
  }
  announce(1u);
  report_counters("servers_probe", serve_until("RESTART"));

  // Stop and start every server again; the host probes them once more.
  stop_servers();
  s_counters = {};
  status = start_servers();
  net_test_report("servers_restart", status == HAL_OK, status, "");
  if (status == HAL_OK) {
    announce(2u);
    const bool served = serve_until("STOP");
    net_test_report("servers_reprobe",
                    served && s_counters.http > 0u &&
                        s_counters.ws_messages > 0u,
                    served ? HAL_OK : HAL_ETIMEOUT, "");
  }
  // Stop with the host's clients still connected; the host checks that every
  // connection ends and the ports refuse new ones.
  stop_servers();
  const bool stopped = !hal_http_server_is_running() &&
                       !hal_websocket_server_is_running() &&
                       !hal_net_console_is_running();
  net_test_announce("check=stopped");
  net_test_report("servers_stop",
                  stopped && net_test_wait_for_line("DONE", 60000u), HAL_OK,
                  "");
}

#endif /* !JH_NET_TEST_SERVICES */
