#pragma once

#include "hal_project_config.h"

#include <hal/hal_system.h>
#include <hal/hal_wifi.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tools_c.h>

static inline bool bsd_example_resolve_server(const char *host, uint16_t port,
                                              int socktype,
                                              struct sockaddr_in *out) {
  if (!host || !out) {
    return false;
  }

  char service[8] = {};
  snprintf(service, sizeof(service), "%u", (unsigned)port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = socktype;

  struct addrinfo *resolved = NULL;
  const int err = getaddrinfo(host, service, &hints, &resolved);
  if (err != 0 || !resolved ||
      resolved->ai_addrlen < (socklen_t)sizeof(struct sockaddr_in)) {
    derr("getaddrinfo(%s:%s) failed: %s", host, service, gai_strerror(err));
    freeaddrinfo(resolved);
    return false;
  }

  memcpy(out, resolved->ai_addr, sizeof(*out));
  freeaddrinfo(resolved);
  return true;
}

static inline bool bsd_example_wait_for_wifi(const char *hostname) {
  static bool connecting = false;
  static bool announced_ip = false;
  static uint32_t last_attempt_ms = 0u;

  if (hal_wifi_is_connected() && hal_wifi_has_local_ip()) {
    if (!announced_ip) {
      char ip[32] = {};
      if (hal_wifi_get_local_ip(ip, sizeof(ip))) {
        deb("WiFi ready: %s", ip);
      } else {
        deb("WiFi ready");
      }
      announced_ip = true;
    }
    return true;
  }

  announced_ip = false;

  const uint32_t now = hal_millis();
  if (!connecting || (uint32_t)(now - last_attempt_ms) >= 5000u) {
    connecting = true;
    last_attempt_ms = now;
    hal_wifi_set_mode(HAL_WIFI_MODE_STA);
    hal_wifi_set_hostname(hostname);
    hal_wifi_begin_station(BSD_EXAMPLE_WIFI_SSID, BSD_EXAMPLE_WIFI_PASSWORD,
                           true);
    deb("WiFi: connecting to %s", BSD_EXAMPLE_WIFI_SSID);
  }

  hal_delay_ms(50u);
  return false;
}
