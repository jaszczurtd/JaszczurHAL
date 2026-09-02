#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32 && defined(HAL_ENABLE_BLUETOOTH_GAMEPAD)

#include "hal/bluetooth/jh_bluetooth_gamepad_identity.h"
#include "hal/bluetooth/jh_bluetooth_gamepad_parser.h"
#include "hal/bluetooth/jh_gamepad_backend.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/impl/esp32/jh_esp32_nvs_runtime.h"
#include "hal/impl/esp32/jh_esp32_status.h"
#include "hal/system/hal_sync.h"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_hidh.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  JH_GAMEPAD_ADDRESS_LENGTH = 6u,
  JH_GAMEPAD_INQUIRY_LENGTH = 12u,
  JH_GAMEPAD_HID_EVENT_STACK_SIZE = 4096u,
};

typedef enum {
  JH_GAMEPAD_PAIRING_NONE = 0,
  JH_GAMEPAD_PAIRING_PIN_0000,
  JH_GAMEPAD_PAIRING_SSP_CONFIRM,
} jh_gamepad_pairing_method_t;

typedef struct {
  hal_mutex_t mutex;
  jh_bluetooth_gamepad_parser_t parser;
  esp_hidh_dev_t *device;
  hal_gamepad_state_t state;
  hal_status_t last_status;
  uint8_t candidate_address[JH_GAMEPAD_ADDRESS_LENGTH];
  uint8_t known_address[JH_GAMEPAD_ADDRESS_LENGTH];
  uint8_t pairing_address[JH_GAMEPAD_ADDRESS_LENGTH];
  jh_gamepad_pairing_method_t pairing_method;
  bool stack_initialized;
  bool started;
  bool profile_ready;
  bool discovering;
  bool pairing_window_open;
  bool pairing_pending;
  bool pairing_authorized;
  bool candidate_available;
  bool known_device;
  const hal_gamepad_bond_provider_t *bond_provider;
} jh_gamepad_bluedroid_backend_t;

static jh_gamepad_bluedroid_backend_t s_backend;

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

static bool address_equal(const uint8_t *left, const uint8_t *right) {
  return left != NULL && right != NULL &&
         memcmp(left, right, JH_GAMEPAD_ADDRESS_LENGTH) == 0;
}

static bool address_allowed_locked(const uint8_t *address) {
  return (s_backend.candidate_available &&
          address_equal(address, s_backend.candidate_address)) ||
         (s_backend.known_device &&
          address_equal(address, s_backend.known_address));
}

static hal_status_t status_from_esp(esp_err_t status) {
  return jh_esp32_status_from_esp_err_with_fallback(status, HAL_EIO);
}

static void set_failure_locked(hal_status_t status) {
  s_backend.last_status = status;
  s_backend.state = HAL_GAMEPAD_STATE_FAILED;
  s_backend.discovering = false;
  s_backend.pairing_window_open = false;
  s_backend.pairing_pending = false;
  s_backend.pairing_authorized = false;
  s_backend.pairing_method = JH_GAMEPAD_PAIRING_NONE;
}

static size_t property_name(const esp_bt_gap_cb_param_t *parameter, char *name,
                            size_t capacity, uint32_t *out_cod) {
  if (parameter == NULL || name == NULL || capacity == 0u || out_cod == NULL) {
    return 0u;
  }
  name[0] = '\0';
  *out_cod = 0u;
  uint8_t *eir = NULL;
  size_t name_length = 0u;
  for (int index = 0; index < parameter->disc_res.num_prop; ++index) {
    const esp_bt_gap_dev_prop_t *property = &parameter->disc_res.prop[index];
    if (property->val == NULL || property->len <= 0) {
      continue;
    }
    if (property->type == ESP_BT_GAP_DEV_PROP_COD &&
        property->len >= (int)sizeof(*out_cod)) {
      memcpy(out_cod, property->val, sizeof(*out_cod));
    } else if (property->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
      const size_t copy_length = (size_t)property->len < capacity
                                     ? (size_t)property->len
                                     : capacity - 1u;
      name_length = (size_t)property->len;
      if (copy_length > 0u) {
        memcpy(name, property->val, copy_length);
      }
      name[copy_length] = '\0';
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
      name_length = resolved_length;
      const size_t copy_length =
          name_length < capacity ? name_length : capacity - 1u;
      memcpy(name, resolved, copy_length);
      name[copy_length] = '\0';
    }
  }
  return name_length;
}

