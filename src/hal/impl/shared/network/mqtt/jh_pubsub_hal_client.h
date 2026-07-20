#pragma once

#include "../../../../hal_target.h"

#if HAL_TARGET_IS_RP2040
#include "../../../../hal_config.h"

#ifdef HAL_ENABLE_MQTT

#include "../../../../hal_tcp.h"

#include <Client.h>

class JHPubSubHalClient final : public arduino::Client {
public:
  JHPubSubHalClient();
  ~JHPubSubHalClient();

  JHPubSubHalClient(const JHPubSubHalClient &) = delete;
  JHPubSubHalClient &operator=(const JHPubSubHalClient &) = delete;

  int connect(arduino::IPAddress ip, uint16_t port) override;
  int connect(const char *host, uint16_t port) override;
  size_t write(uint8_t data) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  int available() override;
  int read() override;
  int read(uint8_t *buffer, size_t size) override;
  int peek() override;
  void flush() override;
  void stop() override;
  uint8_t connected() override;
  operator bool() override;

private:
  int connect_endpoint(const hal_net_endpoint_t &endpoint);
  uint32_t timeout_ms();

  hal_tcp_socket_t socket_;
  bool has_peeked_byte_;
  uint8_t peeked_byte_;
};

#endif /* HAL_ENABLE_MQTT */
#endif /* HAL_TARGET_IS_RP2040 */
