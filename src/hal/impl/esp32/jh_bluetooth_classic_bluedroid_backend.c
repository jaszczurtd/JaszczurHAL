#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32 && defined(HAL_ENABLE_BLUETOOTH_CLASSIC)

#include "hal/bluetooth/jh_bluetooth_classic_address.h"
#include "hal/bluetooth/jh_bluetooth_classic_backend.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/impl/esp32/jh_esp32_nvs_runtime.h"
#include "hal/impl/esp32/jh_esp32_status.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include <esp_bt.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
#include <esp_hidh.h>
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  JH_CLASSIC_HID_EVENT_STACK_SIZE = 4096u,
};

typedef struct {
  hal_mutex_t mutex;
  jh_bluetooth_classic_backend_event_fn event_handler;
  void *event_context;
  hal_bluetooth_classic_address_t pairing_address;
  hal_bluetooth_classic_pairing_method_t pairing_method;
  hal_bluetooth_classic_scan_result_t scan_cache;
  uint32_t scan_deadline_ms;
  bool stack_initialized;
  bool started;
  bool scan_active;
  bool pairing_pending;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  esp_hidh_dev_t *hid_device;
  hal_bluetooth_classic_address_t hid_address;
#endif
} jh_classic_bluedroid_t;

static jh_classic_bluedroid_t s_backend;

static hal_mutex_t backend_mutex(void) {
  return jh_hal_mutex_create_once(&s_backend.mutex);
}

static bool backend_lock(void) {
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return false;
  }
  hal_mutex_lock(mutex);
  return true;
}

static void backend_unlock(void) { hal_mutex_unlock(s_backend.mutex); }

static hal_status_t status_from_esp(esp_err_t status) {
  return jh_esp32_status_from_esp_err_with_fallback(status, HAL_EIO);
}

static void emit(const jh_bluetooth_classic_backend_event_t *event) {
  if (event == NULL) {
    return;
  }
  jh_bluetooth_classic_backend_event_fn handler = NULL;
  void *context = NULL;
  if (backend_lock()) {
    if (s_backend.started) {
      handler = s_backend.event_handler;
      context = s_backend.event_context;
    }
    backend_unlock();
  }
  if (handler != NULL) {
    handler(context, event);
  }
}

static void emit_error(hal_status_t status, bool fatal) {
  jh_bluetooth_classic_backend_event_t event = {0};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_ERROR;
  event.status = status;
  event.fatal = fatal;
  emit(&event);
}

static size_t
discovery_properties(const esp_bt_gap_cb_param_t *parameter,
                     hal_bluetooth_classic_scan_result_t *result) {
  if (parameter == NULL || result == NULL) {
    return 0u;
  }
  uint8_t *eir = NULL;
  size_t name_length = 0u;
  memcpy(result->address.bytes, parameter->disc_res.bda,
         sizeof(result->address.bytes));
  for (int index = 0; index < parameter->disc_res.num_prop; ++index) {
    const esp_bt_gap_dev_prop_t *property = &parameter->disc_res.prop[index];
    if (property->val == NULL || property->len <= 0) {
      continue;
    }
    if (property->type == ESP_BT_GAP_DEV_PROP_COD &&
        property->len >= (int)sizeof(result->class_of_device)) {
      memcpy(&result->class_of_device, property->val,
             sizeof(result->class_of_device));
    } else if (property->type == ESP_BT_GAP_DEV_PROP_RSSI &&
               property->len >= (int)sizeof(result->rssi)) {
      memcpy(&result->rssi, property->val, sizeof(result->rssi));
      result->rssi_valid = true;
    } else if (property->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
      name_length = (size_t)property->len;
      const size_t copied = name_length <= HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN
                                ? name_length
                                : HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN;
      memcpy(result->name, property->val, copied);
      result->name[copied] = '\0';
      result->name_length = (uint8_t)copied;
    } else if (property->type == ESP_BT_GAP_DEV_PROP_EIR) {
      eir = (uint8_t *)property->val;
    }
  }
  if (name_length == 0u && eir != NULL) {
    uint8_t resolved_length = 0u;
    uint8_t *resolved = esp_bt_gap_resolve_eir_data(
        eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &resolved_length);
    if (resolved == NULL) {
      resolved = esp_bt_gap_resolve_eir_data(
          eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &resolved_length);
    }
    if (resolved != NULL) {
      const size_t copied =
          resolved_length <= HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN
              ? resolved_length
              : HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN;
      memcpy(result->name, resolved, copied);
      result->name[copied] = '\0';
      result->name_length = (uint8_t)copied;
      name_length = copied;
    }
  }
  return name_length;
}

