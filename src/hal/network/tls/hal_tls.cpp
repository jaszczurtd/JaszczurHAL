#include "hal/network/tls/hal_tls.h"

#ifdef HAL_ENABLE_TLS

#include "hal/core/hal_mutex_once.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_tcp.h"
#include "hal/network/jh_network_handle_pool.h"
#include "hal/network/tls/BearSSL/jh_bearssl_engine.h"
#include "hal/network/tls/BearSSL/jh_bearssl_hal_tcp_io.h"
#include "hal/network/tls/BearSSL/jh_bearssl_provider.h"
#include "hal/network/tls/jh_tls_timeout_policy.h"
#include "hal/security/jh_secure_random.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "hal/time/hal_time.h"

#include <algorithm>
#include <string.h>

#define JH_TLS_HANDLE_KIND 7u
#define JH_TLS_RESOLVE_MAX_RESULTS 8u

typedef struct {
  hal_tls_client_config_t config;
  char hostname[HAL_TLS_HOSTNAME_MAX_LENGTH + 1u];
  uint16_t port;
  hal_tls_state_t state;
  hal_status_t last_status;
  int32_t provider_error;
  hal_tls_security_config_t security;
  jh_bearssl_client_t *provider;
  hal_tcp_socket_t socket;
  jh_bearssl_hal_tcp_transport_t transport;
  uint32_t operation_started_ms;
  bool security_configured;
  bool has_server_key_pin;
  uint8_t server_key_pin[32];
  bool cancelled;
  bool allocated;
  bool callback_active;
} jh_tls_client_context_t;

typedef struct {
  jh_tls_client_context_t *client;
  jh_network_handle_lease_t lease;
  hal_mutex_t mutex;
} jh_tls_operation_t;

static jh_tls_client_context_t s_clients[HAL_TLS_MAX_CLIENTS];
static jh_network_handle_slot_t s_handle_slots[HAL_TLS_MAX_CLIENTS];
static jh_network_handle_pool_t s_handle_pool;
static hal_mutex_t s_pool_mutex = NULL;
static hal_mutex_t s_client_mutexes[HAL_TLS_MAX_CLIENTS];
static bool s_pool_initialized = false;

hal_status_t hal_tls_default_time(void *, uint64_t *out_unix_seconds) {
  if (out_unix_seconds == NULL) {
    return HAL_EINVAL;
  }
#ifdef HAL_ENABLE_TIME
  *out_unix_seconds = hal_time_unix();
  return *out_unix_seconds >= HAL_TLS_MIN_VALID_UNIX_TIME ? HAL_OK
                                                          : HAL_ECONFIG;
#else
  *out_unix_seconds = 0u;
  return HAL_EUNSUPPORTED;
#endif
}

hal_status_t hal_tls_default_entropy(void *, void *buffer, size_t length) {
  return jh_secure_random_bytes(buffer, length);
}

static hal_status_t pool_lock(void) {
  if (jh_hal_mutex_create_once(&s_pool_mutex) == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_pool_mutex);
  if (!s_pool_initialized) {
    hal_status_t status =
        jh_network_handle_pool_init(&s_handle_pool, s_handle_slots,
                                    HAL_TLS_MAX_CLIENTS, JH_TLS_HANDLE_KIND);
    if (status != HAL_OK) {
      hal_mutex_unlock(s_pool_mutex);
      return status;
    }
    for (size_t index = 0u; index < HAL_TLS_MAX_CLIENTS; ++index) {
      if (jh_hal_mutex_create_once(&s_client_mutexes[index]) == NULL) {
        hal_mutex_unlock(s_pool_mutex);
        return HAL_ENOMEM;
      }
    }
    s_pool_initialized = true;
  }
  return HAL_OK;
}

static void pool_unlock(void) { hal_mutex_unlock(s_pool_mutex); }

static hal_status_t record_error(jh_tls_client_context_t *client,
                                 hal_status_t status, int32_t provider_error) {
  client->last_status = status;
  client->provider_error = provider_error;
  return status;
}

static void release_transport(jh_tls_client_context_t *client) {
  if (client->socket != nullptr) {
    hal_tcp_socket_close(client->socket);
    client->socket = nullptr;
  }
  memset(&client->transport, 0, sizeof(client->transport));
  jh_bearssl_client_release(client->provider);
  client->provider = NULL;
}

