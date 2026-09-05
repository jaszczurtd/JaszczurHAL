#include "hal/bluetooth/hal_bluetooth_classic.h"

#ifdef HAL_ENABLE_BLUETOOTH_CLASSIC

#include "hal/bluetooth/jh_bluetooth_classic_address.h"
#include "hal/bluetooth/jh_bluetooth_classic_backend.h"
#include "hal/bluetooth/jh_bluetooth_classic_bond_codec.h"
#include "hal/bluetooth/jh_bluetooth_classic_runtime.h"
#include "hal/bluetooth/jh_bluetooth_runtime.h"
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
#include "hal/bluetooth/jh_bluetooth_a2dp_runtime.h"
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
#include "hal/bluetooth/jh_bluetooth_avrcp_runtime.h"
#endif
#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_target.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/security/jh_secure_random.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include <string.h>

#define JH_BLUETOOTH_CLASSIC_HANDLE_KIND 15u

namespace {

struct peer_slot_t {
  jh_bluetooth_classic_bond_identity_t identity;
  size_t storage_index;
  bool used;
};

struct classic_runtime_t {
  hal_mutex_t mutex;
  const jh_bluetooth_classic_backend_t *backend;
  jh_handle_pool_t handle_pool;
  jh_handle_slot_t handle_slot;
  hal_bluetooth_classic_bond_provider_t provider;
  hal_bluetooth_classic_info_t info;
  hal_bluetooth_classic_scan_result_t
      scan_results[HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH];
  peer_slot_t peers[HAL_BLUETOOTH_CLASSIC_MAX_PEERS];
  jh_bluetooth_classic_bond_identity_t pending_key;
  hal_bluetooth_classic_address_t pending_save_address;
  hal_bluetooth_classic_address_t approved_pairing_address;
  uint16_t pending_profile_id;
  uint32_t next_sequence;
  size_t scan_head;
  size_t scan_count;
  bool handle_pool_initialized;
  bool operation_active;
  bool overflow_pending;
  bool provider_enabled;
  bool pending_key_valid;
  bool pending_save_valid;
  bool approved_pairing_valid;
  uint32_t pairing_window_started_ms;
  uint32_t pairing_window_duration_ms;
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  bool a2dp_attached;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  bool avrcp_attached;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  hal_bluetooth_hid_info_t hid_info;
  hal_bluetooth_hid_report_t hid_reports[HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH];
  uint8_t hid_descriptor[HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN];
  size_t hid_report_head;
  size_t hid_report_count;
  bool hid_attached;
  bool hid_overflow_pending;
#endif
};

classic_runtime_t s_classic{};

hal_mutex_t runtime_mutex() {
  return jh_hal_mutex_create_once(&s_classic.mutex);
}

hal_status_t ensure_handle_pool_locked() {
  if (s_classic.handle_pool_initialized) {
    return HAL_OK;
  }
  const hal_status_t status =
      jh_handle_pool_init(&s_classic.handle_pool, &s_classic.handle_slot, 1u,
                          JH_BLUETOOTH_CLASSIC_HANDLE_KIND);
  s_classic.handle_pool_initialized = status == HAL_OK;
  return status;
}

bool handle_valid_locked(hal_bluetooth_classic_t classic) {
  if (!s_classic.handle_pool_initialized || s_classic.backend == nullptr) {
    return false;
  }
  void *runtime = nullptr;
  return jh_handle_resolve(&s_classic.handle_pool, classic, &runtime,
                           nullptr) == HAL_OK &&
         runtime == &s_classic;
}

hal_status_t release_handle_locked(const void *handle) {
  void *runtime = nullptr;
  hal_status_t status =
      jh_handle_release(&s_classic.handle_pool, handle, &runtime);
  if (status == HAL_OK && runtime != &s_classic) {
    status = HAL_ESTATE;
  }
  if (status != HAL_OK) {
    jh_handle_invalidate_all(&s_classic.handle_pool);
  }
  return status;
}

void reset_scan_queue_locked() {
  s_classic.scan_head = 0u;
  s_classic.scan_count = 0u;
  s_classic.overflow_pending = false;
  s_classic.info.pending_scan_results = 0u;
  memset(s_classic.scan_results, 0, sizeof(s_classic.scan_results));
}

void queue_scan_result_locked(
    const hal_bluetooth_classic_scan_result_t &result) {
  if (s_classic.scan_count == HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH) {
    ++s_classic.info.dropped_scan_results;
    s_classic.overflow_pending = true;
    return;
  }
  const size_t tail = (s_classic.scan_head + s_classic.scan_count) %
                      HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH;
  s_classic.scan_results[tail] = result;
  ++s_classic.scan_count;
  s_classic.info.pending_scan_results = s_classic.scan_count;
}

size_t peer_count_locked() {
  size_t count = 0u;
  for (const peer_slot_t &slot : s_classic.peers) {
    count += slot.used ? 1u : 0u;
  }
  return count;
}

void clear_bond_state_locked() {
  jh_secure_zeroize(s_classic.peers, sizeof(s_classic.peers));
  jh_secure_zeroize(&s_classic.pending_key, sizeof(s_classic.pending_key));
  memset(&s_classic.pending_save_address, 0,
         sizeof(s_classic.pending_save_address));
  memset(&s_classic.approved_pairing_address, 0,
         sizeof(s_classic.approved_pairing_address));
  s_classic.next_sequence = 0u;
  s_classic.pending_key_valid = false;
  s_classic.pending_save_valid = false;
  s_classic.approved_pairing_valid = false;
  s_classic.info.peer_count = 0u;
}

peer_slot_t *find_peer_locked(const hal_bluetooth_classic_address_t &address) {
  for (peer_slot_t &slot : s_classic.peers) {
    if (slot.used &&
        jh_bluetooth_classic_address_equal(&slot.identity.address, &address)) {
      return &slot;
    }
  }
  return nullptr;
}

peer_slot_t *peer_by_dense_index_locked(size_t index) {
  for (peer_slot_t &slot : s_classic.peers) {
    if (slot.used && index-- == 0u) {
      return &slot;
    }
  }
  return nullptr;
}

peer_slot_t *
select_peer_slot_locked(const hal_bluetooth_classic_address_t &address,
                        size_t *out_index) {
  peer_slot_t *empty = nullptr;
  peer_slot_t *oldest = nullptr;
  for (size_t index = 0u; index < HAL_BLUETOOTH_CLASSIC_MAX_PEERS; ++index) {
    peer_slot_t &slot = s_classic.peers[index];
    if (slot.used &&
        jh_bluetooth_classic_address_equal(&slot.identity.address, &address)) {
      *out_index = slot.storage_index;
      return &slot;
    }
    if (!slot.used && empty == nullptr) {
      empty = &slot;
      *out_index = index;
    } else if (slot.used &&
               (oldest == nullptr ||
                slot.identity.sequence < oldest->identity.sequence)) {
      oldest = &slot;
    }
  }
  if (empty != nullptr) {
    return empty;
  }
  if (oldest == nullptr) {
    return nullptr;
  }
  *out_index = oldest->storage_index;
  return oldest;
}

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
void reset_hid_locked() {
  memset(&s_classic.hid_info, 0, sizeof(s_classic.hid_info));
  s_classic.hid_info.state = s_classic.hid_attached
                                 ? HAL_BLUETOOTH_HID_STATE_READY
                                 : HAL_BLUETOOTH_HID_STATE_CLOSED;
  s_classic.hid_report_head = 0u;
  s_classic.hid_report_count = 0u;
  s_classic.hid_overflow_pending = false;
  memset(s_classic.hid_reports, 0, sizeof(s_classic.hid_reports));
  memset(s_classic.hid_descriptor, 0, sizeof(s_classic.hid_descriptor));
}

void queue_hid_report_locked(const hal_bluetooth_hid_report_t &report) {
  if (!s_classic.hid_attached) {
    return;
  }
  if (s_classic.hid_report_count == HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH) {
    ++s_classic.hid_info.dropped_reports;
    s_classic.hid_overflow_pending = true;
    return;
  }
  const size_t tail = (s_classic.hid_report_head + s_classic.hid_report_count) %
                      HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH;
  s_classic.hid_reports[tail] = report;
  ++s_classic.hid_report_count;
  s_classic.hid_info.pending_reports = s_classic.hid_report_count;
}
#endif

void backend_event(void *, const jh_bluetooth_classic_backend_event_t *event) {
  if (event == nullptr) {
    return;
  }
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  jh_bluetooth_a2dp_sink_backend_event(event);
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  jh_bluetooth_avrcp_target_backend_event(event);
#endif
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_classic.backend == nullptr) {
    hal_mutex_unlock(mutex);
    return;
  }
  switch (event->type) {
  case JH_BLUETOOTH_CLASSIC_EVENT_READY:
    s_classic.info.state = HAL_BLUETOOTH_CLASSIC_STATE_READY;
    s_classic.info.last_status = HAL_OK;
    ++s_classic.info.generation;
    if (s_classic.info.generation == 0u) {
      s_classic.info.generation = 1u;
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_SCAN_RESULT:
    queue_scan_result_locked(event->scan_result);
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_SCAN_STOPPED:
    s_classic.info.scan_active = false;
    if (s_classic.info.state != HAL_BLUETOOTH_CLASSIC_STATE_FAILED) {
      s_classic.info.state = HAL_BLUETOOTH_CLASSIC_STATE_READY;
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_PAIRING_REQUEST:
    s_classic.approved_pairing_valid = false;
    s_classic.info.pairing_pending = true;
    s_classic.info.pairing_method = event->pairing_method;
    s_classic.info.pairing_address = event->address;
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_AUTHENTICATION:
    s_classic.info.last_status = event->status;
    s_classic.info.pairing_pending = false;
    s_classic.info.pairing_method = HAL_BLUETOOTH_CLASSIC_PAIRING_NONE;
    memset(&s_classic.info.pairing_address, 0,
           sizeof(s_classic.info.pairing_address));
    if (event->status != HAL_OK) {
      s_classic.approved_pairing_valid = false;
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_LINK_KEY:
    jh_secure_zeroize(&s_classic.pending_key, sizeof(s_classic.pending_key));
    s_classic.pending_key.address = event->address;
    memcpy(s_classic.pending_key.link_key, event->link_key,
           sizeof(s_classic.pending_key.link_key));
    s_classic.pending_key.link_key_type = event->link_key_type;
    s_classic.pending_key_valid = true;
    break;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  case JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTING:
    if (s_classic.hid_attached &&
        s_classic.hid_info.state != HAL_BLUETOOTH_HID_STATE_CONNECTED) {
      s_classic.hid_info.state = HAL_BLUETOOTH_HID_STATE_CONNECTING;
      s_classic.hid_info.last_status = HAL_OK;
      s_classic.hid_info.peer_address = event->address;
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTED:
    if (s_classic.hid_attached) {
      s_classic.hid_info.state = HAL_BLUETOOTH_HID_STATE_CONNECTED;
      s_classic.hid_info.last_status = HAL_OK;
      s_classic.hid_info.peer_address = event->address;
      ++s_classic.hid_info.generation;
      if (s_classic.hid_info.generation == 0u) {
        s_classic.hid_info.generation = 1u;
      }
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_HID_DESCRIPTOR:
    if (s_classic.hid_attached &&
        event->descriptor_length <= sizeof(s_classic.hid_descriptor)) {
      memcpy(s_classic.hid_descriptor, event->descriptor,
             event->descriptor_length);
      s_classic.hid_info.descriptor_length = event->descriptor_length;
      s_classic.hid_info.descriptor_available = true;
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_HID_REPORT:
    queue_hid_report_locked(event->hid_report);
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_HID_DISCONNECTED:
    if (s_classic.hid_attached) {
      s_classic.hid_info.state = HAL_BLUETOOTH_HID_STATE_READY;
      s_classic.hid_info.last_status = event->status;
      memset(&s_classic.hid_info.peer_address, 0,
             sizeof(s_classic.hid_info.peer_address));
      s_classic.hid_info.descriptor_available = false;
      s_classic.hid_info.descriptor_length = 0u;
      memset(s_classic.hid_descriptor, 0, sizeof(s_classic.hid_descriptor));
    }
    break;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_CONNECTED:
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_FORMAT:
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_STARTED:
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_SUSPENDED:
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_STOPPED:
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_DISCONNECTED:
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_MEDIA:
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_PCM:
    break;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  case JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_CONNECTED:
  case JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_DISCONNECTED:
  case JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_VOLUME:
    break;
#endif
  case JH_BLUETOOTH_CLASSIC_EVENT_ERROR:
    s_classic.info.last_status = event->status;
    if (event->fatal) {
      s_classic.info.state = HAL_BLUETOOTH_CLASSIC_STATE_FAILED;
      s_classic.info.scan_active = false;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
      if (s_classic.hid_attached) {
        s_classic.hid_info.state = HAL_BLUETOOTH_HID_STATE_FAILED;
        s_classic.hid_info.last_status = event->status;
      }
#endif
    }
    break;
  }
  hal_mutex_unlock(mutex);
}

bool backend_valid(const jh_bluetooth_classic_backend_t *backend) {
  if (backend == nullptr || backend->start == nullptr ||
      backend->stop == nullptr || backend->service == nullptr ||
      backend->scan_start == nullptr || backend->scan_stop == nullptr ||
      backend->sdp_query == nullptr || backend->pair == nullptr ||
      backend->pairing_reply == nullptr || backend->peer_restore == nullptr ||
      backend->peer_forget == nullptr || backend->identity_set == nullptr ||
      backend->visibility_set == nullptr) {
    return false;
  }
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  if (backend->hid_connect == nullptr || backend->hid_disconnect == nullptr ||
      backend->hid_report_send == nullptr ||
      backend->hid_report_request == nullptr) {
    return false;
  }
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  if (backend->a2dp_attach == nullptr || backend->a2dp_detach == nullptr ||
      backend->a2dp_decode == nullptr) {
    return false;
  }
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  if (backend->avrcp_attach == nullptr || backend->avrcp_detach == nullptr ||
      backend->avrcp_volume_set == nullptr) {
    return false;
  }
#endif
  return true;
}

hal_status_t restore_provider_records() {
  if (!s_classic.provider_enabled) {
    return HAL_OK;
  }
  for (size_t index = 0u; index < s_classic.provider.capacity; ++index) {
    hal_bluetooth_classic_bond_blob_t blob{};
    hal_status_t status =
        s_classic.provider.load(s_classic.provider.context, index, &blob);
    hal_deb("hal_bluetooth_classic: bond load slot=%u status=%s",
            (unsigned int)index, hal_status_to_string(status));
    if (status == HAL_ENOENT) {
      jh_secure_zeroize(&blob, sizeof(blob));
      continue;
    }
    if (status != HAL_OK) {
      jh_secure_zeroize(&blob, sizeof(blob));
      return status;
    }
    jh_bluetooth_classic_bond_identity_t identity{};
    status = jh_bluetooth_classic_bond_decode(&blob, &identity);
    if (status != HAL_OK) {
      hal_deb("hal_bluetooth_classic: bond decode slot=%u status=%s",
              (unsigned int)index, hal_status_to_string(status));
      jh_secure_zeroize(&identity, sizeof(identity));
      jh_secure_zeroize(&blob, sizeof(blob));
      continue;
    }
    hal_mutex_lock(s_classic.mutex);
    peer_slot_t *slot = find_peer_locked(identity.address);
    if (slot == nullptr) {
      for (peer_slot_t &candidate : s_classic.peers) {
        if (!candidate.used) {
          slot = &candidate;
          break;
        }
      }
    }
    if (slot == nullptr) {
      hal_mutex_unlock(s_classic.mutex);
      jh_secure_zeroize(&identity, sizeof(identity));
      jh_secure_zeroize(&blob, sizeof(blob));
      return HAL_EOVERFLOW;
    }
    slot->identity = identity;
    slot->storage_index = index;
    slot->used = true;
    if (identity.sequence > s_classic.next_sequence) {
      s_classic.next_sequence = identity.sequence;
    }
    s_classic.info.peer_count = peer_count_locked();
    const jh_bluetooth_classic_backend_t *backend = s_classic.backend;
    hal_mutex_unlock(s_classic.mutex);
    status = backend->peer_restore(backend->context, &identity.address,
                                   identity.link_key, identity.link_key_type);
    jh_secure_zeroize(&identity, sizeof(identity));
    jh_secure_zeroize(&blob, sizeof(blob));
    if (status != HAL_OK) {
      return status;
    }
  }
  return HAL_OK;
}

hal_status_t flush_pending_peer(hal_bluetooth_classic_t classic) {
  hal_mutex_lock(s_classic.mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(s_classic.mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.operation_active) {
    hal_mutex_unlock(s_classic.mutex);
    return HAL_EBUSY;
  }
  s_classic.operation_active = true;
  if (!s_classic.pending_key_valid || !s_classic.pending_save_valid ||
      !jh_bluetooth_classic_address_equal(&s_classic.pending_key.address,
                                          &s_classic.pending_save_address)) {
    s_classic.operation_active = false;
    hal_mutex_unlock(s_classic.mutex);
    return HAL_OK;
  }
  jh_bluetooth_classic_bond_identity_t identity = s_classic.pending_key;
  identity.profile_id = s_classic.pending_profile_id;
  identity.sequence = s_classic.next_sequence + 1u;
  if (identity.sequence == 0u) {
    identity.sequence = 1u;
  }
  size_t storage_index = 0u;
  peer_slot_t *slot = select_peer_slot_locked(identity.address, &storage_index);
  if (slot == nullptr) {
    s_classic.operation_active = false;
    hal_mutex_unlock(s_classic.mutex);
    jh_secure_zeroize(&identity, sizeof(identity));
    return HAL_EOVERFLOW;
  }
  const bool provider_enabled = s_classic.provider_enabled;
  const hal_bluetooth_classic_bond_provider_t provider = s_classic.provider;
  const jh_bluetooth_classic_backend_t *backend = s_classic.backend;
  hal_mutex_unlock(s_classic.mutex);

  hal_status_t status = HAL_OK;
  if (provider_enabled) {
    bool key_is_zero = true;
    for (uint8_t byte : identity.link_key) {
      key_is_zero = key_is_zero && byte == 0u;
    }
    if (key_is_zero) {
      status = HAL_EUNSUPPORTED;
    }
    hal_bluetooth_classic_bond_blob_t blob{};
    if (status == HAL_OK) {
      status = jh_bluetooth_classic_bond_encode(&identity, &blob);
    }
    if (status == HAL_OK) {
      status = provider.store(provider.context, storage_index, &blob);
    }
    jh_secure_zeroize(&blob, sizeof(blob));
  }
  if (status == HAL_OK) {
    status = backend->peer_restore(backend->context, &identity.address,
                                   identity.link_key, identity.link_key_type);
  }

  hal_mutex_lock(s_classic.mutex);
  if (status == HAL_OK) {
    s_classic.next_sequence = identity.sequence;
    slot->identity = identity;
    slot->storage_index = storage_index;
    slot->used = true;
    s_classic.info.peer_count = peer_count_locked();
    jh_secure_zeroize(&s_classic.pending_key, sizeof(s_classic.pending_key));
    s_classic.pending_key_valid = false;
    s_classic.pending_save_valid = false;
    s_classic.approved_pairing_valid = false;
  } else {
    s_classic.info.last_status = status;
  }
  s_classic.operation_active = false;
  hal_mutex_unlock(s_classic.mutex);
  jh_secure_zeroize(&identity, sizeof(identity));
  return status;
}

template <typename Operation>
hal_status_t run_backend_operation(hal_bluetooth_classic_t classic,
                                   Operation operation) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_classic.operation_active = true;
  const jh_bluetooth_classic_backend_t *backend = s_classic.backend;
  hal_mutex_unlock(mutex);
  const hal_status_t status = operation(backend);
  hal_mutex_lock(mutex);
  s_classic.operation_active = false;
  if (status != HAL_OK && status != HAL_EAGAIN && status != HAL_EOVERFLOW) {
    s_classic.info.last_status = status;
  }
  hal_mutex_unlock(mutex);
  return status;
}

} // namespace

hal_status_t hal_bluetooth_classic_open(hal_bluetooth_classic_t *out_classic) {
  return hal_bluetooth_classic_open_ex(out_classic, nullptr);
}

hal_status_t hal_bluetooth_classic_open_ex(
    hal_bluetooth_classic_t *out_classic,
    const hal_bluetooth_classic_bond_provider_t *bond_provider) {
  if (out_classic == nullptr) {
    return HAL_EINVAL;
  }
  *out_classic = nullptr;
  if (bond_provider != nullptr &&
      (bond_provider->capacity == 0u ||
       bond_provider->capacity > HAL_BLUETOOTH_CLASSIC_MAX_PEERS ||
       bond_provider->load == nullptr || bond_provider->store == nullptr ||
       bond_provider->erase == nullptr)) {
    return HAL_EINVAL;
  }
  const hal_status_t hardware_status = jh_bluetooth_require_classic_hardware();
  if (hardware_status != HAL_OK) {
    return hardware_status;
  }
  const jh_bluetooth_classic_backend_t *backend =
      jh_bluetooth_classic_backend_instance();
  if (!backend_valid(backend)) {
    return HAL_ECONFIG;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_classic.backend != nullptr || s_classic.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  hal_status_t status = ensure_handle_pool_locked();
  if (status != HAL_OK) {
    hal_mutex_unlock(mutex);
    return status;
  }
  memset(&s_classic.provider, 0, sizeof(s_classic.provider));
  memset(&s_classic.info, 0, sizeof(s_classic.info));
  clear_bond_state_locked();
  reset_scan_queue_locked();
  s_classic.info.pairing_window_open = false;
  s_classic.pairing_window_started_ms = 0u;
  s_classic.pairing_window_duration_ms = 0u;
  s_classic.provider_enabled = bond_provider != nullptr;
  if (bond_provider != nullptr) {
    s_classic.provider = *bond_provider;
  }
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  s_classic.hid_attached = false;
  reset_hid_locked();
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  s_classic.a2dp_attached = false;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  s_classic.avrcp_attached = false;
#endif
  void *handle = nullptr;
  status = jh_handle_allocate(&s_classic.handle_pool, &s_classic, &handle);
  if (status != HAL_OK) {
    hal_mutex_unlock(mutex);
    return status;
  }
  s_classic.backend = backend;
  s_classic.operation_active = true;
  s_classic.info.state = HAL_BLUETOOTH_CLASSIC_STATE_STARTING;
  s_classic.info.last_status = HAL_NONE;
  hal_mutex_unlock(mutex);

  status = backend->start(backend->context, backend_event, nullptr);
  if (status == HAL_OK) {
    status = restore_provider_records();
  }

  hal_mutex_lock(mutex);
  s_classic.operation_active = false;
  if (status == HAL_OK) {
    *out_classic = static_cast<hal_bluetooth_classic_t>(handle);
  } else {
    (void)backend->stop(backend->context);
    (void)release_handle_locked(handle);
    clear_bond_state_locked();
    s_classic.backend = nullptr;
    s_classic.info.state = HAL_BLUETOOTH_CLASSIC_STATE_UNINITIALIZED;
  }
  hal_mutex_unlock(mutex);
  if (status == HAL_OK) {
    jh_bluetooth_publish_available(HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER);
  } else if (status == HAL_EHW || status == HAL_EIO) {
    jh_bluetooth_publish_failed(HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER);
  }
  return status;
}

hal_status_t hal_bluetooth_classic_close(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  if (s_classic.hid_attached) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  if (s_classic.a2dp_attached) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  if (s_classic.avrcp_attached) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
#endif
  hal_mutex_unlock(mutex);
  return run_backend_operation(
      classic, [classic](const jh_bluetooth_classic_backend_t *backend) {
        const hal_status_t backend_status = backend->stop(backend->context);
        hal_mutex_lock(s_classic.mutex);
        const hal_status_t release_status = release_handle_locked(classic);
        clear_bond_state_locked();
        memset(&s_classic.provider, 0, sizeof(s_classic.provider));
        s_classic.provider_enabled = false;
        s_classic.backend = nullptr;
        s_classic.info.state = HAL_BLUETOOTH_CLASSIC_STATE_UNINITIALIZED;
        s_classic.info.scan_active = false;
        hal_mutex_unlock(s_classic.mutex);
        jh_bluetooth_publish_inactive(
            HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER);
        return backend_status != HAL_OK ? backend_status : release_status;
      });
}

hal_status_t hal_bluetooth_classic_poll(hal_bluetooth_classic_t classic) {
  const hal_status_t service_status = run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->service(backend->context);
      });
  if (service_status != HAL_OK) {
    return service_status;
  }
  bool pairing_window_expired = false;
  hal_mutex_lock(s_classic.mutex);
  if (handle_valid_locked(classic) && s_classic.info.pairing_window_open &&
      hal_millis_deadline_expired(s_classic.pairing_window_started_ms,
                                  s_classic.pairing_window_duration_ms)) {
    pairing_window_expired = true;
  }
  hal_mutex_unlock(s_classic.mutex);
  if (pairing_window_expired) {
    const hal_status_t visibility_status =
        hal_bluetooth_classic_pairing_window_close(classic);
    if (visibility_status != HAL_OK) {
      return visibility_status;
    }
  }
  return flush_pending_peer(classic);
}

hal_status_t hal_bluetooth_classic_set_identity(
    hal_bluetooth_classic_t classic,
    const hal_bluetooth_classic_identity_t *identity) {
  if (identity == nullptr || identity->name[0] == '\0' ||
      identity->class_of_device > UINT32_C(0x00ffffff) ||
      memchr(identity->name, '\0', sizeof(identity->name)) == nullptr) {
    return HAL_EINVAL;
  }
  const hal_bluetooth_classic_identity_t copied = *identity;
  return run_backend_operation(
      classic, [&copied](const jh_bluetooth_classic_backend_t *backend) {
        return backend->identity_set(backend->context, &copied);
      });
}

hal_status_t
hal_bluetooth_classic_pairing_window_open(hal_bluetooth_classic_t classic,
                                          uint32_t duration_ms) {
  if (duration_ms < 1000u || duration_ms > 300000u) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.info.state != HAL_BLUETOOTH_CLASSIC_STATE_READY) {
    hal_mutex_unlock(mutex);
    return HAL_ESTATE;
  }
  if (s_classic.info.pairing_window_open) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  hal_mutex_unlock(mutex);
  const hal_status_t status = run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->visibility_set(backend->context, true, true, true);
      });
  if (status == HAL_OK) {
    hal_mutex_lock(mutex);
    if (handle_valid_locked(classic)) {
      s_classic.info.pairing_window_open = true;
      s_classic.pairing_window_started_ms = hal_millis();
      s_classic.pairing_window_duration_ms = duration_ms;
    }
    hal_mutex_unlock(mutex);
  }
  return status;
}

hal_status_t
hal_bluetooth_classic_pairing_window_close(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(classic);
  const bool open = valid && s_classic.info.pairing_window_open;
  hal_mutex_unlock(mutex);
  if (!open) {
    return valid ? HAL_ESTATE : HAL_EUNINIT;
  }
  const hal_status_t status = run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->visibility_set(backend->context, true, false, false);
      });
  if (status == HAL_OK) {
    hal_mutex_lock(mutex);
    s_classic.info.pairing_window_open = false;
    s_classic.pairing_window_started_ms = 0u;
    s_classic.pairing_window_duration_ms = 0u;
    hal_mutex_unlock(mutex);
  }
  return status;
}