static uint32_t service_bit(const esp_bt_uuid_t *uuid) {
  if (uuid == NULL || uuid->len != ESP_UUID_LEN_16) {
    return 0u;
  }
  switch (uuid->uuid.uuid16) {
  case 0x1124u:
    return HAL_BLUETOOTH_CLASSIC_SERVICE_HID;
  case 0x1200u:
    return HAL_BLUETOOTH_CLASSIC_SERVICE_PNP;
  case 0x1101u:
    return HAL_BLUETOOTH_CLASSIC_SERVICE_SERIAL_PORT;
  case 0x110au:
    return HAL_BLUETOOTH_CLASSIC_SERVICE_AUDIO_SOURCE;
  case 0x110bu:
    return HAL_BLUETOOTH_CLASSIC_SERVICE_AUDIO_SINK;
  default:
    return 0u;
  }
}

static bool
pairing_address_allowed(const hal_bluetooth_classic_address_t *address) {
  if (jh_bluetooth_classic_address_equal(address, &s_backend.pairing_address)) {
    return true;
  }
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  return jh_bluetooth_classic_address_equal(address, &s_backend.hid_address);
#else
  return false;
#endif
}

static void pairing_request(const uint8_t *address,
                            hal_bluetooth_classic_pairing_method_t method) {
  hal_bluetooth_classic_address_t peer = {0};
  memcpy(peer.bytes, address, sizeof(peer.bytes));
  if (!pairing_address_allowed(&peer) || !backend_lock()) {
    return;
  }
  s_backend.pairing_address = peer;
  s_backend.pairing_method = method;
  s_backend.pairing_pending = true;
  backend_unlock();
  jh_bluetooth_classic_backend_event_t event = {0};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_PAIRING_REQUEST;
  event.status = HAL_OK;
  event.address = peer;
  event.pairing_method = method;
  emit(&event);
}

