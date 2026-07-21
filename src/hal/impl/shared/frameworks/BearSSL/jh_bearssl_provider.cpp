#include "jh_bearssl_provider.h"
#include "hal/hal_config.h"

#if defined(HAL_ENABLE_TLS) && defined(HAL_ENABLE_BSD_SOCKETS)

#include <string.h>

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

const char *jh_bearssl_provider_source_revision(void) {
  return "aca13833b6f9ddffaea2041a01facc76829dc03b";
}

#endif
