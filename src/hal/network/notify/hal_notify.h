#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_NOTIFY

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef HAL_ENABLE_NOTIFY_TELEGRAM
#include "hal/network/tls/hal_tls.h"
#endif

#ifndef HAL_NOTIFY_MAX_CHANNELS
#define HAL_NOTIFY_MAX_CHANNELS 2u
#endif

#ifndef HAL_NOTIFY_BACKEND_STATE_SIZE
#define HAL_NOTIFY_BACKEND_STATE_SIZE 192u
#endif

#ifndef HAL_NOTIFY_DEFAULT_TIMEOUT_MS
#define HAL_NOTIFY_DEFAULT_TIMEOUT_MS 15000u
#endif

#ifndef HAL_NOTIFY_RECEIPT_ID_MAX
#define HAL_NOTIFY_RECEIPT_ID_MAX 32u
#endif

#if HAL_NOTIFY_MAX_CHANNELS < 1u || HAL_NOTIFY_MAX_CHANNELS > 255u
#error "HAL_NOTIFY_MAX_CHANNELS must be in range 1..255"
#endif

#if HAL_NOTIFY_BACKEND_STATE_SIZE < 1u
#error "HAL_NOTIFY_BACKEND_STATE_SIZE must be at least 1"
#endif

#if HAL_NOTIFY_DEFAULT_TIMEOUT_MS < 1u
#error "HAL_NOTIFY_DEFAULT_TIMEOUT_MS must be at least 1"
#endif

#if HAL_NOTIFY_RECEIPT_ID_MAX < 1u
#error "HAL_NOTIFY_RECEIPT_ID_MAX must be at least 1"
#endif

#define HAL_NOTIFY_BACKEND_API_VERSION 1u

#define HAL_NOTIFY_MESSAGE_SILENT (1u << 0u)
#define HAL_NOTIFY_MESSAGE_SUPPRESS_LINK_PREVIEW (1u << 1u)

#define HAL_NOTIFY_RECEIPT_RESPONSE_TRUNCATED (1u << 0u)
#define HAL_NOTIFY_RECEIPT_PARTIAL_DELIVERY (1u << 1u)

#ifdef HAL_ENABLE_NOTIFY_TELEGRAM
#ifndef HAL_NOTIFY_TELEGRAM_PATH_MAX
#define HAL_NOTIFY_TELEGRAM_PATH_MAX 160u
#endif

#ifndef HAL_NOTIFY_TELEGRAM_TEXT_MAX
#define HAL_NOTIFY_TELEGRAM_TEXT_MAX 3500u
#endif

#ifndef HAL_NOTIFY_TELEGRAM_RESPONSE_BUFFER_SIZE
#define HAL_NOTIFY_TELEGRAM_RESPONSE_BUFFER_SIZE 1024u
#endif

#if HAL_NOTIFY_TELEGRAM_PATH_MAX < 32u
#error "HAL_NOTIFY_TELEGRAM_PATH_MAX must be at least 32"
#endif

#if HAL_NOTIFY_TELEGRAM_TEXT_MAX < 64u
#error "HAL_NOTIFY_TELEGRAM_TEXT_MAX must be at least 64"
#endif

#if HAL_NOTIFY_TELEGRAM_RESPONSE_BUFFER_SIZE < 32u
#error "HAL_NOTIFY_TELEGRAM_RESPONSE_BUFFER_SIZE must be at least 32"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_notify_channel_impl_t *hal_notify_channel_t;

typedef enum {
  HAL_NOTIFY_SEVERITY_INFO = 0,
  HAL_NOTIFY_SEVERITY_WARNING,
  HAL_NOTIFY_SEVERITY_ERROR,
  HAL_NOTIFY_SEVERITY_CRITICAL
} hal_notify_severity_t;

typedef enum {
  HAL_NOTIFY_FORMAT_DEFAULT = 0,
  HAL_NOTIFY_FORMAT_TEXT,
  HAL_NOTIFY_FORMAT_MARKDOWN,
  HAL_NOTIFY_FORMAT_HTML
} hal_notify_format_t;

typedef enum {
  HAL_NOTIFY_TRANSPORT_HTTPS = 0,
  HAL_NOTIFY_TRANSPORT_HTTP
} hal_notify_transport_t;

typedef struct {
  const char *destination;
  const char *device_name;
  const char *title;
  const char *body;
  hal_notify_severity_t severity;
  hal_notify_format_t format;
  uint32_t timeout_ms;
  uint32_t flags;
} hal_notify_message_t;

typedef struct {
  int32_t provider_status;
  int32_t provider_error;
  uint32_t retry_after_s;
  uint32_t flags;
  uint32_t parts_sent;
  uint32_t parts_total;
  char provider_message_id[HAL_NOTIFY_RECEIPT_ID_MAX];
} hal_notify_receipt_t;

typedef struct hal_notify_backend_t {
  uint32_t api_version;
  const char *name;
  size_t state_size;
  hal_status_t (*open)(void *state, const void *config);
  hal_status_t (*send)(void *state, const hal_notify_message_t *message,
                       hal_notify_receipt_t *receipt);
  hal_status_t (*poll)(void *state);
  hal_status_t (*close)(void *state);
} hal_notify_backend_t;

typedef struct {
  const hal_notify_backend_t *backend;
  const void *backend_config;
  const char *device_name;
  uint32_t default_timeout_ms;
  hal_notify_format_t default_format;
} hal_notify_config_t;

hal_status_t hal_notify_config_init(hal_notify_config_t *config);
hal_status_t hal_notify_message_init(hal_notify_message_t *message);
hal_status_t hal_notify_open(const hal_notify_config_t *config,
                             hal_notify_channel_t *out_channel);
hal_status_t hal_notify_send(hal_notify_channel_t channel,
                             const hal_notify_message_t *message,
                             hal_notify_receipt_t *receipt);
hal_status_t hal_notify_send_text(hal_notify_channel_t channel,
                                  const char *text);
hal_status_t hal_notify_poll(hal_notify_channel_t channel);
hal_status_t hal_notify_close(hal_notify_channel_t channel);

#ifdef HAL_ENABLE_NOTIFY_TELEGRAM
typedef struct {
  const char *bot_token;
  const char *default_chat_id;
  const char *api_host;
  uint16_t port;
  hal_notify_transport_t transport;
  const hal_tls_security_config_t *tls_security;
  bool disable_notification;
  bool disable_web_page_preview;
} hal_notify_telegram_config_t;

hal_status_t
hal_notify_telegram_config_init(hal_notify_telegram_config_t *config);

const hal_notify_backend_t *hal_notify_telegram_backend(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_NOTIFY */