static bool discovery_result_matches(const esp_bt_gap_cb_param_t *parameter) {
  char name[sizeof(JH_BLUETOOTH_GAMEPAD_EXPECTED_NAME)];
  uint32_t cod = 0u;
  const size_t name_length = property_name(parameter, name, sizeof(name), &cod);
  return jh_bluetooth_gamepad_candidate_matches(cod, (const uint8_t *)name,
                                                name_length);
}

static void begin_candidate_connection(const uint8_t *address) {
  esp_hidh_dev_t *device =
      esp_hidh_dev_open((uint8_t *)address, ESP_HID_TRANSPORT_BT, 0u);
  if (!backend_lock()) {
    if (device != NULL) {
      (void)esp_hidh_dev_close(device);
    }
    return;
  }
  if (device == NULL) {
    set_failure_locked(HAL_ENOMEM);
  } else if (s_backend.started) {
    s_backend.device = device;
    s_backend.state = HAL_GAMEPAD_STATE_CONNECTING;
    s_backend.last_status = HAL_OK;
  } else {
    (void)esp_hidh_dev_close(device);
  }
  backend_unlock();
}

static void handle_discovery_result(esp_bt_gap_cb_param_t *parameter) {
  if (!discovery_result_matches(parameter) || !backend_lock()) {
    return;
  }
  bool connect = false;
  uint8_t address[JH_GAMEPAD_ADDRESS_LENGTH];
  if (s_backend.started && s_backend.discovering &&
      !s_backend.candidate_available && s_backend.device == NULL) {
    memcpy(address, parameter->disc_res.bda, sizeof(address));
    memcpy(s_backend.candidate_address, address, sizeof(address));
    s_backend.candidate_available = true;
    s_backend.discovering = false;
    s_backend.state = HAL_GAMEPAD_STATE_CONNECTING;
    connect = true;
  }
  backend_unlock();
  if (connect) {
    (void)esp_bt_gap_cancel_discovery();
    begin_candidate_connection(address);
  }
}

static void set_pairing_request(const uint8_t *address,
                                jh_gamepad_pairing_method_t method) {
  if (!backend_lock()) {
    return;
  }
  if (s_backend.started && address_allowed_locked(address) &&
      s_backend.state == HAL_GAMEPAD_STATE_CONNECTING) {
    memcpy(s_backend.pairing_address, address,
           sizeof(s_backend.pairing_address));
    s_backend.pairing_method = method;
    s_backend.pairing_pending = true;
    s_backend.pairing_authorized = false;
  }
  backend_unlock();
}

