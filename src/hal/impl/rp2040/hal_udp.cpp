#include "../../hal_target.h"
#if HAL_TARGET_IS_RP
#include "../../hal_config.h"

#ifdef HAL_ENABLE_UDP

#include "../shared/network/jh_network_backend.h"
#define hal_udp_socket_impl_t jh_cyw43_udp_socket_impl_t
#define hal_udp_socket_t jh_cyw43_udp_socket_t
#define hal_udp_socket_open_ex jh_cyw43_udp_socket_open_ex
#define hal_udp_socket_open jh_cyw43_udp_socket_open
#define hal_udp_socket_bind_ex jh_cyw43_udp_socket_bind_ex
#define hal_udp_socket_bind jh_cyw43_udp_socket_bind
#define hal_udp_socket_sendto_ex jh_cyw43_udp_socket_sendto_ex
#define hal_udp_socket_sendto jh_cyw43_udp_socket_sendto
#define hal_udp_socket_recvfrom_ex jh_cyw43_udp_socket_recvfrom_ex
#define hal_udp_socket_recvfrom jh_cyw43_udp_socket_recvfrom
#define hal_udp_socket_can_recv jh_cyw43_udp_socket_can_recv
#define hal_udp_socket_can_send jh_cyw43_udp_socket_can_send
#define hal_udp_socket_close jh_cyw43_udp_socket_close

#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "../../hal_udp.h"
#include "../shared/hal_mutex_once.h"
#include "../shared/network/jh_net_address_utils.h"
#include "../shared/network/jh_network_runtime.h"

#include "../shared/network/jh_lwip_udp.h"
#include "drivers/rp2040/rp2040_cyw43_provider.h"
#include "rp2040_network_lifecycle.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct hal_udp_socket_impl_t {
  jh_lwip_udp_socket_t udp;
  bool in_use;
  bool bound;
  bool packet_started;
  int pending_packet_size;
  uint8_t last_remote_ip[HAL_NET_IPV4_ADDR_LEN];
  uint16_t last_remote_port;
};

static hal_udp_socket_impl_t s_udp_pool[HAL_UDP_SOCKET_MAX_INSTANCES];
static hal_udp_socket_t s_default_udp = NULL;
static hal_mutex_t s_udp_mutex = NULL;

static inline void udp_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_udp_mutex);
}

static void reset_socket_state(hal_udp_socket_impl_t *socket);

extern "C" hal_status_t jh_rp2040_udp_reset_all(void) {
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);
  const hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(false);
  if (status == HAL_OK) {
    for (size_t index = 0u; index < HAL_UDP_SOCKET_MAX_INSTANCES; ++index) {
      if (s_udp_pool[index].in_use) {
        jh_lwip_udp_socket_close(&s_udp_pool[index].udp);
        reset_socket_state(&s_udp_pool[index]);
        s_udp_pool[index].in_use = false;
      }
    }
    s_default_udp = NULL;
    jh_rp2040_cyw43_provider_lwip_end();
  }
  hal_mutex_unlock(s_udp_mutex);
  return status;
}

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

static bool ip_is_zero(const uint8_t ip[HAL_NET_IPV4_ADDR_LEN]) {
  return ip[0] == 0u && ip[1] == 0u && ip[2] == 0u && ip[3] == 0u;
}

static void reset_last_remote(hal_udp_socket_impl_t *socket) {
  memset(socket->last_remote_ip, 0, sizeof(socket->last_remote_ip));
  socket->last_remote_port = 0u;
}

static void reset_socket_state(hal_udp_socket_impl_t *socket) {
  socket->bound = false;
  socket->packet_started = false;
  socket->pending_packet_size = 0;
  reset_last_remote(socket);
  jh_lwip_udp_socket_init(&socket->udp);
}

static bool is_valid_socket_locked(hal_udp_socket_t socket) {
  for (size_t i = 0u; i < HAL_UDP_SOCKET_MAX_INSTANCES; ++i) {
    if (socket == &s_udp_pool[i] && s_udp_pool[i].in_use) {
      return true;
    }
  }
  return false;
}

static void endpoint_from_ip_address(const uint8_t ip[HAL_NET_IPV4_ADDR_LEN],
                                     uint16_t port, hal_net_endpoint_t *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  out->family = HAL_NET_AF_INET;
  out->addr_len = HAL_NET_IPV4_ADDR_LEN;
  memcpy(out->addr, ip, HAL_NET_IPV4_ADDR_LEN);
  out->port = port;
}