static void finalize_client(void *token) {
  if (token == NULL) {
    return;
  }
  jh_tls_client_context_t *client =
      static_cast<jh_tls_client_context_t *>(token);
  const size_t index = (size_t)(client - &s_clients[0]);
  if (index >= HAL_TLS_MAX_CLIENTS) {
    return;
  }
  hal_mutex_lock(s_client_mutexes[index]);
  release_transport(client);
  if (pool_lock() != HAL_OK) {
    hal_mutex_unlock(s_client_mutexes[index]);
    return;
  }
  memset(client, 0, sizeof(*client));
  pool_unlock();
  hal_mutex_unlock(s_client_mutexes[index]);
}

static hal_status_t begin_operation(hal_tls_client_t handle,
                                    jh_tls_operation_t *operation) {
  if (operation == NULL) {
    return HAL_EINVAL;
  }
  memset(operation, 0, sizeof(*operation));

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  status = jh_network_handle_acquire(&s_handle_pool, handle, &operation->lease);
  if (status != HAL_OK) {
    pool_unlock();
    return HAL_EINVAL;
  }
  operation->client =
      static_cast<jh_tls_client_context_t *>(operation->lease.backend_token);
  const size_t index = (size_t)(operation->client - &s_clients[0]);
  if (index >= HAL_TLS_MAX_CLIENTS || operation->client->callback_active) {
    void *deferred = NULL;
    (void)jh_network_handle_end_operation(&s_handle_pool, &operation->lease,
                                          &deferred);
    status = index >= HAL_TLS_MAX_CLIENTS ? HAL_EINVAL : HAL_EBUSY;
    pool_unlock();
    finalize_client(deferred);
    return status;
  }
  operation->mutex = s_client_mutexes[index];
  pool_unlock();

  hal_mutex_lock(operation->mutex);
  status = pool_lock();
  if (status != HAL_OK) {
    hal_mutex_unlock(operation->mutex);
    return status;
  }
  const bool open =
      jh_network_handle_lease_is_open(&s_handle_pool, &operation->lease);
  const bool callback_active = operation->client->callback_active;
  if (!open || callback_active) {
    void *deferred = NULL;
    (void)jh_network_handle_end_operation(&s_handle_pool, &operation->lease,
                                          &deferred);
    pool_unlock();
    hal_mutex_unlock(operation->mutex);
    finalize_client(deferred);
    return callback_active ? HAL_EBUSY : HAL_EINVAL;
  }
  pool_unlock();
  return HAL_OK;
}

static void end_operation(jh_tls_operation_t *operation) {
  hal_mutex_unlock(operation->mutex);
  void *deferred = NULL;
  if (pool_lock() != HAL_OK) {
    return;
  }
  (void)jh_network_handle_end_operation(&s_handle_pool, &operation->lease,
                                        &deferred);
  pool_unlock();
  finalize_client(deferred);
}

static void begin_callback(jh_tls_operation_t *operation) {
  if (pool_lock() != HAL_OK) {
    return;
  }
  operation->client->callback_active = true;
  pool_unlock();
  hal_mutex_unlock(operation->mutex);
}

static bool end_callback(jh_tls_operation_t *operation) {
  hal_mutex_lock(operation->mutex);
  if (pool_lock() != HAL_OK) {
    return false;
  }
  const bool open =
      jh_network_handle_lease_is_open(&s_handle_pool, &operation->lease);
  operation->client->callback_active = false;
  pool_unlock();
  return open;
}

static hal_status_t call_time(jh_tls_operation_t *operation,
                              uint64_t *out_unix_seconds) {
  hal_tls_time_fn callback = operation->client->security.get_time;
  void *context = operation->client->security.callback_context;
  begin_callback(operation);
  const hal_status_t status = callback(context, out_unix_seconds);
  return end_callback(operation) ? status : HAL_ECANCELED;
}

static hal_status_t call_entropy(jh_tls_operation_t *operation, void *buffer,
                                 size_t length) {
  hal_tls_entropy_fn callback = operation->client->security.get_entropy;
  void *context = operation->client->security.callback_context;
  begin_callback(operation);
  const hal_status_t status = callback(context, buffer, length);
  return end_callback(operation) ? status : HAL_ECANCELED;
}

static bool operation_timed_out(const jh_tls_client_context_t *client) {
  return hal_millis_deadline_expired(client->operation_started_ms,
                                     client->config.operation_timeout_ms);
}

static hal_status_t fail_client(jh_tls_client_context_t *client,
                                hal_status_t status, int32_t provider_error) {
  release_transport(client);
  client->state = HAL_TLS_STATE_FAILED;
  return record_error(client, status, provider_error);
}