static void gap_event_handler(esp_bt_gap_cb_event_t event,
                              esp_bt_gap_cb_param_t *parameter) {
  if (parameter == NULL) {
    return;
  }
  switch (event) {
  case ESP_BT_GAP_DISC_RES_EVT:
    handle_discovery_result(parameter);
    break;
  case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
    if (parameter->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED &&
        backend_lock()) {
      if (s_backend.discovering) {
        s_backend.discovering = false;
        s_backend.pairing_window_open = false;
        s_backend.state = HAL_GAMEPAD_STATE_READY;
        s_backend.last_status = HAL_OK;
      }
      backend_unlock();
    }
    break;
  case ESP_BT_GAP_PIN_REQ_EVT: {
    bool allowed = false;
    if (backend_lock()) {
      allowed =
          s_backend.started && address_allowed_locked(parameter->pin_req.bda);
      backend_unlock();
    }
    if (allowed && !parameter->pin_req.min_16_digit) {
      set_pairing_request(parameter->pin_req.bda, JH_GAMEPAD_PAIRING_PIN_0000);
    } else {
      esp_bt_pin_code_t empty_pin = {0u};
      (void)esp_bt_gap_pin_reply(parameter->pin_req.bda, false, 0u, empty_pin);
    }
    break;
  }
  case ESP_BT_GAP_CFM_REQ_EVT: {
    bool allowed = false;
    if (backend_lock()) {
      allowed =
          s_backend.started && address_allowed_locked(parameter->cfm_req.bda);
      backend_unlock();
    }
    if (allowed) {
      set_pairing_request(parameter->cfm_req.bda,
                          JH_GAMEPAD_PAIRING_SSP_CONFIRM);
    } else {
      (void)esp_bt_gap_ssp_confirm_reply(parameter->cfm_req.bda, false);
    }
    break;
  }
  case ESP_BT_GAP_AUTH_CMPL_EVT:
    if (backend_lock()) {
      if (address_allowed_locked(parameter->auth_cmpl.bda)) {
        if (parameter->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
          s_backend.last_status = HAL_OK;
        } else {
          set_failure_locked(HAL_EAUTH);
        }
        s_backend.pairing_pending = false;
        s_backend.pairing_authorized = false;
        s_backend.pairing_method = JH_GAMEPAD_PAIRING_NONE;
      }
      backend_unlock();
    }
    break;
  default:
    break;
  }
}

static hal_status_t configure_connected_device(esp_hidh_dev_t *device) {
  if (device == NULL ||
      esp_hidh_dev_transport_get(device) != ESP_HID_TRANSPORT_BT) {
    return HAL_EPROTO;
  }
  if (!jh_bluetooth_gamepad_product_matches(
          esp_hidh_dev_vendor_id_get(device),
          esp_hidh_dev_product_id_get(device))) {
    return HAL_EAUTH;
  }
  size_t map_count = 0u;
  esp_hid_raw_report_map_t *maps = NULL;
  if (esp_hidh_dev_report_maps_get(device, &map_count, &maps) != ESP_OK ||
      map_count != 1u || maps == NULL || maps[0].data == NULL) {
    return HAL_EPROTO;
  }
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  const uint8_t *address = esp_hidh_dev_bda_get(device);
  if (!s_backend.started || s_backend.state != HAL_GAMEPAD_STATE_CONNECTING ||
      !address_allowed_locked(address)) {
    backend_unlock();
    return HAL_EAUTH;
  }
  hal_status_t status = HAL_OK;
  if (!s_backend.parser.configured) {
    status = jh_bluetooth_gamepad_parser_configure(&s_backend.parser,
                                                   maps[0].data, maps[0].len);
  }
  if (status == HAL_OK) {
    status = jh_bluetooth_gamepad_parser_connection_opened(&s_backend.parser);
  }
  if (status == HAL_OK) {
    if (address != NULL) {
      memcpy(s_backend.known_address, address, sizeof(s_backend.known_address));
      s_backend.known_device = true;
    }
    s_backend.device = device;
    s_backend.state = HAL_GAMEPAD_STATE_CONNECTED;
    s_backend.last_status = HAL_OK;
    s_backend.discovering = false;
    s_backend.pairing_window_open = false;
    s_backend.pairing_pending = false;
    s_backend.pairing_authorized = false;
    s_backend.pairing_method = JH_GAMEPAD_PAIRING_NONE;
  } else {
    set_failure_locked(status);
  }
  backend_unlock();
  return status;
}

static void reject_opened_device(esp_hidh_dev_t *device, hal_status_t status) {
  if (backend_lock()) {
    if (s_backend.device == device) {
      s_backend.device = NULL;
    }
    if (s_backend.started) {
      set_failure_locked(status);
    }
    backend_unlock();
  }
  (void)esp_hidh_dev_close(device);
}

