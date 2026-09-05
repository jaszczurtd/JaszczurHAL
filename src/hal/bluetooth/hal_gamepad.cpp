#include "hal/bluetooth/hal_gamepad.h"

#ifdef HAL_ENABLE_BLUETOOTH_GAMEPAD

#include "hal/bluetooth/hal_bluetooth_hid_host.h"
#include "hal/bluetooth/jh_bluetooth_classic_address.h"
#include "hal/bluetooth/jh_bluetooth_gamepad_identity.h"
#include "hal/bluetooth/jh_bluetooth_gamepad_parser.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_target.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#if HAL_TARGET_IS_MOCK
#include "hal/impl/.mock/hal_mock.h"
#endif

#include <string.h>

#define JH_GAMEPAD_HANDLE_KIND 14u
#define JH_GAMEPAD_PAIRING_WINDOW_MS 120000u
#define JH_GAMEPAD_SDP_SETTLE_MS 1000u
#define JH_GAMEPAD_DISCOVERY_RETRY_MS 1000u

namespace {

struct gamepad_runtime_t {
  hal_mutex_t mutex;
  jh_handle_pool_t handle_pool;
  jh_handle_slot_t handle_slot;
  hal_bluetooth_classic_t classic;
  hal_bluetooth_hid_host_t hid;
  hal_gamepad_t public_handle;
  hal_gamepad_bond_provider_t legacy_provider;
  hal_bluetooth_classic_bond_provider_t classic_provider;
  jh_bluetooth_gamepad_parser_t parser;
  hal_bluetooth_classic_address_t selected_address;
  hal_gamepad_info_t info;
  bool handle_pool_initialized;
  bool operation_active;
  bool legacy_provider_enabled;
  bool selected_valid;
  bool connect_pending;
  bool connection_attempt_active;
  bool parser_connected;
  bool input_validated;
  bool peer_save_requested;
  bool discovery_retry_pending;
  uint32_t discovery_retry_started_ms;
  uint32_t connect_settle_started_ms;
  uint32_t pairing_window_started_ms;
#if HAL_TARGET_IS_MOCK
  uint32_t mock_bond_store_calls;
  uint32_t mock_bond_erase_calls;
  hal_status_t mock_last_bond_store_status;
#endif
};

gamepad_runtime_t s_gamepad{};

hal_mutex_t runtime_mutex() {
  return jh_hal_mutex_create_once(&s_gamepad.mutex);
}

hal_status_t ensure_handle_pool_locked() {
  if (s_gamepad.handle_pool_initialized) {
    return HAL_OK;
  }
  const hal_status_t status =
      jh_handle_pool_init(&s_gamepad.handle_pool, &s_gamepad.handle_slot, 1u,
                          JH_GAMEPAD_HANDLE_KIND);
  s_gamepad.handle_pool_initialized = status == HAL_OK;
  return status;
}

bool handle_valid_locked(hal_gamepad_t gamepad) {
  if (!s_gamepad.handle_pool_initialized || s_gamepad.classic == nullptr ||
      s_gamepad.hid == nullptr) {
    return false;
  }
  void *runtime = nullptr;
  return jh_handle_resolve(&s_gamepad.handle_pool, gamepad, &runtime,
                           nullptr) == HAL_OK &&
         runtime == &s_gamepad;
}

hal_status_t release_handle_locked(const void *handle) {
  void *runtime = nullptr;
  hal_status_t status =
      jh_handle_release(&s_gamepad.handle_pool, handle, &runtime);
  if (status == HAL_OK && runtime != &s_gamepad) {
    status = HAL_ESTATE;
  }
  if (status != HAL_OK) {
    jh_handle_invalidate_all(&s_gamepad.handle_pool);
  }
  return status;
}

hal_status_t legacy_load(void *context, size_t index,
                         hal_bluetooth_classic_bond_blob_t *out_blob) {
  gamepad_runtime_t *runtime = static_cast<gamepad_runtime_t *>(context);
  if (runtime == nullptr || out_blob == nullptr || index != 0u) {
    return HAL_EINVAL;
  }
  if (!runtime->legacy_provider_enabled ||
      runtime->legacy_provider.load == nullptr) {
    return HAL_ENOENT;
  }
  return runtime->legacy_provider.load(runtime->legacy_provider.context,
                                       out_blob);
}

hal_status_t legacy_store(void *context, size_t index,
                          const hal_bluetooth_classic_bond_blob_t *blob) {
  gamepad_runtime_t *runtime = static_cast<gamepad_runtime_t *>(context);
  if (runtime == nullptr || blob == nullptr || index != 0u) {
    return HAL_EINVAL;
  }
  if (!runtime->legacy_provider_enabled ||
      runtime->legacy_provider.store == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  const hal_status_t status =
      runtime->legacy_provider.store(runtime->legacy_provider.context, blob);
#if HAL_TARGET_IS_MOCK
  ++runtime->mock_bond_store_calls;
  runtime->mock_last_bond_store_status = status;
#endif
  return status;
}

hal_status_t legacy_erase(void *context, size_t index) {
  gamepad_runtime_t *runtime = static_cast<gamepad_runtime_t *>(context);
  if (runtime == nullptr || index != 0u) {
    return HAL_EINVAL;
  }
  if (!runtime->legacy_provider_enabled ||
      runtime->legacy_provider.erase == nullptr) {
    return HAL_OK;
  }
#if HAL_TARGET_IS_MOCK
  ++runtime->mock_bond_erase_calls;
#endif
  return runtime->legacy_provider.erase(runtime->legacy_provider.context);
}

template <typename Operation>
hal_status_t run_operation(hal_gamepad_t gamepad, Operation operation) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(gamepad)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_gamepad.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_gamepad.operation_active = true;
  hal_bluetooth_classic_t classic = s_gamepad.classic;
  hal_bluetooth_hid_host_t hid = s_gamepad.hid;
  hal_mutex_unlock(mutex);

  const hal_status_t status = operation(classic, hid);

  hal_mutex_lock(mutex);
  s_gamepad.operation_active = false;
  hal_mutex_unlock(mutex);
  return status;
}

void copy_snapshot(const jh_bluetooth_gamepad_snapshot_t &source,
                   hal_gamepad_snapshot_t *destination) {
  destination->generation = source.generation;
  destination->buttons = source.buttons;
  memcpy(destination->axes, source.axes, sizeof(destination->axes));
  destination->axes_present = source.axes_present;
  destination->dpad = source.dpad;
  destination->connected = source.connected;
}

bool matching_saved_peer(hal_bluetooth_classic_t classic,
                         hal_bluetooth_classic_peer_t *out_peer) {
  size_t count = 0u;
  if (hal_bluetooth_classic_peer_count(classic, &count) != HAL_OK) {
    return false;
  }
  bool found = false;
  hal_bluetooth_classic_peer_t newest{};
  for (size_t index = 0u; index < count; ++index) {
    hal_bluetooth_classic_peer_t peer{};
    if (hal_bluetooth_classic_peer_get(classic, index, &peer) == HAL_OK &&
        peer.profile_id == JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID &&
        (!found || peer.sequence > newest.sequence)) {
      newest = peer;
      found = true;
    }
  }
  if (found && out_peer != nullptr) {
    *out_peer = newest;
  }
  return found;
}

void close_parser_locked(hal_status_t status) {
  if (s_gamepad.parser_connected) {
    (void)jh_bluetooth_gamepad_parser_connection_closed(&s_gamepad.parser);
  }
  s_gamepad.parser_connected = false;
  s_gamepad.input_validated = false;
  s_gamepad.peer_save_requested = false;
  if (status != HAL_OK) {
    s_gamepad.info.last_status = status;
  }
}

hal_status_t restart_pairing_discovery(hal_bluetooth_classic_t classic,
                                       hal_status_t attempt_status) {
  (void)classic;
  hal_mutex_lock(s_gamepad.mutex);
  const bool pairing_window_open = s_gamepad.info.pairing_window_open;
  const uint32_t pairing_started = s_gamepad.pairing_window_started_ms;
  s_gamepad.selected_valid = false;
  s_gamepad.connect_pending = false;
  s_gamepad.connection_attempt_active = false;
  s_gamepad.discovery_retry_pending = pairing_window_open;
  s_gamepad.discovery_retry_started_ms = hal_millis();
  s_gamepad.info.last_status = attempt_status;
  hal_mutex_unlock(s_gamepad.mutex);

  if (!pairing_window_open) {
    return attempt_status;
  }
  const uint32_t elapsed = hal_millis() - pairing_started;
  if (elapsed >= JH_GAMEPAD_PAIRING_WINDOW_MS) {
    hal_mutex_lock(s_gamepad.mutex);
    s_gamepad.info.pairing_window_open = false;
    s_gamepad.discovery_retry_pending = false;
    hal_mutex_unlock(s_gamepad.mutex);
    return HAL_ETIMEOUT;
  }
  hal_deb("hal_gamepad: discovery retry scheduled cause=%s elapsed_ms=%lu",
          hal_status_to_string(attempt_status), (unsigned long)elapsed);
  return HAL_OK;
}

hal_status_t discard_pending_scan_results(hal_bluetooth_classic_t classic) {
  for (;;) {
    hal_bluetooth_classic_scan_result_t discarded{};
    const hal_status_t status =
        hal_bluetooth_classic_scan_result_next(classic, &discarded);
    if (status == HAL_EAGAIN) {
      return HAL_OK;
    }
    if (status != HAL_OK && status != HAL_EOVERFLOW) {
      return status;
    }
  }
}

hal_status_t resume_pairing_discovery(hal_bluetooth_classic_t classic) {
  hal_mutex_lock(s_gamepad.mutex);
  const bool retry_pending = s_gamepad.discovery_retry_pending;
  const uint32_t retry_started = s_gamepad.discovery_retry_started_ms;
  const uint32_t pairing_started = s_gamepad.pairing_window_started_ms;
  hal_mutex_unlock(s_gamepad.mutex);
  if (!retry_pending) {
    return HAL_OK;
  }

  hal_status_t status = discard_pending_scan_results(classic);
  if (status != HAL_OK || !hal_elapsed_u32(hal_millis(), retry_started,
                                           JH_GAMEPAD_DISCOVERY_RETRY_MS)) {
    return status;
  }

  const uint32_t elapsed = hal_millis() - pairing_started;
  if (elapsed >= JH_GAMEPAD_PAIRING_WINDOW_MS) {
    hal_mutex_lock(s_gamepad.mutex);
    s_gamepad.info.pairing_window_open = false;
    s_gamepad.discovery_retry_pending = false;
    hal_mutex_unlock(s_gamepad.mutex);
    return HAL_ETIMEOUT;
  }
  status = hal_bluetooth_classic_scan_start(
      classic, JH_GAMEPAD_PAIRING_WINDOW_MS - elapsed);
  hal_mutex_lock(s_gamepad.mutex);
  s_gamepad.discovery_retry_pending = false;
  if (status != HAL_OK) {
    s_gamepad.info.pairing_window_open = false;
    s_gamepad.info.last_status = status;
  }
  hal_mutex_unlock(s_gamepad.mutex);
  hal_deb("hal_gamepad: discovery retry resumed elapsed_ms=%lu status=%s",
          (unsigned long)elapsed, hal_status_to_string(status));
  return status;
}

hal_status_t
select_scan_result(hal_bluetooth_classic_t classic,
                   hal_bluetooth_hid_host_t hid,
                   const hal_bluetooth_classic_scan_result_t &result) {
  hal_mutex_lock(s_gamepad.mutex);
  const bool selected = s_gamepad.selected_valid;
  const hal_bluetooth_classic_address_t selected_address =
      s_gamepad.selected_address;
  hal_mutex_unlock(s_gamepad.mutex);

  if (!result.services_resolved) {
    if (selected) {
      return HAL_OK;
    }
    if (!jh_bluetooth_gamepad_candidate_matches(
            result.class_of_device,
            reinterpret_cast<const uint8_t *>(result.name),
            result.name_length)) {
      return HAL_OK;
    }
    hal_mutex_lock(s_gamepad.mutex);
    s_gamepad.selected_address = result.address;
    s_gamepad.selected_valid = true;
    hal_mutex_unlock(s_gamepad.mutex);
    hal_deb("hal_gamepad: candidate accepted class=0x%06lx name_length=%u",
            (unsigned long)result.class_of_device,
            (unsigned int)result.name_length);
    hal_bluetooth_classic_info_t info{};
    if (hal_bluetooth_classic_get_info(classic, &info) == HAL_OK &&
        info.scan_active) {
      const hal_status_t stop_status = hal_bluetooth_classic_scan_stop(classic);
      if (stop_status != HAL_OK) {
        return stop_status;
      }
    }
    return hal_bluetooth_classic_sdp_query(classic, &result.address);
  }

  const bool is_selected = selected && jh_bluetooth_classic_address_equal(
                                           &selected_address, &result.address);
  if (!is_selected) {
    return HAL_OK;
  }
  const uint32_t required =
      HAL_BLUETOOTH_CLASSIC_SERVICE_HID | HAL_BLUETOOTH_CLASSIC_SERVICE_PNP;
  hal_deb("hal_gamepad: candidate SDP services=0x%08lx required=0x%08lx",
          (unsigned long)result.services, (unsigned long)required);
  if ((result.services & required) != required) {
    return restart_pairing_discovery(classic, HAL_EPROTO);
  }
  (void)hid;
  hal_mutex_lock(s_gamepad.mutex);
  s_gamepad.connect_pending = true;
  s_gamepad.connect_settle_started_ms = hal_millis();
  hal_mutex_unlock(s_gamepad.mutex);
  return HAL_OK;
}

hal_status_t process_scan_results(hal_bluetooth_classic_t classic,
                                  hal_bluetooth_hid_host_t hid) {
  for (;;) {
    hal_bluetooth_classic_scan_result_t result{};
    const hal_status_t next =
        hal_bluetooth_classic_scan_result_next(classic, &result);
    if (next == HAL_EAGAIN) {
      return HAL_OK;
    }
    if (next != HAL_OK) {
      return next;
    }
    const hal_status_t status = select_scan_result(classic, hid, result);
    if (status != HAL_OK) {
      return status;
    }
  }
}

hal_status_t connect_selected_after_sdp_settle(hal_bluetooth_classic_t classic,
                                               hal_bluetooth_hid_host_t hid) {
  hal_mutex_lock(s_gamepad.mutex);
  if (!s_gamepad.connect_pending ||
      !hal_millis_deadline_expired(s_gamepad.connect_settle_started_ms,
                                   JH_GAMEPAD_SDP_SETTLE_MS)) {
    hal_mutex_unlock(s_gamepad.mutex);
    return HAL_OK;
  }
  const hal_bluetooth_classic_address_t address = s_gamepad.selected_address;
  s_gamepad.connect_pending = false;
  hal_mutex_unlock(s_gamepad.mutex);

  const hal_status_t status = hal_bluetooth_hid_host_connect(hid, &address);
  hal_mutex_lock(s_gamepad.mutex);
  if (status == HAL_OK) {
    s_gamepad.connection_attempt_active = true;
    hal_deb("hal_gamepad: HID connection attempt started");
  }
  hal_mutex_unlock(s_gamepad.mutex);
  return status == HAL_OK ? HAL_OK : restart_pairing_discovery(classic, status);
}

hal_status_t open_parser_from_descriptor(hal_bluetooth_hid_host_t hid) {
  uint8_t descriptor[HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN]{};
  size_t descriptor_length = 0u;
  const hal_status_t descriptor_status = hal_bluetooth_hid_host_descriptor(
      hid, descriptor, sizeof(descriptor), &descriptor_length);
  if (descriptor_status != HAL_OK) {
    return descriptor_status;
  }
  hal_deb("hal_gamepad: configuring HID descriptor length=%u",
          (unsigned int)descriptor_length);
  hal_mutex_lock(s_gamepad.mutex);
  const uint32_t previous_generation = s_gamepad.parser.current.generation;
  hal_status_t status = jh_bluetooth_gamepad_parser_configure(
      &s_gamepad.parser, descriptor, descriptor_length);
  if (status == HAL_OK) {
    s_gamepad.parser.current.generation = previous_generation;
    status = jh_bluetooth_gamepad_parser_connection_opened(&s_gamepad.parser);
  }
  s_gamepad.parser_connected = status == HAL_OK;
  s_gamepad.input_validated = false;
  s_gamepad.peer_save_requested = false;
  if (status != HAL_OK) {
    s_gamepad.info.last_status = status;
  }
  hal_mutex_unlock(s_gamepad.mutex);
  return status;
}

hal_status_t process_hid(hal_bluetooth_classic_t classic,
                         hal_bluetooth_hid_host_t hid) {
  hal_bluetooth_hid_info_t hid_info{};
  hal_status_t status = hal_bluetooth_hid_host_get_info(hid, &hid_info);
  if (status != HAL_OK) {
    return status;
  }

  hal_mutex_lock(s_gamepad.mutex);
  const bool parser_connected = s_gamepad.parser_connected;
  hal_mutex_unlock(s_gamepad.mutex);
  if (hid_info.state == HAL_BLUETOOTH_HID_STATE_CONNECTED &&
      !parser_connected) {
    status = open_parser_from_descriptor(hid);
    if (status == HAL_EAGAIN) {
      return HAL_OK;
    }
    if (status != HAL_OK) {
      (void)hal_bluetooth_hid_host_disconnect(hid);
      return status;
    }
    hal_mutex_lock(s_gamepad.mutex);
    s_gamepad.connection_attempt_active = false;
    s_gamepad.info.pairing_window_open = false;
    hal_mutex_unlock(s_gamepad.mutex);
  } else if (hid_info.state != HAL_BLUETOOTH_HID_STATE_CONNECTED &&
             parser_connected) {
    hal_mutex_lock(s_gamepad.mutex);
    close_parser_locked(hid_info.last_status);
    hal_mutex_unlock(s_gamepad.mutex);
  }

  if (hid_info.state == HAL_BLUETOOTH_HID_STATE_READY) {
    hal_mutex_lock(s_gamepad.mutex);
    const bool connection_attempt_active = s_gamepad.connection_attempt_active;
    const bool retry_discovery = s_gamepad.connection_attempt_active &&
                                 s_gamepad.info.pairing_window_open;
    if (connection_attempt_active && !retry_discovery) {
      s_gamepad.connection_attempt_active = false;
    }
    hal_mutex_unlock(s_gamepad.mutex);

    if (retry_discovery) {
      return restart_pairing_discovery(classic, hid_info.last_status);
    }
  }

  if (hid_info.state != HAL_BLUETOOTH_HID_STATE_CONNECTED) {
    return hid_info.state == HAL_BLUETOOTH_HID_STATE_FAILED
               ? hid_info.last_status
               : HAL_OK;
  }

  bool validated_now = false;
  for (;;) {
    hal_bluetooth_hid_report_t report{};
    const hal_status_t next = hal_bluetooth_hid_host_report_next(hid, &report);
    if (next == HAL_EAGAIN) {
      break;
    }
    if (next == HAL_EOVERFLOW) {
      status = HAL_EOVERFLOW;
      continue;
    }
    if (next != HAL_OK) {
      return next;
    }
    if (report.type != HAL_BLUETOOTH_HID_REPORT_INPUT || report.length == 0u) {
      continue;
    }
    hal_mutex_lock(s_gamepad.mutex);
    const hal_status_t parse_status = jh_bluetooth_gamepad_parser_parse_input(
        &s_gamepad.parser, report.data, report.length);
    if (parse_status == HAL_OK && !s_gamepad.input_validated) {
      s_gamepad.input_validated = true;
      validated_now = true;
    }
    if (parse_status != HAL_OK) {
      s_gamepad.info.last_status = parse_status;
    }
    hal_mutex_unlock(s_gamepad.mutex);
    if (parse_status != HAL_OK) {
      (void)hal_bluetooth_hid_host_disconnect(hid);
      return parse_status;
    }
  }

  if (validated_now) {
    hal_mutex_lock(s_gamepad.mutex);
    const bool save_needed = !s_gamepad.peer_save_requested;
    hal_mutex_unlock(s_gamepad.mutex);
    if (save_needed) {
      const hal_status_t save_status = hal_bluetooth_classic_peer_save(
          classic, &hid_info.peer_address, JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID);
      if (save_status != HAL_OK) {
        return save_status;
      }
      hal_mutex_lock(s_gamepad.mutex);
      s_gamepad.peer_save_requested = true;
      hal_mutex_unlock(s_gamepad.mutex);
      const hal_status_t flush_status = hal_bluetooth_classic_poll(classic);
      if (flush_status != HAL_OK) {
        return flush_status;
      }
    }
  }
  return status;
}

void refresh_info(hal_bluetooth_classic_t classic, hal_bluetooth_hid_host_t hid,
                  hal_status_t poll_status) {
  hal_bluetooth_classic_info_t classic_info{};
  hal_bluetooth_hid_info_t hid_info{};
  const hal_status_t classic_status =
      hal_bluetooth_classic_get_info(classic, &classic_info);
  const hal_status_t hid_status =
      hal_bluetooth_hid_host_get_info(hid, &hid_info);
  const bool known = matching_saved_peer(classic, nullptr);

  hal_mutex_lock(s_gamepad.mutex);
  const bool candidate_in_progress = s_gamepad.selected_valid ||
                                     s_gamepad.connect_pending ||
                                     s_gamepad.discovery_retry_pending;
  jh_bluetooth_gamepad_parser_diagnostics_t diagnostics{};
  jh_bluetooth_gamepad_parser_diagnostics(&s_gamepad.parser, &diagnostics);
  s_gamepad.info.known_device = known;
  s_gamepad.info.pairing_pending =
      classic_status == HAL_OK && classic_info.pairing_pending;
  s_gamepad.info.generation = s_gamepad.parser.current.generation;
  s_gamepad.info.dropped_snapshots = diagnostics.dropped_snapshots;
  s_gamepad.info.pending_snapshots = s_gamepad.parser.queue_count;
  if (poll_status != HAL_OK && poll_status != HAL_EOVERFLOW &&
      poll_status != HAL_EBUSY && poll_status != HAL_EAGAIN) {
    s_gamepad.info.state = HAL_GAMEPAD_STATE_FAILED;
    s_gamepad.info.last_status = poll_status;
  } else if (classic_status != HAL_OK || hid_status != HAL_OK) {
    s_gamepad.info.state = HAL_GAMEPAD_STATE_FAILED;
    s_gamepad.info.last_status =
        classic_status != HAL_OK ? classic_status : hid_status;
  } else if (classic_info.state == HAL_BLUETOOTH_CLASSIC_STATE_STARTING) {
    s_gamepad.info.state = HAL_GAMEPAD_STATE_STARTING;
  } else if (classic_info.state == HAL_BLUETOOTH_CLASSIC_STATE_FAILED ||
             hid_info.state == HAL_BLUETOOTH_HID_STATE_FAILED) {
    s_gamepad.info.state = HAL_GAMEPAD_STATE_FAILED;
    s_gamepad.info.last_status =
        hid_info.state == HAL_BLUETOOTH_HID_STATE_FAILED
            ? hid_info.last_status
            : classic_info.last_status;
  } else if (hid_info.state == HAL_BLUETOOTH_HID_STATE_CONNECTED &&
             s_gamepad.parser_connected) {
    s_gamepad.info.state = HAL_GAMEPAD_STATE_CONNECTED;
    s_gamepad.info.last_status = HAL_OK;
  } else if (hid_info.state == HAL_BLUETOOTH_HID_STATE_CONNECTING ||
             hid_info.state == HAL_BLUETOOTH_HID_STATE_CONNECTED) {
    s_gamepad.info.state = HAL_GAMEPAD_STATE_CONNECTING;
  } else if (classic_info.scan_active || s_gamepad.info.pairing_window_open) {
    s_gamepad.info.state = HAL_GAMEPAD_STATE_DISCOVERING;
    if (!classic_info.scan_active && !classic_info.pairing_pending &&
        !candidate_in_progress) {
      s_gamepad.info.pairing_window_open = false;
      s_gamepad.info.state = HAL_GAMEPAD_STATE_READY;
    }
  } else {
    s_gamepad.info.state = HAL_GAMEPAD_STATE_READY;
    if (hid_info.last_status != HAL_OK && hid_info.last_status != HAL_NONE) {
      s_gamepad.info.last_status = hid_info.last_status;
    } else if (poll_status == HAL_OK) {
      s_gamepad.info.last_status = HAL_OK;
    }
  }
  hal_mutex_unlock(s_gamepad.mutex);
}

hal_status_t service_adapter(hal_bluetooth_classic_t classic,
                             hal_bluetooth_hid_host_t hid) {
  hal_status_t status = hal_bluetooth_classic_poll(classic);
  if (status == HAL_OK) {
    status = resume_pairing_discovery(classic);
  }
  if (status == HAL_OK) {
    hal_mutex_lock(s_gamepad.mutex);
    const bool retry_pending = s_gamepad.discovery_retry_pending;
    hal_mutex_unlock(s_gamepad.mutex);
    if (!retry_pending) {
      status = process_scan_results(classic, hid);
    }
  }
  if (status == HAL_OK) {
    status = connect_selected_after_sdp_settle(classic, hid);
  }
  if (status == HAL_OK || status == HAL_EOVERFLOW || status == HAL_EBUSY ||
      status == HAL_EAGAIN) {
    const hal_status_t hid_process_status = process_hid(classic, hid);
    if (status == HAL_OK || hid_process_status != HAL_OK) {
      status = hid_process_status;
    }
  }
  if (status != HAL_OK && status != HAL_EOVERFLOW && status != HAL_EBUSY &&
      status != HAL_EAGAIN) {
    hal_mutex_lock(s_gamepad.mutex);
    close_parser_locked(status);
    hal_mutex_unlock(s_gamepad.mutex);
  }
  refresh_info(classic, hid, status);
  hal_mutex_lock(s_gamepad.mutex);
  const bool parser_overflow = s_gamepad.parser.overflow_pending;
  hal_mutex_unlock(s_gamepad.mutex);
  return status == HAL_OK && parser_overflow ? HAL_EOVERFLOW : status;
}

} // namespace

