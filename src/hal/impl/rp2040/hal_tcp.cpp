#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#ifdef HAL_ENABLE_TCP

#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "../../hal_tcp.h"
#include "../shared/hal_mutex_once.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <limits.h>

struct hal_tcp_socket_impl_t {
  WiFiClient client;
  bool in_use;
  bool connected;
  hal_net_endpoint_t remote_endpoint;
};

struct hal_tcp_listener_impl_t {
  WiFiServer server;
  bool in_use;
  bool bound;
  bool listening;
  hal_net_endpoint_t local_endpoint;
  WiFiClient pending_client;
  bool has_pending_client;
  uint8_t backlog;
};

static hal_tcp_socket_impl_t s_tcp_pool[HAL_TCP_SOCKET_MAX_INSTANCES];
static hal_tcp_listener_impl_t
    s_tcp_listener_pool[HAL_TCP_LISTENER_MAX_INSTANCES];
static hal_mutex_t s_tcp_mutex = NULL;

static inline void tcp_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_tcp_mutex);
}

static void reset_endpoint(hal_net_endpoint_t *endpoint) {
  if (!endpoint) {
    return;
  }
  endpoint->family = HAL_NET_AF_UNSPEC;
  endpoint->addr[0] = 0u;
  endpoint->addr[1] = 0u;
  endpoint->addr[2] = 0u;
  endpoint->addr[3] = 0u;
  endpoint->port = 0u;
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

static bool is_valid_socket_locked(hal_tcp_socket_t socket) {
  for (size_t i = 0u; i < HAL_TCP_SOCKET_MAX_INSTANCES; ++i) {
    if (socket == &s_tcp_pool[i] && s_tcp_pool[i].in_use) {
      return true;
    }
  }
  return false;
}

static bool is_valid_listener_locked(hal_tcp_listener_t listener) {
  for (size_t i = 0u; i < HAL_TCP_LISTENER_MAX_INSTANCES; ++i) {
    if (listener == &s_tcp_listener_pool[i] && s_tcp_listener_pool[i].in_use) {
      return true;
    }
  }
  return false;
}

static IPAddress ip_address_from_endpoint(const hal_net_endpoint_t *endpoint) {
  return IPAddress(endpoint->addr[0], endpoint->addr[1], endpoint->addr[2],
                   endpoint->addr[3]);
}

static void endpoint_from_ip_address(const IPAddress &ip, uint16_t port,
                                     hal_net_endpoint_t *out) {
  if (!out) {
    return;
  }
  out->family = HAL_NET_AF_INET;
  out->addr[0] = (uint8_t)ip[0];
  out->addr[1] = (uint8_t)ip[1];
  out->addr[2] = (uint8_t)ip[2];
  out->addr[3] = (uint8_t)ip[3];
  out->port = port;
}

static unsigned long stream_timeout_from_ms(uint32_t timeout_ms) {
  if (timeout_ms == HAL_NET_TIMEOUT_FOREVER) {
    return ULONG_MAX;
  }
  return (unsigned long)timeout_ms;
}

static uint8_t capped_backlog(uint8_t backlog) {
  if ((size_t)backlog > (size_t)HAL_TCP_LISTENER_BACKLOG_MAX) {
    return (uint8_t)HAL_TCP_LISTENER_BACKLOG_MAX;
  }
  return backlog;
}

static void reset_socket_state_locked(hal_tcp_socket_impl_t *socket) {
  socket->client.stop();
  socket->connected = false;
  reset_endpoint(&socket->remote_endpoint);
}

static hal_tcp_socket_t allocate_socket_locked(void) {
  for (size_t i = 0u; i < HAL_TCP_SOCKET_MAX_INSTANCES; ++i) {
    if (!s_tcp_pool[i].in_use) {
      reset_socket_state_locked(&s_tcp_pool[i]);
      s_tcp_pool[i].in_use = true;
      return &s_tcp_pool[i];
    }
  }
  return NULL;
}

static void drain_unaccepted_clients_locked(hal_tcp_listener_impl_t *listener) {
  if (listener->has_pending_client) {
    listener->pending_client.stop();
    listener->pending_client = WiFiClient();
    listener->has_pending_client = false;
  }

  for (;;) {
    WiFiClient client = listener->server.accept();
    if (!client) {
      break;
    }
    client.stop();
  }
}

static void reset_listener_state_locked(hal_tcp_listener_impl_t *listener) {
  listener->bound = false;
  listener->listening = false;
  listener->pending_client = WiFiClient();
  listener->has_pending_client = false;
  listener->backlog = 0u;
  reset_endpoint(&listener->local_endpoint);
}

hal_status_t hal_tcp_socket_open_ex(hal_tcp_socket_t *out_socket) {
  if (!out_socket) {
    return HAL_EINVAL;
  }
  *out_socket = NULL;
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  *out_socket = allocate_socket_locked();

  hal_mutex_unlock(s_tcp_mutex);

  if (!*out_socket) {
    hal_derr("hal_tcp_socket_open: socket pool exhausted");
    return HAL_ENOMEM;
  }
  return HAL_OK;
}

hal_tcp_socket_t hal_tcp_socket_open(void) {
  hal_tcp_socket_t socket = NULL;
  (void)hal_tcp_socket_open_ex(&socket);
  return socket;
}

hal_status_t hal_tcp_socket_connect_ex(hal_tcp_socket_t socket,
                                       const hal_net_endpoint_t *remote,
                                       uint32_t timeout_ms) {
  if (!validate_endpoint(remote, "hal_tcp_socket_connect", "remote")) {
    return HAL_EINVAL;
  }

  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  if (!is_valid_socket_locked(socket)) {
    hal_mutex_unlock(s_tcp_mutex);
    hal_derr("hal_tcp_socket_connect: socket handle is invalid");
    return HAL_EINVAL;
  }

  socket->client.stop();
  socket->connected = false;
  socket->remote_endpoint = *remote;
  socket->client.setTimeout(stream_timeout_from_ms(timeout_ms));

  const IPAddress remote_ip = ip_address_from_endpoint(remote);
  const int ok = socket->client.connect(remote_ip, remote->port);
  socket->connected = (ok == 1);

  hal_mutex_unlock(s_tcp_mutex);

  if (ok != 1) {
    hal_derr("hal_tcp_socket_connect: connect failed");
  }
  return ok == 1 ? HAL_OK : HAL_EIO;
}

bool hal_tcp_socket_connect(hal_tcp_socket_t socket,
                            const hal_net_endpoint_t *remote,
                            uint32_t timeout_ms) {
  return hal_status_to_bool(
      hal_tcp_socket_connect_ex(socket, remote, timeout_ms));
}

hal_status_t hal_tcp_socket_send_ex(hal_tcp_socket_t socket, const void *data,
                                    size_t len, size_t *out_sent) {
  if (out_sent) {
    *out_sent = 0u;
  }
  if (!out_sent) {
    return HAL_EINVAL;
  }
  if (len > 0u && data == NULL) {
    hal_derr("hal_tcp_socket_send: data is NULL while len > 0");
    return HAL_EINVAL;
  }
  if (len > (size_t)INT_MAX) {
    hal_derr("hal_tcp_socket_send: payload is too large");
    return HAL_EOVERFLOW;
  }

  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  if (!is_valid_socket_locked(socket)) {
    hal_mutex_unlock(s_tcp_mutex);
    hal_derr("hal_tcp_socket_send: socket is invalid");
    return HAL_EINVAL;
  }
  if (!socket->connected || !socket->client.connected()) {
    if (is_valid_socket_locked(socket)) {
      socket->connected = false;
    }
    hal_mutex_unlock(s_tcp_mutex);
    hal_derr("hal_tcp_socket_send: socket is invalid or not connected");
    return HAL_ESTATE;
  }

  size_t written = 0u;
  if (len > 0u) {
    written = socket->client.write((const uint8_t *)data, len);
  }

  hal_mutex_unlock(s_tcp_mutex);
  *out_sent = written;
  return HAL_OK;
}

int hal_tcp_socket_send(hal_tcp_socket_t socket, const void *data, size_t len) {
  size_t sent = 0u;
  return hal_status_is_ok(hal_tcp_socket_send_ex(socket, data, len, &sent))
             ? (int)sent
             : -1;
}

hal_status_t hal_tcp_socket_recv_ex(hal_tcp_socket_t socket, void *buffer,
                                    size_t max_len, uint32_t timeout_ms,
                                    size_t *out_received) {
  if (out_received) {
    *out_received = 0u;
  }
  if (!out_received) {
    return HAL_EINVAL;
  }
  if (max_len > 0u && buffer == NULL) {
    hal_derr("hal_tcp_socket_recv: buffer is NULL while max_len > 0");
    return HAL_EINVAL;
  }
  if (max_len > (size_t)INT_MAX) {
    return HAL_EOVERFLOW;
  }

  const uint32_t start_ms = hal_millis();

  for (;;) {
    tcp_ensure_mutex();
    hal_mutex_lock(s_tcp_mutex);

    if (!is_valid_socket_locked(socket)) {
      hal_mutex_unlock(s_tcp_mutex);
      hal_derr("hal_tcp_socket_recv: socket handle is invalid");
      return HAL_EINVAL;
    }

    const int available = socket->client.available();
    const bool backend_connected = socket->client.connected() != 0u;
    socket->connected = backend_connected || available > 0;

    if (!socket->connected) {
      hal_mutex_unlock(s_tcp_mutex);
      return HAL_OK;
    }

    if (available > 0) {
      int to_read = available;
      if ((size_t)to_read > max_len) {
        to_read = (int)max_len;
      }

      int read_count = 0;
      if (to_read > 0) {
        read_count = socket->client.read((uint8_t *)buffer, (size_t)to_read);
      }

      hal_mutex_unlock(s_tcp_mutex);
      if (read_count < 0) {
        return HAL_EIO;
      }
      *out_received = (size_t)read_count;
      return HAL_OK;
    }

    hal_mutex_unlock(s_tcp_mutex);

    if (timeout_ms == 0u) {
      return HAL_OK;
    }
    if (timeout_ms != HAL_NET_TIMEOUT_FOREVER &&
        (uint32_t)(hal_millis() - start_ms) >= timeout_ms) {
      return HAL_OK;
    }

    hal_idle();
    hal_delay_ms(1u);
  }
}

int hal_tcp_socket_recv(hal_tcp_socket_t socket, void *buffer, size_t max_len,
                        uint32_t timeout_ms) {
  size_t received = 0u;
  return hal_status_is_ok(hal_tcp_socket_recv_ex(socket, buffer, max_len,
                                                 timeout_ms, &received))
             ? (int)received
             : -1;
}

bool hal_tcp_socket_can_recv(hal_tcp_socket_t socket) {
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  bool ready = false;
  if (is_valid_socket_locked(socket)) {
    const int available = socket->client.available();
    const bool backend_connected = socket->client.connected() != 0u;
    socket->connected = backend_connected || available > 0;
    ready = available > 0;
  }

  hal_mutex_unlock(s_tcp_mutex);
  return ready;
}

bool hal_tcp_socket_can_send(hal_tcp_socket_t socket) {
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  bool ready = false;
  if (is_valid_socket_locked(socket)) {
    const int available = socket->client.available();
    const bool backend_connected = socket->client.connected() != 0u;
    socket->connected = backend_connected || available > 0;
    ready = socket->connected && backend_connected;
  }

  hal_mutex_unlock(s_tcp_mutex);
  return ready;
}

bool hal_tcp_socket_is_connected(hal_tcp_socket_t socket) {
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  bool connected = false;
  if (is_valid_socket_locked(socket)) {
    const int available = socket->client.available();
    connected = socket->connected &&
                (socket->client.connected() != 0u || available > 0);
    socket->connected = connected;
  }

  hal_mutex_unlock(s_tcp_mutex);
  return connected;
}

void hal_tcp_socket_shutdown(hal_tcp_socket_t socket) {
  if (!socket) {
    return;
  }

  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  if (is_valid_socket_locked(socket)) {
    socket->client.stop();
    socket->connected = false;
    reset_endpoint(&socket->remote_endpoint);
  }

  hal_mutex_unlock(s_tcp_mutex);
}

void hal_tcp_socket_close(hal_tcp_socket_t socket) {
  if (!socket) {
    return;
  }

  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  if (is_valid_socket_locked(socket)) {
    reset_socket_state_locked(socket);
    socket->in_use = false;
  }

  hal_mutex_unlock(s_tcp_mutex);
}

hal_status_t hal_tcp_listener_open_ex(hal_tcp_listener_t *out_listener) {
  if (!out_listener) {
    return HAL_EINVAL;
  }
  *out_listener = NULL;
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  for (size_t i = 0u; i < HAL_TCP_LISTENER_MAX_INSTANCES; ++i) {
    if (!s_tcp_listener_pool[i].in_use) {
      drain_unaccepted_clients_locked(&s_tcp_listener_pool[i]);
      s_tcp_listener_pool[i].server.stop();
      reset_listener_state_locked(&s_tcp_listener_pool[i]);
      s_tcp_listener_pool[i].in_use = true;
      *out_listener = &s_tcp_listener_pool[i];
      break;
    }
  }

  hal_mutex_unlock(s_tcp_mutex);

  if (!*out_listener) {
    hal_derr("hal_tcp_listener_open: listener pool exhausted");
    return HAL_ENOMEM;
  }
  return HAL_OK;
}

hal_tcp_listener_t hal_tcp_listener_open(void) {
  hal_tcp_listener_t listener = NULL;
  (void)hal_tcp_listener_open_ex(&listener);
  return listener;
}

hal_status_t hal_tcp_listener_bind_ex(hal_tcp_listener_t listener,
                                      const hal_net_endpoint_t *local) {
  if (!validate_endpoint(local, "hal_tcp_listener_bind", "local")) {
    return HAL_EINVAL;
  }

  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  if (!is_valid_listener_locked(listener)) {
    hal_mutex_unlock(s_tcp_mutex);
    hal_derr("hal_tcp_listener_bind: listener handle is invalid");
    return HAL_EINVAL;
  }

  drain_unaccepted_clients_locked(listener);
  listener->server.stop();
  reset_listener_state_locked(listener);
  listener->local_endpoint = *local;
  listener->bound = true;

  hal_mutex_unlock(s_tcp_mutex);
  return HAL_OK;
}

bool hal_tcp_listener_bind(hal_tcp_listener_t listener,
                           const hal_net_endpoint_t *local) {
  return hal_status_to_bool(hal_tcp_listener_bind_ex(listener, local));
}

hal_status_t hal_tcp_listener_listen_ex(hal_tcp_listener_t listener,
                                        uint8_t backlog) {
  if (backlog == 0u) {
    hal_derr("hal_tcp_listener_listen: backlog must be > 0");
    return HAL_EINVAL;
  }

  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  if (!is_valid_listener_locked(listener)) {
    hal_mutex_unlock(s_tcp_mutex);
    hal_derr("hal_tcp_listener_listen: listener is invalid");
    return HAL_EINVAL;
  }
  if (!listener->bound) {
    hal_mutex_unlock(s_tcp_mutex);
    hal_derr("hal_tcp_listener_listen: listener is not bound");
    return HAL_ESTATE;
  }

  drain_unaccepted_clients_locked(listener);
  listener->server.stop();
  listener->backlog = capped_backlog(backlog);
  listener->listening = false;
  listener->server.begin(listener->local_endpoint.port, listener->backlog);
  listener->listening = (bool)listener->server;
  const bool listening = listener->listening;
  const uint16_t local_port = listener->local_endpoint.port;

  hal_mutex_unlock(s_tcp_mutex);

  if (!listening) {
    hal_derr("hal_tcp_listener_listen: begin(%u) failed", (unsigned)local_port);
  }
  return listening ? HAL_OK : HAL_EIO;
}

bool hal_tcp_listener_listen(hal_tcp_listener_t listener, uint8_t backlog) {
  return hal_status_to_bool(hal_tcp_listener_listen_ex(listener, backlog));
}

hal_status_t hal_tcp_listener_accept_ex(hal_tcp_listener_t listener,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        hal_tcp_socket_t *out_socket) {
  if (!out_socket) {
    return HAL_EINVAL;
  }
  *out_socket = NULL;
  const uint32_t start_ms = hal_millis();

  for (;;) {
    tcp_ensure_mutex();
    hal_mutex_lock(s_tcp_mutex);

    if (!is_valid_listener_locked(listener)) {
      hal_mutex_unlock(s_tcp_mutex);
      hal_derr("hal_tcp_listener_accept: listener is invalid");
      return HAL_EINVAL;
    }
    if (!listener->listening) {
      hal_mutex_unlock(s_tcp_mutex);
      hal_derr("hal_tcp_listener_accept: listener is not listening");
      return HAL_ESTATE;
    }

    WiFiClient accepted;
    if (listener->has_pending_client) {
      accepted = listener->pending_client;
      listener->pending_client = WiFiClient();
      listener->has_pending_client = false;
    } else {
      accepted = listener->server.accept();
    }
    if (accepted) {
      hal_tcp_socket_t socket = allocate_socket_locked();
      if (!socket) {
        accepted.stop();
        hal_mutex_unlock(s_tcp_mutex);
        hal_derr("hal_tcp_listener_accept: socket pool exhausted");
        return HAL_ENOMEM;
      }

      socket->client = accepted;
      const int available = socket->client.available();
      socket->connected = socket->client.connected() != 0u || available > 0;
      endpoint_from_ip_address(socket->client.remoteIP(),
                               socket->client.remotePort(),
                               &socket->remote_endpoint);
      if (remote) {
        *remote = socket->remote_endpoint;
      }

      hal_mutex_unlock(s_tcp_mutex);
      *out_socket = socket;
      return HAL_OK;
    }

    hal_mutex_unlock(s_tcp_mutex);

    if (timeout_ms == 0u) {
      return HAL_EAGAIN;
    }
    if (timeout_ms != HAL_NET_TIMEOUT_FOREVER &&
        (uint32_t)(hal_millis() - start_ms) >= timeout_ms) {
      return HAL_EAGAIN;
    }

    hal_idle();
    hal_delay_ms(1u);
  }
}

hal_tcp_socket_t hal_tcp_listener_accept(hal_tcp_listener_t listener,
                                         hal_net_endpoint_t *remote,
                                         uint32_t timeout_ms) {
  hal_tcp_socket_t socket = NULL;
  (void)hal_tcp_listener_accept_ex(listener, remote, timeout_ms, &socket);
  return socket;
}

bool hal_tcp_listener_can_accept(hal_tcp_listener_t listener) {
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  bool ready = false;
  if (is_valid_listener_locked(listener) && listener->listening) {
    if (listener->has_pending_client) {
      ready = true;
    } else {
      WiFiClient accepted = listener->server.accept();
      if (accepted) {
        listener->pending_client = accepted;
        listener->has_pending_client = true;
        ready = true;
      }
    }
  }

  hal_mutex_unlock(s_tcp_mutex);
  return ready;
}

void hal_tcp_listener_close(hal_tcp_listener_t listener) {
  if (!listener) {
    return;
  }

  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  if (is_valid_listener_locked(listener)) {
    drain_unaccepted_clients_locked(listener);
    listener->server.stop();
    reset_listener_state_locked(listener);
    listener->in_use = false;
  }

  hal_mutex_unlock(s_tcp_mutex);
}

#endif /* HAL_ENABLE_TCP */
#endif /* HAL_TARGET_IS_RP2040 */
