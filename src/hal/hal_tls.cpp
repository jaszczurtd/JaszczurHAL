#include "hal_tls.h"

#ifdef HAL_ENABLE_TLS

#include "hal_net.h"
#include "hal_sync.h"
#include "impl/shared/hal_mutex_once.h"
#include "impl/shared/network/jh_network_handle_pool.h"

#include <string.h>

#define JH_TLS_HANDLE_KIND 7u

typedef struct {
  hal_tls_client_config_t config;
  char hostname[HAL_TLS_HOSTNAME_MAX_LENGTH + 1u];
  uint16_t port;
  hal_tls_state_t state;
  hal_status_t last_status;
  int32_t provider_error;
  bool allocated;
} jh_tls_client_context_t;

static jh_tls_client_context_t s_clients[HAL_TLS_MAX_CLIENTS];
static jh_network_handle_slot_t s_handle_slots[HAL_TLS_MAX_CLIENTS];
static jh_network_handle_pool_t s_handle_pool;
static hal_mutex_t s_tls_mutex = NULL;
static bool s_pool_initialized = false;

static void tls_lock(void) {
  (void)jh_hal_mutex_create_once(&s_tls_mutex);
  hal_mutex_lock(s_tls_mutex);
  if (!s_pool_initialized) {
    (void)jh_network_handle_pool_init(&s_handle_pool, s_handle_slots,
                                      HAL_TLS_MAX_CLIENTS, JH_TLS_HANDLE_KIND);
    s_pool_initialized = true;
  }
}

static void tls_unlock(void) { hal_mutex_unlock(s_tls_mutex); }

static hal_status_t resolve_client(hal_tls_client_t handle,
                                   jh_tls_client_context_t **out_client) {
  void *token = NULL;
  hal_status_t status =
      jh_network_handle_resolve(&s_handle_pool, handle, &token, NULL);
  if (status != HAL_OK || token == NULL) {
    *out_client = NULL;
    return HAL_EINVAL;
  }
  *out_client = static_cast<jh_tls_client_context_t *>(token);
  return HAL_OK;
}

static hal_status_t record_error(jh_tls_client_context_t *client,
                                 hal_status_t status, int32_t provider_error) {
  client->last_status = status;
  client->provider_error = provider_error;
  return status;
}

static bool config_is_valid(const hal_tls_client_config_t *config) {
  return config != NULL &&
         (config->execution_model == HAL_TLS_EXECUTION_POLL ||
          config->execution_model == HAL_TLS_EXECUTION_BOUNDED_WORKER) &&
         config->transport_timeout_ms > 0u &&
         config->transport_timeout_ms != HAL_NET_TIMEOUT_FOREVER &&
         config->operation_timeout_ms > 0u &&
         config->operation_timeout_ms != HAL_NET_TIMEOUT_FOREVER &&
         config->poll_step_budget > 0u;
}

hal_status_t hal_tls_client_config_init(hal_tls_client_config_t *config) {
  if (config == NULL) {
    return HAL_EINVAL;
  }
  config->execution_model = HAL_TLS_EXECUTION_POLL;
  config->transport_timeout_ms = HAL_TLS_DEFAULT_TRANSPORT_TIMEOUT_MS;
  config->operation_timeout_ms = HAL_TLS_DEFAULT_OPERATION_TIMEOUT_MS;
  config->poll_step_budget = HAL_TLS_DEFAULT_POLL_STEP_BUDGET;
  return HAL_OK;
}

hal_status_t hal_tls_client_create_ex(const hal_tls_client_config_t *config,
                                      hal_tls_client_t *out_client) {
  if (out_client == NULL) {
    return HAL_EINVAL;
  }
  *out_client = NULL;
  if (!config_is_valid(config)) {
    return HAL_EINVAL;
  }

  tls_lock();
  jh_tls_client_context_t *client = NULL;
  for (size_t index = 0u; index < HAL_TLS_MAX_CLIENTS; ++index) {
    if (!s_clients[index].allocated) {
      client = &s_clients[index];
      memset(client, 0, sizeof(*client));
      client->allocated = true;
      client->config = *config;
      client->state = HAL_TLS_STATE_CREATED;
      client->last_status = HAL_OK;
      break;
    }
  }
  if (client == NULL) {
    tls_unlock();
    return HAL_ENOMEM;
  }

  void *handle = NULL;
  const hal_status_t status =
      jh_network_handle_allocate(&s_handle_pool, client, &handle);
  if (status != HAL_OK) {
    memset(client, 0, sizeof(*client));
    tls_unlock();
    return status;
  }
  *out_client = static_cast<hal_tls_client_t>(handle);
  tls_unlock();
  return HAL_OK;
}

hal_status_t hal_tls_client_configure_server_ex(hal_tls_client_t handle,
                                                const char *hostname,
                                                uint16_t port) {
  if (hostname == NULL || hostname[0] == '\0' || port == 0u) {
    return HAL_EINVAL;
  }
  const size_t hostname_length = strlen(hostname);
  if (hostname_length > HAL_TLS_HOSTNAME_MAX_LENGTH) {
    return HAL_EOVERFLOW;
  }

  tls_lock();
  jh_tls_client_context_t *client = NULL;
  hal_status_t status = resolve_client(handle, &client);
  if (status == HAL_OK && client->state != HAL_TLS_STATE_CREATED &&
      client->state != HAL_TLS_STATE_CONFIGURED) {
    status = HAL_ESTATE;
  }
  if (status == HAL_OK) {
    memcpy(client->hostname, hostname, hostname_length + 1u);
    client->port = port;
    client->state = HAL_TLS_STATE_CONFIGURED;
    client->last_status = HAL_OK;
    client->provider_error = 0;
  }
  tls_unlock();
  return status;
}