hal_status_t
hal_bluetooth_classic_get_info(hal_bluetooth_classic_t classic,
                               hal_bluetooth_classic_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_classic.info.pending_scan_results = s_classic.scan_count;
  s_classic.info.peer_count = peer_count_locked();
  *out_info = s_classic.info;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_bluetooth_classic_scan_start(hal_bluetooth_classic_t classic,
                                              uint32_t duration_ms) {
  if (duration_ms == 0u || duration_ms > 120000u) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (s_classic.info.state != HAL_BLUETOOTH_CLASSIC_STATE_READY ||
      s_classic.info.scan_active) {
    hal_mutex_unlock(mutex);
    return HAL_ESTATE;
  }
  reset_scan_queue_locked();
  s_classic.info.scan_active = true;
  s_classic.info.state = HAL_BLUETOOTH_CLASSIC_STATE_SCANNING;
  hal_mutex_unlock(mutex);
  const hal_status_t status = run_backend_operation(
      classic, [duration_ms](const jh_bluetooth_classic_backend_t *backend) {
        return backend->scan_start(backend->context, duration_ms);
      });
  if (status != HAL_OK) {
    hal_mutex_lock(mutex);
    s_classic.info.scan_active = false;
    s_classic.info.state = HAL_BLUETOOTH_CLASSIC_STATE_READY;
    hal_mutex_unlock(mutex);
  }
  return status;
}

hal_status_t hal_bluetooth_classic_scan_stop(hal_bluetooth_classic_t classic) {
  return run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->scan_stop(backend->context);
      });
}

