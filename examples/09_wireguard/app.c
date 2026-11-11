/*
 * Start a WireGuard tunnel over WiFi and periodically check the peer
 * connection. Replace the placeholder keys and addresses and configure the
 * remote peer.
 */

#include <hal/core/hal_app.h>
#include <hal/network/hal_wifi.h>
#include <hal/network/wireguard/hal_wireguard.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";

static const char *WG_LOCAL_IP = "10.8.0.2";
static const char *WG_PRIVATE_KEY = "base64-private-key";
static const char *WG_PEER_HOST = "vpn.example.com";
static const uint16_t WG_PEER_PORT = 51820;
static const char *WG_PEER_PUBLIC_KEY = "base64-peer-public-key";
/* Optional: the base64 preshared key of this peer, "" when it has none. */
static const char *WG_PRESHARED_KEY = "";
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

  hal_wifi_state_t state = HAL_WIFI_STATE_OFF;
  (void)hal_wifi_get_state_ex(&state);
  deb("WiFi: connecting to %s (state=%s)", WIFI_SSID,
      hal_wifi_state_to_string(state));
  hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  hal_wifi_set_hostname("jaszczurhal-wg");
  const hal_status_t join =
      hal_wifi_begin_station_ex(WIFI_SSID, WIFI_PASSWORD, true);
  if (join != HAL_OK) {
    derr("WiFi: join rejected (%s)", hal_status_to_string(join));
  }
}

static void startWireGuard(void) {
  if (!hal_wifi_is_connected() || tunnel_started) {
    return;
  }

  /* The keepalive keeps its default (HAL_WIREGUARD_KEEPALIVE_DEFAULT_S), so
   * the tunnel stays up behind NAT and renews its keys on its own. */
  hal_wireguard_config_t config;
  tunnel_started =
      hal_wireguard_config_init(&config) == HAL_OK &&
      hal_wireguard_parse_ipv4(WG_LOCAL_IP, config.local_ip) &&
      hal_wireguard_parse_ipv4(WG_ALLOWED_IP, config.allowed_ip) &&
      hal_wireguard_parse_ipv4(WG_ALLOWED_MASK, config.allowed_mask);
  if (tunnel_started) {
    config.private_key = WG_PRIVATE_KEY;
    config.remote_peer_address = WG_PEER_HOST;
    config.remote_peer_public_key = WG_PEER_PUBLIC_KEY;
    config.remote_peer_port = WG_PEER_PORT;
    config.preshared_key = WG_PRESHARED_KEY;
    tunnel_started = hal_wireguard_begin_config(&config);
  }
  /* Initialization does not prove the peer is connected; serviceWireGuard
   * checks that. */
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

void app_start(void) { hal_debug_init_default(); }

void app_task0(void) {
  connectWifi();
  startWireGuard();
  serviceWireGuard();
}
