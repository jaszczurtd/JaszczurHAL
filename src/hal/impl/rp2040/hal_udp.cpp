#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#ifdef HAL_ENABLE_UDP

#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "../../hal_udp.h"
#include "../shared/hal_mutex_once.h"

#include <IPAddress.h>
#include <WiFiUdp.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct hal_udp_socket_impl_t {
  WiFiUDP udp;
  bool in_use;
  bool bound;
  bool packet_started;
  int pending_packet_size;
  IPAddress last_remote_ip;
  uint16_t last_remote_port;
};

static hal_udp_socket_impl_t s_udp_pool[HAL_UDP_SOCKET_MAX_INSTANCES];
static hal_udp_socket_t s_default_udp = NULL;
static hal_mutex_t s_udp_mutex = NULL;

static inline void udp_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_udp_mutex);
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

static bool ip_is_zero(const IPAddress &ip) {
  return ip[0] == 0u && ip[1] == 0u && ip[2] == 0u && ip[3] == 0u;
}

static void reset_last_remote(hal_udp_socket_impl_t *socket) {
  socket->last_remote_ip = IPAddress(0, 0, 0, 0);
  socket->last_remote_port = 0u;
}

static void reset_socket_state(hal_udp_socket_impl_t *socket) {
  socket->bound = false;
  socket->packet_started = false;
  socket->pending_packet_size = 0;
  reset_last_remote(socket);
}