hal_status_t hal_gamepad_open(hal_gamepad_t *out_gamepad) {
  return hal_gamepad_open_ex(out_gamepad, nullptr);
}

hal_status_t
hal_gamepad_open_ex(hal_gamepad_t *out_gamepad,
                    const hal_gamepad_bond_provider_t *bond_provider) {
  if (out_gamepad == nullptr) {
    return HAL_EINVAL;
  }
  *out_gamepad = nullptr;
  if (bond_provider != nullptr &&
      (bond_provider->load == nullptr || bond_provider->store == nullptr ||
       bond_provider->erase == nullptr)) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_gamepad.classic != nullptr || s_gamepad.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  hal_status_t status = ensure_handle_pool_locked();
  void *handle = nullptr;
  if (status == HAL_OK) {
    status = jh_handle_allocate(&s_gamepad.handle_pool, &s_gamepad, &handle);
  }
  if (status != HAL_OK) {
    hal_mutex_unlock(mutex);
    return status;
  }
  memset(&s_gamepad.legacy_provider, 0, sizeof(s_gamepad.legacy_provider));
  memset(&s_gamepad.classic_provider, 0, sizeof(s_gamepad.classic_provider));
  memset(&s_gamepad.info, 0, sizeof(s_gamepad.info));
  memset(&s_gamepad.selected_address, 0, sizeof(s_gamepad.selected_address));
  jh_bluetooth_gamepad_parser_init(&s_gamepad.parser);
  s_gamepad.legacy_provider_enabled = bond_provider != nullptr;
  if (bond_provider != nullptr) {
    s_gamepad.legacy_provider = *bond_provider;
    s_gamepad.classic_provider.context = &s_gamepad;
    s_gamepad.classic_provider.capacity = 1u;
    s_gamepad.classic_provider.load = legacy_load;
    s_gamepad.classic_provider.store = legacy_store;
    s_gamepad.classic_provider.erase = legacy_erase;
  }
  s_gamepad.operation_active = true;
  s_gamepad.public_handle = static_cast<hal_gamepad_t>(handle);
  s_gamepad.selected_valid = false;
  s_gamepad.connect_pending = false;
  s_gamepad.connection_attempt_active = false;
  s_gamepad.parser_connected = false;
  s_gamepad.input_validated = false;
  s_gamepad.peer_save_requested = false;
  s_gamepad.discovery_retry_pending = false;
  s_gamepad.discovery_retry_started_ms = 0u;
  s_gamepad.connect_settle_started_ms = 0u;
  s_gamepad.info.state = HAL_GAMEPAD_STATE_STARTING;
  s_gamepad.info.last_status = HAL_NONE;
#if HAL_TARGET_IS_MOCK
  s_gamepad.mock_bond_store_calls = 0u;
  s_gamepad.mock_bond_erase_calls = 0u;
  s_gamepad.mock_last_bond_store_status = HAL_NONE;
#endif
  hal_mutex_unlock(mutex);

  hal_bluetooth_classic_t classic = nullptr;
  status = hal_bluetooth_classic_open_ex(
      &classic,
      bond_provider != nullptr ? &s_gamepad.classic_provider : nullptr);
  hal_bluetooth_hid_host_t hid = nullptr;
  if (status == HAL_OK) {
    status = hal_bluetooth_hid_host_open(classic, &hid);
  }
  if (status != HAL_OK && classic != nullptr) {
    (void)hal_bluetooth_classic_close(classic);
  }

  hal_mutex_lock(mutex);
  s_gamepad.operation_active = false;
  if (status == HAL_OK) {
    s_gamepad.classic = classic;
    s_gamepad.hid = hid;
    *out_gamepad = static_cast<hal_gamepad_t>(handle);
  } else {
    (void)release_handle_locked(handle);
    s_gamepad.public_handle = nullptr;
  }
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_gamepad_close(hal_gamepad_t gamepad) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(gamepad)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_gamepad.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_gamepad.operation_active = true;
  hal_bluetooth_hid_host_t hid = s_gamepad.hid;
  hal_bluetooth_classic_t classic = s_gamepad.classic;
  hal_mutex_unlock(mutex);

  hal_bluetooth_hid_info_t hid_info{};
  if (hal_bluetooth_hid_host_get_info(hid, &hid_info) == HAL_OK &&
      (hid_info.state == HAL_BLUETOOTH_HID_STATE_CONNECTED ||
       hid_info.state == HAL_BLUETOOTH_HID_STATE_CONNECTING)) {
    (void)hal_bluetooth_hid_host_disconnect(hid);
  }
  hal_status_t status = hal_bluetooth_hid_host_close(hid);
  const hal_status_t classic_status = hal_bluetooth_classic_close(classic);
  if (status == HAL_OK) {
    status = classic_status;
  }

  hal_mutex_lock(mutex);
  close_parser_locked(HAL_OK);
  const hal_status_t release_status = release_handle_locked(gamepad);
  s_gamepad.classic = nullptr;
  s_gamepad.hid = nullptr;
  s_gamepad.public_handle = nullptr;
  s_gamepad.operation_active = false;
  s_gamepad.info.state = HAL_GAMEPAD_STATE_UNINITIALIZED;
  s_gamepad.info.pairing_window_open = false;
  s_gamepad.info.pairing_pending = false;
  s_gamepad.connect_pending = false;
  s_gamepad.connection_attempt_active = false;
  if (status == HAL_OK) {
    status = release_status;
  }
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_gamepad_poll(hal_gamepad_t gamepad) {
  return run_operation(gamepad, [](hal_bluetooth_classic_t classic,
                                   hal_bluetooth_hid_host_t hid) {
    return service_adapter(classic, hid);
  });
}

hal_status_t hal_gamepad_get_info(hal_gamepad_t gamepad,
                                  hal_gamepad_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  return run_operation(gamepad, [out_info](hal_bluetooth_classic_t classic,
                                           hal_bluetooth_hid_host_t hid) {
    refresh_info(classic, hid, HAL_OK);
    hal_mutex_lock(s_gamepad.mutex);
    *out_info = s_gamepad.info;
    hal_mutex_unlock(s_gamepad.mutex);
    return HAL_OK;
  });
}

hal_status_t hal_gamepad_snapshot(hal_gamepad_t gamepad,
                                  hal_gamepad_snapshot_t *out_snapshot) {
  if (out_snapshot == nullptr) {
    return HAL_EINVAL;
  }
  return run_operation(gamepad, [out_snapshot](hal_bluetooth_classic_t,
                                               hal_bluetooth_hid_host_t) {
    jh_bluetooth_gamepad_snapshot_t snapshot{};
    hal_mutex_lock(s_gamepad.mutex);
    const hal_status_t status =
        jh_bluetooth_gamepad_parser_snapshot(&s_gamepad.parser, &snapshot);
    if (status == HAL_OK) {
      copy_snapshot(snapshot, out_snapshot);
    }
    hal_mutex_unlock(s_gamepad.mutex);
    return status;
  });
}

hal_status_t hal_gamepad_snapshot_next(hal_gamepad_t gamepad,
                                       hal_gamepad_snapshot_t *out_snapshot) {
  if (out_snapshot == nullptr) {
    return HAL_EINVAL;
  }
  return run_operation(gamepad, [out_snapshot](hal_bluetooth_classic_t,
                                               hal_bluetooth_hid_host_t) {
    jh_bluetooth_gamepad_snapshot_t snapshot{};
    hal_mutex_lock(s_gamepad.mutex);
    const hal_status_t status =
        jh_bluetooth_gamepad_parser_next(&s_gamepad.parser, &snapshot);
    if (status == HAL_OK) {
      copy_snapshot(snapshot, out_snapshot);
    }
    s_gamepad.info.pending_snapshots = s_gamepad.parser.queue_count;
    hal_mutex_unlock(s_gamepad.mutex);
    return status;
  });
}

hal_status_t hal_gamepad_pairing_open(hal_gamepad_t gamepad) {
  return run_operation(gamepad, [](hal_bluetooth_classic_t classic,
                                   hal_bluetooth_hid_host_t hid) {
    hal_bluetooth_hid_info_t hid_info{};
    hal_bluetooth_classic_info_t classic_info{};
    hal_status_t status = hal_bluetooth_hid_host_get_info(hid, &hid_info);
    if (status == HAL_OK) {
      status = hal_bluetooth_classic_get_info(classic, &classic_info);
    }
    if (status != HAL_OK) {
      return status;
    }
    if (classic_info.state != HAL_BLUETOOTH_CLASSIC_STATE_READY ||
        classic_info.scan_active ||
        hid_info.state != HAL_BLUETOOTH_HID_STATE_READY) {
      return HAL_ESTATE;
    }
    status =
        hal_bluetooth_classic_scan_start(classic, JH_GAMEPAD_PAIRING_WINDOW_MS);
    if (status == HAL_OK) {
      hal_mutex_lock(s_gamepad.mutex);
      s_gamepad.info.pairing_window_open = true;
      s_gamepad.pairing_window_started_ms = hal_millis();
      s_gamepad.selected_valid = false;
      s_gamepad.connect_pending = false;
      s_gamepad.connection_attempt_active = false;
      s_gamepad.discovery_retry_pending = false;
      hal_mutex_unlock(s_gamepad.mutex);
    }
    return status;
  });
}

hal_status_t hal_gamepad_pairing_authorize(hal_gamepad_t gamepad) {
  return run_operation(
      gamepad, [](hal_bluetooth_classic_t classic, hal_bluetooth_hid_host_t) {
        return hal_bluetooth_classic_pairing_authorize(classic);
      });
}

hal_status_t hal_gamepad_reconnect(hal_gamepad_t gamepad) {
  return run_operation(gamepad, [](hal_bluetooth_classic_t classic,
                                   hal_bluetooth_hid_host_t hid) {
    hal_bluetooth_classic_peer_t peer{};
    if (!matching_saved_peer(classic, &peer)) {
      return HAL_ESTATE;
    }
    const hal_status_t status =
        hal_bluetooth_hid_host_connect(hid, &peer.address);
    if (status == HAL_OK) {
      hal_mutex_lock(s_gamepad.mutex);
      s_gamepad.connection_attempt_active = true;
      hal_mutex_unlock(s_gamepad.mutex);
    }
    return status;
  });
}

hal_status_t hal_gamepad_disconnect(hal_gamepad_t gamepad) {
  return run_operation(
      gamepad, [](hal_bluetooth_classic_t, hal_bluetooth_hid_host_t hid) {
        hal_bluetooth_hid_info_t info{};
        const hal_status_t status = hal_bluetooth_hid_host_get_info(hid, &info);
        if (status != HAL_OK) {
          return status;
        }
        if (info.state != HAL_BLUETOOTH_HID_STATE_CONNECTED &&
            info.state != HAL_BLUETOOTH_HID_STATE_CONNECTING) {
          return HAL_ESTATE;
        }
        return hal_bluetooth_hid_host_disconnect(hid);
      });
}

hal_status_t hal_gamepad_forget(hal_gamepad_t gamepad) {
  return run_operation(gamepad, [](hal_bluetooth_classic_t classic,
                                   hal_bluetooth_hid_host_t hid) {
    hal_bluetooth_hid_info_t hid_info{};
    if (hal_bluetooth_hid_host_get_info(hid, &hid_info) == HAL_OK &&
        (hid_info.state == HAL_BLUETOOTH_HID_STATE_CONNECTED ||
         hid_info.state == HAL_BLUETOOTH_HID_STATE_CONNECTING)) {
      (void)hal_bluetooth_hid_host_disconnect(hid);
    }
    hal_mutex_lock(s_gamepad.mutex);
    close_parser_locked(HAL_OK);
    s_gamepad.selected_valid = false;
    s_gamepad.connect_pending = false;
    s_gamepad.connection_attempt_active = false;
    s_gamepad.discovery_retry_pending = false;
    s_gamepad.info.pairing_window_open = false;
    hal_mutex_unlock(s_gamepad.mutex);
    hal_bluetooth_classic_peer_t peer{};
    if (!matching_saved_peer(classic, &peer)) {
      return HAL_OK;
    }
    return hal_bluetooth_classic_peer_forget(classic, &peer.address);
  });
}

#if HAL_TARGET_IS_MOCK

namespace {

const hal_bluetooth_classic_address_t s_mock_address = {
    {0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u}};

const uint8_t s_mock_descriptor[] = {
    0x05u, 0x01u, 0x09u, 0x05u, 0xa1u, 0x01u, 0x15u, 0x00u, 0x25u, 0x01u, 0x75u,
    0x01u, 0x95u, 0x01u, 0x05u, 0x09u, 0x09u, 0x01u, 0x81u, 0x02u, 0xc0u,
};

hal_status_t mock_authorize(hal_bluetooth_classic_t classic,
                            const hal_bluetooth_classic_address_t *address) {
  hal_status_t status = hal_mock_bluetooth_classic_inject_pairing_request(
      address, HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS);
  return status == HAL_OK ? hal_bluetooth_classic_pairing_authorize(classic)
                          : status;
}

hal_status_t mock_components(hal_gamepad_t *out_gamepad,
                             hal_bluetooth_classic_t *out_classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(s_gamepad.public_handle);
  if (valid) {
    *out_gamepad = s_gamepad.public_handle;
    *out_classic = s_gamepad.classic;
  }
  hal_mutex_unlock(mutex);
  return valid ? HAL_OK : HAL_EUNINIT;
}

void mock_enqueue_locked(const hal_gamepad_snapshot_t &source) {
  if (s_gamepad.parser.queue_count == JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY) {
    s_gamepad.parser.diagnostics.dropped_snapshots +=
        s_gamepad.parser.queue_count;
    s_gamepad.parser.queue_head = 0u;
    s_gamepad.parser.queue_count = 0u;
    s_gamepad.parser.overflow_pending = true;
  }
  jh_bluetooth_gamepad_snapshot_t snapshot{};
  snapshot.generation = s_gamepad.parser.current.generation;
  snapshot.buttons = source.buttons;
  memcpy(snapshot.axes, source.axes, sizeof(snapshot.axes));
  snapshot.axes_present = source.axes_present;
  snapshot.dpad = source.dpad;
  snapshot.connected = true;
  s_gamepad.parser.current = snapshot;
  const uint8_t tail =
      (uint8_t)((s_gamepad.parser.queue_head + s_gamepad.parser.queue_count) %
                JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY);
  s_gamepad.parser.queue[tail] = snapshot;
  ++s_gamepad.parser.queue_count;
}

} // namespace

void hal_mock_gamepad_runtime_full_reset(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_gamepad.handle_pool_initialized) {
    jh_handle_invalidate_all(&s_gamepad.handle_pool);
  }
  s_gamepad.classic = nullptr;
  s_gamepad.hid = nullptr;
  s_gamepad.public_handle = nullptr;
  s_gamepad.operation_active = false;
  s_gamepad.parser_connected = false;
  hal_mutex_unlock(mutex);
  hal_mock_bluetooth_hid_runtime_full_reset();
  hal_mock_bluetooth_classic_runtime_full_reset();
}