static void parse_input_report(const esp_hidh_event_data_t *parameter) {
  uint8_t report[JH_BLUETOOTH_GAMEPAD_REPORT_MAX];
  const uint8_t *data = parameter->input.data;
  size_t length = parameter->input.length;
  if (!backend_lock()) {
    return;
  }
  if (!s_backend.started || parameter->input.dev != s_backend.device ||
      s_backend.state != HAL_GAMEPAD_STATE_CONNECTED || data == NULL) {
    backend_unlock();
    return;
  }
  if (s_backend.parser.report_ids_declared) {
    if (length >= sizeof(report) || parameter->input.report_id > UINT8_MAX) {
      s_backend.last_status = HAL_EOVERFLOW;
      backend_unlock();
      return;
    }
    report[0] = (uint8_t)parameter->input.report_id;
    memcpy(&report[1], data, length);
    data = report;
    ++length;
  }
  const hal_status_t status =
      jh_bluetooth_gamepad_parser_parse_input(&s_backend.parser, data, length);
  if (status != HAL_OK && status != HAL_EAGAIN) {
    s_backend.last_status = status;
  }
  backend_unlock();
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
  case ESP_HIDH_START_EVENT:
    if (backend_lock()) {
      s_backend.profile_ready = parameter->start.status == ESP_OK;
      if (s_backend.profile_ready && s_backend.started) {
        s_backend.state = HAL_GAMEPAD_STATE_READY;
        s_backend.last_status = HAL_OK;
      } else if (!s_backend.profile_ready) {
        set_failure_locked(status_from_esp(parameter->start.status));
      }
      backend_unlock();
    }
    break;
  case ESP_HIDH_OPEN_EVENT:
    if (parameter->open.status == ESP_OK) {
      const hal_status_t status =
          configure_connected_device(parameter->open.dev);
      if (status != HAL_OK) {
        reject_opened_device(parameter->open.dev, status);
      }
    } else if (backend_lock()) {
      set_failure_locked(status_from_esp(parameter->open.status));
      s_backend.device = NULL;
      backend_unlock();
    }
    break;
  case ESP_HIDH_INPUT_EVENT:
    parse_input_report(parameter);
    break;
  case ESP_HIDH_CLOSE_EVENT:
    if (backend_lock()) {
      if (s_backend.device == parameter->close.dev) {
        if (s_backend.parser.current.connected) {
          (void)jh_bluetooth_gamepad_parser_connection_closed(
              &s_backend.parser);
        }
        s_backend.device = NULL;
        s_backend.pairing_pending = false;
        s_backend.pairing_window_open = false;
        s_backend.pairing_authorized = false;
        s_backend.pairing_method = JH_GAMEPAD_PAIRING_NONE;
        if (s_backend.started) {
          s_backend.state = HAL_GAMEPAD_STATE_READY;
          s_backend.last_status = status_from_esp(parameter->close.status);
        } else {
          s_backend.state = HAL_GAMEPAD_STATE_UNINITIALIZED;
          s_backend.last_status = HAL_OK;
        }
      }
      backend_unlock();
    }
    (void)esp_hidh_dev_free(parameter->close.dev);
    break;
  case ESP_HIDH_STOP_EVENT:
    if (backend_lock()) {
      s_backend.profile_ready = false;
      if (parameter->stop.status != ESP_OK) {
        set_failure_locked(status_from_esp(parameter->stop.status));
      }
      backend_unlock();
    }
    break;
  default:
    break;
  }
}

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
    (void)esp_bluedroid_disable();
    (void)esp_bluedroid_deinit();
    (void)esp_bt_controller_disable();
    (void)esp_bt_controller_deinit();
    return status_from_esp(esp_status);
  }

  const esp_hidh_config_t hidh_config = {
      .callback = hidh_event_handler,
      .event_stack_size = JH_GAMEPAD_HID_EVENT_STACK_SIZE,
      .callback_arg = NULL,
  };
  esp_status = esp_hidh_init(&hidh_config);
  if (esp_status != ESP_OK) {
    (void)esp_bluedroid_disable();
    (void)esp_bluedroid_deinit();
    (void)esp_bt_controller_disable();
    (void)esp_bt_controller_deinit();
    return status_from_esp(esp_status);
  }
  return HAL_OK;
}

