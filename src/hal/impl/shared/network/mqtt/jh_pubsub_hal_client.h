#pragma once

#include "../../../../hal_target.h"

#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474
#include "../../../../hal_config.h"

#ifdef HAL_ENABLE_MQTT

#include "../../../../hal_tcp.h"
#ifdef HAL_ENABLE_TLS
#include "../../../../hal_tls.h"
#endif

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

#ifdef HAL_ENABLE_TLS
  hal_status_t configure_tls(const hal_tls_security_config_t *security);
  void disable_tls();
#endif

private:
  int connect_endpoint(const hal_net_endpoint_t &endpoint);
  uint32_t timeout_ms();
#ifdef HAL_ENABLE_TLS
  int connect_tls(const char *host, uint16_t port);
  hal_status_t wait_for_tls_connection();
#endif

  hal_tcp_socket_t socket_;
  bool has_peeked_byte_;
  uint8_t peeked_byte_;
#ifdef HAL_ENABLE_TLS
  bool tls_enabled_;
  hal_tls_security_config_t tls_security_;
  hal_tls_client_t tls_client_;
#endif
};

#endif /* HAL_ENABLE_MQTT */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
