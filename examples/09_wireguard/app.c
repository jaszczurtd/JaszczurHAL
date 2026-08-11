#include <hal/core/hal_app.h>
#include <hal/network/hal_wifi.h>
#include <hal/network/wireguard/hal_wireguard.h>
#include <hal/system/hal_system.h>
#include <tools_c.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";

static const char *WG_LOCAL_IP = "10.8.0.2";
static const char *WG_PRIVATE_KEY = "base64-private-key";
static const char *WG_PEER_HOST = "vpn.example.com";
static const uint16_t WG_PEER_PORT = 51820;
static const char *WG_PEER_PUBLIC_KEY = "base64-peer-public-key";
static const char *WG_ALLOWED_IP = "10.8.0.0";
static const char *WG_ALLOWED_MASK = "255.255.255.0";
static const char *WG_PROBE_IP = "10.8.0.1";
static const uint16_t WG_PROBE_PORT = 33434;

static uint32_t last_wifi_check_ms = 0;
static uint32_t last_tunnel_check_ms = 0;
static bool tunnel_started = false;

static void connectWifi(void) {
  if (hal_wifi_is_connected()) {
    return;
  }

  const uint32_t now = hal_millis();
  if (now - last_wifi_check_ms < 5000u) {
    return;
  }
  last_wifi_check_ms = now;

  deb("WiFi: connecting to %s", WIFI_SSID);
  hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  hal_wifi_set_hostname("jaszczurhal-wg");
  hal_wifi_begin_station(WIFI_SSID, WIFI_PASSWORD, true);
}

static void startWireGuard(void) {
  if (!hal_wifi_is_connected() || tunnel_started) {
    return;
  }

  tunnel_started = hal_wireguard_begin_advanced_text(
      WG_LOCAL_IP, WG_PRIVATE_KEY, WG_PEER_HOST, WG_PEER_PUBLIC_KEY,
      WG_PEER_PORT, WG_ALLOWED_IP, WG_ALLOWED_MASK);
  if (tunnel_started) {
    deb("WireGuard: tunnel started");
  } else {
    derr("WireGuard: begin failed");
  }
}

static void serviceWireGuard(void) {
  if (!tunnel_started) {
    return;
  }

  const uint32_t now = hal_millis();
  if (now - last_tunnel_check_ms < 5000u) {
    return;
  }
  last_tunnel_check_ms = now;

  char endpoint_ip[HAL_WIREGUARD_IP_STR_LEN] = {};
  uint16_t endpoint_port = 0;
  if (hal_wireguard_peer_up(endpoint_ip, sizeof(endpoint_ip), &endpoint_port)) {
    deb("WireGuard: peer up via %s:%u", endpoint_ip, endpoint_port);
  } else {
    deb("WireGuard: peer not up, kicking handshake");
    hal_wireguard_kick_handshake_text(WG_PROBE_IP, WG_PROBE_PORT, 5000u);
  }
}

void app_start(void) { debugInit(); }

void app_task0(void) {
  connectWifi();
  startWireGuard();
  serviceWireGuard();
}