static int socket_parse_packet_locked(hal_udp_socket_impl_t *socket,
                                      bool *out_has_packet) {
  if (out_has_packet != nullptr) {
    *out_has_packet = false;
  }
  if (!socket->bound) {
    return 0;
  }
  if (socket->pending_packet_size > 0) {
    if (out_has_packet != nullptr) {
      *out_has_packet = true;
    }
    return socket->pending_packet_size;
  }

  int packet_size = 0;
  const hal_status_t context_status =
      jh_rp2040_cyw43_provider_lwip_begin(false);
  if (context_status != HAL_OK) {
    return -1;
  }
  packet_size = jh_lwip_udp_socket_parse(&socket->udp);
  const bool has_packet = jh_lwip_udp_socket_has_packet(&socket->udp);
  if (has_packet) {
    ip4_addr_t remote_address;
    if (jh_lwip_udp_socket_get_last_remote(&socket->udp, &remote_address,
                                           &socket->last_remote_port)) {
      socket->last_remote_ip[0] = ip4_addr1(&remote_address);
      socket->last_remote_ip[1] = ip4_addr2(&remote_address);
      socket->last_remote_ip[2] = ip4_addr3(&remote_address);
      socket->last_remote_ip[3] = ip4_addr4(&remote_address);
    }
  }
  if (out_has_packet != nullptr) {
    *out_has_packet = has_packet;
  }
  jh_rp2040_cyw43_provider_lwip_end();
  if (packet_size > 0) {
    socket->pending_packet_size = packet_size;
  }
  return packet_size;
}

hal_status_t hal_udp_socket_open_ex(hal_udp_socket_t *out_socket) {
  if (!out_socket) {
    return HAL_EINVAL;
  }
  *out_socket = NULL;
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  for (size_t i = 0u; i < HAL_UDP_SOCKET_MAX_INSTANCES; ++i) {
    if (!s_udp_pool[i].in_use) {
      reset_socket_state(&s_udp_pool[i]);
      s_udp_pool[i].in_use = true;
      *out_socket = &s_udp_pool[i];
      break;
    }
  }

  hal_mutex_unlock(s_udp_mutex);

  if (!*out_socket) {
    hal_derr("hal_udp_socket_open: socket pool exhausted");
    return HAL_ENOMEM;
  }
  return HAL_OK;
}

hal_udp_socket_t hal_udp_socket_open(void) {
  hal_udp_socket_t socket = NULL;
  (void)hal_udp_socket_open_ex(&socket);
  return socket;
}

