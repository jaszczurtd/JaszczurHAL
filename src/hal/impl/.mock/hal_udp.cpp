#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"

#ifdef HAL_ENABLE_UDP

#include "../../hal_serial.h"
#include "../../hal_udp.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

#define MOCK_UDP_HOST_BUF_SIZE 128u
#define MOCK_UDP_PAYLOAD_BUF_SIZE 512u

struct hal_udp_socket_impl_t {
  bool in_use;
  bool bound;
  bool packet_started;

  hal_net_endpoint_t local_endpoint;
  hal_net_endpoint_t remote_endpoint;

  char last_begin_packet_host[MOCK_UDP_HOST_BUF_SIZE];
  uint16_t last_begin_packet_port;
  hal_net_endpoint_t last_tx_remote;
  bool last_tx_remote_valid;

  char remote_ip[HAL_UDP_IP_STR_LEN];
  uint16_t remote_port;

  uint8_t rx_payload[MOCK_UDP_PAYLOAD_BUF_SIZE];
  uint16_t rx_len;
  uint16_t rx_pos;
  bool rx_pending;

  uint8_t tx_payload[MOCK_UDP_PAYLOAD_BUF_SIZE];
  uint16_t tx_len;
  bool end_packet_called;
  bool end_packet_result;
};

static hal_udp_socket_impl_t s_udp_pool[HAL_UDP_SOCKET_MAX_INSTANCES];
static hal_udp_socket_t s_default_udp = NULL;

static bool validate_out(char *out, size_t out_size, const char *fn) {
  if (!out) {
    hal_derr("%s: output buffer is NULL", fn);
    return false;
  }
  if (out_size == 0u) {
    hal_derr("%s: output buffer size is 0", fn);
    return false;
  }
  return true;
}

static bool validate_non_empty(const char *value, const char *fn,
                               const char *name) {
  if (!value || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return false;
  }
  return true;
}

static bool validate_endpoint(const hal_net_endpoint_t *endpoint,
                              const char *fn, const char *name) {
  if (!endpoint) {
    hal_derr("%s: %s endpoint is NULL", fn, name);
    return false;
  }
  if (endpoint->family != HAL_NET_AF_INET) {
    hal_derr("%s: %s endpoint family is unsupported", fn, name);
    return false;
  }
  if (endpoint->port == 0u) {
    hal_derr("%s: %s endpoint port must be > 0", fn, name);
    return false;
  }
  return true;
}

static void endpoint_to_ip_string(const hal_net_endpoint_t *endpoint, char *out,
                                  size_t out_size) {
  (void)snprintf(out, out_size, "%u.%u.%u.%u", (unsigned)endpoint->addr[0],
                 (unsigned)endpoint->addr[1], (unsigned)endpoint->addr[2],
                 (unsigned)endpoint->addr[3]);
}

static hal_net_endpoint_t endpoint_from_ip_string(const char *ip,
                                                  uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.port = port;

  unsigned octets[HAL_NET_IPV4_ADDR_LEN] = {0u, 0u, 0u, 0u};
  if (ip &&
      sscanf(ip, "%u.%u.%u.%u", &octets[0], &octets[1], &octets[2],
             &octets[3]) == 4 &&
      octets[0] <= 255u && octets[1] <= 255u && octets[2] <= 255u &&
      octets[3] <= 255u) {
    endpoint.addr[0] = (uint8_t)octets[0];
    endpoint.addr[1] = (uint8_t)octets[1];
    endpoint.addr[2] = (uint8_t)octets[2];
    endpoint.addr[3] = (uint8_t)octets[3];
  }

  return endpoint;
}

static bool endpoint_is_zero_ipv4(const hal_net_endpoint_t *endpoint) {
  return endpoint->addr[0] == 0u && endpoint->addr[1] == 0u &&
         endpoint->addr[2] == 0u && endpoint->addr[3] == 0u;
}

static void reset_remote(hal_udp_socket_impl_t *socket) {
  socket->remote_endpoint = {};
  socket->remote_endpoint.family = HAL_NET_AF_UNSPEC;
  socket->remote_endpoint.port = 0u;
  (void)snprintf(socket->remote_ip, sizeof(socket->remote_ip), "%s", "0.0.0.0");
  socket->remote_port = 0u;
}