static hal_status_t advance_engine(jh_tls_client_context_t *client,
                                   bool pending_read = false) {
  jh_bearssl_poll_result_t result = {};
  hal_status_t status =
      pending_read
          ? jh_bearssl_engine_poll_for_read(
                &client->provider->client.eng, &client->transport.transport,
                client->config.poll_step_budget, &result)
          : jh_bearssl_engine_poll(&client->provider->client.eng,
                                   &client->transport.transport,
                                   client->config.poll_step_budget, &result);
  if (status == HAL_EAGAIN) {
    return status;
  }
  if (status != HAL_OK || result.event == JH_BEARSSL_EVENT_FAILED) {
    const hal_status_t mapped =
        result.engine_error != 0 ? jh_bearssl_error_to_hal(result.engine_error)
                                 : status;
    return fail_client(client, mapped, result.engine_error);
  }
  if (result.event == JH_BEARSSL_EVENT_APPLICATION_READABLE ||
      result.event == JH_BEARSSL_EVENT_APPLICATION_WRITABLE) {
    if (client->state == HAL_TLS_STATE_CONNECTING) {
      if (client->has_server_key_pin) {
        const hal_status_t pin_status = jh_bearssl_verify_server_key_pin(
            client->provider, client->server_key_pin);
        if (pin_status != HAL_OK) {
          return fail_client(client, pin_status, 0);
        }
      }
      client->state = HAL_TLS_STATE_CONNECTED;
    }
    return HAL_OK;
  }
  if (result.event == JH_BEARSSL_EVENT_CLOSED) {
    release_transport(client);
    client->state = HAL_TLS_STATE_CLOSED;
    client->last_status = HAL_OK;
    client->provider_error = 0;
    return HAL_OK;
  }
  return HAL_EAGAIN;
}