void hal_mock_gamepad_reset(void) { hal_mock_bluetooth_classic_reset(); }

hal_status_t hal_mock_gamepad_inject_ready(bool known_device) {
  hal_gamepad_t gamepad = nullptr;
  hal_bluetooth_classic_t classic = nullptr;
  hal_status_t status = mock_components(&gamepad, &classic);
  if (status != HAL_OK) {
    return status;
  }
  status = hal_mock_bluetooth_classic_inject_ready();
  if (status == HAL_OK && known_device) {
    const uint8_t key[16] = {1u, 2u,  3u,  4u,  5u,  6u,  7u,  8u,
                             9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u};
    status = mock_authorize(classic, &s_mock_address);
    if (status == HAL_OK) {
      status =
          hal_mock_bluetooth_classic_inject_link_key(&s_mock_address, key, 4u);
    }
    if (status == HAL_OK) {
      status = hal_bluetooth_classic_peer_save(
          classic, &s_mock_address, JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID);
    }
    if (status == HAL_OK) {
      status = hal_bluetooth_classic_poll(classic);
    }
  }
  if (status == HAL_OK) {
    refresh_info(classic, s_gamepad.hid, HAL_OK);
  }
  return status;
}

hal_status_t hal_mock_gamepad_inject_pairing_request(void) {
  return hal_mock_bluetooth_classic_inject_pairing_request(
      &s_mock_address, HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS);
}