static void reset_socket(hal_udp_socket_impl_t *socket) {
  memset(socket, 0, sizeof(*socket));
  reset_remote(socket);
  socket->end_packet_result = true;
}

static void reset_socket_io(hal_udp_socket_impl_t *socket) {
  socket->packet_started = false;
  socket->last_begin_packet_host[0] = '\0';
  socket->last_begin_packet_port = 0u;
  socket->last_tx_remote = {};
  socket->last_tx_remote.family = HAL_NET_AF_UNSPEC;
  socket->last_tx_remote_valid = false;
  socket->rx_len = 0u;
  socket->rx_pos = 0u;
  socket->rx_pending = false;
  socket->tx_len = 0u;
  socket->end_packet_called = false;
  reset_remote(socket);
}

static bool is_valid_socket(hal_udp_socket_t socket) {
  for (size_t i = 0u; i < HAL_UDP_SOCKET_MAX_INSTANCES; ++i) {
    if (socket == &s_udp_pool[i] && s_udp_pool[i].in_use) {
      return true;
    }
  }
  return false;
}

static hal_udp_socket_t default_socket(void) {
  if (is_valid_socket(s_default_udp)) {
    return s_default_udp;
  }
  return NULL;
}

static int socket_read(hal_udp_socket_impl_t *socket, uint8_t *buffer,
                       uint16_t max_len) {
  if (!socket->bound || !socket->rx_pending || max_len == 0u) {
    return 0;
  }

  const uint16_t available = (uint16_t)(socket->rx_len - socket->rx_pos);
  uint16_t to_copy = max_len;
  if (to_copy > available) {
    to_copy = available;
  }

  memcpy(buffer, socket->rx_payload + socket->rx_pos, to_copy);
  socket->rx_pos = (uint16_t)(socket->rx_pos + to_copy);

  if (socket->rx_pos >= socket->rx_len) {
    socket->rx_pending = false;
    socket->rx_len = 0u;
    socket->rx_pos = 0u;
  }

  return (int)to_copy;
}

void hal_mock_udp_reset(void) {
  for (size_t i = 0u; i < HAL_UDP_SOCKET_MAX_INSTANCES; ++i) {
    reset_socket(&s_udp_pool[i]);
  }
  s_default_udp = NULL;
}

hal_status_t hal_udp_socket_open_ex(hal_udp_socket_t *out_socket) {
  if (!out_socket) {
    return HAL_EINVAL;
  }
  *out_socket = NULL;
  for (size_t i = 0u; i < HAL_UDP_SOCKET_MAX_INSTANCES; ++i) {
    if (!s_udp_pool[i].in_use) {
      reset_socket(&s_udp_pool[i]);
      s_udp_pool[i].in_use = true;
      *out_socket = &s_udp_pool[i];
      return HAL_OK;
    }
  }

  hal_derr("hal_udp_socket_open: socket pool exhausted");
  return HAL_ENOMEM;
}

hal_udp_socket_t hal_udp_socket_open(void) {
  hal_udp_socket_t socket = NULL;
  (void)hal_udp_socket_open_ex(&socket);
  return socket;
}

hal_status_t hal_udp_socket_bind_ex(hal_udp_socket_t socket,
                                    const hal_net_endpoint_t *local) {
  if (!validate_endpoint(local, "hal_udp_socket_bind", "local")) {
    return HAL_EINVAL;
  }
  if (!is_valid_socket(socket)) {
    hal_derr("hal_udp_socket_bind: socket handle is invalid");
    return HAL_EINVAL;
  }

  socket->local_endpoint = *local;
  socket->bound = true;
  reset_socket_io(socket);
  return HAL_OK;
}

bool hal_udp_socket_bind(hal_udp_socket_t socket,
                         const hal_net_endpoint_t *local) {
  return hal_status_to_bool(hal_udp_socket_bind_ex(socket, local));
}

