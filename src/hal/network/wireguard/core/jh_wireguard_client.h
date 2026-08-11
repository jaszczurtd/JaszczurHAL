#pragma once

#include <stdint.h>

class JHWireGuardClient final {
public:
  bool begin(const uint8_t local_ip[4], const char *private_key,
             const char *remote_peer_address,
             const char *remote_peer_public_key, uint16_t remote_peer_port);

  bool begin_advanced(const uint8_t local_ip[4], const char *private_key,
                      const char *remote_peer_address,
                      const char *remote_peer_public_key,
                      uint16_t remote_peer_port, const uint8_t allowed_ip[4],
                      const uint8_t allowed_mask[4]);

  void end();
  bool is_initialized() const { return initialized_; }
  bool peer_up(uint8_t current_endpoint_ip[4],
               uint16_t *current_endpoint_port) const;
  bool kick_handshake(const uint8_t probe_ip[4], uint16_t probe_port,
                      uint32_t min_interval_ms = 250u);

private:
  bool initialized_ = false;
  bool has_kicked_ = false;
  uint32_t last_kick_ms_ = 0u;
};
