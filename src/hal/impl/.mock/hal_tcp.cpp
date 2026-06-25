#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"

#ifdef HAL_ENABLE_TCP

#include "../../hal_serial.h"
#include "../../hal_tcp.h"
#include "hal_mock.h"

#include <string.h>

#define MOCK_TCP_PAYLOAD_BUF_SIZE 512u

struct hal_tcp_socket_impl_t {
  bool in_use;
  bool connected;
  hal_net_endpoint_t remote_endpoint;

  uint8_t rx_payload[MOCK_TCP_PAYLOAD_BUF_SIZE];
  uint16_t rx_len;
  uint16_t rx_pos;

  uint8_t tx_payload[MOCK_TCP_PAYLOAD_BUF_SIZE];
  uint16_t tx_len;
};

struct hal_tcp_listener_impl_t {
  bool in_use;
  bool bound;
  bool listening;
  hal_net_endpoint_t local_endpoint;
  uint8_t backlog;
  hal_net_endpoint_t pending[HAL_TCP_LISTENER_BACKLOG_MAX];
  uint8_t pending_head;
  uint8_t pending_count;
};

static hal_tcp_socket_impl_t s_tcp_pool[HAL_TCP_SOCKET_MAX_INSTANCES];
static hal_tcp_listener_impl_t
    s_tcp_listener_pool[HAL_TCP_LISTENER_MAX_INSTANCES];
static bool s_connect_result = true;