hal_status_t hal_udp_socket_sendto_ex(hal_udp_socket_t socket, const void *data,
                                      size_t len,
                                      const hal_net_endpoint_t *remote,
                                      size_t *out_sent) {
  if (out_sent) {
    *out_sent = 0u;
  }
  if (!out_sent) {
    return HAL_EINVAL;
  }
  if (len > 0u && data == NULL) {
    hal_derr("hal_udp_socket_sendto: data is NULL while len > 0");
    return HAL_EINVAL;
  }
  if (!validate_endpoint(remote, "hal_udp_socket_sendto", "remote")) {
    return HAL_EINVAL;
  }
  if (!is_valid_socket(socket)) {
    hal_derr("hal_udp_socket_sendto: socket is invalid");
    return HAL_EINVAL;
  }
  if (!socket->bound) {
    hal_derr("hal_udp_socket_sendto: socket is not bound");
    return HAL_ESTATE;
  }

  size_t to_copy = len;
  if (to_copy > MOCK_UDP_PAYLOAD_BUF_SIZE) {
    to_copy = MOCK_UDP_PAYLOAD_BUF_SIZE;
  }

  if (to_copy > 0u) {
    memcpy(socket->tx_payload, data, to_copy);
  }

  socket->tx_len = (uint16_t)to_copy;
  socket->last_tx_remote = *remote;
  socket->last_tx_remote_valid = true;
  endpoint_to_ip_string(remote, socket->last_begin_packet_host,
                        sizeof(socket->last_begin_packet_host));
  socket->last_begin_packet_port = remote->port;
  *out_sent = to_copy;
  return HAL_OK;
}

int hal_udp_socket_sendto(hal_udp_socket_t socket, const void *data, size_t len,
                          const hal_net_endpoint_t *remote) {
  size_t sent = 0u;
  return hal_status_is_ok(
             hal_udp_socket_sendto_ex(socket, data, len, remote, &sent))
             ? (int)sent
             : -1;
}

hal_status_t hal_udp_socket_recvfrom_ex(hal_udp_socket_t socket, void *buffer,
                                        size_t max_len,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        size_t *out_received) {
  (void)timeout_ms;
  if (out_received) {
    *out_received = 0u;
  }
  if (!out_received) {
    return HAL_EINVAL;
  }
  if (max_len > 0u && buffer == NULL) {
    hal_derr("hal_udp_socket_recvfrom: buffer is NULL while max_len > 0");
    return HAL_EINVAL;
  }
  if (!is_valid_socket(socket)) {
    hal_derr("hal_udp_socket_recvfrom: socket is invalid");
    return HAL_EINVAL;
  }
  if (!socket->bound) {
    hal_derr("hal_udp_socket_recvfrom: socket is not bound");
    return HAL_ESTATE;
  }
  if (!socket->rx_pending) {
    return HAL_OK;
  }

  if (remote) {
    *remote = socket->remote_endpoint;
  }

  if (max_len == 0u) {
    return HAL_OK;
  }

  size_t read_max = max_len;
  if (read_max > 65535u) {
    read_max = 65535u;
  }
  *out_received =
      (size_t)socket_read(socket, (uint8_t *)buffer, (uint16_t)read_max);
  return HAL_OK;
}

int hal_udp_socket_recvfrom(hal_udp_socket_t socket, void *buffer,
                            size_t max_len, hal_net_endpoint_t *remote,
                            uint32_t timeout_ms) {
  size_t received = 0u;
  return hal_status_is_ok(hal_udp_socket_recvfrom_ex(
             socket, buffer, max_len, remote, timeout_ms, &received))
             ? (int)received
             : -1;
}

bool hal_udp_socket_can_recv(hal_udp_socket_t socket) {
  return is_valid_socket(socket) && socket->bound && socket->rx_pending;
}

bool hal_udp_socket_can_send(hal_udp_socket_t socket) {
  return is_valid_socket(socket) && socket->bound;
}

void hal_udp_socket_close(hal_udp_socket_t socket) {
  if (!is_valid_socket(socket)) {
    return;
  }
  if (s_default_udp == socket) {
    s_default_udp = NULL;
  }
  reset_socket(socket);
}