hal_status_t hal_mock_gamepad_inject_connect(void) {
  hal_gamepad_t gamepad = nullptr;
  hal_bluetooth_classic_t classic = nullptr;
  hal_status_t status = mock_components(&gamepad, &classic);
  if (status != HAL_OK) {
    return status;
  }
  const uint8_t key[16] = {1u, 2u,  3u,  4u,  5u,  6u,  7u,  8u,
                           9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u};
  hal_bluetooth_classic_info_t classic_info{};
  if (hal_bluetooth_classic_get_info(classic, &classic_info) == HAL_OK &&
      classic_info.scan_active) {
    status = hal_bluetooth_classic_scan_stop(classic);
  }
  if (status == HAL_OK) {
    hal_bluetooth_classic_peer_t peer{};
    const bool needs_save = !matching_saved_peer(classic, &peer);
    if (needs_save) {
      status = mock_authorize(classic, &s_mock_address);
    }
    if (status == HAL_OK && needs_save) {
      status =
          hal_mock_bluetooth_classic_inject_link_key(&s_mock_address, key, 4u);
    }
    if (status == HAL_OK) {
      status = hal_mock_bluetooth_hid_inject_connected(&s_mock_address);
    }
    if (status == HAL_OK) {
      status = hal_mock_bluetooth_hid_inject_descriptor(
          s_mock_descriptor, sizeof(s_mock_descriptor));
    }
    if (status == HAL_OK && needs_save) {
      status = hal_bluetooth_classic_peer_save(
          classic, &s_mock_address, JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID);
    }
    if (status == HAL_OK && needs_save) {
      status = hal_bluetooth_classic_poll(classic);
    }
  }
  if (status == HAL_OK) {
    status = hal_gamepad_poll(gamepad);
  }
  return status;
}