static hal_status_t
backend_start(void *context, const hal_gamepad_bond_provider_t *bond_provider) {
  (void)context;
  /* Bluedroid persists bonded devices itself via NVS (unlike BTstack's
   * RAM-only link key db on RP/STM32), so this backend does not need a
   * hal_gamepad bond provider to survive a reboot; the parameter is accepted
   * for vtable compatibility only. backend_forget() below still routes
   * factory reset through the caller's provider when one is given, so a
   * consumer that wants a single code path across backends can still pass
   * one. */
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (s_backend.started) {
    backend_unlock();
    return HAL_EBUSY;
  }
  if (s_backend.device != NULL) {
    backend_unlock();
    return HAL_EBUSY;
  }
  jh_bluetooth_gamepad_parser_init(&s_backend.parser);
  memset(s_backend.candidate_address, 0, sizeof(s_backend.candidate_address));
  memset(s_backend.known_address, 0, sizeof(s_backend.known_address));
  memset(s_backend.pairing_address, 0, sizeof(s_backend.pairing_address));
  s_backend.bond_provider = bond_provider;
  s_backend.started = true;
  s_backend.state = HAL_GAMEPAD_STATE_STARTING;
  s_backend.last_status = HAL_NONE;
  s_backend.discovering = false;
  s_backend.pairing_window_open = false;
  s_backend.pairing_pending = false;
  s_backend.pairing_authorized = false;
  s_backend.candidate_available = false;
  s_backend.known_device = false;
  const bool already_initialized = s_backend.stack_initialized;
  backend_unlock();

  if (already_initialized) {
    if (backend_lock()) {
      s_backend.state = s_backend.profile_ready ? HAL_GAMEPAD_STATE_READY
                                                : HAL_GAMEPAD_STATE_STARTING;
      backend_unlock();
    }
    return HAL_OK;
  }
  const hal_status_t status = initialize_stack();
  hal_mutex_lock(s_backend.mutex);
  if (status == HAL_OK) {
    s_backend.stack_initialized = true;
  } else {
    s_backend.started = false;
    set_failure_locked(status);
  }
  backend_unlock();
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
  const bool cancel_discovery = s_backend.discovering;
  esp_hidh_dev_t *device = s_backend.device;
  s_backend.device = NULL;
  s_backend.started = false;
  s_backend.discovering = false;
  s_backend.pairing_window_open = false;
  s_backend.pairing_pending = false;
  s_backend.pairing_authorized = false;
  s_backend.pairing_method = JH_GAMEPAD_PAIRING_NONE;
  s_backend.state = HAL_GAMEPAD_STATE_UNINITIALIZED;
  s_backend.last_status = HAL_OK;
  backend_unlock();
  if (cancel_discovery) {
    (void)esp_bt_gap_cancel_discovery();
  }
  if (device != NULL) {
    const esp_err_t status = esp_hidh_dev_close(device);
    return status == ESP_OK ? HAL_OK : status_from_esp(status);
  }
  return HAL_OK;
}

static hal_status_t backend_service(void *context) {
  (void)context;
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  const hal_status_t status =
      !s_backend.started
          ? HAL_EUNINIT
          : (s_backend.state == HAL_GAMEPAD_STATE_FAILED ? s_backend.last_status
                                                         : HAL_OK);
  backend_unlock();
  return status;
}