hal_status_t hal_bluetooth_classic_scan_result_next(
    hal_bluetooth_classic_t classic,
    hal_bluetooth_classic_scan_result_t *out_result) {
  if (out_result == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.overflow_pending) {
    s_classic.overflow_pending = false;
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  if (s_classic.scan_count == 0u) {
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  *out_result = s_classic.scan_results[s_classic.scan_head];
  s_classic.scan_head =
      (s_classic.scan_head + 1u) % HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH;
  --s_classic.scan_count;
  s_classic.info.pending_scan_results = s_classic.scan_count;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_bluetooth_classic_sdp_query(
    hal_bluetooth_classic_t classic,
    const hal_bluetooth_classic_address_t *address) {
  if (address == nullptr || jh_bluetooth_classic_address_is_zero(address)) {
    return HAL_EINVAL;
  }
  return run_backend_operation(
      classic, [address](const jh_bluetooth_classic_backend_t *backend) {
        return backend->sdp_query(backend->context, address);
      });
}

hal_status_t
hal_bluetooth_classic_pair(hal_bluetooth_classic_t classic,
                           const hal_bluetooth_classic_address_t *address) {
  if (address == nullptr || jh_bluetooth_classic_address_is_zero(address)) {
    return HAL_EINVAL;
  }
  return run_backend_operation(
      classic, [address](const jh_bluetooth_classic_backend_t *backend) {
        return backend->pair(backend->context, address);
      });
}

hal_status_t
hal_bluetooth_classic_pairing_authorize(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(classic);
  const bool pending = valid && s_classic.info.pairing_pending;
  const hal_bluetooth_classic_address_t address =
      s_classic.info.pairing_address;
  hal_mutex_unlock(mutex);
  if (!pending) {
    return valid ? HAL_ESTATE : HAL_EUNINIT;
  }
  const hal_status_t status = run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->pairing_reply(backend->context, true);
      });
  if (status == HAL_OK) {
    hal_mutex_lock(mutex);
    if (handle_valid_locked(classic)) {
      s_classic.approved_pairing_address = address;
      s_classic.approved_pairing_valid = true;
    }
    hal_mutex_unlock(mutex);
  }
  return status;
}

hal_status_t
hal_bluetooth_classic_pairing_reject(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(classic);
  const bool pending = valid && s_classic.info.pairing_pending;
  hal_mutex_unlock(mutex);
  if (!pending) {
    return valid ? HAL_ESTATE : HAL_EUNINIT;
  }
  return run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->pairing_reply(backend->context, false);
      });
}