hal_status_t
hal_mock_gamepad_inject_snapshot(const hal_gamepad_snapshot_t *snapshot) {
  if (snapshot == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(s_gamepad.public_handle)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (!s_gamepad.parser_connected) {
    hal_mutex_unlock(mutex);
    return HAL_ESTATE;
  }
  mock_enqueue_locked(*snapshot);
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_mock_gamepad_inject_disconnect(void) {
  hal_gamepad_t gamepad = nullptr;
  hal_bluetooth_classic_t classic = nullptr;
  hal_status_t status = mock_components(&gamepad, &classic);
  if (status != HAL_OK) {
    return status;
  }
  status = hal_mock_bluetooth_hid_inject_disconnected(HAL_OK);
  return status == HAL_OK ? hal_gamepad_poll(gamepad) : status;
}

hal_status_t hal_mock_gamepad_inject_transport_error(hal_status_t status) {
  if (status >= HAL_NONE) {
    return HAL_EINVAL;
  }
  return hal_mock_bluetooth_classic_inject_error(status, true);
}

void hal_mock_gamepad_set_service_status(hal_status_t status) {
  if (status != HAL_OK) {
    (void)hal_mock_bluetooth_classic_inject_error(status, true);
  }
}

hal_status_t hal_mock_gamepad_inject_bond_store(void) {
  hal_gamepad_t gamepad = nullptr;
  hal_bluetooth_classic_t classic = nullptr;
  hal_status_t status = mock_components(&gamepad, &classic);
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_gamepad.mutex);
  const bool provider_enabled = s_gamepad.legacy_provider_enabled;
  hal_mutex_unlock(s_gamepad.mutex);
  if (!provider_enabled) {
    return HAL_EUNSUPPORTED;
  }
  const uint8_t key[16] = {1u, 2u,  3u,  4u,  5u,  6u,  7u,  8u,
                           9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u};
  status = mock_authorize(classic, &s_mock_address);
  if (status == HAL_OK) {
    status =
        hal_mock_bluetooth_classic_inject_link_key(&s_mock_address, key, 4u);
  }
  if (status == HAL_OK) {
    status = hal_bluetooth_classic_peer_save(
        classic, &s_mock_address, JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID);
  }
  return status == HAL_OK ? hal_bluetooth_classic_poll(classic) : status;
}

uint32_t hal_mock_gamepad_bond_store_calls(void) {
  return s_gamepad.mock_bond_store_calls;
}

uint32_t hal_mock_gamepad_bond_erase_calls(void) {
  return s_gamepad.mock_bond_erase_calls;
}

hal_status_t hal_mock_gamepad_last_bond_store_status(void) {
  return s_gamepad.mock_last_bond_store_status;
}

#endif /* HAL_TARGET_IS_MOCK */

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD */
