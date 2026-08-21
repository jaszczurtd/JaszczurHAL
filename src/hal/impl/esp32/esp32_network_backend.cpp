#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32_FAMILY
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_ESP_IDF)

#include "hal/core/hal_mutex_once.h"
#include "hal/network/jh_lwip_status.h"
#include "hal/network/jh_net_address_utils.h"
#include "hal/network/jh_network_backend.h"
#include "hal/system/hal_board.h"
#include "hal/system/hal_sync.h"
#include "hal/system/jh_board_runtime.h"
#include "jh_esp32_network.h"
#include "jh_esp32_status.h"

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/ip_addr.h>
#include <lwip/netdb.h>
#include <lwip/netifapi.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <nvs_flash.h>
#include <ping/ping_sock.h>

#include <limits.h>
#include <new>
#include <string.h>

#if defined(HAL_ENABLE_WIREGUARD) && !LWIP_TCPIP_CORE_LOCKING
#error "HAL_ENABLE_WIREGUARD requires CONFIG_LWIP_TCPIP_CORE_LOCKING"
#endif

namespace {

constexpr EventBits_t kGotIpv4Bit = BIT0;
constexpr EventBits_t kConnectionFailedBit = BIT1;
constexpr EventBits_t kScanDoneBit = BIT2;
constexpr uint32_t kPingCompletionSlackMs = 1000u;

hal_mutex_t s_lifecycle_mutex;
portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
StaticEventGroup_t s_event_group_storage;
EventGroupHandle_t s_event_group;

esp_netif_t *s_station_netif;
esp_event_handler_instance_t s_wifi_event_handler;
esp_event_handler_instance_t s_ip_event_handler;
hal_wifi_scan_result_t *s_scan_results;
size_t s_scan_count;
TaskHandle_t s_stack_owner;
uint32_t s_stack_depth;
uint32_t s_stack_acquirers;
hal_wifi_state_t s_wifi_state = HAL_WIFI_STATE_OFF;
uint32_t s_scan_status;
bool s_wifi_driver_initialized;
bool s_station_started;
bool s_station_attached;
bool s_default_handlers_registered;
bool s_wifi_handler_registered;
bool s_ip_handler_registered;
bool s_connect_requested;
bool s_stopping;
bool s_got_ipv4;
bool s_runtime_state_dirty;

bool stack_access_busy(void) {
  portENTER_CRITICAL(&s_state_mux);
  const bool busy = s_stack_depth != 0u || s_stack_acquirers != 0u;
  portEXIT_CRITICAL(&s_state_mux);
  return busy;
}

#if defined(HAL_ENABLE_TCP)
extern "C" const jh_network_tcp_ops_t *jh_esp32_network_tcp_ops(void);
#endif
#if defined(HAL_ENABLE_UDP)
extern "C" const jh_network_udp_ops_t *jh_esp32_network_udp_ops(void);
#endif

TickType_t timeout_to_ticks(uint32_t timeout_ms) {
  if (timeout_ms == HAL_NET_TIMEOUT_FOREVER) {
    return portMAX_DELAY;
  }
  const uint64_t ticks =
      (static_cast<uint64_t>(timeout_ms) * configTICK_RATE_HZ + 999u) / 1000u;
  if (ticks == 0u) {
    return timeout_ms == 0u ? 0u : 1u;
  }
  return ticks >= static_cast<uint64_t>(portMAX_DELAY)
             ? portMAX_DELAY - 1u
             : static_cast<TickType_t>(ticks);
}

hal_status_t ensure_primitives(void) {
  if (jh_hal_mutex_create_once(&s_lifecycle_mutex) == nullptr) {
    return HAL_ENOMEM;
  }
  portENTER_CRITICAL(&s_state_mux);
  if (s_event_group == nullptr) {
    s_event_group = xEventGroupCreateStatic(&s_event_group_storage);
  }
  const bool ready = s_event_group != nullptr;
  portEXIT_CRITICAL(&s_state_mux);
  return ready ? HAL_OK : HAL_ENOMEM;
}

class LifecycleGuard final {
public:
  LifecycleGuard() : status_(ensure_primitives()), locked_(false) {
    if (status_ == HAL_OK) {
      hal_mutex_lock(s_lifecycle_mutex);
      locked_ = true;
    }
  }
  ~LifecycleGuard() {
    if (locked_) {
      hal_mutex_unlock(s_lifecycle_mutex);
    }
  }

  hal_status_t status() const { return status_; }

  LifecycleGuard(const LifecycleGuard &) = delete;
  LifecycleGuard &operator=(const LifecycleGuard &) = delete;

private:
  hal_status_t status_;
  bool locked_;
};

void set_wifi_state(hal_wifi_state_t state) {
  portENTER_CRITICAL(&s_state_mux);
  s_wifi_state = state;
  portEXIT_CRITICAL(&s_state_mux);
}

bool reason_is_auth_failure(uint8_t reason) {
  switch (reason) {
  case WIFI_REASON_MIC_FAILURE:
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
  case WIFI_REASON_IE_IN_4WAY_DIFFERS:
  case WIFI_REASON_802_1X_AUTH_FAILED:
  case WIFI_REASON_AUTH_FAIL:
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
  case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    return true;
  default:
    return false;
  }
}

bool reason_is_missing_network(uint8_t reason) {
  return reason == WIFI_REASON_NO_AP_FOUND ||
         reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD;
}

void handle_wifi_event(int32_t event_id, void *event_data) {
  bool reconnect = false;
  bool create_ipv6 = false;
  EventBits_t bits = 0u;

  portENTER_CRITICAL(&s_state_mux);
  switch (event_id) {
  case WIFI_EVENT_STA_START:
    if (s_connect_requested && !s_stopping) {
      s_wifi_state = HAL_WIFI_STATE_CONNECTING;
      reconnect = true;
    } else if (!s_stopping) {
      s_wifi_state = HAL_WIFI_STATE_IDLE;
    }
    break;
  case WIFI_EVENT_STA_CONNECTED:
    if (!s_stopping) {
      s_wifi_state = HAL_WIFI_STATE_CONNECTED_NO_IP;
      create_ipv6 = true;
    }
    break;
  case WIFI_EVENT_STA_DISCONNECTED: {
    const auto *event =
        static_cast<wifi_event_sta_disconnected_t *>(event_data);
    s_got_ipv4 = false;
    bits |= kGotIpv4Bit;
    if (!s_connect_requested || s_stopping) {
      s_wifi_state = s_stopping ? HAL_WIFI_STATE_OFF : HAL_WIFI_STATE_IDLE;
    } else if (event != nullptr && reason_is_auth_failure(event->reason)) {
      s_connect_requested = false;
      s_wifi_state = HAL_WIFI_STATE_AUTH_FAILED;
      bits |= kConnectionFailedBit;
    } else if (event != nullptr && reason_is_missing_network(event->reason)) {
      s_connect_requested = false;
      s_wifi_state = HAL_WIFI_STATE_NO_NETWORK;
      bits |= kConnectionFailedBit;
    } else {
      s_wifi_state = HAL_WIFI_STATE_CONNECTING;
      reconnect = true;
    }
    break;
  }
  case WIFI_EVENT_SCAN_DONE: {
    const auto *event = static_cast<wifi_event_sta_scan_done_t *>(event_data);
    s_scan_status = event != nullptr ? event->status : 1u;
    bits |= kScanDoneBit;
    break;
  }
  case WIFI_EVENT_STA_STOP:
    s_got_ipv4 = false;
    s_wifi_state = s_stopping ? HAL_WIFI_STATE_OFF : HAL_WIFI_STATE_IDLE;
    bits |= kGotIpv4Bit;
    break;
  default:
    break;
  }
  esp_netif_t *const netif = s_station_netif;
  EventGroupHandle_t const events = s_event_group;
  portEXIT_CRITICAL(&s_state_mux);

  if ((bits & kGotIpv4Bit) != 0u && events != nullptr) {
    (void)xEventGroupClearBits(events, kGotIpv4Bit);
  }
  if ((bits & (kConnectionFailedBit | kScanDoneBit)) != 0u &&
      events != nullptr) {
    (void)xEventGroupSetBits(events,
                             bits & (kConnectionFailedBit | kScanDoneBit));
  }
  if (create_ipv6 && netif != nullptr) {
#if LWIP_IPV6
    (void)esp_netif_create_ip6_linklocal(netif);
#else
    (void)netif;
#endif
  }
  if (reconnect) {
    const esp_err_t status = esp_wifi_connect();
    if (status != ESP_OK && status != ESP_ERR_WIFI_STATE) {
      portENTER_CRITICAL(&s_state_mux);
      s_connect_requested = false;
      s_wifi_state = HAL_WIFI_STATE_FAILED;
      portEXIT_CRITICAL(&s_state_mux);
      if (events != nullptr) {
        (void)xEventGroupSetBits(events, kConnectionFailedBit);
      }
    }
  }
}

void handle_ip_event(int32_t event_id) {
  EventGroupHandle_t events = nullptr;
  portENTER_CRITICAL(&s_state_mux);
  if (event_id == IP_EVENT_STA_GOT_IP) {
    s_got_ipv4 = true;
    s_wifi_state = HAL_WIFI_STATE_CONNECTED;
    events = s_event_group;
  } else if (event_id == IP_EVENT_STA_LOST_IP) {
    s_got_ipv4 = false;
    if (s_wifi_state == HAL_WIFI_STATE_CONNECTED) {
      s_wifi_state = HAL_WIFI_STATE_CONNECTED_NO_IP;
    }
    events = s_event_group;
  }
  portEXIT_CRITICAL(&s_state_mux);

  if (events != nullptr) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
      (void)xEventGroupSetBits(events, kGotIpv4Bit);
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
      (void)xEventGroupClearBits(events, kGotIpv4Bit);
    }
  }
}

void network_event_handler(void *, esp_event_base_t event_base,
                           int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT) {
    handle_wifi_event(event_id, event_data);
  } else if (event_base == IP_EVENT) {
    handle_ip_event(event_id);
  }
}

void clear_scan_snapshot_locked(void) {
  delete[] s_scan_results;
  s_scan_results = nullptr;
  s_scan_count = 0u;
}

hal_status_t initialize_transport_sockets(void) {
#if defined(HAL_ENABLE_TCP) || defined(HAL_ENABLE_UDP)
  return jh_esp32_network_sockets_initialize();
#else
  return HAL_OK;
#endif
}

hal_status_t shutdown_transport_sockets(void) {
#if defined(HAL_ENABLE_TCP) || defined(HAL_ENABLE_UDP)
  return jh_esp32_network_sockets_shutdown_all();
#else
  return HAL_OK;
#endif
}

hal_status_t teardown_failure(hal_status_t status) {
  set_wifi_state(HAL_WIFI_STATE_FAILED);
  if (jh_board_runtime_set_failed(HAL_BOARD_CAP_NATIVE_WIFI) == HAL_OK) {
    s_runtime_state_dirty = true;
  }
  return status;
}

hal_status_t teardown_wifi_locked(void) {
  portENTER_CRITICAL(&s_state_mux);
  s_stopping = true;
  s_connect_requested = false;
  s_got_ipv4 = false;
  portEXIT_CRITICAL(&s_state_mux);

  if (s_wifi_handler_registered) {
    const esp_err_t status = esp_event_handler_instance_unregister(
        WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_handler);
    if (status != ESP_OK) {
      return teardown_failure(jh_esp32_status_from_esp_err(status));
    }
    s_wifi_handler_registered = false;
  }
  if (s_ip_handler_registered) {
    const esp_err_t status = esp_event_handler_instance_unregister(
        IP_EVENT, ESP_EVENT_ANY_ID, s_ip_event_handler);
    if (status != ESP_OK) {
      return teardown_failure(jh_esp32_status_from_esp_err(status));
    }
    s_ip_handler_registered = false;
  }
  if (s_station_started) {
    const esp_err_t status = esp_wifi_stop();
    if (status != ESP_OK && status != ESP_ERR_WIFI_NOT_STARTED &&
        status != ESP_ERR_WIFI_NOT_INIT) {
      return teardown_failure(jh_esp32_status_from_esp_err(status));
    }
    portENTER_CRITICAL(&s_state_mux);
    s_station_started = false;
    portEXIT_CRITICAL(&s_state_mux);
  }
  if (s_wifi_driver_initialized) {
    const esp_err_t status = esp_wifi_deinit();
    if (status != ESP_OK && status != ESP_ERR_WIFI_NOT_INIT) {
      return teardown_failure(jh_esp32_status_from_esp_err(status));
    }
    portENTER_CRITICAL(&s_state_mux);
    s_wifi_driver_initialized = false;
    portEXIT_CRITICAL(&s_state_mux);
  }
  if (s_station_netif != nullptr) {
    if (s_station_attached) {
      const esp_err_t status =
          esp_wifi_clear_default_wifi_driver_and_handlers(s_station_netif);
      if (status != ESP_OK) {
        return teardown_failure(jh_esp32_status_from_esp_err(status));
      }
      s_station_attached = false;
      s_default_handlers_registered = false;
    }
    esp_netif_destroy(s_station_netif);
    portENTER_CRITICAL(&s_state_mux);
    s_station_netif = nullptr;
    portEXIT_CRITICAL(&s_state_mux);
  }
  clear_scan_snapshot_locked();
  const hal_status_t runtime_status =
      jh_board_runtime_set_inactive(HAL_BOARD_CAP_NATIVE_WIFI);
  if (runtime_status != HAL_OK) {
    s_runtime_state_dirty = true;
    return teardown_failure(runtime_status);
  }
  s_runtime_state_dirty = false;

  portENTER_CRITICAL(&s_state_mux);
  s_stopping = false;
  s_wifi_state = HAL_WIFI_STATE_OFF;
  portEXIT_CRITICAL(&s_state_mux);
  (void)xEventGroupClearBits(s_event_group,
                             kGotIpv4Bit | kConnectionFailedBit | kScanDoneBit);
  return HAL_OK;
}

hal_status_t initialize_nvs(void) {
  const esp_err_t status = nvs_flash_init();
  if (status == ESP_ERR_NVS_NO_FREE_PAGES ||
      status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    return HAL_ECONFIG;
  }
  return jh_esp32_status_from_esp_err(status);
}

hal_status_t initialize_wifi_locked(void) {
  if (s_wifi_driver_initialized && s_station_started &&
      s_station_netif != nullptr && s_station_attached &&
      s_default_handlers_registered && s_wifi_handler_registered &&
      s_ip_handler_registered && !s_stopping) {
    return HAL_OK;
  }
  if (s_wifi_driver_initialized || s_station_started ||
      s_station_netif != nullptr || s_wifi_handler_registered ||
      s_ip_handler_registered || s_station_attached ||
      s_default_handlers_registered || s_runtime_state_dirty) {
    return HAL_EBUSY;
  }

  hal_status_t status = initialize_nvs();
  if (status != HAL_OK) {
    return status;
  }
  status = jh_esp32_status_from_esp_err(esp_netif_init());
  if (status != HAL_OK) {
    return status;
  }
  const esp_err_t loop_status = esp_event_loop_create_default();
  if (loop_status != ESP_OK && loop_status != ESP_ERR_INVALID_STATE) {
    return jh_esp32_status_from_esp_err(loop_status);
  }

  wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
  const esp_err_t wifi_status = esp_wifi_init(&wifi_config);
  if (wifi_status != ESP_OK) {
    if (wifi_status != ESP_ERR_INVALID_STATE) {
      s_runtime_state_dirty =
          jh_board_runtime_set_failed(HAL_BOARD_CAP_NATIVE_WIFI) == HAL_OK;
    }
    return wifi_status == ESP_ERR_INVALID_STATE
               ? HAL_EBUSY
               : jh_esp32_status_from_esp_err_with_fallback(wifi_status,
                                                            HAL_EHW);
  }
  portENTER_CRITICAL(&s_state_mux);
  s_wifi_driver_initialized = true;
  portEXIT_CRITICAL(&s_state_mux);

  esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
  esp_netif_t *const station_netif = esp_netif_new(&netif_config);
  portENTER_CRITICAL(&s_state_mux);
  s_station_netif = station_netif;
  portEXIT_CRITICAL(&s_state_mux);
  if (station_netif == nullptr) {
    (void)teardown_wifi_locked();
    return HAL_ENOMEM;
  }
  s_station_attached = true;
  esp_err_t esp_status = esp_netif_attach_wifi_station(s_station_netif);
  if (esp_status == ESP_OK) {
    esp_status = esp_wifi_set_default_wifi_sta_handlers();
    s_default_handlers_registered = esp_status == ESP_OK;
  }
  if (esp_status == ESP_OK) {
    esp_status = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, network_event_handler, nullptr,
        &s_wifi_event_handler);
    s_wifi_handler_registered = esp_status == ESP_OK;
  }
  if (esp_status == ESP_OK) {
    esp_status = esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, network_event_handler, nullptr,
        &s_ip_event_handler);
    s_ip_handler_registered = esp_status == ESP_OK;
  }
  if (esp_status == ESP_OK) {
    esp_status = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  }
  if (esp_status == ESP_OK) {
    esp_status = esp_wifi_set_mode(WIFI_MODE_STA);
  }

  portENTER_CRITICAL(&s_state_mux);
  s_stopping = false;
  s_connect_requested = false;
  s_got_ipv4 = false;
  s_wifi_state = HAL_WIFI_STATE_IDLE;
  portEXIT_CRITICAL(&s_state_mux);
  (void)xEventGroupClearBits(s_event_group,
                             kGotIpv4Bit | kConnectionFailedBit | kScanDoneBit);

  if (esp_status == ESP_OK) {
    esp_status = esp_wifi_start();
    portENTER_CRITICAL(&s_state_mux);
    s_station_started = esp_status == ESP_OK;
    portEXIT_CRITICAL(&s_state_mux);
  }
  if (esp_status != ESP_OK) {
    const esp_err_t initial_error = esp_status;
    (void)teardown_wifi_locked();
    return jh_esp32_status_from_esp_err(initial_error);
  }

  s_runtime_state_dirty = true;
  status = jh_board_runtime_set_available(HAL_BOARD_CAP_NATIVE_WIFI);
  if (status != HAL_OK) {
    (void)teardown_wifi_locked();
  }
  return status;
}