static hal_status_t backend_get_info(void *context,
                                     hal_gamepad_info_t *out_info) {
  (void)context;
  if (out_info == NULL) {
    return HAL_EINVAL;
  }
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (!s_backend.started) {
    backend_unlock();
    return HAL_EUNINIT;
  }
  jh_bluetooth_gamepad_parser_diagnostics_t diagnostics;
  jh_bluetooth_gamepad_parser_diagnostics(&s_backend.parser, &diagnostics);
  memset(out_info, 0, sizeof(*out_info));
  out_info->state = s_backend.state;
  out_info->last_status = s_backend.last_status;
  out_info->generation = s_backend.parser.current.generation;
  out_info->dropped_snapshots = diagnostics.dropped_snapshots;
  out_info->pending_snapshots = s_backend.parser.queue_count;
  out_info->pairing_window_open = s_backend.pairing_window_open;
  out_info->pairing_pending = s_backend.pairing_pending;
  out_info->known_device = s_backend.known_device;
  backend_unlock();
  return HAL_OK;
}

static hal_status_t backend_snapshot(void *context,
                                     hal_gamepad_snapshot_t *out_snapshot) {
  (void)context;
  if (out_snapshot == NULL) {
    return HAL_EINVAL;
  }
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  jh_bluetooth_gamepad_snapshot_t snapshot;
  const hal_status_t status =
      !s_backend.started
          ? HAL_EUNINIT
          : jh_bluetooth_gamepad_parser_snapshot(&s_backend.parser, &snapshot);
  if (status == HAL_OK) {
    jh_gamepad_copy_snapshot(&snapshot, out_snapshot);
  }
  backend_unlock();
  return status;
}

static hal_status_t
backend_snapshot_next(void *context, hal_gamepad_snapshot_t *out_snapshot) {
  (void)context;
  if (out_snapshot == NULL) {
    return HAL_EINVAL;
  }
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  jh_bluetooth_gamepad_snapshot_t snapshot;
  const hal_status_t status =
      !s_backend.started
          ? HAL_EUNINIT
          : jh_bluetooth_gamepad_parser_next(&s_backend.parser, &snapshot);
  if (status == HAL_OK) {
    jh_gamepad_copy_snapshot(&snapshot, out_snapshot);
  }
  backend_unlock();
  return status;
}

static hal_status_t backend_pairing_open(void *context) {
  (void)context;
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (!s_backend.started || !s_backend.profile_ready) {
    backend_unlock();
    return HAL_EUNINIT;
  }
  if (s_backend.state != HAL_GAMEPAD_STATE_READY || s_backend.device != NULL ||
      s_backend.discovering) {
    backend_unlock();
    return HAL_ESTATE;
  }
  s_backend.discovering = true;
  s_backend.pairing_window_open = true;
  s_backend.candidate_available = false;
  s_backend.state = HAL_GAMEPAD_STATE_DISCOVERING;
  s_backend.last_status = HAL_OK;
  backend_unlock();
  const esp_err_t esp_status = esp_bt_gap_start_discovery(
      ESP_BT_INQ_MODE_GENERAL_INQUIRY, JH_GAMEPAD_INQUIRY_LENGTH, 0u);
  if (esp_status == ESP_OK) {
    return HAL_OK;
  }
  if (backend_lock()) {
    s_backend.discovering = false;
    s_backend.pairing_window_open = false;
    s_backend.state = HAL_GAMEPAD_STATE_READY;
    s_backend.last_status = status_from_esp(esp_status);
    backend_unlock();
  }
  return status_from_esp(esp_status);
}

static hal_status_t backend_pairing_authorize(void *context) {
  (void)context;
  uint8_t address[JH_GAMEPAD_ADDRESS_LENGTH];
  jh_gamepad_pairing_method_t method = JH_GAMEPAD_PAIRING_NONE;
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (!s_backend.started) {
    backend_unlock();
    return HAL_EUNINIT;
  }
  if (!s_backend.pairing_pending || s_backend.pairing_authorized) {
    backend_unlock();
    return HAL_ESTATE;
  }
  memcpy(address, s_backend.pairing_address, sizeof(address));
  method = s_backend.pairing_method;
  s_backend.pairing_authorized = true;
  backend_unlock();

  esp_err_t esp_status = ESP_ERR_NOT_SUPPORTED;
  if (method == JH_GAMEPAD_PAIRING_PIN_0000) {
    esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
    esp_status = esp_bt_gap_pin_reply(address, true, 4u, pin);
  } else if (method == JH_GAMEPAD_PAIRING_SSP_CONFIRM) {
    esp_status = esp_bt_gap_ssp_confirm_reply(address, true);
  }
  if (esp_status != ESP_OK && backend_lock()) {
    s_backend.pairing_authorized = false;
    s_backend.last_status = status_from_esp(esp_status);
    backend_unlock();
  }
  return status_from_esp(esp_status);
}

