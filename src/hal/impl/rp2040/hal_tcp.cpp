#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#ifdef HAL_ENABLE_TCP

#include "../shared/network/jh_network_backend.h"
#if defined(HAL_NETWORK_BACKEND_CYW43)
#define hal_tcp_socket_impl_t jh_cyw43_tcp_socket_impl_t
#define hal_tcp_listener_impl_t jh_cyw43_tcp_listener_impl_t
#define hal_tcp_socket_t jh_cyw43_tcp_socket_t
#define hal_tcp_listener_t jh_cyw43_tcp_listener_t
#define hal_tcp_socket_open_ex jh_cyw43_tcp_socket_open_ex
#define hal_tcp_socket_open jh_cyw43_tcp_socket_open
#define hal_tcp_socket_connect_ex jh_cyw43_tcp_socket_connect_ex
#define hal_tcp_socket_connect jh_cyw43_tcp_socket_connect
#define hal_tcp_socket_send_ex jh_cyw43_tcp_socket_send_ex
#define hal_tcp_socket_send jh_cyw43_tcp_socket_send
#define hal_tcp_socket_recv_ex jh_cyw43_tcp_socket_recv_ex
#define hal_tcp_socket_recv jh_cyw43_tcp_socket_recv
#define hal_tcp_socket_can_recv jh_cyw43_tcp_socket_can_recv
#define hal_tcp_socket_can_send jh_cyw43_tcp_socket_can_send
#define hal_tcp_socket_is_connected jh_cyw43_tcp_socket_is_connected
#define hal_tcp_socket_shutdown jh_cyw43_tcp_socket_shutdown
#define hal_tcp_socket_close jh_cyw43_tcp_socket_close
#define hal_tcp_listener_open_ex jh_cyw43_tcp_listener_open_ex
#define hal_tcp_listener_open jh_cyw43_tcp_listener_open
#define hal_tcp_listener_bind_ex jh_cyw43_tcp_listener_bind_ex
#define hal_tcp_listener_bind jh_cyw43_tcp_listener_bind
#define hal_tcp_listener_listen_ex jh_cyw43_tcp_listener_listen_ex
#define hal_tcp_listener_listen jh_cyw43_tcp_listener_listen
#define hal_tcp_listener_accept_ex jh_cyw43_tcp_listener_accept_ex
#define hal_tcp_listener_accept jh_cyw43_tcp_listener_accept
#define hal_tcp_listener_can_accept jh_cyw43_tcp_listener_can_accept
#define hal_tcp_listener_close jh_cyw43_tcp_listener_close
#endif

#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "../../hal_tcp.h"
#include "../shared/hal_mutex_once.h"
#include "../shared/network/jh_net_address_utils.h"

#if defined(HAL_NETWORK_BACKEND_CYW43)
#include "../shared/network/jh_lwip_tcp.h"
#include "drivers/rp2040/rp2040_cyw43_provider.h"
#include "rp2040_network_lifecycle.h"
#else
#include <Arduino.h>
#include <IPAddress.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#endif
#include <limits.h>
#include <string.h>

struct hal_tcp_socket_impl_t {
#if defined(HAL_NETWORK_BACKEND_CYW43)
  jh_lwip_tcp_socket_t client;
#else
  WiFiClient client;
#endif
  bool in_use;
  bool connected;
  hal_net_endpoint_t remote_endpoint;
};

struct hal_tcp_listener_impl_t {
#if defined(HAL_NETWORK_BACKEND_CYW43)
  jh_lwip_tcp_listener_t listener;
#else
  WiFiServer server;
#endif
  bool in_use;
  bool bound;
  bool listening;
  hal_net_endpoint_t local_endpoint;
#if !defined(HAL_NETWORK_BACKEND_CYW43)
  WiFiClient pending_client;
  bool has_pending_client;
#endif
  uint8_t backlog;
};

static hal_tcp_socket_impl_t s_tcp_pool[HAL_TCP_SOCKET_MAX_INSTANCES];
static hal_tcp_listener_impl_t
    s_tcp_listener_pool[HAL_TCP_LISTENER_MAX_INSTANCES];