hal_status_t hal_udp_begin_ex(uint16_t local_port) {
  if (local_port == 0u) {
    hal_derr("hal_udp_begin: local_port must be > 0");
    return HAL_EINVAL;
  }

  if (!is_valid_socket(s_default_udp)) {
    const hal_status_t open_status = hal_udp_socket_open_ex(&s_default_udp);
    if (hal_status_is_error(open_status)) {
      hal_derr("hal_udp_begin: socket allocation failed");
      return open_status;
    }
  }

  hal_net_endpoint_t local = {};
  local.family = HAL_NET_AF_INET;
  local.port = local_port;
  return hal_udp_socket_bind_ex(s_default_udp, &local);
}

bool hal_udp_begin(uint16_t local_port) {
  return hal_status_to_bool(hal_udp_begin_ex(local_port));
}

void hal_udp_stop(void) { hal_udp_socket_close(s_default_udp); }

hal_status_t hal_udp_parse_packet_ex(int *out_size) {
  if (!out_size) {
    return HAL_EINVAL;
  }
  *out_size = 0;
  hal_udp_socket_t socket = default_socket();
  if (!socket || !socket->bound) {
    return HAL_EUNINIT;
  }
  if (!socket->rx_pending) {
    return HAL_OK;
  }
  *out_size = (int)(socket->rx_len - socket->rx_pos);
  return HAL_OK;
}

int hal_udp_parse_packet(void) {
  int size = 0;
  (void)hal_udp_parse_packet_ex(&size);
  return size;
}

hal_status_t hal_udp_read_ex(uint8_t *buffer, uint16_t max_len,
                             uint16_t *out_read) {
  if (out_read) {
    *out_read = 0u;
  }
  if (!out_read) {
    return HAL_EINVAL;
  }
  if (max_len > 0u && buffer == NULL) {
    hal_derr("hal_udp_read: buffer is NULL while max_len > 0");
    return HAL_EINVAL;
  }

  hal_udp_socket_t socket = default_socket();
  if (!socket || !socket->bound) {
    return HAL_EUNINIT;
  }
  if (max_len == 0u) {
    return HAL_OK;
  }
  *out_read = (uint16_t)socket_read(socket, buffer, max_len);
  return HAL_OK;
}

int hal_udp_read(uint8_t *buffer, uint16_t max_len) {
  uint16_t read = 0u;
  const hal_status_t status = hal_udp_read_ex(buffer, max_len, &read);
  return hal_status_is_ok(status) || status == HAL_EUNINIT ? (int)read : -1;
}

hal_status_t hal_udp_remote_ip_ex(char *out, size_t out_size) {
  if (!validate_out(out, out_size, "hal_udp_remote_ip")) {
    return HAL_EINVAL;
  }

  hal_udp_socket_t socket = default_socket();
  if (!socket) {
    const int written = snprintf(out, out_size, "%s", "0.0.0.0");
    if (written < 0) {
      return HAL_EIO;
    }
    if ((size_t)written >= out_size) {
      return HAL_EOVERFLOW;
    }
    return HAL_EUNINIT;
  }
  const int written = snprintf(out, out_size, "%s", socket->remote_ip);
  if (written < 0) {
    hal_derr("hal_udp_remote_ip: snprintf failed");
    return HAL_EIO;
  }
  if ((size_t)written >= out_size) {
    return HAL_EOVERFLOW;
  }
  return socket->remote_port != 0u ? HAL_OK : HAL_ENOENT;
}

bool hal_udp_remote_ip(char *out, size_t out_size) {
  return hal_status_to_bool(hal_udp_remote_ip_ex(out, out_size));
}

hal_status_t hal_udp_remote_port_ex(uint16_t *out_port) {
  if (!out_port) {
    return HAL_EINVAL;
  }
  *out_port = 0u;
  hal_udp_socket_t socket = default_socket();
  if (!socket || !socket->bound) {
    return HAL_EUNINIT;
  }
  *out_port = socket->remote_port;
  return *out_port != 0u ? HAL_OK : HAL_ENOENT;
}

uint16_t hal_udp_remote_port(void) {
  uint16_t port = 0u;
  (void)hal_udp_remote_port_ex(&port);
  return port;
}