static hal_status_t backend_reconnect(void *context) {
  (void)context;
  uint8_t address[JH_GAMEPAD_ADDRESS_LENGTH];
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (!s_backend.started || !s_backend.profile_ready) {
    backend_unlock();
    return HAL_EUNINIT;
  }
  if (!s_backend.known_device || s_backend.device != NULL ||
      s_backend.state != HAL_GAMEPAD_STATE_READY || s_backend.discovering) {
    backend_unlock();
    return HAL_ESTATE;
  }
  memcpy(address, s_backend.known_address, sizeof(address));
  s_backend.state = HAL_GAMEPAD_STATE_CONNECTING;
  s_backend.last_status = HAL_OK;
  backend_unlock();
  begin_candidate_connection(address);
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  const hal_status_t status = s_backend.device != NULL ? HAL_OK : HAL_ENOMEM;
  backend_unlock();
  return status;
}

static hal_status_t backend_disconnect(void *context) {
  (void)context;
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (!s_backend.started) {
    backend_unlock();
    return HAL_EUNINIT;
  }
  if (s_backend.state != HAL_GAMEPAD_STATE_CONNECTED ||
      s_backend.device == NULL) {
    backend_unlock();
    return HAL_ESTATE;
  }
  esp_hidh_dev_t *device = s_backend.device;
  backend_unlock();
  return status_from_esp(esp_hidh_dev_close(device));
}

static hal_status_t backend_forget(void *context) {
  (void)context;
  if (!backend_lock()) {
    return HAL_ENOMEM;
  }
  if (!s_backend.started) {
    backend_unlock();
    return HAL_EUNINIT;
  }
  esp_hidh_dev_t *device = s_backend.device;
  uint8_t address[JH_GAMEPAD_ADDRESS_LENGTH];
  memcpy(address, s_backend.known_address, sizeof(address));
  const bool had_known_device = s_backend.known_device;
  memset(s_backend.known_address, 0, sizeof(s_backend.known_address));
  s_backend.known_device = false;
  const hal_gamepad_bond_provider_t *bond_provider = s_backend.bond_provider;
  backend_unlock();

  if (device != NULL) {
    (void)esp_hidh_dev_close(device);
  }
  /* Bluedroid owns this bond's persistence via NVS; removing it here is the
   * native equivalent of a provider's erase(). */
  hal_status_t status = HAL_OK;
  if (had_known_device) {
    status = status_from_esp(esp_bt_gap_remove_bond_device(address));
  }
  if (bond_provider != NULL && bond_provider->erase != NULL) {
    const hal_status_t erase_status =
        bond_provider->erase(bond_provider->context);
    if (status == HAL_OK) {
      status = erase_status;
    }
  }
  return status;
}

static const jh_gamepad_backend_t s_gamepad_backend = {
    .context = NULL,
    .start = backend_start,
    .stop = backend_stop,
    .service = backend_service,
    .get_info = backend_get_info,
    .snapshot = backend_snapshot,
    .snapshot_next = backend_snapshot_next,
    .pairing_open = backend_pairing_open,
    .pairing_authorize = backend_pairing_authorize,
    .reconnect = backend_reconnect,
    .disconnect = backend_disconnect,
    .forget = backend_forget,
};

const jh_gamepad_backend_t *jh_gamepad_backend_instance(void) {
  return &s_gamepad_backend;
}

#endif /* HAL_TARGET_IS_ESP32 && HAL_ENABLE_BLUETOOTH_GAMEPAD */