static hal_mutex_t s_tcp_mutex = NULL;

static inline void tcp_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_tcp_mutex);
}

#if defined(HAL_NETWORK_BACKEND_CYW43)
static void reset_socket_state_locked(hal_tcp_socket_impl_t *socket);
static void reset_listener_state_locked(hal_tcp_listener_impl_t *listener);

extern "C" hal_status_t jh_rp2040_tcp_reset_all(void) {
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);
  const hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(false);
  if (status == HAL_OK) {
    for (size_t index = 0u; index < HAL_TCP_SOCKET_MAX_INSTANCES; ++index) {
      if (s_tcp_pool[index].in_use) {
        jh_lwip_tcp_socket_close(&s_tcp_pool[index].client);
        reset_socket_state_locked(&s_tcp_pool[index]);
        s_tcp_pool[index].in_use = false;
      }
    }
    for (size_t index = 0u; index < HAL_TCP_LISTENER_MAX_INSTANCES; ++index) {
      if (s_tcp_listener_pool[index].in_use) {
        jh_lwip_tcp_listener_close(&s_tcp_listener_pool[index].listener);
        reset_listener_state_locked(&s_tcp_listener_pool[index]);
        s_tcp_listener_pool[index].in_use = false;
      }
    }
    jh_rp2040_cyw43_provider_lwip_end();
  }
  hal_mutex_unlock(s_tcp_mutex);
  return status;
}
#endif

static void reset_endpoint(hal_net_endpoint_t *endpoint) {
  if (!endpoint) {
    return;
  }
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->family = HAL_NET_AF_UNSPEC;
}