hal_status_t hal_udp_begin_packet_ex(const char *host_or_ip,
                                     uint16_t remote_port) {
  if (!validate_non_empty(host_or_ip, "hal_udp_begin_packet", "host_or_ip")) {
    return HAL_EINVAL;
  }
  if (remote_port == 0u) {
    hal_derr("hal_udp_begin_packet: remote_port must be > 0");
    return HAL_EINVAL;
  }

  hal_udp_socket_t socket = default_socket();
  if (!socket || !socket->bound) {
    hal_derr("hal_udp_begin_packet: UDP socket is not started");
    return HAL_EUNINIT;
  }

  snprintf(socket->last_begin_packet_host,
           sizeof(socket->last_begin_packet_host), "%s", host_or_ip);
  socket->last_begin_packet_port = remote_port;
  socket->packet_started = true;
  socket->tx_len = 0u;
  socket->end_packet_called = false;
  socket->last_tx_remote_valid = false;
  return HAL_OK;
}

bool hal_udp_begin_packet(const char *host_or_ip, uint16_t remote_port) {
  return hal_status_to_bool(hal_udp_begin_packet_ex(host_or_ip, remote_port));
}

hal_status_t hal_udp_begin_packet_remote_ex(void) {
  hal_udp_socket_t socket = default_socket();
  if (!socket || !socket->bound) {
    hal_derr("hal_udp_begin_packet_remote: UDP socket is not started");
    return HAL_EUNINIT;
  }
  if (socket->remote_port == 0u ||
      endpoint_is_zero_ipv4(&socket->remote_endpoint)) {
    hal_derr("hal_udp_begin_packet_remote: remote endpoint is not available");
    return HAL_ENOENT;
  }

  snprintf(socket->last_begin_packet_host,
           sizeof(socket->last_begin_packet_host), "%s", socket->remote_ip);
  socket->last_begin_packet_port = socket->remote_port;
  socket->packet_started = true;
  socket->tx_len = 0u;
  socket->end_packet_called = false;
  socket->last_tx_remote = socket->remote_endpoint;
  socket->last_tx_remote_valid = true;
  return HAL_OK;
}

bool hal_udp_begin_packet_remote(void) {
  return hal_status_to_bool(hal_udp_begin_packet_remote_ex());
}

hal_status_t hal_udp_write_ex(const uint8_t *data, uint16_t len,
                              uint16_t *out_written) {
  if (out_written) {
    *out_written = 0u;
  }
  if (!out_written) {
    return HAL_EINVAL;
  }
  if (len > 0u && data == NULL) {
    hal_derr("hal_udp_write: data is NULL while len > 0");
    return HAL_EINVAL;
  }

  hal_udp_socket_t socket = default_socket();
  if (!socket || !socket->bound) {
    return HAL_EUNINIT;
  }
  if (!socket->packet_started) {
    return HAL_ESTATE;
  }
  if (len == 0u) {
    return HAL_OK;
  }

  uint16_t room = (uint16_t)(MOCK_UDP_PAYLOAD_BUF_SIZE - socket->tx_len);
  uint16_t to_copy = len;
  if (to_copy > room) {
    to_copy = room;
  }

  if (to_copy > 0u) {
    memcpy(socket->tx_payload + socket->tx_len, data, to_copy);
    socket->tx_len = (uint16_t)(socket->tx_len + to_copy);
  }

  *out_written = to_copy;
  return to_copy == len ? HAL_OK : HAL_EOVERFLOW;
}

uint16_t hal_udp_write(const uint8_t *data, uint16_t len) {
  uint16_t written = 0u;
  (void)hal_udp_write_ex(data, len, &written);
  return written;
}

hal_status_t hal_udp_write_str_ex(const char *text, uint16_t *out_written) {
  if (!text) {
    hal_derr("hal_udp_write_str: text is NULL");
    return HAL_EINVAL;
  }
  if (!out_written) {
    return HAL_EINVAL;
  }
  const size_t len = strlen(text);
  if (len > UINT16_MAX) {
    *out_written = 0u;
    return HAL_EOVERFLOW;
  }
  return hal_udp_write_ex((const uint8_t *)text, (uint16_t)len, out_written);
}

uint16_t hal_udp_write_str(const char *text) {
  uint16_t written = 0u;
  (void)hal_udp_write_str_ex(text, &written);
  return written;
}