static void gap_event_handler(esp_bt_gap_cb_event_t event,
                              esp_bt_gap_cb_param_t *parameter) {
  if (parameter == NULL) {
    return;
  }
  switch (event) {
  case ESP_BT_GAP_DISC_RES_EVT: {
    jh_bluetooth_classic_backend_event_t output = {0};
    output.type = JH_BLUETOOTH_CLASSIC_EVENT_SCAN_RESULT;
    output.status = HAL_OK;
    (void)discovery_properties(parameter, &output.scan_result);
    output.address = output.scan_result.address;
    if (backend_lock()) {
      s_backend.scan_cache = output.scan_result;
      backend_unlock();
    }
    emit(&output);
    break;
  }
  case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
    if (parameter->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
      bool repeat = false;
      if (backend_lock()) {
        repeat = s_backend.scan_active &&
                 (int32_t)(hal_millis() - s_backend.scan_deadline_ms) < 0;
        if (!repeat) {
          s_backend.scan_active = false;
        }
        backend_unlock();
      }
      if (repeat) {
        (void)esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                         ESP_BT_GAP_MAX_INQ_LEN, 0u);
      } else {
        jh_bluetooth_classic_backend_event_t output = {0};
        output.type = JH_BLUETOOTH_CLASSIC_EVENT_SCAN_STOPPED;
        output.status = HAL_OK;
        emit(&output);
      }
    }
    break;
  case ESP_BT_GAP_RMT_SRVCS_EVT: {
    jh_bluetooth_classic_backend_event_t output = {0};
    output.type = JH_BLUETOOTH_CLASSIC_EVENT_SCAN_RESULT;
    output.status =
        parameter->rmt_srvcs.stat == ESP_BT_STATUS_SUCCESS ? HAL_OK : HAL_EIO;
    if (backend_lock()) {
      output.scan_result = s_backend.scan_cache;
      backend_unlock();
    }
    memcpy(output.scan_result.address.bytes, parameter->rmt_srvcs.bda,
           sizeof(output.scan_result.address.bytes));
    output.address = output.scan_result.address;
    output.scan_result.services = 0u;
    for (int index = 0; index < parameter->rmt_srvcs.num_uuids; ++index) {
      output.scan_result.services |=
          service_bit(&parameter->rmt_srvcs.uuid_list[index]);
    }
    output.scan_result.services_resolved = output.status == HAL_OK;
    emit(&output);
    break;
  }
  case ESP_BT_GAP_PIN_REQ_EVT:
    pairing_request(parameter->pin_req.bda, HAL_BLUETOOTH_CLASSIC_PAIRING_PIN);
    break;
  case ESP_BT_GAP_CFM_REQ_EVT:
    pairing_request(parameter->cfm_req.bda,
                    HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS);
    break;
  case ESP_BT_GAP_KEY_REQ_EVT:
    pairing_request(parameter->key_req.bda,
                    HAL_BLUETOOTH_CLASSIC_PAIRING_PASSKEY);
    break;
  case ESP_BT_GAP_AUTH_CMPL_EVT: {
    jh_bluetooth_classic_backend_event_t output = {0};
    output.type = JH_BLUETOOTH_CLASSIC_EVENT_AUTHENTICATION;
    output.status =
        parameter->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS ? HAL_OK : HAL_EAUTH;
    memcpy(output.address.bytes, parameter->auth_cmpl.bda,
           sizeof(output.address.bytes));
    emit(&output);
    if (output.status == HAL_OK) {
      /* Bluedroid keeps the actual link key in NVS and does not expose it.
       * A zero key marks native persistence; the portable manager accepts it
       * only in RAM-only mode. */
      output.type = JH_BLUETOOTH_CLASSIC_EVENT_LINK_KEY;
      output.link_key_type = parameter->auth_cmpl.lk_type;
      emit(&output);
    }
    if (backend_lock()) {
      s_backend.pairing_pending = false;
      backend_unlock();
    }
    break;
  }
  default:
    break;
  }
}

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
static void emit_hid_report(hal_bluetooth_hid_report_type_t type,
                            uint16_t report_id, const uint8_t *data,
                            size_t length) {
  if (data == NULL || report_id > UINT8_MAX ||
      length + (report_id != 0u ? 1u : 0u) > HAL_BLUETOOTH_HID_REPORT_MAX_LEN) {
    emit_error(HAL_EOVERFLOW, false);
    return;
  }
  jh_bluetooth_classic_backend_event_t output = {0};
  output.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_REPORT;
  output.status = HAL_OK;
  output.address = s_backend.hid_address;
  output.hid_report.type = type;
  output.hid_report.report_id = (uint8_t)report_id;
  if (report_id != 0u) {
    output.hid_report.data[0] = (uint8_t)report_id;
    memcpy(&output.hid_report.data[1], data, length);
    output.hid_report.length = (uint8_t)(length + 1u);
  } else {
    memcpy(output.hid_report.data, data, length);
    output.hid_report.length = (uint8_t)length;
  }
  emit(&output);
}

static void hidh_event_handler(void *handler_argument,
                               esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
  (void)handler_argument;
  (void)event_base;
  esp_hidh_event_data_t *parameter = (esp_hidh_event_data_t *)event_data;
  if (parameter == NULL) {
    return;
  }
  switch ((esp_hidh_event_t)event_id) {
  case ESP_HIDH_OPEN_EVENT:
    if (parameter->open.status != ESP_OK || parameter->open.dev == NULL) {
      emit_error(status_from_esp(parameter->open.status), false);
      break;
    }
    if (backend_lock()) {
      s_backend.hid_device = parameter->open.dev;
      const uint8_t *address = esp_hidh_dev_bda_get(parameter->open.dev);
      if (address != NULL) {
        memcpy(s_backend.hid_address.bytes, address,
               sizeof(s_backend.hid_address.bytes));
      }
      backend_unlock();
    }
    {
      jh_bluetooth_classic_backend_event_t output = {0};
      output.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTED;
      output.status = HAL_OK;
      output.address = s_backend.hid_address;
      emit(&output);
      size_t map_count = 0u;
      esp_hid_raw_report_map_t *maps = NULL;
      if (esp_hidh_dev_report_maps_get(parameter->open.dev, &map_count,
                                       &maps) != ESP_OK ||
          map_count == 0u || maps == NULL || maps[0].data == NULL ||
          maps[0].len > sizeof(output.descriptor)) {
        emit_error(HAL_EPROTO, false);
        break;
      }
      output.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DESCRIPTOR;
      memcpy(output.descriptor, maps[0].data, maps[0].len);
      output.descriptor_length = maps[0].len;
      emit(&output);
    }
    break;
  case ESP_HIDH_INPUT_EVENT:
    emit_hid_report(HAL_BLUETOOTH_HID_REPORT_INPUT, parameter->input.report_id,
                    parameter->input.data, parameter->input.length);
    break;
  case ESP_HIDH_FEATURE_EVENT:
    if (parameter->feature.status == ESP_OK) {
      emit_hid_report(HAL_BLUETOOTH_HID_REPORT_FEATURE,
                      parameter->feature.report_id, parameter->feature.data,
                      parameter->feature.length);
    }
    break;
  case ESP_HIDH_CLOSE_EVENT: {
    jh_bluetooth_classic_backend_event_t output = {0};
    output.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DISCONNECTED;
    output.status = status_from_esp(parameter->close.status);
    if (backend_lock()) {
      output.address = s_backend.hid_address;
      s_backend.hid_device = NULL;
      memset(&s_backend.hid_address, 0, sizeof(s_backend.hid_address));
      backend_unlock();
    }
    emit(&output);
    (void)esp_hidh_dev_free(parameter->close.dev);
    break;
  }
  case ESP_HIDH_START_EVENT:
    if (parameter->start.status != ESP_OK) {
      emit_error(status_from_esp(parameter->start.status), true);
    }
    break;
  case ESP_HIDH_STOP_EVENT:
    if (parameter->stop.status != ESP_OK) {
      emit_error(status_from_esp(parameter->stop.status), false);
    }
    break;
  default:
    break;
  }
}
#endif