hal_status_t hal_udp_socket_bind_ex(hal_udp_socket_t socket,
                                    const hal_net_endpoint_t *local) {
  const hal_status_t endpoint_status =
      validate_endpoint(local, true, "hal_udp_socket_bind", "local");
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
  }

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(socket)) {
    hal_mutex_unlock(s_udp_mutex);
    hal_derr("hal_udp_socket_bind: socket handle is invalid");
    return HAL_EINVAL;
  }

  if (socket->bound) {
    const hal_status_t context_status =
        jh_rp2040_cyw43_provider_lwip_begin(false);
    if (context_status != HAL_OK) {
      hal_mutex_unlock(s_udp_mutex);
      return context_status;
    }
    jh_lwip_udp_socket_close(&socket->udp);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  socket->bound = false;
  socket->packet_started = false;
  socket->pending_packet_size = 0;
  reset_last_remote(socket);

  hal_status_t bind_status = jh_rp2040_cyw43_provider_lwip_begin(false);
  if (bind_status == HAL_OK) {
    bind_status = jh_lwip_udp_socket_bind(&socket->udp, local->port);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  const bool ok = bind_status == HAL_OK;
  if (ok) {
    socket->bound = true;
  }

  hal_mutex_unlock(s_udp_mutex);

  if (!ok) {
    hal_derr("hal_udp_socket_bind: begin(%u) failed", (unsigned)local->port);
  }
  return bind_status;
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
  if (len > (size_t)INT_MAX) {
    hal_derr("hal_udp_socket_sendto: payload is too large");
    return HAL_EOVERFLOW;
  }
  const hal_status_t endpoint_status =
      validate_endpoint(remote, false, "hal_udp_socket_sendto", "remote");
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
  }

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(socket)) {
    hal_mutex_unlock(s_udp_mutex);
    hal_derr("hal_udp_socket_sendto: socket is invalid");
    return HAL_EINVAL;
  }
  if (!socket->bound) {
    hal_mutex_unlock(s_udp_mutex);
    hal_derr("hal_udp_socket_sendto: socket is not bound");
    return HAL_ESTATE;
  }

  ip4_addr_t remote_ip;
  IP4_ADDR(&remote_ip, remote->addr[0], remote->addr[1], remote->addr[2],
           remote->addr[3]);
  hal_status_t send_status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (send_status == HAL_OK) {
    send_status = jh_lwip_udp_socket_sendto(&socket->udp, data, len, &remote_ip,
                                            remote->port, out_sent);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  hal_mutex_unlock(s_udp_mutex);
  return send_status;
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
  if (max_len > (size_t)INT_MAX) {
    return HAL_EOVERFLOW;
  }

  const uint32_t start_ms = hal_millis();

  for (;;) {
    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    if (!is_valid_socket_locked(socket)) {
      hal_mutex_unlock(s_udp_mutex);
      hal_derr("hal_udp_socket_recvfrom: socket is invalid");
      return HAL_EINVAL;
    }
    if (!socket->bound) {
      hal_mutex_unlock(s_udp_mutex);
      hal_derr("hal_udp_socket_recvfrom: socket is not bound");
      return HAL_ESTATE;
    }

    bool has_packet = false;
    const int packet_size = socket_parse_packet_locked(socket, &has_packet);
    if (packet_size < 0) {
      hal_mutex_unlock(s_udp_mutex);
      return HAL_EIO;
    }
    if (has_packet) {
      endpoint_from_ip_address(socket->last_remote_ip, socket->last_remote_port,
                               remote);
      size_t received = 0u;
      hal_status_t read_status = HAL_OK;
      if (max_len > 0u) {
        read_status = jh_rp2040_cyw43_provider_lwip_begin(false);
        if (read_status == HAL_OK) {
          read_status = jh_lwip_udp_socket_read(&socket->udp, buffer, max_len,
                                                true, &received);
          jh_rp2040_cyw43_provider_lwip_end();
        }
        socket->pending_packet_size = 0;
      }

      hal_mutex_unlock(s_udp_mutex);
      if (read_status != HAL_OK) {
        return read_status;
      }
      *out_received = received;
      return HAL_OK;
    }

    hal_mutex_unlock(s_udp_mutex);

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
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  bool has_packet = false;
  if (is_valid_socket_locked(socket) && socket->bound) {
    (void)socket_parse_packet_locked(socket, &has_packet);
  }
  const bool ready = has_packet;

  hal_mutex_unlock(s_udp_mutex);
  return ready;
}

bool hal_udp_socket_can_send(hal_udp_socket_t socket) {
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  const bool ready = is_valid_socket_locked(socket) && socket->bound;

  hal_mutex_unlock(s_udp_mutex);
  return ready;
}

void hal_udp_socket_close(hal_udp_socket_t socket) {
  if (!socket) {
    return;
  }

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(socket)) {
    hal_mutex_unlock(s_udp_mutex);
    return;
  }

  const hal_status_t context_status =
      jh_rp2040_cyw43_provider_lwip_begin(false);
  if (context_status == HAL_OK) {
    jh_lwip_udp_socket_close(&socket->udp);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  socket->in_use = false;
  reset_socket_state(socket);
  if (s_default_udp == socket) {
    s_default_udp = NULL;
  }

  hal_mutex_unlock(s_udp_mutex);
}

hal_status_t hal_udp_begin_ex(uint16_t local_port) {
  if (local_port == 0u) {
    hal_derr("hal_udp_begin: local_port must be > 0");
    return HAL_EINVAL;
  }

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);
  const bool default_valid = is_valid_socket_locked(s_default_udp);
  hal_mutex_unlock(s_udp_mutex);

  if (!default_valid) {
    const hal_status_t open_status = hal_udp_socket_open_ex(&s_default_udp);
    if (hal_status_is_error(open_status)) {
      hal_derr("hal_udp_begin: socket allocation failed");
      return open_status;
    }
  }

  hal_net_endpoint_t local = {};
  local.family = HAL_NET_AF_INET;
  local.addr_len = HAL_NET_IPV4_ADDR_LEN;
  local.port = local_port;

  const hal_status_t status = hal_udp_socket_bind_ex(s_default_udp, &local);
  if (hal_status_is_error(status)) {
    hal_derr("hal_udp_begin: bind(%u) failed", (unsigned)local_port);
  }
  return status;
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
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound) {
    hal_mutex_unlock(s_udp_mutex);
    return HAL_EUNINIT;
  }

  const int packet_size = socket_parse_packet_locked(s_default_udp, NULL);

  hal_mutex_unlock(s_udp_mutex);
  if (packet_size < 0) {
    return HAL_EIO;
  }
  *out_size = packet_size;
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
  if (max_len == 0u) {
    return HAL_OK;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound) {
    hal_mutex_unlock(s_udp_mutex);
    return HAL_EUNINIT;
  }

  size_t read_count = 0u;
  hal_status_t read_status = jh_rp2040_cyw43_provider_lwip_begin(false);
  if (read_status == HAL_OK) {
    read_status = jh_lwip_udp_socket_read(&s_default_udp->udp, buffer, max_len,
                                          false, &read_count);
    s_default_udp->pending_packet_size =
        jh_lwip_udp_socket_parse(&s_default_udp->udp);
    jh_rp2040_cyw43_provider_lwip_end();
  }

  hal_mutex_unlock(s_udp_mutex);
  if (read_status != HAL_OK) {
    return read_status;
  }
  *out_read = (uint16_t)read_count;
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
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  uint8_t remote_ip[HAL_NET_IPV4_ADDR_LEN] = {};
  uint16_t remote_port = 0u;
  const bool default_ready =
      is_valid_socket_locked(s_default_udp) && s_default_udp->bound;
  if (default_ready) {
    memcpy(remote_ip, s_default_udp->last_remote_ip, sizeof(remote_ip));
    remote_port = s_default_udp->last_remote_port;
  }

  hal_mutex_unlock(s_udp_mutex);

  if (remote_port == 0u || ip_is_zero(remote_ip)) {
    const int written = snprintf(out, out_size, "%s", "0.0.0.0");
    if (written < 0) {
      hal_derr("hal_udp_remote_ip: snprintf failed for empty endpoint");
      return HAL_EIO;
    }
    if ((size_t)written >= out_size) {
      return HAL_EOVERFLOW;
    }
    return default_ready ? HAL_ENOENT : HAL_EUNINIT;
  }

  const int written = snprintf(out, out_size, "%u.%u.%u.%u",
                               (unsigned)remote_ip[0], (unsigned)remote_ip[1],
                               (unsigned)remote_ip[2], (unsigned)remote_ip[3]);
  if (written < 0) {
    hal_derr("hal_udp_remote_ip: snprintf failed");
    return HAL_EIO;
  }
  return (size_t)written < out_size ? HAL_OK : HAL_EOVERFLOW;
}