static hal_status_t advance_operation(jh_tls_operation_t *operation,
                                      bool pending_read = false) {
  jh_tls_client_context_t *client = operation->client;
  if (client->cancelled) {
    return fail_client(client, HAL_ECANCELED, 0);
  }
  if (client->security.is_cancelled != NULL) {
    hal_tls_cancel_fn callback = client->security.is_cancelled;
    void *context = client->security.callback_context;
    begin_callback(operation);
    const bool cancelled = callback(context);
    if (!end_callback(operation)) {
      return HAL_ECANCELED;
    }
    if (cancelled) {
      return fail_client(client, HAL_ECANCELED, 0);
    }
  }
  /* operation_timeout_ms bounds handshakes and close_notify, not the lifetime
   * of an established TLS session. Applying the original connect deadline to
   * application reads/writes forced sustained MQTTS sessions to reconnect. */
  if (jh_tls_operation_timeout_applies(client->state) &&
      operation_timed_out(client)) {
    return fail_client(client, HAL_ETIMEOUT, 0);
  }
  if (client->security.service != NULL) {
    hal_tls_service_fn callback = client->security.service;
    void *context = client->security.callback_context;
    begin_callback(operation);
    callback(context);
    if (!end_callback(operation)) {
      return HAL_ECANCELED;
    }
  }
  return advance_engine(client, pending_read);
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

static hal_status_t open_transport(jh_tls_client_context_t *client) {
  hal_net_endpoint_t endpoints[JH_TLS_RESOLVE_MAX_RESULTS] = {};
  size_t endpoint_count = 0u;
  hal_status_t status =
      hal_net_resolve_ex(client->hostname, HAL_NET_AF_UNSPEC, endpoints,
                         JH_TLS_RESOLVE_MAX_RESULTS, &endpoint_count);
  if (status != HAL_OK) {
    return status;
  }
  if (endpoint_count == 0u) {
    return HAL_ENOENT;
  }

  hal_status_t last_status = HAL_EIO;
  for (size_t index = 0u; index < endpoint_count; ++index) {
    endpoints[index].port = client->port;
    hal_tcp_socket_t socket = nullptr;
    status = hal_tcp_socket_open_ex(&socket);
    if (status != HAL_OK) {
      return status;
    }
    status = hal_tcp_socket_connect_ex(socket, &endpoints[index],
                                       client->config.transport_timeout_ms);
    if (status == HAL_OK) {
      client->socket = socket;
      status =
          jh_bearssl_hal_tcp_transport_init(&client->transport, client->socket);
      if (status == HAL_OK) {
        return HAL_OK;
      }
      hal_tcp_socket_close(client->socket);
      client->socket = nullptr;
      return status;
    }
    last_status = status;
    hal_tcp_socket_close(socket);
  }
  return last_status;
}

typedef struct {
  hal_tls_trust_anchor_storage_t *storage;
  size_t length;
  bool overflow;
} jh_tls_dn_collector_t;

static void collect_subject_dn(void *context, const void *data, size_t length) {
  jh_tls_dn_collector_t *collector =
      static_cast<jh_tls_dn_collector_t *>(context);
  if (length > sizeof(collector->storage->subject_dn) - collector->length) {
    collector->overflow = true;
    return;
  }
  memcpy(collector->storage->subject_dn + collector->length, data, length);
  collector->length += length;
}

hal_status_t
hal_tls_trust_anchor_from_der_ex(const void *certificate_der,
                                 size_t certificate_der_length,
                                 hal_tls_trust_anchor_storage_t *out_storage) {
  if (certificate_der == NULL || certificate_der_length == 0u ||
      out_storage == NULL) {
    return HAL_EINVAL;
  }
  memset(out_storage, 0, sizeof(*out_storage));
  jh_tls_dn_collector_t collector = {out_storage, 0u, false};
  br_x509_decoder_context decoder = {};
  br_x509_decoder_init(&decoder, collect_subject_dn, &collector, NULL, NULL);
  br_x509_decoder_push(&decoder, certificate_der, certificate_der_length);
  br_x509_pkey *key = br_x509_decoder_get_pkey(&decoder);
  if (collector.overflow) {
    return HAL_EOVERFLOW;
  }
  if (key == NULL || collector.length == 0u ||
      br_x509_decoder_last_error(&decoder) != 0 ||
      !br_x509_decoder_isCA(&decoder)) {
    return HAL_EAUTH;
  }
  out_storage->anchor.subject_dn = out_storage->subject_dn;
  out_storage->anchor.subject_dn_length = collector.length;
  if (key->key_type == BR_KEYTYPE_RSA) {
    if (key->key.rsa.nlen > sizeof(out_storage->key) ||
        key->key.rsa.elen > sizeof(out_storage->rsa_exponent)) {
      return HAL_EOVERFLOW;
    }
    memcpy(out_storage->key, key->key.rsa.n, key->key.rsa.nlen);
    memcpy(out_storage->rsa_exponent, key->key.rsa.e, key->key.rsa.elen);
    out_storage->anchor.key_type = HAL_TLS_TRUST_KEY_RSA;
    out_storage->anchor.key.rsa.modulus = out_storage->key;
    out_storage->anchor.key.rsa.modulus_length = key->key.rsa.nlen;
    out_storage->anchor.key.rsa.exponent = out_storage->rsa_exponent;
    out_storage->anchor.key.rsa.exponent_length = key->key.rsa.elen;
  } else if (key->key_type == BR_KEYTYPE_EC) {
    if (key->key.ec.qlen > sizeof(out_storage->key)) {
      return HAL_EOVERFLOW;
    }
    memcpy(out_storage->key, key->key.ec.q, key->key.ec.qlen);
    out_storage->anchor.key_type = HAL_TLS_TRUST_KEY_EC;
    out_storage->anchor.key.ec.curve = key->key.ec.curve;
    out_storage->anchor.key.ec.point = out_storage->key;
    out_storage->anchor.key.ec.point_length = key->key.ec.qlen;
  } else {
    return HAL_EAUTH;
  }
  return HAL_OK;
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

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
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
    pool_unlock();
    return HAL_ENOMEM;
  }

  void *handle = NULL;
  status = jh_network_handle_allocate(&s_handle_pool, client, &handle);
  if (status != HAL_OK) {
    memset(client, 0, sizeof(*client));
    pool_unlock();
    return status;
  }
  *out_client = static_cast<hal_tls_client_t>(handle);
  pool_unlock();
  return HAL_OK;
}