static hal_status_t initialize_stack(void) {
  hal_status_t status = jh_esp32_nvs_initialize();
  if (status != HAL_OK) {
    return status;
  }
  esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  controller.mode = ESP_BT_MODE_CLASSIC_BT;
  esp_err_t esp_status = esp_bt_controller_init(&controller);
  if (esp_status != ESP_OK) {
    return status_from_esp(esp_status);
  }
  esp_status = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
  if (esp_status != ESP_OK) {
    (void)esp_bt_controller_deinit();
    return status_from_esp(esp_status);
  }
  esp_bluedroid_config_t bluedroid = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  esp_status = esp_bluedroid_init_with_cfg(&bluedroid);
  if (esp_status != ESP_OK) {
    (void)esp_bt_controller_disable();
    (void)esp_bt_controller_deinit();
    return status_from_esp(esp_status);
  }
  esp_status = esp_bluedroid_enable();
  if (esp_status != ESP_OK) {
    (void)esp_bluedroid_deinit();
    (void)esp_bt_controller_disable();
    (void)esp_bt_controller_deinit();
    return status_from_esp(esp_status);
  }
  esp_bt_io_cap_t io_capability = ESP_BT_IO_CAP_IO;
  esp_bt_pin_code_t empty_pin = {0u};
  if ((esp_status =
           esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_capability,
                                         sizeof(io_capability))) != ESP_OK ||
      (esp_status = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0u,
                                       empty_pin)) != ESP_OK ||
      (esp_status = esp_bt_gap_register_callback(gap_event_handler)) !=
          ESP_OK ||
      (esp_status = esp_bt_gap_set_scan_mode(
           ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE)) != ESP_OK) {
    return status_from_esp(esp_status);
  }
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  const esp_hidh_config_t hidh_config = {
      .callback = hidh_event_handler,
      .event_stack_size = JH_CLASSIC_HID_EVENT_STACK_SIZE,
      .callback_arg = NULL,
  };
  esp_status = esp_hidh_init(&hidh_config);
  if (esp_status != ESP_OK) {
    return status_from_esp(esp_status);
  }
#endif
  return HAL_OK;
}

static hal_status_t
backend_start(void *context,
              jh_bluetooth_classic_backend_event_fn event_handler,
              void *event_context) {
  (void)context;
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (s_backend.started) {
    backend_unlock();
    return HAL_EBUSY;
  }
  s_backend.event_handler = event_handler;
  s_backend.event_context = event_context;
  s_backend.started = true;
  const bool initialized = s_backend.stack_initialized;
  backend_unlock();
  hal_status_t status = HAL_OK;
  if (!initialized) {
    status = initialize_stack();
    if (backend_lock()) {
      s_backend.stack_initialized = status == HAL_OK;
      if (status != HAL_OK) {
        s_backend.started = false;
      }
      backend_unlock();
    }
  }
  if (status == HAL_OK) {
    jh_bluetooth_classic_backend_event_t ready = {0};
    ready.type = JH_BLUETOOTH_CLASSIC_EVENT_READY;
    ready.status = HAL_OK;
    emit(&ready);
  }
  return status;
}