bool hal_udp_remote_ip(char *out, size_t out_size) {
  return hal_status_to_bool(hal_udp_remote_ip_ex(out, out_size));
}

hal_status_t hal_udp_remote_port_ex(uint16_t *out_port) {
  if (!out_port) {
    return HAL_EINVAL;
  }
  *out_port = 0u;
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  const bool default_ready =
      is_valid_socket_locked(s_default_udp) && s_default_udp->bound;
  const uint16_t remote_port =
      default_ready ? s_default_udp->last_remote_port : 0u;

  hal_mutex_unlock(s_udp_mutex);
  *out_port = remote_port;
  if (!default_ready) {
    return HAL_EUNINIT;
  }
  return remote_port != 0u ? HAL_OK : HAL_ENOENT;
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
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);
  const bool default_ready =
      is_valid_socket_locked(s_default_udp) && s_default_udp->bound;
  hal_mutex_unlock(s_udp_mutex);
  if (!default_ready) {
    hal_derr("hal_udp_begin_packet: UDP socket is not started");
    return HAL_EUNINIT;
  }

  uint8_t resolved_address[HAL_NET_IPV4_ADDR_LEN] = {};
  const hal_status_t resolve_status =
      hal_net_resolve_ipv4_ex(host_or_ip, resolved_address);
  if (resolve_status != HAL_OK) {
    return resolve_status;
  }
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound) {
    hal_mutex_unlock(s_udp_mutex);
    hal_derr("hal_udp_begin_packet: UDP socket is not started");
    return HAL_EUNINIT;
  }

  ip4_addr_t remote_address;
  IP4_ADDR(&remote_address, resolved_address[0], resolved_address[1],
           resolved_address[2], resolved_address[3]);
  hal_status_t begin_status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (begin_status == HAL_OK) {
    begin_status = jh_lwip_udp_socket_begin_packet(
        &s_default_udp->udp, &remote_address, remote_port);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  const bool ok = begin_status == HAL_OK;
  s_default_udp->packet_started = ok;

  hal_mutex_unlock(s_udp_mutex);

  if (!ok) {
    hal_derr("hal_udp_begin_packet: beginPacket failed");
  }
  return begin_status;
}

bool hal_udp_begin_packet(const char *host_or_ip, uint16_t remote_port) {
  return hal_status_to_bool(hal_udp_begin_packet_ex(host_or_ip, remote_port));
}