hal_status_t hal_tls_client_configure_security_ex(
    hal_tls_client_t handle, const hal_tls_security_config_t *security) {
  if (security == NULL || security->trust_anchors == NULL ||
      security->trust_anchor_count == 0u ||
      security->trust_anchor_count > HAL_TLS_MAX_TRUST_ANCHORS ||
      security->get_time == NULL || security->get_entropy == NULL) {
    return HAL_ECONFIG;
  }
  jh_tls_operation_t operation = {};
  hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
  if (status == HAL_OK && client->state != HAL_TLS_STATE_CREATED &&
      client->state != HAL_TLS_STATE_CONFIGURED) {
    status = HAL_ESTATE;
  }
  if (status == HAL_OK) {
    client->security = *security;
    client->has_server_key_pin = security->server_public_key_sha256 != NULL;
    if (client->has_server_key_pin) {
      memcpy(client->server_key_pin, security->server_public_key_sha256,
             sizeof(client->server_key_pin));
    } else {
      memset(client->server_key_pin, 0, sizeof(client->server_key_pin));
    }
    client->security_configured = true;
    client->last_status = HAL_OK;
    client->provider_error = 0;
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
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

  jh_tls_operation_t operation = {};
  hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
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
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

hal_status_t hal_tls_client_connect_ex(hal_tls_client_t handle) {
  jh_tls_operation_t operation = {};
  hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
  if (status == HAL_OK && client->state != HAL_TLS_STATE_CONFIGURED) {
    status = HAL_ESTATE;
  }
  if (status == HAL_OK) {
    if (!client->security_configured) {
      status = record_error(client, HAL_ECONFIG, 0);
    } else {
      uint64_t unix_seconds = 0u;
      unsigned char entropy[JH_BEARSSL_ENTROPY_SIZE] = {};
      status = call_time(&operation, &unix_seconds);
      if (status == HAL_OK && unix_seconds < HAL_TLS_MIN_VALID_UNIX_TIME) {
        status = HAL_ECONFIG;
      }
      if (status == HAL_OK) {
        status = call_entropy(&operation, entropy, sizeof(entropy));
      }
      if (status == HAL_OK) {
        status = open_transport(client);
      }
      if (status == HAL_OK) {
        client->provider = jh_bearssl_client_allocate();
        if (client->provider == NULL) {
          status = HAL_ENOMEM;
        }
      }
      if (status == HAL_OK) {
        status = jh_bearssl_client_init(
            client->provider, client->security.trust_anchors,
            client->security.trust_anchor_count, client->hostname, unix_seconds,
            entropy, sizeof(entropy));
      }
      memset(entropy, 0, sizeof(entropy));
      if (status == HAL_OK) {
        client->cancelled = false;
        client->state = HAL_TLS_STATE_CONNECTING;
        client->operation_started_ms = hal_millis();
        status = advance_operation(&operation);
      } else {
        status = fail_client(client, status, 0);
      }
    }
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

hal_status_t hal_tls_client_poll_ex(hal_tls_client_t handle) {
  jh_tls_operation_t operation = {};
  hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
  if (status == HAL_OK) {
    status = client->state == HAL_TLS_STATE_CONNECTING ||
                     client->state == HAL_TLS_STATE_CLOSING
                 ? advance_operation(&operation)
                 : HAL_ESTATE;
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

hal_status_t hal_tls_client_read_ex(hal_tls_client_t handle, void *buffer,
                                    size_t capacity, size_t *out_received) {
  if (out_received == NULL || (capacity > 0u && buffer == NULL)) {
    return HAL_EINVAL;
  }
  *out_received = 0u;
  jh_tls_operation_t operation = {};
  hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
  if (status == HAL_OK) {
    if (client->state != HAL_TLS_STATE_CONNECTED) {
      status = HAL_ESTATE;
    } else if (capacity == 0u) {
      status = HAL_OK;
    } else {
      size_t available = 0u;
      unsigned char *source =
          br_ssl_engine_recvapp_buf(&client->provider->client.eng, &available);
      if (source == NULL || available == 0u) {
        status = advance_operation(&operation, true);
        if (status == HAL_OK) {
          status = HAL_EAGAIN;
        }
      } else {
        *out_received = std::min(capacity, available);
        memcpy(buffer, source, *out_received);
        br_ssl_engine_recvapp_ack(&client->provider->client.eng, *out_received);
        status = HAL_OK;
      }
    }
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

hal_status_t hal_tls_client_write_ex(hal_tls_client_t handle, const void *data,
                                     size_t length, size_t *out_written) {
  if (out_written == NULL || (length > 0u && data == NULL)) {
    return HAL_EINVAL;
  }
  *out_written = 0u;
  jh_tls_operation_t operation = {};
  hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
  if (status == HAL_OK) {
    if (client->state != HAL_TLS_STATE_CONNECTED) {
      status = HAL_ESTATE;
    } else if (length == 0u) {
      status = HAL_OK;
    } else {
      size_t available = 0u;
      unsigned char *destination =
          br_ssl_engine_sendapp_buf(&client->provider->client.eng, &available);
      if (destination == NULL || available == 0u) {
        status = advance_operation(&operation);
        if (status == HAL_OK) {
          status = HAL_EAGAIN;
        }
      } else {
        *out_written = std::min(length, available);
        memcpy(destination, data, *out_written);
        br_ssl_engine_sendapp_ack(&client->provider->client.eng, *out_written);
        br_ssl_engine_flush(&client->provider->client.eng, 0);
        status = HAL_OK;
      }
    }
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

hal_status_t hal_tls_client_shutdown_ex(hal_tls_client_t handle) {
  jh_tls_operation_t operation = {};
  hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
  if (status == HAL_OK) {
    if (client->state == HAL_TLS_STATE_CREATED ||
        client->state == HAL_TLS_STATE_CONFIGURED ||
        client->state == HAL_TLS_STATE_FAILED) {
      client->state = HAL_TLS_STATE_CLOSED;
      client->last_status = HAL_OK;
    } else if (client->state == HAL_TLS_STATE_CLOSED) {
      status = HAL_OK;
    } else if (client->state == HAL_TLS_STATE_CONNECTED) {
      br_ssl_engine_close(&client->provider->client.eng);
      client->state = HAL_TLS_STATE_CLOSING;
      client->operation_started_ms = hal_millis();
      status = HAL_EAGAIN;
    } else {
      status = HAL_ESTATE;
    }
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

hal_status_t hal_tls_client_cancel_ex(hal_tls_client_t handle) {
  jh_tls_operation_t operation = {};
  hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
  if (status == HAL_OK) {
    client->cancelled = true;
    if (client->state == HAL_TLS_STATE_CONNECTING ||
        client->state == HAL_TLS_STATE_CONNECTED ||
        client->state == HAL_TLS_STATE_CLOSING) {
      status = fail_client(client, HAL_ECANCELED, 0);
    }
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

hal_status_t hal_tls_client_get_state_ex(hal_tls_client_t handle,
                                         hal_tls_state_t *out_state) {
  if (out_state == NULL) {
    return HAL_EINVAL;
  }
  jh_tls_operation_t operation = {};
  const hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
  if (status == HAL_OK) {
    *out_state = client->state;
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

hal_status_t hal_tls_client_get_last_error_ex(hal_tls_client_t handle,
                                              hal_status_t *out_status,
                                              int32_t *out_provider_error) {
  if (out_status == NULL) {
    return HAL_EINVAL;
  }
  jh_tls_operation_t operation = {};
  const hal_status_t status = begin_operation(handle, &operation);
  jh_tls_client_context_t *client = operation.client;
  if (status == HAL_OK) {
    *out_status = client->last_status;
    if (out_provider_error != NULL) {
      *out_provider_error = client->provider_error;
    }
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

hal_status_t hal_tls_client_close_ex(hal_tls_client_t handle) {
  const hal_status_t pool_status = pool_lock();
  if (pool_status != HAL_OK) {
    return pool_status;
  }
  void *released = NULL;
  const hal_status_t status =
      jh_network_handle_begin_close(&s_handle_pool, handle, &released);
  pool_unlock();
  finalize_client(released);
  return status;
}

#if HAL_TARGET_IS_MOCK
hal_status_t hal_mock_tls_mark_connected_for_zero_io(hal_tls_client_t handle) {
  jh_tls_operation_t operation = {};
  hal_status_t status = begin_operation(handle, &operation);
  if (status == HAL_OK) {
    operation.client->state = HAL_TLS_STATE_CONNECTED;
  }
  if (operation.lease.active) {
    end_operation(&operation);
  }
  return status;
}

/* Test-only: force the pool mutex and every client mutex through a real
 * destroy so Helgrind/DRD can observe the teardown path, then mark the
 * pool uninitialized so the next client operation recreates them from
 * scratch. Firmware never calls this. Call only when no other thread is
 * using the TLS pool. */
void hal_mock_tls_full_reset(void) {
  for (size_t index = 0u; index < HAL_TLS_MAX_CLIENTS; ++index) {
    if (s_client_mutexes[index] != NULL) {
      hal_mutex_destroy(s_client_mutexes[index]);
    }
  }
  std::fill_n(s_client_mutexes, HAL_TLS_MAX_CLIENTS, nullptr);
  memset(s_clients, 0, sizeof(s_clients));
  if (s_pool_mutex != NULL) {
    hal_mutex_destroy(s_pool_mutex);
    s_pool_mutex = NULL;
  }
  s_pool_initialized = false;
}
#endif

#endif