static hal_status_t backend_stop(void *context) {
  (void)context;
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (!s_backend.started) {
    backend_unlock();
    return HAL_EUNINIT;
  }
  const bool cancel_scan = s_backend.scan_active;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  esp_hidh_dev_t *device = s_backend.hid_device;
  s_backend.hid_device = NULL;
#endif
  s_backend.started = false;
  s_backend.scan_active = false;
  s_backend.pairing_pending = false;
  backend_unlock();
  if (cancel_scan) {
    (void)esp_bt_gap_cancel_discovery();
  }
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  if (device != NULL) {
    (void)esp_hidh_dev_close(device);
  }
#endif
  return HAL_OK;
}

static hal_status_t backend_service(void *context) {
  (void)context;
  return s_backend.started ? HAL_OK : HAL_EUNINIT;
}

static hal_status_t
backend_identity_set(void *context,
                     const hal_bluetooth_classic_identity_t *identity) {
  (void)context;
  if (!s_backend.started || identity == NULL) {
    return !s_backend.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  esp_err_t status = esp_bt_gap_set_device_name(identity->name);
  if (status != ESP_OK) {
    return status_from_esp(status);
  }
  const esp_bt_cod_t cod = {
      .major = (identity->class_of_device >> 8u) & 0x1fu,
      .minor = (identity->class_of_device >> 2u) & 0x3fu,
      .service = (identity->class_of_device >> 13u) & 0x7ffu,
  };
  return status_from_esp(esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL));
}

static hal_status_t backend_visibility_set(void *context, bool connectable,
                                           bool discoverable,
                                           bool pairing_allowed) {
  (void)context;
  (void)pairing_allowed;
  if (!s_backend.started) {
    return HAL_EUNINIT;
  }
  const esp_bt_connection_mode_t connection =
      connectable ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE;
  const esp_bt_discovery_mode_t discovery =
      discoverable ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE;
  return status_from_esp(esp_bt_gap_set_scan_mode(connection, discovery));
}

static hal_status_t backend_scan_start(void *context, uint32_t duration_ms) {
  (void)context;
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (!s_backend.started || s_backend.scan_active) {
    const hal_status_t status = !s_backend.started ? HAL_EUNINIT : HAL_ESTATE;
    backend_unlock();
    return status;
  }
  s_backend.scan_active = true;
  s_backend.scan_deadline_ms = hal_millis() + duration_ms;
  backend_unlock();
  uint32_t units = (duration_ms + 1279u) / 1280u;
  if (units > ESP_BT_GAP_MAX_INQ_LEN) {
    units = ESP_BT_GAP_MAX_INQ_LEN;
  }
  const esp_err_t status = esp_bt_gap_start_discovery(
      ESP_BT_INQ_MODE_GENERAL_INQUIRY, (uint8_t)units, 0u);
  return status_from_esp(status);
}

static hal_status_t backend_scan_stop(void *context) {
  (void)context;
  if (!s_backend.started) {
    return HAL_EUNINIT;
  }
  return status_from_esp(esp_bt_gap_cancel_discovery());
}

