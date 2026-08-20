#include "hal/network/notify/hal_notify.h"

#ifdef HAL_ENABLE_NOTIFY

#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"

#include <cstddef>
#include <string.h>

#define JH_NOTIFY_HANDLE_KIND 10u

typedef struct {
  const hal_notify_backend_t *backend;
  const char *default_device_name;
  uint32_t default_timeout_ms;
  hal_notify_format_t default_format;
  hal_mutex_t mutex;
  alignas(std::max_align_t) uint8_t state[HAL_NOTIFY_BACKEND_STATE_SIZE];
  bool allocated;
  bool backend_open;
} jh_notify_channel_context_t;

static jh_notify_channel_context_t s_contexts[HAL_NOTIFY_MAX_CHANNELS] = {};
static jh_handle_slot_t s_handle_slots[HAL_NOTIFY_MAX_CHANNELS] = {};
static jh_handle_pool_t s_handle_pool = {};
static hal_mutex_t s_pool_mutex = NULL;
static bool s_pool_initialized = false;

static bool format_valid(hal_notify_format_t format, bool allow_default) {
  return format == HAL_NOTIFY_FORMAT_TEXT ||
         format == HAL_NOTIFY_FORMAT_MARKDOWN ||
         format == HAL_NOTIFY_FORMAT_HTML ||
         (allow_default && format == HAL_NOTIFY_FORMAT_DEFAULT);
}

static bool severity_valid(hal_notify_severity_t severity) {
  return severity == HAL_NOTIFY_SEVERITY_INFO ||
         severity == HAL_NOTIFY_SEVERITY_WARNING ||
         severity == HAL_NOTIFY_SEVERITY_ERROR ||
         severity == HAL_NOTIFY_SEVERITY_CRITICAL;
}

static bool optional_single_line(const char *value) {
  return value == NULL ||
         (strchr(value, '\r') == NULL && strchr(value, '\n') == NULL);
}

static hal_notify_format_t resolve_format(hal_notify_format_t requested,
                                          hal_notify_format_t fallback) {
  if (requested != HAL_NOTIFY_FORMAT_DEFAULT) {
    return requested;
  }
  return fallback == HAL_NOTIFY_FORMAT_DEFAULT ? HAL_NOTIFY_FORMAT_TEXT
                                               : fallback;
}

static bool backend_valid(const hal_notify_backend_t *backend) {
  return backend != NULL &&
         backend->api_version == HAL_NOTIFY_BACKEND_API_VERSION &&
         backend->name != NULL && backend->send != NULL &&
         backend->open != NULL && backend->close != NULL &&
         backend->state_size <= HAL_NOTIFY_BACKEND_STATE_SIZE;
}

static hal_status_t pool_lock(void) {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_pool_mutex);
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_pool_initialized) {
    hal_status_t status =
        jh_handle_pool_init(&s_handle_pool, s_handle_slots,
                            HAL_NOTIFY_MAX_CHANNELS, JH_NOTIFY_HANDLE_KIND);
    if (status != HAL_OK) {
      hal_mutex_unlock(mutex);
      return status;
    }
    for (size_t index = 0u; index < HAL_NOTIFY_MAX_CHANNELS; ++index) {
      if (jh_hal_mutex_create_once(&s_contexts[index].mutex) == NULL) {
        hal_mutex_unlock(mutex);
        return HAL_ENOMEM;
      }
    }
    s_pool_initialized = true;
  }
  return HAL_OK;
}

static void pool_unlock(void) { hal_mutex_unlock(s_pool_mutex); }

static void context_clear(jh_notify_channel_context_t *context) {
  hal_mutex_t mutex = context->mutex;
  memset(context->state, 0, sizeof(context->state));
  context->backend = NULL;
  context->default_device_name = NULL;
  context->default_timeout_ms = HAL_NOTIFY_DEFAULT_TIMEOUT_MS;
  context->default_format = HAL_NOTIFY_FORMAT_TEXT;
  context->mutex = mutex;
  context->allocated = false;
  context->backend_open = false;
}

static hal_status_t context_release(jh_notify_channel_context_t *context) {
  if (context == NULL) {
    return HAL_EINVAL;
  }
  hal_status_t status = HAL_OK;
  if (context->backend_open && context->backend != NULL &&
      context->backend->close != NULL) {
    status = context->backend->close(context->state);
  }

  const hal_status_t lock_status = pool_lock();
  if (lock_status != HAL_OK) {
    return status == HAL_OK ? lock_status : status;
  }
  context_clear(context);
  pool_unlock();
  return status;
}

static hal_status_t reserve_context(const hal_notify_config_t *config,
                                    jh_notify_channel_context_t **out_context) {
  *out_context = NULL;
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  for (size_t index = 0u; index < HAL_NOTIFY_MAX_CHANNELS; ++index) {
    jh_notify_channel_context_t *context = &s_contexts[index];
    if (!context->allocated) {
      context_clear(context);
      context->allocated = true;
      context->backend = config->backend;
      context->default_device_name = config->device_name;
      context->default_timeout_ms = config->default_timeout_ms;
      context->default_format =
          resolve_format(config->default_format, HAL_NOTIFY_FORMAT_TEXT);
      *out_context = context;
      pool_unlock();
      return HAL_OK;
    }
  }
  pool_unlock();
  return HAL_ENOMEM;
}

static hal_status_t acquire_channel(hal_notify_channel_t channel,
                                    jh_handle_lease_t *out_lease) {
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  status = jh_handle_acquire(
      &s_handle_pool, reinterpret_cast<const void *>(channel), out_lease);
  pool_unlock();
  return status;
}

