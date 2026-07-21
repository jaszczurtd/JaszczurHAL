#include "jh_bearssl_bsd_io.h"
#include "hal/hal_config.h"

#if defined(HAL_ENABLE_TLS) && defined(HAL_ENABLE_BSD_SOCKETS)

#include "hal/hal_net.h"
#include "hal/hal_system.h"

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
  return (uint32_t)(hal_millis() - started_ms) >= io->timeout_ms;
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

#endif
