#include "jh_pubsub_hal_client.h"

#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474) &&                       \
    defined(HAL_ENABLE_MQTT)

#include "../../../../hal_net.h"
#include "../../../../hal_serial.h"
#include "../../../../hal_system.h"

#include <limits.h>
#include <string.h>

#define JH_PUBSUB_HAL_DEFAULT_TIMEOUT_MS 15000UL

JHPubSubHalClient::JHPubSubHalClient()
    : socket_(NULL), timeout_ms_(JH_PUBSUB_HAL_DEFAULT_TIMEOUT_MS),
      has_peeked_byte_(false), peeked_byte_(0u)
#ifdef HAL_ENABLE_TLS
      ,
      tls_enabled_(false), tls_security_(), tls_client_(NULL)
#endif
{
}

JHPubSubHalClient::~JHPubSubHalClient() { stop(); }

uint32_t JHPubSubHalClient::timeout_ms() { return timeout_ms_; }

void JHPubSubHalClient::set_timeout_ms(uint32_t timeout_ms) {
  timeout_ms_ = timeout_ms;
}

int JHPubSubHalClient::connect_endpoint(const hal_net_endpoint_t &endpoint) {
  stop();

  hal_tcp_socket_t socket = NULL;
  hal_status_t status = hal_tcp_socket_open_ex(&socket);
  if (status != HAL_OK) {
    hal_derr("JHPubSubHalClient: socket open failed: %s",
             hal_status_to_string(status));
    return 0;
  }

  status = hal_tcp_socket_connect_ex(socket, &endpoint, timeout_ms());
  if (status != HAL_OK) {
    hal_derr("JHPubSubHalClient: socket connect failed: %s",
             hal_status_to_string(status));
    hal_tcp_socket_close(socket);
    return 0;
  }

  socket_ = socket;
  return 1;
}

int JHPubSubHalClient::connect(const uint8_t ipv4[4], uint16_t port) {
  if (ipv4 == NULL || port == 0u) {
    return 0;
  }

  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  memcpy(endpoint.addr, ipv4, HAL_NET_IPV4_ADDR_LEN);
  endpoint.port = port;
  return connect_endpoint(endpoint);
}

int JHPubSubHalClient::connect(const char *host, uint16_t port) {
  if (host == NULL || host[0] == '\0' || port == 0u) {
    return 0;
  }

#ifdef HAL_ENABLE_TLS
  if (tls_enabled_) {
    return connect_tls(host, port);
  }
#endif

  stop();
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.port = port;
  const hal_status_t resolve_status =
      hal_net_resolve_ipv4_ex(host, endpoint.addr);
  if (resolve_status != HAL_OK) {
    hal_derr("JHPubSubHalClient: resolve failed: %s",
             hal_status_to_string(resolve_status));
    return 0;
  }
  return connect_endpoint(endpoint);
}

size_t JHPubSubHalClient::write(uint8_t data) { return write(&data, 1u); }

size_t JHPubSubHalClient::write(const uint8_t *buffer, size_t size) {
  if (size == 0u) {
    return 0u;
  }
  if (buffer == NULL ||
#ifdef HAL_ENABLE_TLS
      (tls_enabled_ ? tls_client_ == NULL : socket_ == NULL)
#else
      socket_ == NULL
#endif
  ) {
    return 0u;
  }

  const uint32_t started_ms = hal_millis();
  const uint32_t write_timeout_ms = timeout_ms();
  size_t total_sent = 0u;

  while (total_sent < size) {
    size_t sent = 0u;
    const hal_status_t status =
#ifdef HAL_ENABLE_TLS
        tls_enabled_ ? hal_tls_client_write_ex(tls_client_, buffer + total_sent,
                                               size - total_sent, &sent)
                     :
#endif
                     hal_tcp_socket_send_ex(socket_, buffer + total_sent,
                                            size - total_sent, &sent);
    if (sent > size - total_sent) {
      stop();
      return total_sent;
    }
    total_sent += sent;

    if (total_sent == size) {
      return total_sent;
    }
    if (status != HAL_OK && status != HAL_EAGAIN) {
      stop();
      return total_sent;
    }
    if (!connected()) {
      stop();
      return total_sent;
    }
    if ((uint32_t)(hal_millis() - started_ms) >= write_timeout_ms) {
      stop();
      return total_sent;
    }

    hal_idle();
    hal_delay_ms(1u);
  }

  return total_sent;
}

int JHPubSubHalClient::available() {
  if (has_peeked_byte_) {
    return 1;
  }
#ifdef HAL_ENABLE_TLS
  if (tls_enabled_) {
    return peek() >= 0 ? 1 : 0;
  }
#endif
  return socket_ != NULL && hal_tcp_socket_can_recv(socket_) ? 1 : 0;
}

int JHPubSubHalClient::read() {
  uint8_t value = 0u;
  return read(&value, 1u) == 1 ? (int)value : -1;
}

int JHPubSubHalClient::read(uint8_t *buffer, size_t size) {
  if (buffer == NULL || size == 0u || size > (size_t)INT_MAX ||
      (
#ifdef HAL_ENABLE_TLS
          tls_enabled_ ? tls_client_ == NULL :
#endif
                       socket_ == NULL)) {
    return -1;
  }

  size_t copied = 0u;
  if (has_peeked_byte_) {
    buffer[copied++] = peeked_byte_;
    has_peeked_byte_ = false;
  }
  if (copied == size) {
    return (int)copied;
  }

  size_t received = 0u;
  const hal_status_t status =
#ifdef HAL_ENABLE_TLS
      tls_enabled_ ? hal_tls_client_read_ex(tls_client_, buffer + copied,
                                            size - copied, &received)
                   :
#endif
                   hal_tcp_socket_recv_ex(socket_, buffer + copied,
                                          size - copied, 0u, &received);
  if (status == HAL_EAGAIN) {
    return copied > 0u ? (int)copied : -1;
  }
  if (status != HAL_OK) {
    stop();
    return copied > 0u ? (int)copied : -1;
  }
  copied += received;
  return copied > 0u ? (int)copied : -1;
}

