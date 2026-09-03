#include "jh_bearssl_bsd_io.h"
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_TLS) && defined(HAL_ENABLE_BSD_SOCKETS)

#include "hal/network/hal_net.h"
#include "hal/system/hal_system.h"

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef EINTR
#define EINTR EAGAIN
#endif
#ifndef ECANCELED
#define ECANCELED EIO
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT EIO
#endif

static bool io_timed_out(const jh_bearssl_bsd_io_t *io, uint32_t started_ms) {
  return hal_millis_deadline_expired(started_ms, io->timeout_ms);
}

static bool io_cancelled(jh_bearssl_bsd_io_t *io) {
  if (io->is_cancelled == NULL || !io->is_cancelled(io->callback_context)) {
    return false;
  }
  io->last_errno = ECANCELED;
  io->last_status = HAL_ECANCELED;
  return true;
}

static void io_wait_once(jh_bearssl_bsd_io_t *io) {
  if (io->service != NULL) {
    io->service(io->callback_context);
  }
  hal_idle();
  hal_delay_ms(1u);
}

static hal_status_t bsd_transport_send(void *context, const void *data,
                                       size_t length, size_t *out_sent) {
  jh_bearssl_bsd_transport_t *adapter =
      static_cast<jh_bearssl_bsd_transport_t *>(context);
  if (adapter == nullptr || adapter->fd < 0 || out_sent == nullptr) {
    return HAL_EINVAL;
  }
  *out_sent = 0u;
  const ssize_t sent = send(adapter->fd, data, length, MSG_DONTWAIT);
  if (sent > 0) {
    *out_sent = (size_t)sent;
    return HAL_OK;
  }
  if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    return HAL_EAGAIN;
  }
  return HAL_EIO;
}

static hal_status_t bsd_transport_receive(void *context, void *buffer,
                                          size_t capacity,
                                          size_t *out_received) {
  jh_bearssl_bsd_transport_t *adapter =
      static_cast<jh_bearssl_bsd_transport_t *>(context);
  if (adapter == nullptr || adapter->fd < 0 || out_received == nullptr) {
    return HAL_EINVAL;
  }
  *out_received = 0u;
  const ssize_t received = recv(adapter->fd, buffer, capacity, MSG_DONTWAIT);
  if (received > 0) {
    *out_received = (size_t)received;
    return HAL_OK;
  }
  if (received < 0 &&
      (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    return HAL_EAGAIN;
  }
  return HAL_EPROTO;
}

hal_status_t jh_bearssl_bsd_transport_init(jh_bearssl_bsd_transport_t *adapter,
                                           int fd) {
  if (adapter == nullptr || fd < 0) {
    return HAL_EINVAL;
  }
  memset(adapter, 0, sizeof(*adapter));
  adapter->fd = fd;
  adapter->transport.context = adapter;
  adapter->transport.send = bsd_transport_send;
  adapter->transport.receive = bsd_transport_receive;
  return HAL_OK;
}

hal_status_t jh_bearssl_bsd_io_init(jh_bearssl_bsd_io_t *io, int fd,
                                    uint32_t timeout_ms,
                                    jh_bearssl_cancel_fn is_cancelled,
                                    jh_bearssl_service_fn service,
                                    void *callback_context) {
  if (io == NULL || fd < 0 || timeout_ms == 0u ||
      timeout_ms == HAL_NET_TIMEOUT_FOREVER) {
    return HAL_EINVAL;
  }

  memset(io, 0, sizeof(*io));
  io->fd = fd;
  io->timeout_ms = timeout_ms;
  io->last_status = HAL_OK;
  io->is_cancelled = is_cancelled;
  io->service = service;
  io->callback_context = callback_context;
  return HAL_OK;
}

int jh_bearssl_bsd_read(void *context, unsigned char *buffer, size_t length) {
  jh_bearssl_bsd_io_t *io = static_cast<jh_bearssl_bsd_io_t *>(context);
  if (io == NULL || buffer == NULL || length == 0u || length > INT_MAX) {
    if (io != NULL) {
      io->last_errno = EINVAL;
      io->last_status = HAL_EINVAL;
    }
    return -1;
  }

  const uint32_t started_ms = hal_millis();
  for (;;) {
    if (io_cancelled(io)) {
      return -1;
    }
    const ssize_t received = recv(io->fd, buffer, length, MSG_DONTWAIT);
    if (received > 0) {
      io->last_errno = 0;
      io->last_status = HAL_OK;
      return (int)received;
    }
    if (received == 0) {
      io->last_errno = 0;
      io->last_status = HAL_EIO;
      return -1;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      io->last_errno = errno;
      io->last_status = HAL_EIO;
      return -1;
    }
    if (io_timed_out(io, started_ms)) {
      io->last_errno = ETIMEDOUT;
      io->last_status = HAL_ETIMEOUT;
      return -1;
    }
    io_wait_once(io);
  }
}

int jh_bearssl_bsd_write(void *context, const unsigned char *data,
                         size_t length) {
  jh_bearssl_bsd_io_t *io = static_cast<jh_bearssl_bsd_io_t *>(context);
  if (io == NULL || data == NULL || length == 0u || length > INT_MAX) {
    if (io != NULL) {
      io->last_errno = EINVAL;
      io->last_status = HAL_EINVAL;
    }
    return -1;
  }

  const uint32_t started_ms = hal_millis();
  for (;;) {
    if (io_cancelled(io)) {
      return -1;
    }
    const ssize_t sent = send(io->fd, data, length, MSG_DONTWAIT);
    if (sent > 0) {
      io->last_errno = 0;
      io->last_status = HAL_OK;
      return (int)sent;
    }
    if (sent == 0) {
      io->last_errno = EIO;
      io->last_status = HAL_EIO;
      return -1;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      io->last_errno = errno;
      io->last_status = HAL_EIO;
      return -1;
    }
    if (io_timed_out(io, started_ms)) {
      io->last_errno = ETIMEDOUT;
      io->last_status = HAL_ETIMEOUT;
      return -1;
    }
    io_wait_once(io);
  }
}

hal_status_t jh_bearssl_blocking_io_init(jh_bearssl_blocking_io_t *provider,
                                         br_ssl_engine_context *engine, int fd,
                                         uint32_t timeout_ms,
                                         jh_bearssl_cancel_fn is_cancelled,
                                         jh_bearssl_service_fn service,
                                         void *callback_context) {
  if (provider == NULL || engine == NULL) {
    return HAL_EINVAL;
  }
  memset(provider, 0, sizeof(*provider));
  hal_status_t status =
      jh_bearssl_bsd_io_init(&provider->transport, fd, timeout_ms, is_cancelled,
                             service, callback_context);
  if (status != HAL_OK) {
    return status;
  }
  br_sslio_init(&provider->io, engine, jh_bearssl_bsd_read,
                &provider->transport, jh_bearssl_bsd_write,
                &provider->transport);
  return HAL_OK;
}

#endif