hal_status_t service_initialize(void) {
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  const hal_status_t socket_status = initialize_transport_sockets();
  if (socket_status != HAL_OK) {
    return socket_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);
  const hal_status_t status = initialize_wifi_locked();
  hal_mutex_unlock(s_lifecycle_mutex);
  return status;
}

hal_status_t service_deinitialize(void) {
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);

  if (stack_access_busy()) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_EBUSY;
  }
  if (!s_wifi_driver_initialized && s_station_netif == nullptr &&
      !s_wifi_handler_registered && !s_ip_handler_registered &&
      !s_station_attached && !s_station_started &&
      !s_default_handlers_registered && !s_runtime_state_dirty) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_OK;
  }

  portENTER_CRITICAL(&s_state_mux);
  s_stopping = true;
  s_connect_requested = false;
  s_got_ipv4 = false;
  portEXIT_CRITICAL(&s_state_mux);
  const hal_status_t socket_status = shutdown_transport_sockets();
  if (socket_status != HAL_OK) {
    const hal_status_t status = teardown_failure(socket_status);
    hal_mutex_unlock(s_lifecycle_mutex);
    return status;
  }
  const hal_status_t status = teardown_wifi_locked();
  hal_mutex_unlock(s_lifecycle_mutex);
  return status;
}

hal_status_t service_service(void) { return jh_esp32_network_require_ready(); }

hal_status_t service_stack_enter(bool require_ipv4) {
  return jh_esp32_network_stack_enter(require_ipv4);
}

void service_stack_leave(void) { jh_esp32_network_stack_leave(); }

hal_status_t wifi_set_mode(hal_wifi_mode_t mode) {
  if (mode == HAL_WIFI_MODE_OFF) {
    return service_deinitialize();
  }
  if (mode != HAL_WIFI_MODE_STA) {
    return mode == HAL_WIFI_MODE_AP || mode == HAL_WIFI_MODE_AP_STA
               ? HAL_EUNSUPPORTED
               : HAL_EINVAL;
  }
  return service_initialize();
}

hal_status_t wifi_disconnect(bool erase_credentials) {
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);
  if (!s_wifi_driver_initialized) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_EUNINIT;
  }

  if (stack_access_busy()) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_EBUSY;
  }

  portENTER_CRITICAL(&s_state_mux);
  s_connect_requested = false;
  portEXIT_CRITICAL(&s_state_mux);
  esp_err_t esp_status = esp_wifi_disconnect();
  if (esp_status == ESP_ERR_WIFI_NOT_CONNECT) {
    esp_status = ESP_OK;
  }
  if (esp_status != ESP_OK) {
    set_wifi_state(HAL_WIFI_STATE_FAILED);
    hal_mutex_unlock(s_lifecycle_mutex);
    return jh_esp32_status_from_esp_err(esp_status);
  }

  portENTER_CRITICAL(&s_state_mux);
  s_got_ipv4 = false;
  s_wifi_state = HAL_WIFI_STATE_IDLE;
  portEXIT_CRITICAL(&s_state_mux);
  (void)xEventGroupClearBits(s_event_group, kGotIpv4Bit | kConnectionFailedBit);
  const hal_status_t socket_status = shutdown_transport_sockets();
  if (socket_status != HAL_OK) {
    set_wifi_state(HAL_WIFI_STATE_FAILED);
    hal_mutex_unlock(s_lifecycle_mutex);
    return socket_status;
  }
  if (erase_credentials) {
    wifi_config_t empty_config = {};
    esp_status = esp_wifi_set_config(WIFI_IF_STA, &empty_config);
  }
  hal_mutex_unlock(s_lifecycle_mutex);
  return jh_esp32_status_from_esp_err(esp_status);
}

hal_status_t wifi_set_hostname(const char *hostname) {
  if (hostname == nullptr || hostname[0] == '\0') {
    return HAL_EINVAL;
  }
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);

  if (stack_access_busy()) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_EBUSY;
  }
  hal_status_t status = HAL_EUNINIT;
  if (s_station_netif != nullptr) {
    status = jh_esp32_status_from_esp_err(
        esp_netif_set_hostname(s_station_netif, hostname));
    esp_netif_dhcp_status_t dhcp_status = ESP_NETIF_DHCP_INIT;
    if (status == HAL_OK) {
      status = jh_esp32_status_from_esp_err(
          esp_netif_dhcpc_get_status(s_station_netif, &dhcp_status));
    }
    if (status == HAL_OK && dhcp_status == ESP_NETIF_DHCP_STARTED) {
      auto *const netif = static_cast<struct netif *>(
          esp_netif_get_netif_impl(s_station_netif));
      status = netif == nullptr
                   ? HAL_ESTATE
                   : jh_lwip_status_to_hal(netifapi_dhcp_renew(netif));
    }
  }
  hal_mutex_unlock(s_lifecycle_mutex);
  return status;
}