static hal_status_t finish_channel_operation(jh_handle_lease_t *lease) {
  void *deferred_token = NULL;
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  status = jh_handle_end_operation(&s_handle_pool, lease, &deferred_token);
  pool_unlock();
  if (status != HAL_OK) {
    return status;
  }
  if (deferred_token != NULL) {
    return context_release(
        static_cast<jh_notify_channel_context_t *>(deferred_token));
  }
  return HAL_OK;
}

static hal_status_t validate_message(const hal_notify_message_t *message) {
  if (message == NULL || message->body == NULL || message->body[0] == '\0' ||
      !severity_valid(message->severity) ||
      !format_valid(message->format, true) ||
      !optional_single_line(message->device_name)) {
    return HAL_EINVAL;
  }
  return HAL_OK;
}

hal_status_t hal_notify_config_init(hal_notify_config_t *config) {
  if (config == NULL) {
    return HAL_EINVAL;
  }
  memset(config, 0, sizeof(*config));
  config->default_timeout_ms = HAL_NOTIFY_DEFAULT_TIMEOUT_MS;
  config->default_format = HAL_NOTIFY_FORMAT_TEXT;
  return HAL_OK;
}

hal_status_t hal_notify_message_init(hal_notify_message_t *message) {
  if (message == NULL) {
    return HAL_EINVAL;
  }
  memset(message, 0, sizeof(*message));
  message->severity = HAL_NOTIFY_SEVERITY_INFO;
  message->format = HAL_NOTIFY_FORMAT_DEFAULT;
  return HAL_OK;
}

hal_status_t hal_notify_open(const hal_notify_config_t *config,
                             hal_notify_channel_t *out_channel) {
  if (out_channel != NULL) {
    *out_channel = NULL;
  }
  if (config == NULL || out_channel == NULL ||
      !backend_valid(config->backend) || config->default_timeout_ms == 0u ||
      !format_valid(config->default_format, true) ||
      !optional_single_line(config->device_name)) {
    hal_derr("hal_notify_open: invalid configuration");
    return HAL_EINVAL;
  }

  jh_notify_channel_context_t *context = NULL;
  hal_status_t status = reserve_context(config, &context);
  if (status != HAL_OK) {
    return status;
  }

  status = context->backend->open(context->state, config->backend_config);
  if (status != HAL_OK) {
    (void)context_release(context);
    return status;
  }
  context->backend_open = true;

  void *handle = NULL;
  status = pool_lock();
  if (status == HAL_OK) {
    status = jh_handle_allocate(&s_handle_pool, context, &handle);
    pool_unlock();
  }
  if (status != HAL_OK) {
    (void)context_release(context);
    return status;
  }

  *out_channel = reinterpret_cast<hal_notify_channel_t>(handle);
  return HAL_OK;
}

hal_status_t hal_notify_send(hal_notify_channel_t channel,
                             const hal_notify_message_t *message,
                             hal_notify_receipt_t *receipt) {
  if (receipt != NULL) {
    memset(receipt, 0, sizeof(*receipt));
  }
  hal_status_t status = validate_message(message);
  if (status != HAL_OK) {
    return status;
  }

  jh_handle_lease_t lease = {};
  status = acquire_channel(channel, &lease);
  if (status != HAL_OK) {
    return HAL_EINVAL;
  }

  jh_notify_channel_context_t *context =
      static_cast<jh_notify_channel_context_t *>(lease.token);
  hal_notify_message_t effective = *message;
  if (effective.device_name == NULL || effective.device_name[0] == '\0') {
    effective.device_name = context->default_device_name;
  }
  effective.timeout_ms = effective.timeout_ms == 0u
                             ? context->default_timeout_ms
                             : effective.timeout_ms;
  effective.format = resolve_format(effective.format, context->default_format);

  hal_mutex_lock(context->mutex);
  status = context->backend->send(context->state, &effective, receipt);
  hal_mutex_unlock(context->mutex);
  const hal_status_t finish_status = finish_channel_operation(&lease);
  return status == HAL_OK ? finish_status : status;
}

hal_status_t hal_notify_send_text(hal_notify_channel_t channel,
                                  const char *text) {
  hal_notify_message_t message = {};
  hal_status_t status = hal_notify_message_init(&message);
  if (status != HAL_OK) {
    return status;
  }
  message.body = text;
  return hal_notify_send(channel, &message, NULL);
}

hal_status_t hal_notify_poll(hal_notify_channel_t channel) {
  jh_handle_lease_t lease = {};
  hal_status_t status = acquire_channel(channel, &lease);
  if (status != HAL_OK) {
    return HAL_EINVAL;
  }

  jh_notify_channel_context_t *context =
      static_cast<jh_notify_channel_context_t *>(lease.token);
  hal_mutex_lock(context->mutex);
  status = context->backend->poll != NULL
               ? context->backend->poll(context->state)
               : HAL_OK;
  hal_mutex_unlock(context->mutex);
  const hal_status_t finish_status = finish_channel_operation(&lease);
  return status == HAL_OK ? finish_status : status;
}

hal_status_t hal_notify_close(hal_notify_channel_t channel) {
  void *token = NULL;
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  status = jh_handle_begin_close(
      &s_handle_pool, reinterpret_cast<const void *>(channel), &token);
  pool_unlock();
  if (status != HAL_OK) {
    return HAL_EINVAL;
  }
  if (token != NULL) {
    return context_release(static_cast<jh_notify_channel_context_t *>(token));
  }
  return HAL_OK;
}

#endif /* HAL_ENABLE_NOTIFY */