static hal_status_t validate_endpoint(const hal_net_endpoint_t *endpoint,
                                      bool allow_unspecified_address,
                                      const char *fn, const char *name) {
  const hal_status_t shape =
      jh_net_validate_endpoint_shape(endpoint, true, allow_unspecified_address);
  if (shape != HAL_OK) {
    hal_derr("%s: %s endpoint is malformed", fn, name);
    return shape;
  }
  if (endpoint->family != HAL_NET_AF_INET) {
    hal_derr("%s: %s endpoint family is unsupported", fn, name);
    return HAL_EUNSUPPORTED;
  }
  return HAL_OK;
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

#if !defined(HAL_NETWORK_BACKEND_CYW43)
static IPAddress ip_address_from_endpoint(const hal_net_endpoint_t *endpoint) {
  return IPAddress(endpoint->addr[0], endpoint->addr[1], endpoint->addr[2],
                   endpoint->addr[3]);
}

static void endpoint_from_ip_address(const IPAddress &ip, uint16_t port,
                                     hal_net_endpoint_t *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  out->family = HAL_NET_AF_INET;
  out->addr_len = HAL_NET_IPV4_ADDR_LEN;
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
#endif

static uint8_t capped_backlog(uint8_t backlog) {
  if ((size_t)backlog > (size_t)HAL_TCP_LISTENER_BACKLOG_MAX) {
    return (uint8_t)HAL_TCP_LISTENER_BACKLOG_MAX;
  }
  return backlog;
}

static void reset_socket_state_locked(hal_tcp_socket_impl_t *socket) {
#if defined(HAL_NETWORK_BACKEND_CYW43)
  jh_lwip_tcp_socket_init(&socket->client);
#else
  socket->client.stop();
#endif
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

#if !defined(HAL_NETWORK_BACKEND_CYW43)
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
#endif

static void reset_listener_state_locked(hal_tcp_listener_impl_t *listener) {
#if defined(HAL_NETWORK_BACKEND_CYW43)
  jh_lwip_tcp_listener_init(&listener->listener);
#else
  listener->pending_client = WiFiClient();
  listener->has_pending_client = false;
#endif
  listener->backlog = 0u;
  listener->bound = false;
  listener->listening = false;
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
  const hal_status_t endpoint_status =
      validate_endpoint(remote, false, "hal_tcp_socket_connect", "remote");
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
  }

  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  if (!is_valid_socket_locked(socket)) {
    hal_mutex_unlock(s_tcp_mutex);
    hal_derr("hal_tcp_socket_connect: socket handle is invalid");
    return HAL_EINVAL;
  }

#if defined(HAL_NETWORK_BACKEND_CYW43)
  socket->connected = false;
  socket->remote_endpoint = *remote;
  ip4_addr_t remote_address;
  IP4_ADDR(&remote_address, remote->addr[0], remote->addr[1], remote->addr[2],
           remote->addr[3]);
  hal_status_t connect_status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (connect_status == HAL_OK) {
    jh_lwip_tcp_socket_close(&socket->client);
    connect_status = jh_lwip_tcp_socket_connect(&socket->client,
                                                &remote_address, remote->port);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  hal_mutex_unlock(s_tcp_mutex);
  if (connect_status != HAL_OK) {
    return connect_status;
  }

  const uint32_t start_ms = hal_millis();
  for (;;) {
    hal_mutex_lock(s_tcp_mutex);
    if (!is_valid_socket_locked(socket)) {
      hal_mutex_unlock(s_tcp_mutex);
      return HAL_EINVAL;
    }
    hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(false);
    if (status == HAL_OK) {
      status = jh_lwip_tcp_socket_connection_status(&socket->client);
      socket->connected = status == HAL_OK;
      jh_rp2040_cyw43_provider_lwip_end();
    }
    hal_mutex_unlock(s_tcp_mutex);

    if (status == HAL_OK) {
      return HAL_OK;
    }
    if (status != HAL_EAGAIN) {
      return status;
    }
    if (timeout_ms != HAL_NET_TIMEOUT_FOREVER &&
        (uint32_t)(hal_millis() - start_ms) >= timeout_ms) {
      hal_mutex_lock(s_tcp_mutex);
      const hal_status_t context_status =
          jh_rp2040_cyw43_provider_lwip_begin(false);
      if (context_status == HAL_OK) {
        jh_lwip_tcp_socket_close(&socket->client);
        jh_rp2040_cyw43_provider_lwip_end();
      }
      socket->connected = false;
      hal_mutex_unlock(s_tcp_mutex);
      return HAL_ETIMEOUT;
    }
    hal_idle();
    hal_delay_ms(1u);
  }
#else
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
#endif
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
#if defined(HAL_NETWORK_BACKEND_CYW43)
  hal_status_t send_status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (send_status != HAL_OK) {
    socket->connected = false;
    hal_mutex_unlock(s_tcp_mutex);
    return send_status;
  }
  socket->connected = jh_lwip_tcp_socket_is_connected(&socket->client);
  if (!socket->connected) {
    jh_rp2040_cyw43_provider_lwip_end();
    hal_mutex_unlock(s_tcp_mutex);
    hal_derr("hal_tcp_socket_send: socket is invalid or not connected");
    return HAL_ESTATE;
  }
  if (!jh_lwip_tcp_socket_can_send(&socket->client)) {
    jh_rp2040_cyw43_provider_lwip_end();
    hal_mutex_unlock(s_tcp_mutex);
    return HAL_EAGAIN;
  }

  send_status = jh_lwip_tcp_socket_send(&socket->client, data, len, out_sent);
  jh_rp2040_cyw43_provider_lwip_end();
  hal_mutex_unlock(s_tcp_mutex);
  return send_status;
#else
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
#endif
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

#if defined(HAL_NETWORK_BACKEND_CYW43)
    hal_status_t receive_status = jh_rp2040_cyw43_provider_lwip_begin(false);
    if (receive_status != HAL_OK) {
      hal_mutex_unlock(s_tcp_mutex);
      return receive_status;
    }
    const size_t available = jh_lwip_tcp_socket_available(&socket->client);
    socket->connected = jh_lwip_tcp_socket_is_connected(&socket->client);

    if (available > 0u) {
      receive_status = jh_lwip_tcp_socket_receive(&socket->client, buffer,
                                                  max_len, out_received);
      socket->connected = jh_lwip_tcp_socket_is_connected(&socket->client);
      jh_rp2040_cyw43_provider_lwip_end();
      hal_mutex_unlock(s_tcp_mutex);
      return receive_status;
    }
    const bool connected = socket->connected;
    jh_rp2040_cyw43_provider_lwip_end();
    if (!connected) {
      hal_mutex_unlock(s_tcp_mutex);
      return HAL_OK;
    }
#else
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
#endif

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
#if defined(HAL_NETWORK_BACKEND_CYW43)
    const hal_status_t context_status =
        jh_rp2040_cyw43_provider_lwip_begin(false);
    if (context_status == HAL_OK) {
      ready = jh_lwip_tcp_socket_available(&socket->client) > 0u;
      socket->connected = jh_lwip_tcp_socket_is_connected(&socket->client);
      jh_rp2040_cyw43_provider_lwip_end();
    }
#else
    const int available = socket->client.available();
    const bool backend_connected = socket->client.connected() != 0u;
    socket->connected = backend_connected || available > 0;
    ready = available > 0;
#endif
  }

  hal_mutex_unlock(s_tcp_mutex);
  return ready;
}

bool hal_tcp_socket_can_send(hal_tcp_socket_t socket) {
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  bool ready = false;
  if (is_valid_socket_locked(socket)) {
#if defined(HAL_NETWORK_BACKEND_CYW43)
    const hal_status_t context_status =
        jh_rp2040_cyw43_provider_lwip_begin(false);
    if (context_status == HAL_OK) {
      ready = jh_lwip_tcp_socket_can_send(&socket->client);
      socket->connected = jh_lwip_tcp_socket_is_connected(&socket->client);
      jh_rp2040_cyw43_provider_lwip_end();
    }
#else
    const int available = socket->client.available();
    const bool backend_connected = socket->client.connected() != 0u;
    socket->connected = backend_connected || available > 0;
    ready = socket->connected && backend_connected;
#endif
  }

  hal_mutex_unlock(s_tcp_mutex);
  return ready;
}

bool hal_tcp_socket_is_connected(hal_tcp_socket_t socket) {
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  bool connected = false;
  if (is_valid_socket_locked(socket)) {
#if defined(HAL_NETWORK_BACKEND_CYW43)
    const hal_status_t context_status =
        jh_rp2040_cyw43_provider_lwip_begin(false);
    if (context_status == HAL_OK) {
      connected = jh_lwip_tcp_socket_is_connected(&socket->client);
      socket->connected = connected;
      jh_rp2040_cyw43_provider_lwip_end();
    }
#else
    const int available = socket->client.available();
    connected = socket->connected &&
                (socket->client.connected() != 0u || available > 0);
    socket->connected = connected;
#endif
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
#if defined(HAL_NETWORK_BACKEND_CYW43)
    const hal_status_t context_status =
        jh_rp2040_cyw43_provider_lwip_begin(false);
    if (context_status == HAL_OK) {
      jh_lwip_tcp_socket_close(&socket->client);
      jh_rp2040_cyw43_provider_lwip_end();
    }
#else
    socket->client.stop();
#endif
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
#if defined(HAL_NETWORK_BACKEND_CYW43)
    const hal_status_t context_status =
        jh_rp2040_cyw43_provider_lwip_begin(false);
    if (context_status == HAL_OK) {
      jh_lwip_tcp_socket_close(&socket->client);
      jh_rp2040_cyw43_provider_lwip_end();
    }
#endif
    reset_socket_state_locked(socket);
    socket->in_use = false;
  }

  hal_mutex_unlock(s_tcp_mutex);
}

#if defined(HAL_NETWORK_BACKEND_CYW43)

hal_status_t hal_tcp_listener_open_ex(hal_tcp_listener_t *out_listener) {
  if (!out_listener) {
    return HAL_EINVAL;
  }
  *out_listener = NULL;
  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);

  for (size_t i = 0u; i < HAL_TCP_LISTENER_MAX_INSTANCES; ++i) {
    if (!s_tcp_listener_pool[i].in_use) {
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
  const hal_status_t endpoint_status =
      validate_endpoint(local, true, "hal_tcp_listener_bind", "local");
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
  }

  tcp_ensure_mutex();
  hal_mutex_lock(s_tcp_mutex);
  if (!is_valid_listener_locked(listener)) {
    hal_mutex_unlock(s_tcp_mutex);
    hal_derr("hal_tcp_listener_bind: listener handle is invalid");
    return HAL_EINVAL;
  }

  ip4_addr_t local_address;
  IP4_ADDR(&local_address, local->addr[0], local->addr[1], local->addr[2],
           local->addr[3]);
  hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(true);
  const bool context_active = status == HAL_OK;
  if (status == HAL_OK) {
    status = jh_lwip_tcp_listener_bind(&listener->listener, &local_address,
                                       local->port);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  if (status == HAL_OK) {
    listener->local_endpoint = *local;
    listener->bound = true;
    listener->listening = false;
    listener->backlog = 0u;
  } else if (context_active) {
    reset_listener_state_locked(listener);
  }
  hal_mutex_unlock(s_tcp_mutex);
  return status;
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

  const uint8_t effective_backlog = capped_backlog(backlog);
  hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (status == HAL_OK) {
    status =
        jh_lwip_tcp_listener_listen(&listener->listener, effective_backlog);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  if (status == HAL_OK) {
    listener->backlog = effective_backlog;
    listener->listening = true;
  }
  hal_mutex_unlock(s_tcp_mutex);
  return status;
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

    hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(false);
    const bool context_active = status == HAL_OK;
    hal_tcp_socket_t socket = NULL;
    if (context_active) {
      if (!jh_lwip_tcp_listener_can_accept(&listener->listener)) {
        status = HAL_EAGAIN;
      } else {
        socket = allocate_socket_locked();
        if (!socket) {
          status = HAL_ENOMEM;
        } else {
          ip4_addr_t remote_address;
          uint16_t remote_port = 0u;
          status =
              jh_lwip_tcp_listener_accept(&listener->listener, &socket->client,
                                          &remote_address, &remote_port);
          if (status == HAL_OK) {
            socket->connected =
                jh_lwip_tcp_socket_is_connected(&socket->client);
            memset(&socket->remote_endpoint, 0,
                   sizeof(socket->remote_endpoint));
            socket->remote_endpoint.family = HAL_NET_AF_INET;
            socket->remote_endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
            socket->remote_endpoint.addr[0] = ip4_addr1(&remote_address);
            socket->remote_endpoint.addr[1] = ip4_addr2(&remote_address);
            socket->remote_endpoint.addr[2] = ip4_addr3(&remote_address);
            socket->remote_endpoint.addr[3] = ip4_addr4(&remote_address);
            socket->remote_endpoint.port = remote_port;
          } else {
            reset_socket_state_locked(socket);
            socket->in_use = false;
            socket = NULL;
          }
        }
      }
    }
    if (context_active) {
      jh_rp2040_cyw43_provider_lwip_end();
    }
    hal_mutex_unlock(s_tcp_mutex);

    if (status == HAL_OK && socket) {
      if (remote) {
        *remote = socket->remote_endpoint;
      }
      *out_socket = socket;
      return HAL_OK;
    }
    if (status != HAL_EAGAIN) {
      return status;
    }
    if (timeout_ms == 0u ||
        (timeout_ms != HAL_NET_TIMEOUT_FOREVER &&
         (uint32_t)(hal_millis() - start_ms) >= timeout_ms)) {
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
    const hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(false);
    if (status == HAL_OK) {
      ready = jh_lwip_tcp_listener_can_accept(&listener->listener);
      jh_rp2040_cyw43_provider_lwip_end();
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
    const hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(false);
    if (status == HAL_OK) {
      jh_lwip_tcp_listener_close(&listener->listener);
      jh_rp2040_cyw43_provider_lwip_end();
    }
    reset_listener_state_locked(listener);
    listener->in_use = false;
  }
  hal_mutex_unlock(s_tcp_mutex);
}

#else

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
  const hal_status_t endpoint_status =
      validate_endpoint(local, true, "hal_tcp_listener_bind", "local");
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
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

#endif

#if defined(HAL_NETWORK_BACKEND_CYW43)
static hal_status_t backend_socket_open(void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  hal_tcp_socket_t socket = nullptr;
  const hal_status_t status = hal_tcp_socket_open_ex(&socket);
  *out_socket = socket;
  return status;
}

static hal_status_t backend_socket_connect(void *socket,
                                           const hal_net_endpoint_t *remote,
                                           uint32_t timeout_ms) {
  return hal_tcp_socket_connect_ex(static_cast<hal_tcp_socket_t>(socket),
                                   remote, timeout_ms);
}

static hal_status_t backend_socket_send(void *socket, const void *data,
                                        size_t len, size_t *out_sent) {
  return hal_tcp_socket_send_ex(static_cast<hal_tcp_socket_t>(socket), data,
                                len, out_sent);
}

static hal_status_t backend_socket_recv(void *socket, void *buffer,
                                        size_t max_len, uint32_t timeout_ms,
                                        size_t *out_received) {
  return hal_tcp_socket_recv_ex(static_cast<hal_tcp_socket_t>(socket), buffer,
                                max_len, timeout_ms, out_received);
}

static bool backend_socket_can_recv(void *socket) {
  return hal_tcp_socket_can_recv(static_cast<hal_tcp_socket_t>(socket));
}

static bool backend_socket_can_send(void *socket) {
  return hal_tcp_socket_can_send(static_cast<hal_tcp_socket_t>(socket));
}

static bool backend_socket_is_connected(void *socket) {
  return hal_tcp_socket_is_connected(static_cast<hal_tcp_socket_t>(socket));
}

static void backend_socket_shutdown(void *socket) {
  hal_tcp_socket_shutdown(static_cast<hal_tcp_socket_t>(socket));
}

static void backend_socket_close(void *socket) {
  hal_tcp_socket_close(static_cast<hal_tcp_socket_t>(socket));
}

static hal_status_t backend_listener_open(void **out_listener) {
  if (out_listener == nullptr) {
    return HAL_EINVAL;
  }
  hal_tcp_listener_t listener = nullptr;
  const hal_status_t status = hal_tcp_listener_open_ex(&listener);
  *out_listener = listener;
  return status;
}

static hal_status_t backend_listener_bind(void *listener,
                                          const hal_net_endpoint_t *local) {
  return hal_tcp_listener_bind_ex(static_cast<hal_tcp_listener_t>(listener),
                                  local);
}

static hal_status_t backend_listener_listen(void *listener, uint8_t backlog) {
  return hal_tcp_listener_listen_ex(static_cast<hal_tcp_listener_t>(listener),
                                    backlog);
}

static hal_status_t backend_listener_accept(void *listener,
                                            hal_net_endpoint_t *remote,
                                            uint32_t timeout_ms,
                                            void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  hal_tcp_socket_t socket = nullptr;
  const hal_status_t status = hal_tcp_listener_accept_ex(
      static_cast<hal_tcp_listener_t>(listener), remote, timeout_ms, &socket);
  *out_socket = socket;
  return status;
}

static bool backend_listener_can_accept(void *listener) {
  return hal_tcp_listener_can_accept(static_cast<hal_tcp_listener_t>(listener));
}

static void backend_listener_close(void *listener) {
  hal_tcp_listener_close(static_cast<hal_tcp_listener_t>(listener));
}

extern "C" const jh_network_tcp_ops_t *jh_rp2040_cyw43_tcp_ops(void) {
  static const jh_network_tcp_ops_t ops = {
      backend_socket_open,         backend_socket_connect,
      backend_socket_send,         backend_socket_recv,
      backend_socket_can_recv,     backend_socket_can_send,
      backend_socket_is_connected, backend_socket_shutdown,
      backend_socket_close,        backend_listener_open,
      backend_listener_bind,       backend_listener_listen,
      backend_listener_accept,     backend_listener_can_accept,
      backend_listener_close,
  };
  return &ops;
}
#endif

#endif /* HAL_ENABLE_TCP */
#endif /* HAL_TARGET_IS_RP2040 */