hal_status_t wifi_join(const char *ssid, const char *password,
                       bool non_blocking, uint32_t timeout_ms) {
  if (ssid == nullptr || ssid[0] == '\0' || password == nullptr) {
    return HAL_EINVAL;
  }
  const size_t ssid_length = strlen(ssid);
  const size_t password_length = strlen(password);
  wifi_config_t config = {};
  if (ssid_length > sizeof(config.sta.ssid) ||
      password_length > sizeof(config.sta.password)) {
    return HAL_EOVERFLOW;
  }

  hal_status_t status = service_initialize();
  if (status != HAL_OK) {
    return status;
  }
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);

  if (stack_access_busy()) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_EBUSY;
  }

  memcpy(config.sta.ssid, ssid, ssid_length);
  memcpy(config.sta.password, password, password_length);
  config.sta.threshold.authmode = WIFI_AUTH_OPEN;

  portENTER_CRITICAL(&s_state_mux);
  s_connect_requested = false;
  s_got_ipv4 = false;
  s_wifi_state = HAL_WIFI_STATE_CONNECTING;
  portEXIT_CRITICAL(&s_state_mux);
  (void)xEventGroupClearBits(s_event_group, kGotIpv4Bit | kConnectionFailedBit);
  const esp_err_t disconnect_status = esp_wifi_disconnect();
  if (disconnect_status != ESP_OK &&
      disconnect_status != ESP_ERR_WIFI_NOT_CONNECT) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return jh_esp32_status_from_esp_err(disconnect_status);
  }

  esp_err_t esp_status = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (esp_status == ESP_OK) {
    portENTER_CRITICAL(&s_state_mux);
    s_connect_requested = true;
    portEXIT_CRITICAL(&s_state_mux);
    esp_status = esp_wifi_connect();
  }
  if (esp_status != ESP_OK) {
    portENTER_CRITICAL(&s_state_mux);
    s_connect_requested = false;
    s_wifi_state = HAL_WIFI_STATE_FAILED;
    portEXIT_CRITICAL(&s_state_mux);
    hal_mutex_unlock(s_lifecycle_mutex);
    return jh_esp32_status_from_esp_err(esp_status);
  }

  if (non_blocking) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_OK;
  }
  const EventBits_t result =
      xEventGroupWaitBits(s_event_group, kGotIpv4Bit | kConnectionFailedBit,
                          pdFALSE, pdFALSE, timeout_to_ticks(timeout_ms));
  hal_mutex_unlock(s_lifecycle_mutex);
  if ((result & kGotIpv4Bit) != 0u) {
    return HAL_OK;
  }
  if ((result & kConnectionFailedBit) != 0u) {
    hal_wifi_state_t state = HAL_WIFI_STATE_FAILED;
    portENTER_CRITICAL(&s_state_mux);
    state = s_wifi_state;
    portEXIT_CRITICAL(&s_state_mux);
    return state == HAL_WIFI_STATE_AUTH_FAILED ? HAL_EAUTH : HAL_ENOENT;
  }
  return HAL_ETIMEOUT;
}

hal_status_t wifi_get_state(hal_wifi_state_t *out_state) {
  if (out_state == nullptr) {
    return HAL_EINVAL;
  }
  portENTER_CRITICAL(&s_state_mux);
  *out_state = s_wifi_state;
  const bool initialized = s_wifi_driver_initialized;
  portEXIT_CRITICAL(&s_state_mux);
  return initialized ? HAL_OK : HAL_EUNINIT;
}

void fill_ipv4_endpoint(const esp_ip4_addr_t *address,
                        hal_net_endpoint_t *out_address) {
  memset(out_address, 0, sizeof(*out_address));
  out_address->family = HAL_NET_AF_INET;
  out_address->addr_len = HAL_NET_IPV4_ADDR_LEN;
  out_address->addr[0] = esp_ip4_addr1(address);
  out_address->addr[1] = esp_ip4_addr2(address);
  out_address->addr[2] = esp_ip4_addr3(address);
  out_address->addr[3] = esp_ip4_addr4(address);
}

hal_status_t wifi_get_local_address(hal_net_endpoint_t *out_address) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  memset(out_address, 0, sizeof(*out_address));
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);
  esp_netif_ip_info_t info = {};
  const esp_err_t status = s_station_netif == nullptr
                               ? ESP_ERR_INVALID_STATE
                               : esp_netif_get_ip_info(s_station_netif, &info);
  if (status == ESP_OK) {
    fill_ipv4_endpoint(&info.ip, out_address);
  }
  hal_mutex_unlock(s_lifecycle_mutex);
  if (status != ESP_OK) {
    return jh_esp32_status_from_esp_err(status);
  }
  return (out_address->addr[0] | out_address->addr[1] | out_address->addr[2] |
          out_address->addr[3]) != 0u
             ? HAL_OK
             : HAL_ENOENT;
}

hal_status_t wifi_get_dns_address(hal_net_endpoint_t *out_address) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  memset(out_address, 0, sizeof(*out_address));
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);
  esp_netif_dns_info_t dns = {};
  const esp_err_t status =
      s_station_netif == nullptr
          ? ESP_ERR_INVALID_STATE
          : esp_netif_get_dns_info(s_station_netif, ESP_NETIF_DNS_MAIN, &dns);
  if (status == ESP_OK && dns.ip.type == ESP_IPADDR_TYPE_V4) {
    fill_ipv4_endpoint(&dns.ip.u_addr.ip4, out_address);
  }
  hal_mutex_unlock(s_lifecycle_mutex);
  if (status != ESP_OK) {
    return jh_esp32_status_from_esp_err(status);
  }
  if (dns.ip.type != ESP_IPADDR_TYPE_V4) {
    return HAL_EUNSUPPORTED;
  }
  return (out_address->addr[0] | out_address->addr[1] | out_address->addr[2] |
          out_address->addr[3]) != 0u
             ? HAL_OK
             : HAL_ENOENT;
}

hal_status_t wifi_get_mac(uint8_t out_mac[HAL_WIFI_BSSID_LEN]) {
  if (out_mac == nullptr) {
    return HAL_EINVAL;
  }
  LifecycleGuard guard;
  if (guard.status() != HAL_OK) {
    return guard.status();
  }
  return jh_esp32_status_from_esp_err(esp_wifi_get_mac(WIFI_IF_STA, out_mac));
}

hal_status_t wifi_get_rssi(int32_t *out_rssi) {
  if (out_rssi == nullptr) {
    return HAL_EINVAL;
  }
  LifecycleGuard guard;
  if (guard.status() != HAL_OK) {
    return guard.status();
  }
  wifi_ap_record_t info = {};
  const esp_err_t status = esp_wifi_sta_get_ap_info(&info);
  if (status == ESP_OK) {
    *out_rssi = info.rssi;
  }
  return jh_esp32_status_from_esp_err(status);
}

struct PingContext {
  StaticSemaphore_t completion_storage;
  SemaphoreHandle_t completion;
  int result;
};

void ping_success(esp_ping_handle_t ping, void *argument) {
  auto *context = static_cast<PingContext *>(argument);
  uint32_t ttl = 0u;
  if (context != nullptr && esp_ping_get_profile(ping, ESP_PING_PROF_TTL, &ttl,
                                                 sizeof(ttl)) == ESP_OK) {
    context->result =
        ttl > static_cast<uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(ttl);
  }
}

void ping_timeout(esp_ping_handle_t, void *) {}