static void reset_endpoint(hal_net_endpoint_t *endpoint) {
  if (!endpoint) {
    return;
  }
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->family = HAL_NET_AF_UNSPEC;
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

static uint8_t capped_backlog(uint8_t backlog) {
  if ((size_t)backlog > (size_t)HAL_TCP_LISTENER_BACKLOG_MAX) {
    return (uint8_t)HAL_TCP_LISTENER_BACKLOG_MAX;
  }
  return backlog;
}

static void reset_socket(hal_tcp_socket_impl_t *socket) {
  memset(socket, 0, sizeof(*socket));
  reset_endpoint(&socket->remote_endpoint);
}

static void reset_listener(hal_tcp_listener_impl_t *listener) {
  memset(listener, 0, sizeof(*listener));
  reset_endpoint(&listener->local_endpoint);
  for (size_t i = 0u; i < HAL_TCP_LISTENER_BACKLOG_MAX; ++i) {
    reset_endpoint(&listener->pending[i]);
  }
}

static bool is_valid_socket(hal_tcp_socket_t socket) {
  for (size_t i = 0u; i < HAL_TCP_SOCKET_MAX_INSTANCES; ++i) {
    if (socket == &s_tcp_pool[i] && s_tcp_pool[i].in_use) {
      return true;
    }
  }
  return false;
}

static bool is_valid_listener(hal_tcp_listener_t listener) {
  for (size_t i = 0u; i < HAL_TCP_LISTENER_MAX_INSTANCES; ++i) {
    if (listener == &s_tcp_listener_pool[i] && s_tcp_listener_pool[i].in_use) {
      return true;
    }
  }
  return false;
}

static hal_tcp_socket_t allocate_socket(void) {
  for (size_t i = 0u; i < HAL_TCP_SOCKET_MAX_INSTANCES; ++i) {
    if (!s_tcp_pool[i].in_use) {
      reset_socket(&s_tcp_pool[i]);
      s_tcp_pool[i].in_use = true;
      return &s_tcp_pool[i];
    }
  }
  return NULL;
}

static int socket_read(hal_tcp_socket_impl_t *socket, uint8_t *buffer,
                       uint16_t max_len) {
  if (!socket->connected || max_len == 0u || socket->rx_pos >= socket->rx_len) {
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
    socket->rx_len = 0u;
    socket->rx_pos = 0u;
  }

  return (int)to_copy;
}

void hal_mock_tcp_reset(void) {
  for (size_t i = 0u; i < HAL_TCP_SOCKET_MAX_INSTANCES; ++i) {
    reset_socket(&s_tcp_pool[i]);
  }
  for (size_t i = 0u; i < HAL_TCP_LISTENER_MAX_INSTANCES; ++i) {
    reset_listener(&s_tcp_listener_pool[i]);
  }
  s_connect_result = true;
}

void hal_mock_tcp_set_connect_result(bool result) { s_connect_result = result; }

hal_tcp_socket_t hal_tcp_socket_open(void) {
  hal_tcp_socket_t socket = allocate_socket();
  if (!socket) {
    hal_derr("hal_tcp_socket_open: socket pool exhausted");
  }
  return socket;
}

bool hal_tcp_socket_connect(hal_tcp_socket_t socket,
                            const hal_net_endpoint_t *remote,
                            uint32_t timeout_ms) {
  (void)timeout_ms;

  if (!validate_endpoint(remote, "hal_tcp_socket_connect", "remote")) {
    return false;
  }
  if (!is_valid_socket(socket)) {
    hal_derr("hal_tcp_socket_connect: socket handle is invalid");
    return false;
  }

  socket->connected = false;
  socket->remote_endpoint = *remote;
  socket->rx_len = 0u;
  socket->rx_pos = 0u;
  socket->tx_len = 0u;

  if (!s_connect_result) {
    return false;
  }

  socket->connected = true;
  return true;
}

int hal_tcp_socket_send(hal_tcp_socket_t socket, const void *data, size_t len) {
  if (len > 0u && data == NULL) {
    hal_derr("hal_tcp_socket_send: data is NULL while len > 0");
    return -1;
  }
  if (!is_valid_socket(socket) || !socket->connected) {
    hal_derr("hal_tcp_socket_send: socket is invalid or not connected");
    return -1;
  }

  size_t to_copy = len;
  if (to_copy > MOCK_TCP_PAYLOAD_BUF_SIZE) {
    to_copy = MOCK_TCP_PAYLOAD_BUF_SIZE;
  }

  if (to_copy > 0u) {
    memcpy(socket->tx_payload, data, to_copy);
  }
  socket->tx_len = (uint16_t)to_copy;

  return (int)to_copy;
}

int hal_tcp_socket_recv(hal_tcp_socket_t socket, void *buffer, size_t max_len,
                        uint32_t timeout_ms) {
  (void)timeout_ms;

  if (max_len > 0u && buffer == NULL) {
    hal_derr("hal_tcp_socket_recv: buffer is NULL while max_len > 0");
    return -1;
  }
  if (!is_valid_socket(socket) || !socket->connected) {
    hal_derr("hal_tcp_socket_recv: socket is invalid or not connected");
    return -1;
  }
  if (max_len == 0u) {
    return 0;
  }

  size_t read_max = max_len;
  if (read_max > 65535u) {
    read_max = 65535u;
  }
  return socket_read(socket, (uint8_t *)buffer, (uint16_t)read_max);
}

bool hal_tcp_socket_can_recv(hal_tcp_socket_t socket) {
  return is_valid_socket(socket) && socket->connected &&
         socket->rx_pos < socket->rx_len;
}

bool hal_tcp_socket_can_send(hal_tcp_socket_t socket) {
  return is_valid_socket(socket) && socket->connected;
}

bool hal_tcp_socket_is_connected(hal_tcp_socket_t socket) {
  return is_valid_socket(socket) && socket->connected;
}

void hal_tcp_socket_shutdown(hal_tcp_socket_t socket) {
  if (!is_valid_socket(socket)) {
    return;
  }

  socket->connected = false;
  socket->rx_len = 0u;
  socket->rx_pos = 0u;
}

void hal_tcp_socket_close(hal_tcp_socket_t socket) {
  if (!is_valid_socket(socket)) {
    return;
  }
  reset_socket(socket);
}

hal_tcp_listener_t hal_tcp_listener_open(void) {
  for (size_t i = 0u; i < HAL_TCP_LISTENER_MAX_INSTANCES; ++i) {
    if (!s_tcp_listener_pool[i].in_use) {
      reset_listener(&s_tcp_listener_pool[i]);
      s_tcp_listener_pool[i].in_use = true;
      return &s_tcp_listener_pool[i];
    }
  }

  hal_derr("hal_tcp_listener_open: listener pool exhausted");
  return NULL;
}

bool hal_tcp_listener_bind(hal_tcp_listener_t listener,
                           const hal_net_endpoint_t *local) {
  if (!validate_endpoint(local, "hal_tcp_listener_bind", "local")) {
    return false;
  }
  if (!is_valid_listener(listener)) {
    hal_derr("hal_tcp_listener_bind: listener handle is invalid");
    return false;
  }

  listener->local_endpoint = *local;
  listener->bound = true;
  listener->listening = false;
  listener->pending_head = 0u;
  listener->pending_count = 0u;
  return true;
}

bool hal_tcp_listener_listen(hal_tcp_listener_t listener, uint8_t backlog) {
  if (backlog == 0u) {
    hal_derr("hal_tcp_listener_listen: backlog must be > 0");
    return false;
  }
  if (!is_valid_listener(listener) || !listener->bound) {
    hal_derr("hal_tcp_listener_listen: listener is invalid or not bound");
    return false;
  }

  listener->backlog = capped_backlog(backlog);
  listener->listening = true;
  listener->pending_head = 0u;
  listener->pending_count = 0u;
  return true;
}

hal_tcp_socket_t hal_tcp_listener_accept(hal_tcp_listener_t listener,
                                         hal_net_endpoint_t *remote,
                                         uint32_t timeout_ms) {
  (void)timeout_ms;

  if (!is_valid_listener(listener) || !listener->listening) {
    hal_derr("hal_tcp_listener_accept: listener is invalid or not listening");
    return NULL;
  }
  if (listener->pending_count == 0u) {
    return NULL;
  }

  hal_tcp_socket_t socket = allocate_socket();
  if (!socket) {
    hal_derr("hal_tcp_listener_accept: socket pool exhausted");
    return NULL;
  }

  const hal_net_endpoint_t pending_remote =
      listener->pending[listener->pending_head];
  listener->pending_head =
      (uint8_t)((listener->pending_head + 1u) % HAL_TCP_LISTENER_BACKLOG_MAX);
  listener->pending_count--;

  socket->connected = true;
  socket->remote_endpoint = pending_remote;
  if (remote) {
    *remote = pending_remote;
  }

  return socket;
}

bool hal_tcp_listener_can_accept(hal_tcp_listener_t listener) {
  return is_valid_listener(listener) && listener->listening &&
         listener->pending_count > 0u;
}

void hal_tcp_listener_close(hal_tcp_listener_t listener) {
  if (!is_valid_listener(listener)) {
    return;
  }
  reset_listener(listener);
}

void hal_mock_tcp_inject_rx(hal_tcp_socket_t socket, const uint8_t *payload,
                            uint16_t len) {
  if (!is_valid_socket(socket)) {
    return;
  }

  socket->rx_len = len;
  if (socket->rx_len > MOCK_TCP_PAYLOAD_BUF_SIZE) {
    socket->rx_len = MOCK_TCP_PAYLOAD_BUF_SIZE;
  }
  if (payload && socket->rx_len > 0u) {
    memcpy(socket->rx_payload, payload, socket->rx_len);
  }
  socket->rx_pos = 0u;
}

const uint8_t *hal_mock_tcp_get_last_tx_payload(hal_tcp_socket_t socket) {
  if (!is_valid_socket(socket)) {
    return NULL;
  }
  return socket->tx_payload;
}

uint16_t hal_mock_tcp_get_last_tx_len(hal_tcp_socket_t socket) {
  if (!is_valid_socket(socket)) {
    return 0u;
  }
  return socket->tx_len;
}

bool hal_mock_tcp_get_remote_endpoint(hal_tcp_socket_t socket,
                                      hal_net_endpoint_t *out) {
  if (!is_valid_socket(socket) || !out) {
    return false;
  }
  *out = socket->remote_endpoint;
  return socket->remote_endpoint.family != HAL_NET_AF_UNSPEC;
}

bool hal_mock_tcp_listener_inject_client(hal_tcp_listener_t listener,
                                         const hal_net_endpoint_t *remote) {
  if (!validate_endpoint(remote, "hal_mock_tcp_listener_inject_client",
                         "remote")) {
    return false;
  }
  if (!is_valid_listener(listener) || !listener->listening) {
    return false;
  }
  if (listener->pending_count >= listener->backlog) {
    return false;
  }

  const uint8_t tail =
      (uint8_t)((listener->pending_head + listener->pending_count) %
                HAL_TCP_LISTENER_BACKLOG_MAX);
  listener->pending[tail] = *remote;
  listener->pending_count++;
  return true;
}

uint16_t hal_mock_tcp_listener_get_local_port(hal_tcp_listener_t listener) {
  if (!is_valid_listener(listener)) {
    return 0u;
  }
  return listener->local_endpoint.port;
}

uint8_t hal_mock_tcp_listener_get_backlog(hal_tcp_listener_t listener) {
  if (!is_valid_listener(listener)) {
    return 0u;
  }
  return listener->backlog;
}

uint8_t hal_mock_tcp_listener_get_pending_count(hal_tcp_listener_t listener) {
  if (!is_valid_listener(listener)) {
    return 0u;
  }
  return listener->pending_count;
}

#endif /* HAL_ENABLE_TCP */
#endif /* HAL_TARGET_IS_MOCK */