hal_status_t
hal_bluetooth_classic_peer_save(hal_bluetooth_classic_t classic,
                                const hal_bluetooth_classic_address_t *address,
                                uint16_t profile_id) {
  if (address == nullptr || jh_bluetooth_classic_address_is_zero(address) ||
      profile_id == 0u) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (!s_classic.approved_pairing_valid ||
      !jh_bluetooth_classic_address_equal(&s_classic.approved_pairing_address,
                                          address)) {
    const peer_slot_t *existing = find_peer_locked(*address);
    if (existing != nullptr && existing->identity.profile_id == profile_id) {
      hal_mutex_unlock(mutex);
      return HAL_OK;
    }
    hal_mutex_unlock(mutex);
    return HAL_EAUTH;
  }
  s_classic.pending_save_address = *address;
  s_classic.pending_profile_id = profile_id;
  s_classic.pending_save_valid = true;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_bluetooth_classic_peer_count(hal_bluetooth_classic_t classic,
                                              size_t *out_count) {
  if (out_count == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  *out_count = peer_count_locked();
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t
hal_bluetooth_classic_peer_get(hal_bluetooth_classic_t classic, size_t index,
                               hal_bluetooth_classic_peer_t *out_peer) {
  if (out_peer == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  peer_slot_t *slot = peer_by_dense_index_locked(index);
  if (slot == nullptr) {
    hal_mutex_unlock(mutex);
    return HAL_ENOENT;
  }
  out_peer->address = slot->identity.address;
  out_peer->sequence = slot->identity.sequence;
  out_peer->profile_id = slot->identity.profile_id;
  out_peer->storage_index = slot->storage_index;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_bluetooth_classic_peer_forget(
    hal_bluetooth_classic_t classic,
    const hal_bluetooth_classic_address_t *address) {
  if (address == nullptr || jh_bluetooth_classic_address_is_zero(address)) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_classic.operation_active = true;
  peer_slot_t *slot = find_peer_locked(*address);
  if (slot == nullptr) {
    s_classic.operation_active = false;
    hal_mutex_unlock(mutex);
    return HAL_ENOENT;
  }
  const size_t storage_index = slot->storage_index;
  const bool provider_enabled = s_classic.provider_enabled;
  const hal_bluetooth_classic_bond_provider_t provider = s_classic.provider;
  const jh_bluetooth_classic_backend_t *backend = s_classic.backend;
  hal_mutex_unlock(mutex);

  if (provider_enabled) {
    const hal_status_t status = provider.erase(provider.context, storage_index);
    if (status != HAL_OK) {
      hal_mutex_lock(mutex);
      s_classic.info.last_status = status;
      s_classic.operation_active = false;
      hal_mutex_unlock(mutex);
      return status;
    }
  }

  const hal_status_t status = backend->peer_forget(backend->context, address);
  hal_mutex_lock(mutex);
  slot->used = false;
  jh_secure_zeroize(&slot->identity, sizeof(slot->identity));
  s_classic.info.peer_count = peer_count_locked();
  if (status != HAL_OK) {
    s_classic.info.last_status = status;
  }
  s_classic.operation_active = false;
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t
hal_bluetooth_classic_peer_forget_all(hal_bluetooth_classic_t classic) {
  hal_bluetooth_classic_info_t info{};
  hal_status_t status = hal_bluetooth_classic_get_info(classic, &info);
  if (status != HAL_OK) {
    return status;
  }
  if (info.pairing_window_open) {
    status = hal_bluetooth_classic_pairing_window_close(classic);
    if (status != HAL_OK) {
      return status;
    }
  }
  for (;;) {
    size_t count = 0u;
    status = hal_bluetooth_classic_peer_count(classic, &count);
    if (status != HAL_OK || count == 0u) {
      return status;
    }
    hal_bluetooth_classic_peer_t peer{};
    status = hal_bluetooth_classic_peer_get(classic, 0u, &peer);
    if (status != HAL_OK) {
      return status;
    }
    status = hal_bluetooth_classic_peer_forget(classic, &peer.address);
    if (status != HAL_OK) {
      return status;
    }
  }
}

hal_status_t hal_bluetooth_classic_format_address(
    const hal_bluetooth_classic_address_t *address, char *out,
    size_t out_size) {
  if (address == nullptr || out == nullptr ||
      out_size < HAL_BLUETOOTH_CLASSIC_ADDRESS_TEXT_SIZE) {
    return HAL_EINVAL;
  }
  return hal_text_format_mac_ex(address->bytes, out, out_size);
}

bool jh_bluetooth_classic_handle_valid(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return false;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(classic);
  hal_mutex_unlock(mutex);
  return valid;
}

#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
hal_status_t jh_bluetooth_classic_a2dp_attach(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(classic);
  const bool attached = valid && s_classic.a2dp_attached;
  hal_mutex_unlock(mutex);
  if (!valid || attached) {
    return valid ? HAL_EBUSY : HAL_EUNINIT;
  }
  const hal_status_t status = run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->a2dp_attach(backend->context);
      });
  if (status == HAL_OK) {
    hal_mutex_lock(mutex);
    s_classic.a2dp_attached = true;
    hal_mutex_unlock(mutex);
  }
  return status;
}

hal_status_t jh_bluetooth_classic_a2dp_detach(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(classic);
  const bool attached = valid && s_classic.a2dp_attached;
  hal_mutex_unlock(mutex);
  if (!attached) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  hal_mutex_lock(mutex);
  const bool avrcp_attached = s_classic.avrcp_attached;
  hal_mutex_unlock(mutex);
  if (avrcp_attached) {
    return HAL_EBUSY;
  }
#endif
  const hal_status_t status = run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->a2dp_detach(backend->context);
      });
  if (status == HAL_OK) {
    hal_mutex_lock(mutex);
    s_classic.a2dp_attached = false;
    hal_mutex_unlock(mutex);
  }
  return status;
}

hal_status_t jh_bluetooth_classic_a2dp_decode(hal_bluetooth_classic_t classic,
                                              const uint8_t *data,
                                              size_t length) {
  if (data == nullptr || length == 0u) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(classic);
  const bool attached = valid && s_classic.a2dp_attached;
  hal_mutex_unlock(mutex);
  if (!attached) {
    return HAL_EUNINIT;
  }
  return run_backend_operation(
      classic, [data, length](const jh_bluetooth_classic_backend_t *backend) {
        return backend->a2dp_decode(backend->context, data, length);
      });
}
#endif

#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
hal_status_t jh_bluetooth_classic_avrcp_attach(hal_bluetooth_classic_t classic,
                                               uint8_t initial_volume) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(classic);
  const bool attached = valid && s_classic.avrcp_attached;
  hal_mutex_unlock(mutex);
  if (!valid || attached) {
    return valid ? HAL_EBUSY : HAL_EUNINIT;
  }
  const hal_status_t status = run_backend_operation(
      classic, [initial_volume](const jh_bluetooth_classic_backend_t *backend) {
        return backend->avrcp_attach(backend->context, initial_volume);
      });
  if (status == HAL_OK) {
    hal_mutex_lock(mutex);
    s_classic.avrcp_attached = true;
    hal_mutex_unlock(mutex);
  }
  return status;
}

hal_status_t
jh_bluetooth_classic_avrcp_detach(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool valid = handle_valid_locked(classic);
  const bool attached = valid && s_classic.avrcp_attached;
  hal_mutex_unlock(mutex);
  if (!attached) {
    return HAL_EUNINIT;
  }
  const hal_status_t status = run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->avrcp_detach(backend->context);
      });
  if (status == HAL_OK) {
    hal_mutex_lock(mutex);
    s_classic.avrcp_attached = false;
    hal_mutex_unlock(mutex);
  }
  return status;
}