hal_status_t hal_udp_end_packet_ex(void) {
  hal_udp_socket_t socket = default_socket();
  if (!socket || !socket->bound || !socket->packet_started) {
    return HAL_ESTATE;
  }

  socket->packet_started = false;
  socket->end_packet_called = true;
  return socket->end_packet_result ? HAL_OK : HAL_EIO;
}

bool hal_udp_end_packet(void) {
  return hal_status_to_bool(hal_udp_end_packet_ex());
}

void hal_mock_udp_inject_packet_to(hal_udp_socket_t socket,
                                   const char *remote_ip, uint16_t remote_port,
                                   const uint8_t *payload, uint16_t len) {
  if (!is_valid_socket(socket)) {
    return;
  }

  const char *safe_ip =
      (remote_ip && remote_ip[0] != '\0') ? remote_ip : "0.0.0.0";
  socket->remote_endpoint = endpoint_from_ip_string(safe_ip, remote_port);
  snprintf(socket->remote_ip, sizeof(socket->remote_ip), "%s", safe_ip);
  socket->remote_port = remote_port;

  socket->rx_len = len;
  if (socket->rx_len > MOCK_UDP_PAYLOAD_BUF_SIZE) {
    socket->rx_len = MOCK_UDP_PAYLOAD_BUF_SIZE;
  }

  if (payload && socket->rx_len > 0u) {
    memcpy(socket->rx_payload, payload, socket->rx_len);
  }

  socket->rx_pos = 0u;
  socket->rx_pending = (socket->rx_len > 0u);
}

void hal_mock_udp_inject_packet(const char *remote_ip, uint16_t remote_port,
                                const uint8_t *payload, uint16_t len) {
  hal_mock_udp_inject_packet_to(default_socket(), remote_ip, remote_port,
                                payload, len);
}

void hal_mock_udp_set_end_packet_result(bool result) {
  hal_udp_socket_t socket = default_socket();
  if (socket) {
    socket->end_packet_result = result;
  }
}

void hal_mock_udp_set_end_packet_result_for(hal_udp_socket_t socket,
                                            bool result) {
  if (is_valid_socket(socket)) {
    socket->end_packet_result = result;
  }
}

uint16_t hal_mock_udp_get_local_port(void) {
  return hal_mock_udp_get_local_port_for(default_socket());
}

uint16_t hal_mock_udp_get_local_port_for(hal_udp_socket_t socket) {
  if (!is_valid_socket(socket) || !socket->bound) {
    return 0u;
  }
  return socket->local_endpoint.port;
}

const char *hal_mock_udp_get_last_begin_packet_host(void) {
  hal_udp_socket_t socket = default_socket();
  return socket ? socket->last_begin_packet_host : "";
}

uint16_t hal_mock_udp_get_last_begin_packet_port(void) {
  hal_udp_socket_t socket = default_socket();
  return socket ? socket->last_begin_packet_port : 0u;
}

const uint8_t *hal_mock_udp_get_last_tx_payload(void) {
  return hal_mock_udp_get_last_tx_payload_for(default_socket());
}

const uint8_t *hal_mock_udp_get_last_tx_payload_for(hal_udp_socket_t socket) {
  if (!is_valid_socket(socket)) {
    return NULL;
  }
  return socket->tx_payload;
}

uint16_t hal_mock_udp_get_last_tx_len(void) {
  return hal_mock_udp_get_last_tx_len_for(default_socket());
}

uint16_t hal_mock_udp_get_last_tx_len_for(hal_udp_socket_t socket) {
  if (!is_valid_socket(socket)) {
    return 0u;
  }
  return socket->tx_len;
}

bool hal_mock_udp_get_last_tx_remote_for(hal_udp_socket_t socket,
                                         hal_net_endpoint_t *out) {
  if (!is_valid_socket(socket) || !socket->last_tx_remote_valid || !out) {
    return false;
  }
  *out = socket->last_tx_remote;
  return true;
}

bool hal_mock_udp_was_end_packet_called(void) {
  hal_udp_socket_t socket = default_socket();
  return socket ? socket->end_packet_called : false;
}

#endif /* HAL_ENABLE_UDP */
#endif // HAL_TARGET_IS_MOCK