static bool is_valid_socket_locked(hal_udp_socket_t socket) {
  for (size_t i = 0u; i < HAL_UDP_SOCKET_MAX_INSTANCES; ++i) {
    if (socket == &s_udp_pool[i] && s_udp_pool[i].in_use) {
      return true;
    }
  }
  return false;
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

static IPAddress ip_address_from_endpoint(const hal_net_endpoint_t *endpoint) {
  return IPAddress(endpoint->addr[0], endpoint->addr[1], endpoint->addr[2],
                   endpoint->addr[3]);
}

static int socket_parse_packet_locked(hal_udp_socket_impl_t *socket) {
  if (!socket->bound) {
    return 0;
  }
  if (socket->pending_packet_size > 0) {
    return socket->pending_packet_size;
  }

  const int packet_size = socket->udp.parsePacket();
  if (packet_size > 0) {
    socket->pending_packet_size = packet_size;
    socket->last_remote_ip = socket->udp.remoteIP();
    socket->last_remote_port = socket->udp.remotePort();
  }
  return packet_size;
}

hal_status_t hal_udp_socket_open_ex(hal_udp_socket_t *out_socket) {
  if (!out_socket) {
    return HAL_EINVAL;
  }
  *out_socket = NULL;
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
  if (!validate_endpoint(local, "hal_udp_socket_bind", "local")) {
    return HAL_EINVAL;
  }

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(socket)) {
    hal_mutex_unlock(s_udp_mutex);
    hal_derr("hal_udp_socket_bind: socket handle is invalid");
    return HAL_EINVAL;
  }

  if (socket->bound) {
    socket->udp.stop();
  }
  socket->bound = false;
  socket->packet_started = false;
  socket->pending_packet_size = 0;
  reset_last_remote(socket);

  const bool ok = socket->udp.begin(local->port);
  if (ok) {
    socket->bound = true;
  }

  hal_mutex_unlock(s_udp_mutex);

  if (!ok) {
    hal_derr("hal_udp_socket_bind: begin(%u) failed", (unsigned)local->port);
  }
  return ok ? HAL_OK : HAL_EIO;
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
  if (!validate_endpoint(remote, "hal_udp_socket_sendto", "remote")) {
    return HAL_EINVAL;
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

  const IPAddress remote_ip = ip_address_from_endpoint(remote);
  const bool begun = socket->udp.beginPacket(remote_ip, remote->port);
  if (!begun) {
    hal_mutex_unlock(s_udp_mutex);
    hal_derr("hal_udp_socket_sendto: beginPacket failed");
    return HAL_EIO;
  }

  size_t written = 0u;
  if (len > 0u) {
    written = socket->udp.write((const uint8_t *)data, len);
  }

  const int rc = socket->udp.endPacket();

  hal_mutex_unlock(s_udp_mutex);

  if (rc != 1) {
    hal_derr("hal_udp_socket_sendto: endPacket failed (rc=%d)", rc);
    return HAL_EIO;
  }

  *out_sent = written;
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

    const int packet_size = socket_parse_packet_locked(socket);
    if (packet_size > 0) {
      endpoint_from_ip_address(socket->last_remote_ip, socket->last_remote_port,
                               remote);
      int read_count = 0;
      if (max_len > 0u) {
        read_count = socket->udp.read((uint8_t *)buffer, max_len);
        socket->pending_packet_size = 0;
      }

      hal_mutex_unlock(s_udp_mutex);
      if (read_count < 0) {
        return HAL_EIO;
      }
      *out_received = (size_t)read_count;
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

  const bool ready = is_valid_socket_locked(socket) && socket->bound &&
                     socket_parse_packet_locked(socket) > 0;

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

  socket->udp.stop();
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
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound) {
    hal_mutex_unlock(s_udp_mutex);
    return HAL_EUNINIT;
  }

  const int packet_size = socket_parse_packet_locked(s_default_udp);

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

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound) {
    hal_mutex_unlock(s_udp_mutex);
    return HAL_EUNINIT;
  }

  const int read_count = s_default_udp->udp.read(buffer, max_len);
  s_default_udp->pending_packet_size = 0;

  hal_mutex_unlock(s_udp_mutex);
  if (read_count < 0) {
    return HAL_EIO;
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

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  IPAddress remote_ip(0, 0, 0, 0);
  uint16_t remote_port = 0u;
  const bool default_ready =
      is_valid_socket_locked(s_default_udp) && s_default_udp->bound;
  if (default_ready) {
    remote_ip = s_default_udp->last_remote_ip;
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

  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound) {
    hal_mutex_unlock(s_udp_mutex);
    hal_derr("hal_udp_begin_packet: UDP socket is not started");
    return HAL_EUNINIT;
  }

  const bool ok = s_default_udp->udp.beginPacket(host_or_ip, remote_port);
  s_default_udp->packet_started = ok;

  hal_mutex_unlock(s_udp_mutex);

  if (!ok) {
    hal_derr("hal_udp_begin_packet: beginPacket failed");
  }
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_udp_begin_packet(const char *host_or_ip, uint16_t remote_port) {
  return hal_status_to_bool(hal_udp_begin_packet_ex(host_or_ip, remote_port));
}

hal_status_t hal_udp_begin_packet_remote_ex(void) {
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

  const bool ok = s_default_udp->udp.beginPacket(
      s_default_udp->last_remote_ip, s_default_udp->last_remote_port);
  s_default_udp->packet_started = ok;

  hal_mutex_unlock(s_udp_mutex);

  if (!ok) {
    hal_derr("hal_udp_begin_packet_remote: beginPacket failed");
  }
  return ok ? HAL_OK : HAL_EIO;
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

  size_t written = s_default_udp->udp.write(data, len);
  if (written > 65535u) {
    written = 65535u;
  }

  hal_mutex_unlock(s_udp_mutex);

  *out_written = (uint16_t)written;
  return written == len ? HAL_OK : HAL_EIO;
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
  udp_ensure_mutex();
  hal_mutex_lock(s_udp_mutex);

  if (!is_valid_socket_locked(s_default_udp) || !s_default_udp->bound ||
      !s_default_udp->packet_started) {
    hal_mutex_unlock(s_udp_mutex);
    return HAL_ESTATE;
  }

  const int rc = s_default_udp->udp.endPacket();
  s_default_udp->packet_started = false;

  hal_mutex_unlock(s_udp_mutex);

  if (rc != 1) {
    hal_derr("hal_udp_end_packet: endPacket failed (rc=%d)", rc);
    return HAL_EIO;
  }

  return HAL_OK;
}

bool hal_udp_end_packet(void) {
  return hal_status_to_bool(hal_udp_end_packet_ex());
}

#endif /* HAL_ENABLE_UDP */
#endif // HAL_TARGET_IS_RP2040
