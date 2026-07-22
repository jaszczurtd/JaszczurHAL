#include <JaszczurHAL.h>
#include <tools.h>

#include <string.h>

#ifdef HTTP_EXAMPLE_CA_AVAILABLE
#include "ca_certificate.h"
#endif

static bool s_started;

static void perform_request(hal_http_client_transport_t transport,
                            uint16_t port,
                            const hal_tls_security_config_t *security) {
  hal_http_client_request_t request = {};
  (void)hal_http_client_request_init(&request);
  request.transport = transport;
  request.host = HTTP_EXAMPLE_HOST;
  request.port = port;
  request.path = "/";
  request.tls_security = security;
  uint8_t body[512] = {};
  hal_http_client_response_t response = {};
  const hal_status_t status =
      hal_http_client_perform_ex(&request, body, sizeof(body), &response);
  deb("%s status=%s HTTP=%u body=%u",
      transport == HAL_HTTP_CLIENT_TRANSPORT_TLS ? "HTTPS" : "HTTP",
      hal_status_to_string(status), (unsigned)response.status_code,
      (unsigned)response.body_length);
}

extern "C" void app_start(void) {
  debugInit();
  hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  hal_wifi_begin_station(HTTP_EXAMPLE_WIFI_SSID, HTTP_EXAMPLE_WIFI_PASSWORD,
                         true);
}

extern "C" void app_task0(void) {
  if (s_started || !hal_wifi_is_connected() || !hal_wifi_has_local_ip()) {
    hal_delay_ms(50u);
    return;
  }
  s_started = true;
  perform_request(HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT, 80u, NULL);

#ifdef HTTP_EXAMPLE_CA_AVAILABLE
  hal_tls_trust_anchor_storage_t anchor = {};
  if (hal_tls_trust_anchor_from_der_ex(
          http_example_ca_der, http_example_ca_der_len, &anchor) == HAL_OK) {
    hal_tls_security_config_t security = {};
    security.trust_anchors = &anchor.anchor;
    security.trust_anchor_count = 1u;
    security.get_time = hal_tls_default_time;
    security.get_entropy = hal_tls_default_entropy;
    perform_request(HAL_HTTP_CLIENT_TRANSPORT_TLS, 443u, &security);
  }
#else
  deb("HTTPS skipped: provide ca_certificate.h and "
      "HTTP_EXAMPLE_CA_AVAILABLE");
#endif
}