void ping_end(esp_ping_handle_t, void *argument) {
  auto *context = static_cast<PingContext *>(argument);
  if (context != nullptr) {
    (void)xSemaphoreGive(context->completion);
  }
}

hal_status_t wifi_ping(const hal_net_endpoint_t *remote, uint32_t timeout_ms,
                       int *out_result) {
  if (remote == nullptr || out_result == nullptr || timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  if (remote->family != HAL_NET_AF_INET ||
      remote->addr_len != HAL_NET_IPV4_ADDR_LEN) {
    return HAL_EUNSUPPORTED;
  }
  *out_result = -1;
  LifecycleGuard guard;
  if (guard.status() != HAL_OK) {
    return guard.status();
  }
  const hal_status_t ready = jh_esp32_network_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }

  PingContext context = {};
  context.completion =
      xSemaphoreCreateBinaryStatic(&context.completion_storage);
  context.result = -1;
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.count = 1u;
  config.interval_ms = 0u;
  config.timeout_ms = timeout_ms;
  IP_ADDR4(&config.target_addr, remote->addr[0], remote->addr[1],
           remote->addr[2], remote->addr[3]);
  esp_ping_callbacks_t callbacks = {};
  callbacks.cb_args = &context;
  callbacks.on_ping_success = ping_success;
  callbacks.on_ping_timeout = ping_timeout;
  callbacks.on_ping_end = ping_end;

  esp_ping_handle_t ping = nullptr;
  esp_err_t status = esp_ping_new_session(&config, &callbacks, &ping);
  if (status == ESP_OK) {
    status = esp_ping_start(ping);
  }
  if (status != ESP_OK) {
    if (ping != nullptr) {
      (void)esp_ping_delete_session(ping);
    }
    return jh_esp32_status_from_esp_err(status);
  }

  const uint32_t wait_ms =
      timeout_ms == HAL_NET_TIMEOUT_FOREVER
          ? HAL_NET_TIMEOUT_FOREVER
          : (timeout_ms > UINT32_MAX - 1u - kPingCompletionSlackMs
                 ? UINT32_MAX - 1u
                 : timeout_ms + kPingCompletionSlackMs);
  const bool completed =
      xSemaphoreTake(context.completion, timeout_to_ticks(wait_ms)) == pdTRUE;
  if (!completed) {
    (void)esp_ping_stop(ping);
    (void)xSemaphoreTake(context.completion, portMAX_DELAY);
  }
  status = esp_ping_delete_session(ping);
  if (status != ESP_OK) {
    return jh_esp32_status_from_esp_err(status);
  }
  if (context.result < 0) {
    return HAL_ETIMEOUT;
  }
  *out_result = context.result;
  return HAL_OK;
}

hal_wifi_encryption_t encryption_from_auth(wifi_auth_mode_t auth) {
  switch (auth) {
  case WIFI_AUTH_OPEN:
    return HAL_WIFI_ENC_NONE;
  case WIFI_AUTH_WPA_PSK:
    return HAL_WIFI_ENC_WPA;
  case WIFI_AUTH_WPA2_PSK:
    return HAL_WIFI_ENC_WPA2;
  case WIFI_AUTH_WPA_WPA2_PSK:
  case WIFI_AUTH_WPA2_WPA3_PSK:
  case WIFI_AUTH_WPA3_PSK:
    return HAL_WIFI_ENC_AUTO;
  default:
    return HAL_WIFI_ENC_UNKNOWN;
  }
}

hal_status_t wifi_scan(uint32_t timeout_ms, int *out_count) {
  if (out_count == nullptr || timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  *out_count = 0;
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);
  if (!s_station_started) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_EUNINIT;
  }

  clear_scan_snapshot_locked();
  portENTER_CRITICAL(&s_state_mux);
  s_scan_status = 1u;
  portEXIT_CRITICAL(&s_state_mux);
  (void)xEventGroupClearBits(s_event_group, kScanDoneBit);
  wifi_scan_config_t config = {};
  esp_err_t status = esp_wifi_scan_start(&config, false);
  if (status != ESP_OK) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return jh_esp32_status_from_esp_err(status);
  }

  const EventBits_t bits =
      xEventGroupWaitBits(s_event_group, kScanDoneBit, pdTRUE, pdFALSE,
                          timeout_to_ticks(timeout_ms));
  if ((bits & kScanDoneBit) == 0u) {
    (void)esp_wifi_scan_stop();
    (void)esp_wifi_clear_ap_list();
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_ETIMEOUT;
  }
  portENTER_CRITICAL(&s_state_mux);
  const uint32_t scan_status = s_scan_status;
  portEXIT_CRITICAL(&s_state_mux);
  if (scan_status != 0u) {
    (void)esp_wifi_clear_ap_list();
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_EIO;
  }

  uint16_t count = 0u;
  status = esp_wifi_scan_get_ap_num(&count);
  wifi_ap_record_t *records = nullptr;
  hal_wifi_scan_result_t *snapshot = nullptr;
  if (status == ESP_OK && count > 0u) {
    records = new (std::nothrow) wifi_ap_record_t[count]();
    snapshot = new (std::nothrow) hal_wifi_scan_result_t[count]();
    if (records == nullptr || snapshot == nullptr) {
      delete[] records;
      delete[] snapshot;
      (void)esp_wifi_clear_ap_list();
      hal_mutex_unlock(s_lifecycle_mutex);
      return HAL_ENOMEM;
    }
    status = esp_wifi_scan_get_ap_records(&count, records);
  }
  if (status == ESP_OK) {
    for (size_t index = 0u; index < count; ++index) {
      const size_t ssid_length =
          strnlen(reinterpret_cast<const char *>(records[index].ssid),
                  HAL_WIFI_SSID_MAX_LEN - 1u);
      memcpy(snapshot[index].ssid, records[index].ssid, ssid_length);
      snapshot[index].ssid[ssid_length] = '\0';
      memcpy(snapshot[index].bssid, records[index].bssid, HAL_WIFI_BSSID_LEN);
      snapshot[index].encryption =
          encryption_from_auth(records[index].authmode);
      snapshot[index].rssi = records[index].rssi;
      snapshot[index].channel = records[index].primary;
    }
    s_scan_results = snapshot;
    s_scan_count = count;
    *out_count = static_cast<int>(count);
    snapshot = nullptr;
  }
  delete[] records;
  delete[] snapshot;
  if (status != ESP_OK) {
    (void)esp_wifi_clear_ap_list();
  }
  hal_mutex_unlock(s_lifecycle_mutex);
  return jh_esp32_status_from_esp_err(status);
}

hal_status_t wifi_get_scan_result(size_t index,
                                  hal_wifi_scan_result_t *out_result) {
  if (out_result == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);
  const hal_status_t status = index < s_scan_count ? HAL_OK : HAL_ENOENT;
  if (status == HAL_OK) {
    *out_result = s_scan_results[index];
  }
  hal_mutex_unlock(s_lifecycle_mutex);
  return status;
}

hal_status_t endpoint_from_sockaddr(const sockaddr *address,
                                    socklen_t address_length,
                                    hal_net_endpoint_t *out_endpoint) {
  if (address == nullptr || out_endpoint == nullptr) {
    return HAL_EINVAL;
  }
  memset(out_endpoint, 0, sizeof(*out_endpoint));
  if (address->sa_family == AF_INET &&
      address_length >= static_cast<socklen_t>(sizeof(sockaddr_in))) {
    const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
    out_endpoint->family = HAL_NET_AF_INET;
    out_endpoint->addr_len = HAL_NET_IPV4_ADDR_LEN;
    memcpy(out_endpoint->addr, &ipv4->sin_addr.s_addr, HAL_NET_IPV4_ADDR_LEN);
    out_endpoint->port = ntohs(ipv4->sin_port);
    return HAL_OK;
  }
#if LWIP_IPV6
  if (address->sa_family == AF_INET6 &&
      address_length >= static_cast<socklen_t>(sizeof(sockaddr_in6))) {
    const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(address);
    out_endpoint->family = HAL_NET_AF_INET6;
    out_endpoint->addr_len = HAL_NET_IPV6_ADDR_LEN;
    memcpy(out_endpoint->addr, ipv6->sin6_addr.s6_addr, HAL_NET_IPV6_ADDR_LEN);
    out_endpoint->port = ntohs(ipv6->sin6_port);
    out_endpoint->scope_id = ipv6->sin6_scope_id;
    return HAL_OK;
  }
#endif
  return HAL_EUNSUPPORTED;
}

hal_status_t resolver_resolve(const char *hostname,
                              hal_net_family_t family_hint,
                              hal_net_endpoint_t *results, size_t capacity,
                              size_t *out_count) {
  if (hostname == nullptr || hostname[0] == '\0' || out_count == nullptr ||
      (capacity > 0u && results == nullptr)) {
    return HAL_EINVAL;
  }
  if (family_hint != HAL_NET_AF_UNSPEC && family_hint != HAL_NET_AF_INET &&
      family_hint != HAL_NET_AF_INET6) {
    return HAL_EINVAL;
  }
#if !LWIP_IPV6
  if (family_hint == HAL_NET_AF_INET6) {
    return HAL_EUNSUPPORTED;
  }
#endif
  *out_count = 0u;
  LifecycleGuard guard;
  if (guard.status() != HAL_OK) {
    return guard.status();
  }

  bool ipv4_ready = false;
  portENTER_CRITICAL(&s_state_mux);
  ipv4_ready = s_got_ipv4;
  portEXIT_CRITICAL(&s_state_mux);
  if (!ipv4_ready) {
    return HAL_EUNINIT;
  }

  addrinfo hints = {};
  hints.ai_family = family_hint == HAL_NET_AF_INET    ? AF_INET
                    : family_hint == HAL_NET_AF_INET6 ? AF_INET6
                                                      : AF_UNSPEC;
  addrinfo *addresses = nullptr;
  const int resolve_status =
      lwip_getaddrinfo(hostname, nullptr, &hints, &addresses);
  if (resolve_status != 0) {
    switch (resolve_status) {
    case EAI_AGAIN:
      return HAL_EAGAIN;
    case EAI_MEMORY:
      return HAL_ENOMEM;
    case EAI_NONAME:
      return HAL_ENOENT;
    case EAI_FAMILY:
      return HAL_EUNSUPPORTED;
    default:
      return HAL_EIO;
    }
  }

  size_t count = 0u;
  for (const addrinfo *current = addresses; current != nullptr;
       current = current->ai_next) {
    if (current->ai_family == AF_INET
#if LWIP_IPV6
        || current->ai_family == AF_INET6
#endif
    ) {
      ++count;
    }
  }
  *out_count = count;
  if (count > capacity) {
    lwip_freeaddrinfo(addresses);
    return HAL_EOVERFLOW;
  }

  size_t index = 0u;
  hal_status_t status = HAL_OK;
  for (const addrinfo *current = addresses; current != nullptr && index < count;
       current = current->ai_next) {
    if (current->ai_family != AF_INET
#if LWIP_IPV6
        && current->ai_family != AF_INET6
#endif
    ) {
      continue;
    }
    status = endpoint_from_sockaddr(current->ai_addr, current->ai_addrlen,
                                    &results[index]);
    if (status != HAL_OK) {
      break;
    }
    results[index].port = 0u;
    ++index;
  }
  lwip_freeaddrinfo(addresses);
  return status;
}

const jh_network_service_ops_t s_service_ops = {
    service_initialize,  service_deinitialize, service_service,
    service_stack_enter, service_stack_leave,
};

const jh_network_wifi_ops_t s_wifi_ops = {
    wifi_set_mode,
    wifi_disconnect,
    wifi_set_hostname,
    wifi_join,
    wifi_get_state,
    wifi_get_local_address,
    wifi_get_dns_address,
    wifi_get_mac,
    wifi_get_rssi,
    wifi_ping,
    wifi_scan,
    wifi_get_scan_result,
};

const jh_network_resolver_ops_t s_resolver_ops = {resolver_resolve};

} // namespace

extern "C" hal_status_t jh_esp32_network_require_ready(void) {
  portENTER_CRITICAL(&s_state_mux);
  const bool initialized = s_wifi_driver_initialized && s_station_started &&
                           s_station_netif != nullptr && !s_stopping;
  portEXIT_CRITICAL(&s_state_mux);
  return initialized ? HAL_OK : HAL_EUNINIT;
}

