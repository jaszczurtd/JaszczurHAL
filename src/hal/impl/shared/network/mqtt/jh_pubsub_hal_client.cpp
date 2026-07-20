#include "jh_pubsub_hal_client.h"

#if HAL_TARGET_IS_RP2040 && defined(HAL_ENABLE_MQTT)

#include "../../../../hal_net.h"
#include "../../../../hal_serial.h"
#include "../../../../hal_system.h"

#include <limits.h>

#define JH_PUBSUB_HAL_DEFAULT_TIMEOUT_MS 15000UL

JHPubSubHalClient::JHPubSubHalClient()
    : socket_(NULL), has_peeked_byte_(false), peeked_byte_(0u) {
  setTimeout(JH_PUBSUB_HAL_DEFAULT_TIMEOUT_MS);
}

JHPubSubHalClient::~JHPubSubHalClient() { stop(); }

uint32_t JHPubSubHalClient::timeout_ms() {
  const unsigned long timeout = getTimeout();
  return timeout > UINT32_MAX ? UINT32_MAX : (uint32_t)timeout;
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

int JHPubSubHalClient::connect(arduino::IPAddress ip, uint16_t port) {
  if (port == 0u) {
    return 0;
  }

  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr[0] = ip[0];
  endpoint.addr[1] = ip[1];
  endpoint.addr[2] = ip[2];
  endpoint.addr[3] = ip[3];
  endpoint.port = port;
  return connect_endpoint(endpoint);
}

int JHPubSubHalClient::connect(const char *host, uint16_t port) {
  if (host == NULL || host[0] == '\0' || port == 0u) {
    return 0;
  }

  stop();
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
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
  if (buffer == NULL || socket_ == NULL) {
    return 0u;
  }

  const uint32_t started_ms = hal_millis();
  const uint32_t write_timeout_ms = timeout_ms();
  size_t total_sent = 0u;

  while (total_sent < size) {
    size_t sent = 0u;
    const hal_status_t status = hal_tcp_socket_send_ex(
        socket_, buffer + total_sent, size - total_sent, &sent);
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
    if (!hal_tcp_socket_is_connected(socket_)) {
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
  return socket_ != NULL && hal_tcp_socket_can_recv(socket_) ? 1 : 0;
}

int JHPubSubHalClient::read() {
  uint8_t value = 0u;
  return read(&value, 1u) == 1 ? (int)value : -1;
}

int JHPubSubHalClient::read(uint8_t *buffer, size_t size) {
  if (buffer == NULL || size == 0u || size > (size_t)INT_MAX ||
      socket_ == NULL) {
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
  const hal_status_t status = hal_tcp_socket_recv_ex(
      socket_, buffer + copied, size - copied, 0u, &received);
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
  if (socket_ == NULL) {
    return -1;
  }

  size_t received = 0u;
  const hal_status_t status =
      hal_tcp_socket_recv_ex(socket_, &peeked_byte_, 1u, 0u, &received);
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
  if (socket_ != NULL) {
    hal_tcp_socket_close(socket_);
    socket_ = NULL;
  }
  has_peeked_byte_ = false;
  peeked_byte_ = 0u;
}

uint8_t JHPubSubHalClient::connected() {
  return socket_ != NULL && hal_tcp_socket_is_connected(socket_) ? 1u : 0u;
}

JHPubSubHalClient::operator bool() { return connected() != 0u; }

#endif /* HAL_TARGET_IS_RP2040 && HAL_ENABLE_MQTT */