hal_status_t hal_udp_begin_packet_remote_ex(void) {
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound) {
    hal_mutex_unlock(s_udp_mutex);
    hal_derr("hal_udp_begin_packet_remote: UDP socket is not started");
    return HAL_EUNINIT;
  }

  if (s_default_udp->last_remote_port == 0u ||
      ip_is_zero(s_default_udp->last_remote_ip)) {
    hal_mutex_unlock(s_udp_mutex);
    hal_derr("hal_udp_begin_packet_remote: remote endpoint is not available");
    return HAL_ENOENT;
  }

  ip4_addr_t remote_address;
  IP4_ADDR(&remote_address, s_default_udp->last_remote_ip[0],
           s_default_udp->last_remote_ip[1], s_default_udp->last_remote_ip[2],
           s_default_udp->last_remote_ip[3]);
  hal_status_t begin_status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (begin_status == HAL_OK) {
    begin_status = jh_lwip_udp_socket_begin_packet(
        &s_default_udp->udp, &remote_address, s_default_udp->last_remote_port);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  const bool ok = begin_status == HAL_OK;
  s_default_udp->packet_started = ok;

  hal_mutex_unlock(s_udp_mutex);

  if (!ok) {
    hal_derr("hal_udp_begin_packet_remote: beginPacket failed");
  }
  return begin_status;
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
  if (len == 0u) {
    return HAL_OK;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound) {
    hal_mutex_unlock(s_udp_mutex);
    return HAL_EUNINIT;
  }
  if (!s_default_udp->packet_started) {
    hal_mutex_unlock(s_udp_mutex);
    return HAL_ESTATE;
  }

  size_t written = 0u;
  hal_status_t write_status = jh_rp2040_cyw43_provider_lwip_begin(false);
  if (write_status == HAL_OK) {
    write_status =
        jh_lwip_udp_socket_write(&s_default_udp->udp, data, len, &written);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  if (written > 65535u) {
    written = 65535u;
  }

  hal_mutex_unlock(s_udp_mutex);

  *out_written = (uint16_t)written;
  return write_status;
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
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound ||
      !s_default_udp->packet_started) {
    hal_mutex_unlock(s_udp_mutex);
    return HAL_ESTATE;
  }

  hal_status_t end_status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (end_status == HAL_OK) {
    end_status = jh_lwip_udp_socket_end_packet(&s_default_udp->udp);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  s_default_udp->packet_started = false;

  hal_mutex_unlock(s_udp_mutex);

  return end_status;
}

bool hal_udp_end_packet(void) {
  return hal_status_to_bool(hal_udp_end_packet_ex());
}

static hal_status_t backend_udp_open(void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  hal_udp_socket_t socket = nullptr;
  const hal_status_t status = hal_udp_socket_open_ex(&socket);
  *out_socket = socket;
  return status;
}

static hal_status_t backend_udp_bind(void *socket,
                                     const hal_net_endpoint_t *local) {
  return hal_udp_socket_bind_ex(static_cast<hal_udp_socket_t>(socket), local);
}

static hal_status_t backend_udp_sendto(void *socket, const void *data,
                                       size_t len,
                                       const hal_net_endpoint_t *remote,
                                       size_t *out_sent) {
  return hal_udp_socket_sendto_ex(static_cast<hal_udp_socket_t>(socket), data,
                                  len, remote, out_sent);
}

static hal_status_t backend_udp_recvfrom(void *socket, void *buffer,
                                         size_t max_len,
                                         hal_net_endpoint_t *remote,
                                         uint32_t timeout_ms,
                                         size_t *out_received) {
  return hal_udp_socket_recvfrom_ex(static_cast<hal_udp_socket_t>(socket),
                                    buffer, max_len, remote, timeout_ms,
                                    out_received);
}

static bool backend_udp_can_recv(void *socket) {
  return hal_udp_socket_can_recv(static_cast<hal_udp_socket_t>(socket));
}

static bool backend_udp_can_send(void *socket) {
  return hal_udp_socket_can_send(static_cast<hal_udp_socket_t>(socket));
}

static void backend_udp_close(void *socket) {
  hal_udp_socket_close(static_cast<hal_udp_socket_t>(socket));
}

extern "C" const jh_network_udp_ops_t *jh_rp2040_cyw43_udp_ops(void) {
  static const jh_network_udp_ops_t ops = {
      backend_udp_open,     backend_udp_bind,     backend_udp_sendto,
      backend_udp_recvfrom, backend_udp_can_recv, backend_udp_can_send,
      backend_udp_close,
  };
  return &ops;
}

#endif /* HAL_ENABLE_UDP */
#endif // HAL_TARGET_IS_RP