static hal_status_t
backend_sdp_query(void *context,
                  const hal_bluetooth_classic_address_t *address) {
  (void)context;
  if (!s_backend.started || address == NULL) {
    return !s_backend.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  return status_from_esp(
      esp_bt_gap_get_remote_services((uint8_t *)address->bytes));
}

static hal_status_t
backend_pair(void *context, const hal_bluetooth_classic_address_t *address) {
  (void)context;
  (void)address;
  return s_backend.started ? HAL_EUNSUPPORTED : HAL_EUNINIT;
}

static hal_status_t backend_pairing_reply(void *context, bool accept) {
  (void)context;
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (!s_backend.started || !s_backend.pairing_pending) {
    const hal_status_t status = !s_backend.started ? HAL_EUNINIT : HAL_ESTATE;
    backend_unlock();
    return status;
  }
  const hal_bluetooth_classic_address_t address = s_backend.pairing_address;
  const hal_bluetooth_classic_pairing_method_t method =
      s_backend.pairing_method;
  backend_unlock();
  esp_err_t status = ESP_ERR_NOT_SUPPORTED;
  if (method == HAL_BLUETOOTH_CLASSIC_PAIRING_PIN) {
    esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
    status = esp_bt_gap_pin_reply((uint8_t *)address.bytes, accept,
                                  accept ? 4u : 0u, pin);
  } else if (method == HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS) {
    status = esp_bt_gap_ssp_confirm_reply((uint8_t *)address.bytes, accept);
  }
  return status_from_esp(status);
}

static bool native_peer_exists(const hal_bluetooth_classic_address_t *address) {
  int count = esp_bt_gap_get_bond_device_num();
  if (count <= 0) {
    return false;
  }
  esp_bd_addr_t peers[HAL_BLUETOOTH_CLASSIC_MAX_PEERS];
  if (count > (int)HAL_BLUETOOTH_CLASSIC_MAX_PEERS) {
    count = (int)HAL_BLUETOOTH_CLASSIC_MAX_PEERS;
  }
  if (esp_bt_gap_get_bond_device_list(&count, peers) != ESP_OK) {
    return false;
  }
  for (int index = 0; index < count; ++index) {
    if (memcmp(peers[index], address->bytes,
               HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN) == 0) {
      return true;
    }
  }
  return false;
}

static hal_status_t
backend_peer_restore(void *context,
                     const hal_bluetooth_classic_address_t *address,
                     const uint8_t link_key[16], uint8_t link_key_type) {
  (void)context;
  (void)link_key;
  (void)link_key_type;
  if (!s_backend.started || address == NULL) {
    return !s_backend.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  return native_peer_exists(address) ? HAL_OK : HAL_EUNSUPPORTED;
}

static hal_status_t
backend_peer_forget(void *context,
                    const hal_bluetooth_classic_address_t *address) {
  (void)context;
  if (!s_backend.started || address == NULL) {
    return !s_backend.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  return status_from_esp(
      esp_bt_gap_remove_bond_device((uint8_t *)address->bytes));
}

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
static hal_status_t
backend_hid_connect(void *context,
                    const hal_bluetooth_classic_address_t *address) {
  (void)context;
  if (!s_backend.started || address == NULL) {
    return !s_backend.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  if (s_backend.hid_device != NULL) {
    return HAL_ESTATE;
  }
  s_backend.hid_address = *address;
  s_backend.pairing_address = *address;
  s_backend.hid_device =
      esp_hidh_dev_open((uint8_t *)address->bytes, ESP_HID_TRANSPORT_BT, 0u);
  return s_backend.hid_device != NULL ? HAL_OK : HAL_ENOMEM;
}

static hal_status_t backend_hid_disconnect(void *context) {
  (void)context;
  return s_backend.hid_device != NULL
             ? status_from_esp(esp_hidh_dev_close(s_backend.hid_device))
             : HAL_ESTATE;
}

static hal_status_t
backend_hid_report_send(void *context,
                        const hal_bluetooth_hid_report_t *report) {
  (void)context;
  if (s_backend.hid_device == NULL || report == NULL) {
    return HAL_ESTATE;
  }
  esp_err_t status = ESP_ERR_INVALID_ARG;
  if (report->type == HAL_BLUETOOTH_HID_REPORT_OUTPUT) {
    status =
        esp_hidh_dev_output_set(s_backend.hid_device, 0u, report->report_id,
                                (uint8_t *)report->data, report->length);
  } else if (report->type == HAL_BLUETOOTH_HID_REPORT_FEATURE) {
    status =
        esp_hidh_dev_feature_set(s_backend.hid_device, 0u, report->report_id,
                                 (uint8_t *)report->data, report->length);
  }
  return status_from_esp(status);
}

static hal_status_t
backend_hid_report_request(void *context, hal_bluetooth_hid_report_type_t type,
                           uint8_t report_id) {
  (void)context;
  if (s_backend.hid_device == NULL) {
    return HAL_ESTATE;
  }
  return status_from_esp(
      esp_hidh_dev_get_report(s_backend.hid_device, 0u, report_id, (int)type,
                              HAL_BLUETOOTH_HID_REPORT_MAX_LEN));
}
#endif

static const jh_bluetooth_classic_backend_t s_backend_ops = {
    .context = NULL,
    .start = backend_start,
    .stop = backend_stop,
    .service = backend_service,
    .identity_set = backend_identity_set,
    .visibility_set = backend_visibility_set,
    .scan_start = backend_scan_start,
    .scan_stop = backend_scan_stop,
    .sdp_query = backend_sdp_query,
    .pair = backend_pair,
    .pairing_reply = backend_pairing_reply,
    .peer_restore = backend_peer_restore,
    .peer_forget = backend_peer_forget,
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
    .hid_connect = backend_hid_connect,
    .hid_disconnect = backend_hid_disconnect,
    .hid_report_send = backend_hid_report_send,
    .hid_report_request = backend_hid_report_request,
#endif
};

const jh_bluetooth_classic_backend_t *
jh_bluetooth_classic_backend_instance(void) {
  return &s_backend_ops;
}

#endif