hal_status_t hal_tls_client_connect_ex(hal_tls_client_t handle) {
  tls_lock();
  jh_tls_client_context_t *client = NULL;
  hal_status_t status = resolve_client(handle, &client);
  if (status == HAL_OK && client->state != HAL_TLS_STATE_CONFIGURED) {
    status = HAL_ESTATE;
  }
  if (status == HAL_OK) {
    /* Point 17 supplies trust anchors, SNI/hostname validation, time and
     * entropy. Refuse to start a connection before that secure configuration
     * exists; an implicit insecure client is deliberately impossible. */
    status = record_error(client, HAL_ECONFIG, 0);
  }
  tls_unlock();
  return status;
}

hal_status_t hal_tls_client_poll_ex(hal_tls_client_t handle) {
  tls_lock();
  jh_tls_client_context_t *client = NULL;
  hal_status_t status = resolve_client(handle, &client);
  if (status == HAL_OK) {
    status = client->state == HAL_TLS_STATE_CONNECTING ||
                     client->state == HAL_TLS_STATE_CLOSING
                 ? HAL_EAGAIN
                 : HAL_ESTATE;
  }
  tls_unlock();
  return status;
}

hal_status_t hal_tls_client_read_ex(hal_tls_client_t handle, void *buffer,
                                    size_t capacity, size_t *out_received) {
  if (out_received == NULL || (capacity > 0u && buffer == NULL)) {
    return HAL_EINVAL;
  }
  *out_received = 0u;
  tls_lock();
  jh_tls_client_context_t *client = NULL;
  hal_status_t status = resolve_client(handle, &client);
  if (status == HAL_OK) {
    status = client->state == HAL_TLS_STATE_CONNECTED ? HAL_EAGAIN : HAL_ESTATE;
  }
  tls_unlock();
  return status;
}

hal_status_t hal_tls_client_write_ex(hal_tls_client_t handle, const void *data,
                                     size_t length, size_t *out_written) {
  if (out_written == NULL || (length > 0u && data == NULL)) {
    return HAL_EINVAL;
  }
  *out_written = 0u;
  tls_lock();
  jh_tls_client_context_t *client = NULL;
  hal_status_t status = resolve_client(handle, &client);
  if (status == HAL_OK) {
    status = client->state == HAL_TLS_STATE_CONNECTED ? HAL_EAGAIN : HAL_ESTATE;
  }
  tls_unlock();
  return status;
}

hal_status_t hal_tls_client_shutdown_ex(hal_tls_client_t handle) {
  tls_lock();
  jh_tls_client_context_t *client = NULL;
  hal_status_t status = resolve_client(handle, &client);
  if (status == HAL_OK) {
    if (client->state == HAL_TLS_STATE_CREATED ||
        client->state == HAL_TLS_STATE_CONFIGURED ||
        client->state == HAL_TLS_STATE_FAILED) {
      client->state = HAL_TLS_STATE_CLOSED;
      client->last_status = HAL_OK;
    } else if (client->state == HAL_TLS_STATE_CLOSED) {
      status = HAL_OK;
    } else if (client->state == HAL_TLS_STATE_CONNECTED) {
      client->state = HAL_TLS_STATE_CLOSING;
      status = HAL_EAGAIN;
    } else {
      status = HAL_ESTATE;
    }
  }
  tls_unlock();
  return status;
}

hal_status_t hal_tls_client_get_state_ex(hal_tls_client_t handle,
                                         hal_tls_state_t *out_state) {
  if (out_state == NULL) {
    return HAL_EINVAL;
  }
  tls_lock();
  jh_tls_client_context_t *client = NULL;
  const hal_status_t status = resolve_client(handle, &client);
  if (status == HAL_OK) {
    *out_state = client->state;
  }
  tls_unlock();
  return status;
}

hal_status_t hal_tls_client_get_last_error_ex(hal_tls_client_t handle,
                                              hal_status_t *out_status,
                                              int32_t *out_provider_error) {
  if (out_status == NULL || out_provider_error == NULL) {
    return HAL_EINVAL;
  }
  tls_lock();
  jh_tls_client_context_t *client = NULL;
  const hal_status_t status = resolve_client(handle, &client);
  if (status == HAL_OK) {
    *out_status = client->last_status;
    *out_provider_error = client->provider_error;
  }
  tls_unlock();
  return status;
}

hal_status_t hal_tls_client_close_ex(hal_tls_client_t handle) {
  tls_lock();
  jh_tls_client_context_t *client = NULL;
  hal_status_t status = resolve_client(handle, &client);
  if (status == HAL_OK) {
    void *released = NULL;
    status = jh_network_handle_release(&s_handle_pool, handle, &released);
    if (status == HAL_OK) {
      memset(client, 0, sizeof(*client));
    }
  }
  tls_unlock();
  return status;
}

#endif
