#pragma once

#include "hal/core/hal_status.h"
#include "hal/network/wireguard/hal_wireguard.h"

#include <stdint.h>

class JHWireGuardClient final {
public:
  // Starts the tunnel from a configuration the facade has validated. The
  // preshared key is decoded here and rejected before lwIP is touched; an
  // all-zero AllowedIPs network makes the tunnel the default route.
  bool begin(const hal_wireguard_config_t &config);

  hal_status_t end();
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
