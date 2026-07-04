#pragma once

#include "hal_config.h"
#include "hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef HAL_ENABLE_NET_CONSOLE
#include "hal_net.h"

#ifndef HAL_NET_CONSOLE_MAX_CLIENTS
#define HAL_NET_CONSOLE_MAX_CLIENTS 2u
#endif

#ifndef HAL_NET_CONSOLE_RX_BUFFER_SIZE
#define HAL_NET_CONSOLE_RX_BUFFER_SIZE 256u
#endif

#ifndef HAL_NET_CONSOLE_TX_BUFFER_SIZE
#define HAL_NET_CONSOLE_TX_BUFFER_SIZE 1024u
#endif

#ifndef HAL_NET_CONSOLE_LINE_BUFFER_SIZE
#define HAL_NET_CONSOLE_LINE_BUFFER_SIZE 128u
#endif

#ifndef HAL_NET_CONSOLE_PASSWORD_MAX
#define HAL_NET_CONSOLE_PASSWORD_MAX 64u
#endif

#ifndef HAL_NET_CONSOLE_DEFAULT_BACKLOG
#define HAL_NET_CONSOLE_DEFAULT_BACKLOG 2u
#endif

#if HAL_NET_CONSOLE_MAX_CLIENTS < 1
#error "HAL_NET_CONSOLE_MAX_CLIENTS must be at least 1"
#endif

#if HAL_NET_CONSOLE_RX_BUFFER_SIZE < 2
#error "HAL_NET_CONSOLE_RX_BUFFER_SIZE must be at least 2"
#endif

#if HAL_NET_CONSOLE_TX_BUFFER_SIZE < 2
#error "HAL_NET_CONSOLE_TX_BUFFER_SIZE must be at least 2"
#endif

#if HAL_NET_CONSOLE_LINE_BUFFER_SIZE < 2
#error "HAL_NET_CONSOLE_LINE_BUFFER_SIZE must be at least 2"
#endif

#if HAL_NET_CONSOLE_PASSWORD_MAX < 2
#error "HAL_NET_CONSOLE_PASSWORD_MAX must be at least 2"
#endif

#define HAL_NET_CONSOLE_DEFAULT_PORT 2323u

typedef uint8_t hal_net_console_client_t;

#define HAL_NET_CONSOLE_INVALID_CLIENT ((hal_net_console_client_t)0xffu)

typedef enum {
  HAL_NET_CONSOLE_EVENT_CONNECT = 0,
  HAL_NET_CONSOLE_EVENT_AUTHENTICATED,
  HAL_NET_CONSOLE_EVENT_DISCONNECT
} hal_net_console_event_t;

typedef void (*hal_net_console_event_cb_t)(hal_net_console_client_t client,
                                           hal_net_console_event_t event,
                                           void *user);

typedef hal_status_t (*hal_net_console_line_cb_t)(
    hal_net_console_client_t client, const char *line, void *user);

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t hal_net_console_set_callbacks(hal_net_console_event_cb_t event_cb,
                                           hal_net_console_line_cb_t line_cb,
                                           void *user);

hal_status_t hal_net_console_start(uint16_t port, const char *password);

void hal_net_console_stop(void);

bool hal_net_console_is_running(void);

void hal_net_console_poll(void);

size_t hal_net_console_client_count(void);

size_t hal_net_console_authenticated_count(void);

bool hal_net_console_client_is_authenticated(hal_net_console_client_t client);

hal_status_t hal_net_console_write(const void *data, size_t len);

hal_status_t hal_net_console_write_text(const char *text);

hal_status_t hal_net_console_write_to(hal_net_console_client_t client,
                                      const void *data, size_t len);

hal_status_t hal_net_console_write_text_to(hal_net_console_client_t client,
                                           const char *text);

int hal_net_console_available(void);

int hal_net_console_read(void *buffer, size_t max_len);

void hal_net_console_close(hal_net_console_client_t client);

void hal_net_console_write_from_serial(const char *data, size_t len);

#ifdef __cplusplus
}
#endif

#else
static inline void hal_net_console_write_from_serial(const char *data,
                                                     size_t len) {
  (void)data;
  (void)len;
}
#endif