extern "C" hal_status_t jh_esp32_network_stack_enter(bool require_ipv4) {
#if !LWIP_TCPIP_CORE_LOCKING
  (void)require_ipv4;
  return HAL_EUNSUPPORTED;
#else
  const hal_status_t primitive_status = ensure_primitives();
  if (primitive_status != HAL_OK) {
    return primitive_status;
  }
  hal_mutex_lock(s_lifecycle_mutex);
  const TaskHandle_t current = xTaskGetCurrentTaskHandle();
  if (current == nullptr) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_ESTATE;
  }

  portENTER_CRITICAL(&s_state_mux);
  const bool ready = s_wifi_driver_initialized && s_station_started &&
                     s_station_netif != nullptr && !s_stopping &&
                     (!require_ipv4 || s_got_ipv4);
  if (!ready) {
    portEXIT_CRITICAL(&s_state_mux);
    hal_mutex_unlock(s_lifecycle_mutex);
    return require_ipv4 ? HAL_ESTATE : HAL_EUNINIT;
  }
  if (s_stack_owner == current) {
    if (s_stack_depth == UINT32_MAX) {
      portEXIT_CRITICAL(&s_state_mux);
      hal_mutex_unlock(s_lifecycle_mutex);
      return HAL_EOVERFLOW;
    }
    ++s_stack_depth;
    portEXIT_CRITICAL(&s_state_mux);
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_OK;
  }
  if (s_stack_acquirers == UINT32_MAX) {
    portEXIT_CRITICAL(&s_state_mux);
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_EOVERFLOW;
  }
  ++s_stack_acquirers;
  portEXIT_CRITICAL(&s_state_mux);
  hal_mutex_unlock(s_lifecycle_mutex);

  LOCK_TCPIP_CORE();
  portENTER_CRITICAL(&s_state_mux);
  const bool still_ready = s_wifi_driver_initialized && s_station_started &&
                           s_station_netif != nullptr && !s_stopping &&
                           (!require_ipv4 || s_got_ipv4);
  if (!still_ready || s_stack_owner != nullptr) {
    portEXIT_CRITICAL(&s_state_mux);
    UNLOCK_TCPIP_CORE();
    portENTER_CRITICAL(&s_state_mux);
    --s_stack_acquirers;
    portEXIT_CRITICAL(&s_state_mux);
    return still_ready ? HAL_EBUSY : HAL_ESTATE;
  }
  --s_stack_acquirers;
  s_stack_owner = current;
  s_stack_depth = 1u;
  portEXIT_CRITICAL(&s_state_mux);
  return HAL_OK;
#endif
}

extern "C" void jh_esp32_network_stack_leave(void) {
#if LWIP_TCPIP_CORE_LOCKING
  if (ensure_primitives() != HAL_OK) {
    return;
  }
  hal_mutex_lock(s_lifecycle_mutex);
  const TaskHandle_t current = xTaskGetCurrentTaskHandle();
  bool unlock = false;
  portENTER_CRITICAL(&s_state_mux);
  if (current != nullptr && s_stack_owner == current && s_stack_depth > 0u) {
    --s_stack_depth;
    if (s_stack_depth == 0u) {
      s_stack_owner = nullptr;
      unlock = true;
    }
  }
  portEXIT_CRITICAL(&s_state_mux);
  if (unlock) {
    UNLOCK_TCPIP_CORE();
  }
  hal_mutex_unlock(s_lifecycle_mutex);
#endif
}

extern "C" hal_status_t jh_esp32_network_underlay_netif(void **out_netif) {
  if (out_netif == nullptr) {
    return HAL_EINVAL;
  }
  *out_netif = nullptr;
#if !LWIP_TCPIP_CORE_LOCKING
  return HAL_EUNSUPPORTED;
#else
  portENTER_CRITICAL(&s_state_mux);
  esp_netif_t *const station = s_station_netif;
  const bool protected_by_guard =
      s_stack_owner == xTaskGetCurrentTaskHandle() && s_stack_depth > 0u;
  portEXIT_CRITICAL(&s_state_mux);
  if (station == nullptr) {
    return HAL_EUNINIT;
  }
  if (!protected_by_guard) {
    return HAL_ESTATE;
  }
  *out_netif = esp_netif_get_netif_impl(station);
  return *out_netif != nullptr ? HAL_OK : HAL_ESTATE;
#endif
}

extern "C" const jh_network_backend_descriptor_t *
jh_network_backend_selected(void) {
  constexpr jh_network_capabilities_t kCapabilities =
      JH_NET_CAP_WIFI_STA | JH_NET_CAP_WIFI_SCAN | JH_NET_CAP_DNS |
      JH_NET_CAP_PING | JH_NET_CAP_IPV4 |
#if defined(HAL_ENABLE_TCP)
      JH_NET_CAP_TCP_CLIENT | JH_NET_CAP_TCP_LISTENER |
#endif
#if defined(HAL_ENABLE_UDP)
      JH_NET_CAP_UDP |
#endif
#if LWIP_IPV6
      JH_NET_CAP_IPV6 |
#endif
#if LWIP_TCPIP_CORE_LOCKING
      JH_NET_CAP_HOST_STACK_L3 | JH_NET_CAP_VIRTUAL_NETIF_ROUTE |
      JH_NET_CAP_STACK_CONTEXT |
#endif
      JH_NET_CAP_SECURE_ENTROPY;
  static const jh_network_backend_descriptor_t backend = {
      JH_NETWORK_BACKEND_ABI_VERSION,
      "esp-idf-native",
      kCapabilities,
      JH_NETWORK_EXECUTION_PLATFORM_OWNED,
      &s_service_ops,
      &s_wifi_ops,
      &s_resolver_ops,
#if defined(HAL_ENABLE_TCP)
      jh_esp32_network_tcp_ops(),
#else
      nullptr,
#endif
#if defined(HAL_ENABLE_UDP)
      jh_esp32_network_udp_ops(),
#else
      nullptr,
#endif
  };
  return &backend;
}

#endif // HAL_ENABLE_NETWORK_CORE && HAL_NETWORK_BACKEND_ESP_IDF
#endif // HAL_TARGET_IS_ESP32_FAMILY