int JHPubSubHalClient::peek() {
  if (has_peeked_byte_) {
    return (int)peeked_byte_;
  }
  if (
#ifdef HAL_ENABLE_TLS
      tls_enabled_ ? tls_client_ == NULL :
#endif
                   socket_ == NULL) {
    return -1;
  }

  size_t received = 0u;
  const hal_status_t status =
#ifdef HAL_ENABLE_TLS
      tls_enabled_
          ? hal_tls_client_read_ex(tls_client_, &peeked_byte_, 1u, &received)
          :
#endif
          hal_tcp_socket_recv_ex(socket_, &peeked_byte_, 1u, 0u, &received);
  if (status == HAL_EAGAIN) {
    return -1;
  }
  if (status != HAL_OK) {
    stop();
    return -1;
  }
  if (received != 1u) {
    return -1;
  }
  has_peeked_byte_ = true;
  return (int)peeked_byte_;
}

void JHPubSubHalClient::flush() {}

void JHPubSubHalClient::stop() {
#ifdef HAL_ENABLE_TLS
  if (tls_client_ != NULL) {
    hal_status_t status = hal_tls_client_shutdown_ex(tls_client_);
    const uint32_t started_ms = hal_millis();
    while (status == HAL_EAGAIN &&
           (uint32_t)(hal_millis() - started_ms) < timeout_ms()) {
      status = hal_tls_client_poll_ex(tls_client_);
      if (status == HAL_EAGAIN) {
        hal_idle();
        hal_delay_ms(1u);
      }
    }
    (void)hal_tls_client_close_ex(tls_client_);
    tls_client_ = NULL;
  }
#endif
  if (socket_ != NULL) {
    hal_tcp_socket_close(socket_);
    socket_ = NULL;
  }
  has_peeked_byte_ = false;
  peeked_byte_ = 0u;
}

bool JHPubSubHalClient::connected() {
#ifdef HAL_ENABLE_TLS
  if (tls_enabled_) {
    hal_tls_state_t state = HAL_TLS_STATE_FAILED;
    return tls_client_ != NULL &&
                   hal_tls_client_get_state_ex(tls_client_, &state) == HAL_OK &&
                   state == HAL_TLS_STATE_CONNECTED
               ? true
               : false;
  }
#endif
  return socket_ != NULL && hal_tcp_socket_is_connected(socket_);
}

#ifdef HAL_ENABLE_TLS
hal_status_t
JHPubSubHalClient::configure_tls(const hal_tls_security_config_t *security) {
  if (security == NULL || security->trust_anchors == NULL ||
      security->trust_anchor_count == 0u || security->get_time == NULL ||
      security->get_entropy == NULL) {
    return HAL_ECONFIG;
  }
  stop();
  tls_security_ = *security;
  tls_enabled_ = true;
  return HAL_OK;
}

void JHPubSubHalClient::disable_tls() {
  stop();
  tls_enabled_ = false;
  tls_security_ = {};
}

hal_status_t JHPubSubHalClient::wait_for_tls_connection() {
  const uint32_t started_ms = hal_millis();
  while ((uint32_t)(hal_millis() - started_ms) < timeout_ms()) {
    hal_tls_state_t state = HAL_TLS_STATE_FAILED;
    hal_status_t status = hal_tls_client_get_state_ex(tls_client_, &state);
    if (status != HAL_OK) {
      return status;
    }
    if (state == HAL_TLS_STATE_CONNECTED) {
      return HAL_OK;
    }
    if (state == HAL_TLS_STATE_FAILED || state == HAL_TLS_STATE_CLOSED) {
      return HAL_EAUTH;
    }
    status = hal_tls_client_poll_ex(tls_client_);
    if (status != HAL_OK && status != HAL_EAGAIN) {
      return status;
    }
    hal_idle();
    hal_delay_ms(1u);
  }
  return HAL_ETIMEOUT;
}

int JHPubSubHalClient::connect_tls(const char *host, uint16_t port) {
  stop();
  hal_tls_client_config_t config = {};
  hal_status_t status = hal_tls_client_config_init(&config);
  if (status == HAL_OK) {
    config.execution_model = HAL_TLS_EXECUTION_BOUNDED_WORKER;
    config.transport_timeout_ms = timeout_ms();
    config.operation_timeout_ms = timeout_ms();
    status = hal_tls_client_create_ex(&config, &tls_client_);
  }
  if (status == HAL_OK) {
    status = hal_tls_client_configure_server_ex(tls_client_, host, port);
  }
  if (status == HAL_OK) {
    status = hal_tls_client_configure_security_ex(tls_client_, &tls_security_);
  }
  if (status == HAL_OK) {
    status = hal_tls_client_connect_ex(tls_client_);
  }
  if (status == HAL_EAGAIN) {
    status = wait_for_tls_connection();
  }
  if (status != HAL_OK) {
    hal_status_t last_status = HAL_OK;
    int32_t provider_error = 0;
    if (tls_client_ != NULL) {
      (void)hal_tls_client_get_last_error_ex(tls_client_, &last_status,
                                             &provider_error);
    }
    hal_derr("JHPubSubHalClient: TLS connect failed: %s last=%s provider=%ld",
             hal_status_to_string(status), hal_status_to_string(last_status),
             (long)provider_error);
    stop();
    return 0;
  }
  return 1;
}
#endif

#endif /* (RP2040 || STM32G474) && HAL_ENABLE_MQTT */