hal_status_t
jh_bluetooth_classic_avrcp_volume_set(hal_bluetooth_classic_t classic,
                                      uint8_t absolute_volume) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool attached =
      handle_valid_locked(classic) && s_classic.avrcp_attached;
  hal_mutex_unlock(mutex);
  if (!attached) {
    return HAL_EUNINIT;
  }
  return run_backend_operation(
      classic,
      [absolute_volume](const jh_bluetooth_classic_backend_t *backend) {
        return backend->avrcp_volume_set(backend->context, absolute_volume);
      });
}
#endif

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
hal_status_t jh_bluetooth_classic_hid_attach(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.hid_attached) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_classic.hid_attached = true;
  reset_hid_locked();
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t jh_bluetooth_classic_hid_detach(hal_bluetooth_classic_t classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic) || !s_classic.hid_attached) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  s_classic.hid_attached = false;
  reset_hid_locked();
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t
jh_bluetooth_classic_hid_get_info(hal_bluetooth_classic_t classic,
                                  hal_bluetooth_hid_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic) || !s_classic.hid_attached) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  s_classic.hid_info.pending_reports = s_classic.hid_report_count;
  *out_info = s_classic.hid_info;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t jh_bluetooth_classic_hid_connect(
    hal_bluetooth_classic_t classic,
    const hal_bluetooth_classic_address_t *address) {
  if (address == nullptr || jh_bluetooth_classic_address_is_zero(address)) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic) || !s_classic.hid_attached) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.hid_info.state != HAL_BLUETOOTH_HID_STATE_READY) {
    hal_mutex_unlock(mutex);
    return HAL_ESTATE;
  }
  s_classic.hid_info.state = HAL_BLUETOOTH_HID_STATE_CONNECTING;
  s_classic.hid_info.peer_address = *address;
  hal_mutex_unlock(mutex);
  const hal_status_t status = run_backend_operation(
      classic, [address](const jh_bluetooth_classic_backend_t *backend) {
        return backend->hid_connect(backend->context, address);
      });
  if (status != HAL_OK) {
    hal_mutex_lock(mutex);
    s_classic.hid_info.state = HAL_BLUETOOTH_HID_STATE_READY;
    s_classic.hid_info.last_status = status;
    hal_mutex_unlock(mutex);
  }
  return status;
}

