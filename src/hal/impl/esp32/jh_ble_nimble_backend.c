#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32_S3 && defined(HAL_ENABLE_BLE)

#ifdef HAL_ENABLE_BLE_STREAM
#error "The ESP32-S3 NimBLE backend does not yet provide HAL_ENABLE_BLE_STREAM"
#endif

#include "hal/bluetooth/jh_ble_backend.h"
#include "hal/impl/esp32/jh_esp32_nvs_runtime.h"

#include <freertos/FreeRTOS.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
#include <host/util/util.h>
#include <nimble/ble.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>

#include <string.h>

typedef struct {
  jh_ble_backend_event_fn event_handler;
  void *event_context;
  uint16_t connection;
  uint8_t own_address_type;
  bool running;
  bool ready;
  bool advertising;
  bool scanning;
  bool stopping;
} jh_esp32_nimble_backend_t;

static jh_esp32_nimble_backend_t s_backend;
static portMUX_TYPE s_backend_mux = portMUX_INITIALIZER_UNLOCKED;

static hal_status_t status_from_nimble(int status) {
  switch (status) {
  case 0:
    return HAL_OK;
  case BLE_HS_EINVAL:
    return HAL_EINVAL;
  case BLE_HS_ENOMEM:
    return HAL_ENOMEM;
  case BLE_HS_EBUSY:
    return HAL_EBUSY;
  case BLE_HS_EALREADY:
    return HAL_ESTATE;
  case BLE_HS_ENOTCONN:
    return HAL_ENOENT;
  case BLE_HS_ETIMEOUT:
    return HAL_ETIMEOUT;
  default:
    return HAL_EIO;
  }
}

static hal_ble_address_type_t address_type(uint8_t type) {
  switch (type) {
  case BLE_ADDR_PUBLIC:
    return HAL_BLE_ADDRESS_PUBLIC;
  case BLE_ADDR_RANDOM:
    return HAL_BLE_ADDRESS_RANDOM;
  case BLE_ADDR_PUBLIC_ID:
    return HAL_BLE_ADDRESS_PUBLIC_IDENTITY;
  case BLE_ADDR_RANDOM_ID:
    return HAL_BLE_ADDRESS_RANDOM_IDENTITY;
  default:
    return HAL_BLE_ADDRESS_UNKNOWN;
  }
}

static hal_ble_address_t address_from_nimble(const ble_addr_t *address) {
  hal_ble_address_t result;
  memset(&result, 0, sizeof(result));
  if (address != NULL) {
    memcpy(result.bytes, address->val, sizeof(result.bytes));
    result.type = address_type(address->type);
  } else {
    result.type = HAL_BLE_ADDRESS_UNKNOWN;
  }
  return result;
}

static void emit_event(const jh_ble_backend_event_t *event) {
  jh_ble_backend_event_fn handler = NULL;
  void *context = NULL;
  portENTER_CRITICAL(&s_backend_mux);
  if (s_backend.running) {
    handler = s_backend.event_handler;
    context = s_backend.event_context;
  }
  portEXIT_CRITICAL(&s_backend_mux);
  if (handler != NULL) {
    handler(context, event);
  }
}

static void emit_simple(jh_ble_backend_event_type_t type) {
  jh_ble_backend_event_t event;
  memset(&event, 0, sizeof(event));
  event.type = type;
  event.status = HAL_OK;
  emit_event(&event);
}

static hal_ble_advertising_event_type_t advertising_event_type(uint8_t type) {
  switch (type) {
  case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:
    return HAL_BLE_ADV_EVENT_CONNECTABLE_UNDIRECTED;
  case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:
    return HAL_BLE_ADV_EVENT_CONNECTABLE_DIRECTED;
  case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:
    return HAL_BLE_ADV_EVENT_SCANNABLE_UNDIRECTED;
  case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND:
    return HAL_BLE_ADV_EVENT_NON_CONNECTABLE_UNDIRECTED;
  case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:
    return HAL_BLE_ADV_EVENT_SCAN_RESPONSE;
  default:
    return HAL_BLE_ADV_EVENT_UNKNOWN;
  }
}

static int gap_event(struct ble_gap_event *gap, void *argument) {
  (void)argument;
  jh_ble_backend_event_t event;
  memset(&event, 0, sizeof(event));
  event.status = HAL_OK;
  switch (gap->type) {
  case BLE_GAP_EVENT_CONNECT: {
    if (gap->connect.status != 0) {
      event.type = JH_BLE_BACKEND_EVENT_ERROR;
      event.status = status_from_nimble(gap->connect.status);
      emit_event(&event);
      return 0;
    }
    struct ble_gap_conn_desc description;
    if (ble_gap_conn_find(gap->connect.conn_handle, &description) != 0) {
      event.type = JH_BLE_BACKEND_EVENT_ERROR;
      event.status = HAL_EIO;
      emit_event(&event);
      return 0;
    }
    portENTER_CRITICAL(&s_backend_mux);
    s_backend.connection = gap->connect.conn_handle;
    s_backend.advertising = false;
    portEXIT_CRITICAL(&s_backend_mux);
    event.type = JH_BLE_BACKEND_EVENT_CONNECTED;
    event.native_connection = gap->connect.conn_handle;
    event.address = address_from_nimble(&description.peer_id_addr);
    emit_event(&event);
    return 0;
  }
  case BLE_GAP_EVENT_DISCONNECT:
    portENTER_CRITICAL(&s_backend_mux);
    s_backend.connection = BLE_HS_CONN_HANDLE_NONE;
    portEXIT_CRITICAL(&s_backend_mux);
    event.type = JH_BLE_BACKEND_EVENT_DISCONNECTED;
    event.native_connection = gap->disconnect.conn.conn_handle;
    event.disconnect_reason = (uint8_t)gap->disconnect.reason;
    emit_event(&event);
    return 0;
  case BLE_GAP_EVENT_ADV_COMPLETE: {
    bool notify = false;
    portENTER_CRITICAL(&s_backend_mux);
    notify = s_backend.advertising && !s_backend.stopping;
    s_backend.advertising = false;
    portEXIT_CRITICAL(&s_backend_mux);
    if (notify) {
      emit_simple(JH_BLE_BACKEND_EVENT_ADVERTISING_STOPPED);
    }
    return 0;
  }
  case BLE_GAP_EVENT_MTU:
    event.type = JH_BLE_BACKEND_EVENT_MTU_UPDATED;
    event.native_connection = gap->mtu.conn_handle;
    event.mtu = gap->mtu.value;
    emit_event(&event);
    return 0;
  case BLE_GAP_EVENT_DISC:
    event.type = JH_BLE_BACKEND_EVENT_ADVERTISING_REPORT;
    event.advertising_report.address = address_from_nimble(&gap->disc.addr);
    event.advertising_report.event_type =
        advertising_event_type(gap->disc.event_type);
    event.advertising_report.rssi = gap->disc.rssi;
    event.advertising_report.data_length =
        gap->disc.length_data > HAL_BLE_LEGACY_ADV_MAX_DATA_LEN
            ? HAL_BLE_LEGACY_ADV_MAX_DATA_LEN
            : gap->disc.length_data;
    memcpy(event.advertising_report.data, gap->disc.data,
           event.advertising_report.data_length);
    emit_event(&event);
    return 0;
  case BLE_GAP_EVENT_DISC_COMPLETE: {
    bool notify = false;
    portENTER_CRITICAL(&s_backend_mux);
    notify = s_backend.scanning && !s_backend.stopping;
    s_backend.scanning = false;
    portEXIT_CRITICAL(&s_backend_mux);
    if (notify) {
      emit_simple(JH_BLE_BACKEND_EVENT_SCAN_STOPPED);
    }
    return 0;
  }
  default:
    return 0;
  }
}