hal_status_t
jh_bluetooth_classic_hid_disconnect(hal_bluetooth_classic_t classic) {
  return run_backend_operation(
      classic, [](const jh_bluetooth_classic_backend_t *backend) {
        return backend->hid_disconnect(backend->context);
      });
}

hal_status_t
jh_bluetooth_classic_hid_descriptor(hal_bluetooth_classic_t classic,
                                    uint8_t *out_descriptor, size_t capacity,
                                    size_t *out_length) {
  if (out_length == nullptr || (out_descriptor == nullptr && capacity != 0u)) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic) || !s_classic.hid_attached) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (!s_classic.hid_info.descriptor_available) {
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  *out_length = s_classic.hid_info.descriptor_length;
  if (capacity < *out_length) {
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  if (*out_length != 0u) {
    memcpy(out_descriptor, s_classic.hid_descriptor, *out_length);
  }
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t
jh_bluetooth_classic_hid_report_next(hal_bluetooth_classic_t classic,
                                     hal_bluetooth_hid_report_t *out_report) {
  if (out_report == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(classic) || !s_classic.hid_attached) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_classic.hid_overflow_pending) {
    s_classic.hid_overflow_pending = false;
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  if (s_classic.hid_report_count == 0u) {
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  *out_report = s_classic.hid_reports[s_classic.hid_report_head];
  s_classic.hid_report_head =
      (s_classic.hid_report_head + 1u) % HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH;
  --s_classic.hid_report_count;
  s_classic.hid_info.pending_reports = s_classic.hid_report_count;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t
jh_bluetooth_classic_hid_report_send(hal_bluetooth_classic_t classic,
                                     const hal_bluetooth_hid_report_t *report) {
  if (report == nullptr || report->length > sizeof(report->data) ||
      (report->type != HAL_BLUETOOTH_HID_REPORT_OUTPUT &&
       report->type != HAL_BLUETOOTH_HID_REPORT_FEATURE)) {
    return HAL_EINVAL;
  }
  return run_backend_operation(
      classic, [report](const jh_bluetooth_classic_backend_t *backend) {
        return backend->hid_report_send(backend->context, report);
      });
}

hal_status_t
jh_bluetooth_classic_hid_report_request(hal_bluetooth_classic_t classic,
                                        hal_bluetooth_hid_report_type_t type,
                                        uint8_t report_id) {
  if (type != HAL_BLUETOOTH_HID_REPORT_INPUT &&
      type != HAL_BLUETOOTH_HID_REPORT_FEATURE) {
    return HAL_EINVAL;
  }
  return run_backend_operation(
      classic,
      [type, report_id](const jh_bluetooth_classic_backend_t *backend) {
        return backend->hid_report_request(backend->context, type, report_id);
      });
}
#endif

#if HAL_TARGET_IS_MOCK
void hal_mock_bluetooth_classic_runtime_full_reset(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_classic.handle_pool_initialized) {
    jh_handle_invalidate_all(&s_classic.handle_pool);
  }
  s_classic.backend = nullptr;
  s_classic.operation_active = false;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  s_classic.hid_attached = false;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  s_classic.a2dp_attached = false;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  s_classic.avrcp_attached = false;
#endif
  s_classic.info.pairing_window_open = false;
  s_classic.pairing_window_started_ms = 0u;
  s_classic.pairing_window_duration_ms = 0u;
  hal_mutex_unlock(mutex);
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_CLASSIC */