static void host_reset(int reason) {
  jh_ble_backend_event_t event;
  memset(&event, 0, sizeof(event));
  event.type = JH_BLE_BACKEND_EVENT_ERROR;
  event.status = status_from_nimble(reason);
  event.fatal = true;
  emit_event(&event);
}

static void host_sync(void) {
  uint8_t own_type = 0u;
  uint8_t bytes[HAL_BLE_ADDRESS_LEN] = {0u};
  int status = ble_hs_util_ensure_addr(0);
  if (status == 0) {
    status = ble_hs_id_infer_auto(0, &own_type);
  }
  if (status == 0) {
    status = ble_hs_id_copy_addr(own_type, bytes, NULL);
  }
  if (status != 0) {
    host_reset(status);
    return;
  }
  portENTER_CRITICAL(&s_backend_mux);
  s_backend.own_address_type = own_type;
  s_backend.ready = true;
  portEXIT_CRITICAL(&s_backend_mux);
  jh_ble_backend_event_t event;
  memset(&event, 0, sizeof(event));
  event.type = JH_BLE_BACKEND_EVENT_READY;
  event.status = HAL_OK;
  memcpy(event.address.bytes, bytes, sizeof(bytes));
  event.address.type = address_type(own_type);
  emit_event(&event);
}

static void host_task(void *argument) {
  (void)argument;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static hal_status_t backend_start(void *context,
                                  jh_ble_backend_event_fn event_handler,
                                  void *event_context) {
  (void)context;
  if (event_handler == NULL) {
    return HAL_EINVAL;
  }
  const hal_status_t nvs_status = jh_esp32_nvs_initialize();
  if (nvs_status != HAL_OK) {
    return nvs_status;
  }
  portENTER_CRITICAL(&s_backend_mux);
  if (s_backend.running) {
    portEXIT_CRITICAL(&s_backend_mux);
    return HAL_EBUSY;
  }
  memset(&s_backend, 0, sizeof(s_backend));
  s_backend.event_handler = event_handler;
  s_backend.event_context = event_context;
  s_backend.connection = BLE_HS_CONN_HANDLE_NONE;
  s_backend.running = true;
  portEXIT_CRITICAL(&s_backend_mux);

  const esp_err_t init_status = nimble_port_init();
  if (init_status != ESP_OK) {
    portENTER_CRITICAL(&s_backend_mux);
    memset(&s_backend, 0, sizeof(s_backend));
    portEXIT_CRITICAL(&s_backend_mux);
    return HAL_EHW;
  }
  ble_hs_cfg.reset_cb = host_reset;
  ble_hs_cfg.sync_cb = host_sync;
  nimble_port_freertos_init(host_task);
  return HAL_OK;
}

static hal_status_t backend_stop(void *context) {
  (void)context;
  portENTER_CRITICAL(&s_backend_mux);
  if (!s_backend.running) {
    portEXIT_CRITICAL(&s_backend_mux);
    return HAL_OK;
  }
  s_backend.stopping = true;
  const bool advertising = s_backend.advertising;
  const bool scanning = s_backend.scanning;
  const uint16_t connection = s_backend.connection;
  s_backend.advertising = false;
  s_backend.scanning = false;
  portEXIT_CRITICAL(&s_backend_mux);
  if (advertising) {
    (void)ble_gap_adv_stop();
  }
  if (scanning) {
    (void)ble_gap_disc_cancel();
  }
  if (connection != BLE_HS_CONN_HANDLE_NONE) {
    (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
  }
  int status = nimble_port_stop();
  if (status == 0) {
    const esp_err_t deinit_status = nimble_port_deinit();
    if (deinit_status != ESP_OK) {
      status = BLE_HS_EUNKNOWN;
    }
  }
  portENTER_CRITICAL(&s_backend_mux);
  memset(&s_backend, 0, sizeof(s_backend));
  portEXIT_CRITICAL(&s_backend_mux);
  return status_from_nimble(status);
}

static hal_status_t backend_service(void *context) {
  (void)context;
  portENTER_CRITICAL(&s_backend_mux);
  const bool running = s_backend.running;
  portEXIT_CRITICAL(&s_backend_mux);
  return running ? HAL_OK : HAL_EUNINIT;
}

static hal_status_t
backend_advertising_start(void *context,
                          const hal_ble_advertising_config_t *config) {
  (void)context;
  int status = ble_gap_adv_set_data(config->data, config->data_length);
  if (status != 0) {
    return status_from_nimble(status);
  }
  struct ble_gap_adv_params parameters;
  memset(&parameters, 0, sizeof(parameters));
  parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
  parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
  parameters.itvl_min = config->interval_min;
  parameters.itvl_max = config->interval_max;
  status = ble_gap_adv_start(s_backend.own_address_type, NULL, BLE_HS_FOREVER,
                             &parameters, gap_event, NULL);
  if (status != 0) {
    return status_from_nimble(status);
  }
  portENTER_CRITICAL(&s_backend_mux);
  s_backend.advertising = true;
  portEXIT_CRITICAL(&s_backend_mux);
  emit_simple(JH_BLE_BACKEND_EVENT_ADVERTISING_STARTED);
  return HAL_OK;
}

static hal_status_t backend_advertising_stop(void *context) {
  (void)context;
  portENTER_CRITICAL(&s_backend_mux);
  const bool active = s_backend.advertising;
  s_backend.advertising = false;
  portEXIT_CRITICAL(&s_backend_mux);
  if (!active) {
    return HAL_ESTATE;
  }
  const int status = ble_gap_adv_stop();
  if (status != 0 && status != BLE_HS_EALREADY) {
    return status_from_nimble(status);
  }
  emit_simple(JH_BLE_BACKEND_EVENT_ADVERTISING_STOPPED);
  return HAL_OK;
}

static hal_status_t backend_disconnect(void *context,
                                       uint16_t native_connection) {
  (void)context;
  return status_from_nimble(
      ble_gap_terminate(native_connection, BLE_ERR_REM_USER_CONN_TERM));
}

static hal_status_t backend_scan_start(void *context,
                                       const hal_ble_scan_config_t *config) {
  (void)context;
  struct ble_gap_disc_params parameters;
  memset(&parameters, 0, sizeof(parameters));
  parameters.itvl = config->interval;
  parameters.window = config->window;
  parameters.passive = 1u;
  parameters.filter_duplicates = config->filter_duplicates ? 1u : 0u;
  const int status = ble_gap_disc(s_backend.own_address_type, BLE_HS_FOREVER,
                                  &parameters, gap_event, NULL);
  if (status != 0) {
    return status_from_nimble(status);
  }
  portENTER_CRITICAL(&s_backend_mux);
  s_backend.scanning = true;
  portEXIT_CRITICAL(&s_backend_mux);
  emit_simple(JH_BLE_BACKEND_EVENT_SCAN_STARTED);
  return HAL_OK;
}

static hal_status_t backend_scan_stop(void *context) {
  (void)context;
  portENTER_CRITICAL(&s_backend_mux);
  const bool active = s_backend.scanning;
  s_backend.scanning = false;
  portEXIT_CRITICAL(&s_backend_mux);
  if (!active) {
    return HAL_ESTATE;
  }
  const int status = ble_gap_disc_cancel();
  if (status != 0 && status != BLE_HS_EALREADY) {
    return status_from_nimble(status);
  }
  emit_simple(JH_BLE_BACKEND_EVENT_SCAN_STOPPED);
  return HAL_OK;
}

static const jh_ble_backend_t s_interface = {
    .context = &s_backend,
    .start = backend_start,
    .stop = backend_stop,
    .service = backend_service,
    .advertising_start = backend_advertising_start,
    .advertising_stop = backend_advertising_stop,
    .disconnect = backend_disconnect,
    .scan_start = backend_scan_start,
    .scan_stop = backend_scan_stop,
};

const jh_ble_backend_t *jh_ble_backend_instance(void) { return &s_interface; }

#endif /* HAL_TARGET_IS_ESP32_S3 && HAL_ENABLE_BLE */
