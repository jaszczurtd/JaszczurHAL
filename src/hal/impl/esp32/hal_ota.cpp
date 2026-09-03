#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_OTA

#include "hal/core/hal_mutex_once.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_tcp.h"
#include "hal/network/hal_udp.h"
#include "hal/network/ota/hal_ota.h"
#include "hal/network/ota/jh_ota_protocol.h"
#include "hal/security/hal_crypto.h"
#include "hal/security/jh_secure_random.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "jh_esp32_status.h"
#include "sdkconfig.h"

#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
#error "HAL_ENABLE_OTA requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE"
#endif

#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_md5.h>
#include <esp_system.h>

#include <stdio.h>
#include <string.h>

namespace {

constexpr size_t kTextBufferSize = 96u;
constexpr size_t kEventQueueSize = 12u;
constexpr size_t kUdpBufferSize = 192u;
constexpr size_t kTcpBufferSize = 1024u;
constexpr uint16_t kDefaultPort = 8266u;
constexpr uint32_t kConnectTimeoutMs = 5000u;
constexpr uint32_t kReceiveTimeoutMs = 5000u;
constexpr size_t kHostnameMaxLength = 63u;
constexpr uint8_t kEspRollbackAttempts = 1u;

enum class EventType : uint8_t {
  kNone = 0,
  kStart,
  kEnd,
  kProgress,
  kError,
};

enum class ServiceState : uint8_t {
  kIdle = 0,
  kWaitAuth,
  kBeginTransfer,
  kReceive,
};

struct Event {
  EventType type;
  hal_ota_command_t command;
  hal_ota_error_t error;
  uint32_t progress;
  uint32_t total;
};

struct OtaState {
  hal_mutex_t mutex;
  bool started;
  bool password_set;
  bool reboot_pending;
  ServiceState state;

  uint16_t port;
  char hostname[kTextBufferSize];
  char password_md5[JH_OTA_MD5_HEX_BUFFER_SIZE];
  char nonce[JH_OTA_MD5_HEX_BUFFER_SIZE];

  hal_udp_socket_t udp;
  hal_tcp_socket_t tcp;
  hal_net_endpoint_t remote_udp;
  jh_ota_invitation_t invitation;
  uint32_t received;
  uint32_t last_activity_ms;

  const esp_partition_t *update_partition;
  esp_ota_handle_t update_handle;
  md5_context_t image_md5;
  bool update_active;

  hal_ota_on_start_callback_t on_start;
  void *on_start_user;
  hal_ota_on_end_callback_t on_end;
  void *on_end_user;
  hal_ota_on_progress_callback_t on_progress;
  void *on_progress_user;
  hal_ota_on_error_callback_t on_error;
  void *on_error_user;

  Event queue[kEventQueueSize];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
};

OtaState s_ota = {};

bool ensure_mutex() {
  return jh_hal_mutex_create_once(&s_ota.mutex) != nullptr;
}

hal_status_t ota_status_from_esp(esp_err_t error) {
  switch (error) {
  case ESP_ERR_OTA_VALIDATE_FAILED:
  case ESP_ERR_OTA_SMALL_SEC_VER:
    return HAL_EAUTH;
  case ESP_ERR_OTA_PARTITION_CONFLICT:
  case ESP_ERR_OTA_ROLLBACK_INVALID_STATE:
  case ESP_ERR_OTA_ROLLBACK_FAILED:
    return HAL_ESTATE;
  case ESP_ERR_OTA_SELECT_INFO_INVALID:
    return HAL_EPROTO;
  default:
    return jh_esp32_status_from_esp_err(error);
  }
}

bool validate_non_empty(const char *value, const char *function,
                        const char *name) {
  if (value == nullptr || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", function, name);
    return false;
  }
  return true;
}

void queue_clear_locked() {
  s_ota.head = 0u;
  s_ota.tail = 0u;
  s_ota.count = 0u;
}

void queue_push_locked(const Event &event) {
  if (s_ota.count >= kEventQueueSize) {
    s_ota.head = static_cast<uint8_t>((s_ota.head + 1u) % kEventQueueSize);
    --s_ota.count;
  }
  s_ota.queue[s_ota.tail] = event;
  s_ota.tail = static_cast<uint8_t>((s_ota.tail + 1u) % kEventQueueSize);
  ++s_ota.count;
}

bool queue_pop_locked(Event *out_event) {
  if (out_event == nullptr || s_ota.count == 0u) {
    return false;
  }
  *out_event = s_ota.queue[s_ota.head];
  s_ota.head = static_cast<uint8_t>((s_ota.head + 1u) % kEventQueueSize);
  --s_ota.count;
  return true;
}

void queue_error_locked(hal_ota_error_t error) {
  Event event = {};
  event.type = EventType::kError;
  event.error = error;
  queue_push_locked(event);
}

bool udp_send_locked(const char *text) {
  if (s_ota.udp == nullptr || text == nullptr) {
    return false;
  }
  const size_t length = strlen(text);
  size_t sent = 0u;
  return hal_udp_socket_sendto_ex(s_ota.udp, text, length, &s_ota.remote_udp,
                                  &sent) == HAL_OK &&
         sent == length;
}

bool tcp_send_locked(const char *text) {
  if (s_ota.tcp == nullptr || text == nullptr) {
    return false;
  }
  const size_t length = strlen(text);
  size_t offset = 0u;
  while (offset < length) {
    size_t sent = 0u;
    const hal_status_t status = hal_tcp_socket_send_ex(s_ota.tcp, text + offset,
                                                       length - offset, &sent);
    if (status != HAL_OK || sent == 0u) {
      return false;
    }
    offset += sent;
  }
  return true;
}

void update_abort_locked() {
  if (s_ota.update_active) {
    (void)esp_ota_abort(s_ota.update_handle);
  }
  s_ota.update_active = false;
  s_ota.update_handle = 0u;
  s_ota.update_partition = nullptr;
  memset(&s_ota.image_md5, 0, sizeof(s_ota.image_md5));
}

hal_status_t update_begin_locked() {
  if (s_ota.invitation.command != 0u) {
    return HAL_EUNSUPPORTED;
  }
  const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
  if (partition == nullptr) {
    return HAL_ENOENT;
  }
  if (s_ota.invitation.image_size > partition->size) {
    return HAL_EOVERFLOW;
  }

  esp_ota_handle_t handle = 0u;
  const esp_err_t result =
      esp_ota_begin(partition, s_ota.invitation.image_size, &handle);
  if (result != ESP_OK) {
    return ota_status_from_esp(result);
  }
  s_ota.update_partition = partition;
  s_ota.update_handle = handle;
  s_ota.update_active = true;
  esp_rom_md5_init(&s_ota.image_md5);
  return HAL_OK;
}

hal_status_t update_write_locked(const uint8_t *data, size_t size) {
  if (!s_ota.update_active || data == nullptr || size == 0u) {
    return HAL_ESTATE;
  }
  const esp_err_t result = esp_ota_write(s_ota.update_handle, data, size);
  if (result != ESP_OK) {
    return ota_status_from_esp(result);
  }
  esp_rom_md5_update(&s_ota.image_md5, data, static_cast<uint32_t>(size));
  return HAL_OK;
}

void digest_to_hex(const uint8_t digest[ESP_ROM_MD5_DIGEST_LEN],
                   char out[JH_OTA_MD5_HEX_BUFFER_SIZE]) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t index = 0u; index < ESP_ROM_MD5_DIGEST_LEN; ++index) {
    out[index * 2u] = kHex[digest[index] >> 4u];
    out[index * 2u + 1u] = kHex[digest[index] & 0x0fu];
  }
  out[JH_OTA_MD5_HEX_CHARS] = '\0';
}

hal_status_t update_finish_locked() {
  if (!s_ota.update_active || s_ota.update_partition == nullptr) {
    return HAL_ESTATE;
  }

  uint8_t digest[ESP_ROM_MD5_DIGEST_LEN] = {};
  char digest_hex[JH_OTA_MD5_HEX_BUFFER_SIZE] = {};
  esp_rom_md5_final(digest, &s_ota.image_md5);
  digest_to_hex(digest, digest_hex);
  const bool digest_matches =
      jh_ota_hex_equal(digest_hex, s_ota.invitation.image_md5);
  jh_secure_zeroize(digest, sizeof(digest));
  jh_secure_zeroize(digest_hex, sizeof(digest_hex));
  if (!digest_matches) {
    update_abort_locked();
    return HAL_EAUTH;
  }

  const esp_ota_handle_t handle = s_ota.update_handle;
  const esp_partition_t *partition = s_ota.update_partition;
  s_ota.update_active = false;
  s_ota.update_handle = 0u;
  s_ota.update_partition = nullptr;
  esp_err_t result = esp_ota_end(handle);
  if (result == ESP_OK) {
    result = esp_ota_set_boot_partition(partition);
  }
  return ota_status_from_esp(result);
}

void transfer_reset_locked() {
  if (s_ota.tcp != nullptr) {
    hal_tcp_socket_close(s_ota.tcp);
    s_ota.tcp = nullptr;
  }
  s_ota.received = 0u;
  memset(&s_ota.invitation, 0, sizeof(s_ota.invitation));
  memset(&s_ota.remote_udp, 0, sizeof(s_ota.remote_udp));
  s_ota.state = ServiceState::kIdle;
}

void transfer_fail_locked(hal_ota_error_t error, const char *udp_message) {
  update_abort_locked();
  if (udp_message != nullptr) {
    (void)udp_send_locked(udp_message);
  }
  queue_error_locked(error);
  transfer_reset_locked();
}

bool make_nonce_locked() {
  uint8_t random[16] = {};
  if (jh_secure_random_bytes(random, sizeof(random)) != HAL_OK) {
    return false;
  }
  digest_to_hex(random, s_ota.nonce);
  jh_secure_zeroize(random, sizeof(random));
  return true;
}

bool authenticate_locked(const jh_ota_auth_response_t &response) {
  char transcript[JH_OTA_AUTH_TRANSCRIPT_BUFFER_SIZE] = {};
  size_t transcript_length = 0u;
  if (jh_ota_format_auth_transcript(
          &s_ota.invitation, s_ota.nonce, response.client_nonce, transcript,
          sizeof(transcript), &transcript_length) != HAL_OK) {
    return false;
  }
  char expected[JH_OTA_AUTH_TAG_HEX_BUFFER_SIZE] = {};
  const bool hashed = hal_hmac_sha256_hex(
      reinterpret_cast<const uint8_t *>(s_ota.password_md5),
      JH_OTA_MD5_HEX_CHARS, reinterpret_cast<const uint8_t *>(transcript),
      transcript_length, expected, sizeof(expected));
  jh_secure_zeroize(transcript, sizeof(transcript));
  const bool authenticated =
      hashed && jh_ota_auth_tag_equal(expected, response.response);
  jh_secure_zeroize(expected, sizeof(expected));
  return authenticated;
}

void invitation_received_locked(const jh_ota_invitation_t &invitation,
                                const hal_net_endpoint_t &remote) {
  if (remote.family != HAL_NET_AF_INET ||
      remote.addr_len != HAL_NET_IPV4_ADDR_LEN) {
    return;
  }
  s_ota.invitation = invitation;
  s_ota.remote_udp = remote;
  s_ota.last_activity_ms = hal_millis();

  if (!s_ota.password_set) {
    s_ota.state = ServiceState::kBeginTransfer;
    return;
  }
  if (!make_nonce_locked()) {
    transfer_fail_locked(HAL_OTA_ERROR_AUTH, "Authentication Failed");
    return;
  }

  char response[6u + JH_OTA_MD5_HEX_BUFFER_SIZE] = {};
  const int length =
      snprintf(response, sizeof(response), "AUTH2 %s", s_ota.nonce);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(response) ||
      !udp_send_locked(response)) {
    transfer_fail_locked(HAL_OTA_ERROR_AUTH, nullptr);
    return;
  }
  s_ota.state = ServiceState::kWaitAuth;
}

void copy_partition_version(const esp_partition_t *partition,
                            char out[HAL_OTA_VERSION_TEXT_SIZE]) {
  out[0] = '\0';
  if (partition == nullptr) {
    return;
  }
  esp_app_desc_t description = {};
  if (esp_ota_get_partition_description(partition, &description) != ESP_OK) {
    return;
  }
  size_t length = strnlen(description.version, sizeof(description.version));
  if (length >= HAL_OTA_VERSION_TEXT_SIZE) {
    length = HAL_OTA_VERSION_TEXT_SIZE - 1u;
  }
  memcpy(out, description.version, length);
  out[length] = '\0';
}

hal_ota_boot_mode_t boot_mode(const esp_partition_t *running,
                              const esp_partition_t *boot);

int format_discovery_response(char *out, size_t out_size,
                              const esp_partition_t *update_partition,
                              const esp_partition_t *running,
                              const esp_partition_t *boot) {
  return snprintf(out, out_size, "JHOTA 1 %s %s %u %lu %lu %u", s_ota.hostname,
                  HAL_TARGET_NAME, static_cast<unsigned>(s_ota.port),
                  static_cast<unsigned long>(update_partition != nullptr
                                                 ? update_partition->size
                                                 : 0u),
                  0ul, static_cast<unsigned>(boot_mode(running, boot)));
}

void send_discovery_locked() {
  const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  char response[kUdpBufferSize] = {};
  const int length = format_discovery_response(response, sizeof(response),
                                               partition, running, boot);
  if (length > 0 && static_cast<size_t>(length) < sizeof(response)) {
    (void)udp_send_locked(response);
  }
}

void udp_service_locked() {
  uint8_t packet[kUdpBufferSize] = {};
  hal_net_endpoint_t remote = {};
  size_t received = 0u;
  const hal_status_t status = hal_udp_socket_recvfrom_ex(
      s_ota.udp, packet, sizeof(packet), &remote, 0u, &received);
  if (status != HAL_OK || received == 0u) {
    return;
  }

  if (s_ota.state == ServiceState::kIdle) {
    static constexpr char kDiscovery[] = "JHOTA DISCOVER 1";
    if (received == sizeof(kDiscovery) - 1u &&
        memcmp(packet, kDiscovery, sizeof(kDiscovery) - 1u) == 0) {
      s_ota.remote_udp = remote;
      send_discovery_locked();
      return;
    }
    jh_ota_invitation_t invitation = {};
    if (jh_ota_parse_invitation(packet, received, &invitation) == HAL_OK) {
      invitation_received_locked(invitation, remote);
    }
    return;
  }

  if (s_ota.state == ServiceState::kWaitAuth) {
    if (!jh_ota_endpoint_equal(&remote, &s_ota.remote_udp)) {
      return;
    }
    jh_ota_auth_response_t response = {};
    if (jh_ota_parse_auth_response(packet, received, &response) != HAL_OK ||
        !authenticate_locked(response)) {
      transfer_fail_locked(HAL_OTA_ERROR_AUTH, "Authentication Failed");
      return;
    }
    s_ota.state = ServiceState::kBeginTransfer;
  }
}

void begin_transfer_locked() {
  if (update_begin_locked() != HAL_OK) {
    transfer_fail_locked(HAL_OTA_ERROR_BEGIN, "ERR: Update Begin");
    return;
  }
  if (!udp_send_locked("OK")) {
    transfer_fail_locked(HAL_OTA_ERROR_CONNECT, nullptr);
    return;
  }

  hal_tcp_socket_t socket = nullptr;
  if (hal_tcp_socket_open_ex(&socket) != HAL_OK) {
    transfer_fail_locked(HAL_OTA_ERROR_CONNECT, nullptr);
    return;
  }
  hal_net_endpoint_t remote = s_ota.remote_udp;
  remote.port = s_ota.invitation.tcp_port;
  if (hal_tcp_socket_connect_ex(socket, &remote, kConnectTimeoutMs) != HAL_OK) {
    hal_tcp_socket_close(socket);
    transfer_fail_locked(HAL_OTA_ERROR_CONNECT, nullptr);
    return;
  }
  s_ota.tcp = socket;
  s_ota.received = 0u;
  s_ota.last_activity_ms = hal_millis();

  Event start = {};
  start.type = EventType::kStart;
  start.command = HAL_OTA_COMMAND_SKETCH;
  queue_push_locked(start);
  Event progress = {};
  progress.type = EventType::kProgress;
  progress.total = s_ota.invitation.image_size;
  queue_push_locked(progress);
  s_ota.state = ServiceState::kReceive;
}

void receive_transfer_locked() {
  const size_t remaining =
      static_cast<size_t>(s_ota.invitation.image_size - s_ota.received);
  uint8_t buffer[kTcpBufferSize] = {};
  const size_t capacity =
      remaining < sizeof(buffer) ? remaining : sizeof(buffer);
  size_t received = 0u;
  const hal_status_t receive_status =
      hal_tcp_socket_recv_ex(s_ota.tcp, buffer, capacity, 0u, &received);
  if (receive_status != HAL_OK && receive_status != HAL_EAGAIN &&
      receive_status != HAL_ETIMEOUT) {
    transfer_fail_locked(HAL_OTA_ERROR_RECEIVE, nullptr);
    return;
  }
  if (received == 0u) {
    if (!hal_tcp_socket_is_connected(s_ota.tcp) ||
        hal_millis_deadline_expired(s_ota.last_activity_ms,
                                    kReceiveTimeoutMs)) {
      transfer_fail_locked(HAL_OTA_ERROR_RECEIVE, nullptr);
    }
    return;
  }

  s_ota.last_activity_ms = hal_millis();
  if (update_write_locked(buffer, received) != HAL_OK) {
    transfer_fail_locked(HAL_OTA_ERROR_RECEIVE, nullptr);
    return;
  }
  s_ota.received += static_cast<uint32_t>(received);

  char acknowledgement[16] = {};
  const int length = snprintf(acknowledgement, sizeof(acknowledgement), "%lu\n",
                              static_cast<unsigned long>(received));
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(acknowledgement) ||
      !tcp_send_locked(acknowledgement)) {
    transfer_fail_locked(HAL_OTA_ERROR_RECEIVE, nullptr);
    return;
  }

  Event progress = {};
  progress.type = EventType::kProgress;
  progress.progress = s_ota.received;
  progress.total = s_ota.invitation.image_size;
  queue_push_locked(progress);
  if (s_ota.received < s_ota.invitation.image_size) {
    return;
  }
  if (update_finish_locked() != HAL_OK) {
    transfer_fail_locked(HAL_OTA_ERROR_END, nullptr);
    return;
  }
  (void)tcp_send_locked("OK");
  Event end = {};
  end.type = EventType::kEnd;
  queue_push_locked(end);
  s_ota.reboot_pending = true;
  transfer_reset_locked();
}

void dispatch_events() {
  for (;;) {
    Event event = {};
    hal_ota_on_start_callback_t on_start = nullptr;
    void *on_start_user = nullptr;
    hal_ota_on_end_callback_t on_end = nullptr;
    void *on_end_user = nullptr;
    hal_ota_on_progress_callback_t on_progress = nullptr;
    void *on_progress_user = nullptr;
    hal_ota_on_error_callback_t on_error = nullptr;
    void *on_error_user = nullptr;

    hal_mutex_lock(s_ota.mutex);
    const bool available = queue_pop_locked(&event);
    if (available) {
      on_start = s_ota.on_start;
      on_start_user = s_ota.on_start_user;
      on_end = s_ota.on_end;
      on_end_user = s_ota.on_end_user;
      on_progress = s_ota.on_progress;
      on_progress_user = s_ota.on_progress_user;
      on_error = s_ota.on_error;
      on_error_user = s_ota.on_error_user;
    }
    hal_mutex_unlock(s_ota.mutex);
    if (!available) {
      return;
    }

    switch (event.type) {
    case EventType::kStart:
      if (on_start != nullptr) {
        on_start(event.command, on_start_user);
      }
      break;
    case EventType::kEnd:
      if (on_end != nullptr) {
        on_end(on_end_user);
      }
      break;
    case EventType::kProgress:
      if (on_progress != nullptr) {
        on_progress(event.progress, event.total, on_progress_user);
      }
      break;
    case EventType::kError:
      if (on_error != nullptr) {
        on_error(event.error, on_error_user);
      }
      break;
    case EventType::kNone:
      break;
    }
  }
}

bool same_partition(const esp_partition_t *left, const esp_partition_t *right) {
  return left != nullptr && right != nullptr && left->type == right->type &&
         left->subtype == right->subtype && left->address == right->address &&
         left->size == right->size;
}

bool is_ota_app_partition(const esp_partition_t *partition) {
  return partition != nullptr && partition->type == ESP_PARTITION_TYPE_APP &&
         partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
         partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX;
}

hal_ota_boot_mode_t boot_mode(const esp_partition_t *running,
                              const esp_partition_t *boot) {
  if (running == nullptr || boot == nullptr) {
    return HAL_OTA_BOOT_RECOVERY;
  }
  if (!same_partition(running, boot)) {
    return HAL_OTA_BOOT_PENDING;
  }
  if (!is_ota_app_partition(running)) {
    return HAL_OTA_BOOT_STABLE;
  }
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t result = esp_ota_get_state_partition(running, &state);
  if (result == ESP_ERR_NOT_FOUND) {
    return HAL_OTA_BOOT_STABLE;
  }
  if (result != ESP_OK) {
    return HAL_OTA_BOOT_RECOVERY;
  }
  switch (state) {
  case ESP_OTA_IMG_NEW:
    return HAL_OTA_BOOT_PENDING;
  case ESP_OTA_IMG_PENDING_VERIFY:
    return HAL_OTA_BOOT_TRIAL;
  case ESP_OTA_IMG_VALID:
  case ESP_OTA_IMG_UNDEFINED:
    return HAL_OTA_BOOT_STABLE;
  case ESP_OTA_IMG_INVALID:
    return HAL_OTA_BOOT_ROLLBACK;
  case ESP_OTA_IMG_ABORTED:
  default:
    return HAL_OTA_BOOT_RECOVERY;
  }
}

} // namespace

bool hal_ota_set_port(uint16_t port) {
  if (port == 0u) {
    hal_derr("hal_ota_set_port: port must be > 0");
    return false;
  }
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  const bool accepted = !s_ota.started;
  if (accepted) {
    s_ota.port = port;
  }
  hal_mutex_unlock(s_ota.mutex);
  return accepted;
}

bool hal_ota_set_hostname(const char *hostname) {
  if (!validate_non_empty(hostname, "hal_ota_set_hostname", "hostname")) {
    return false;
  }
  const size_t length = strlen(hostname);
  if (length > kHostnameMaxLength) {
    hal_derr("hal_ota_set_hostname: hostname exceeds 63 bytes");
    return false;
  }
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  memcpy(s_ota.hostname, hostname, length + 1u);
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

bool hal_ota_set_password(const char *password) {
  if (password == nullptr) {
    hal_derr("hal_ota_set_password: password pointer is NULL");
    return false;
  }
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  const hal_status_t status =
      jh_ota_derive_password_key(password, s_ota.password_md5);
  if (status == HAL_OK) {
    s_ota.password_set = password[0] != '\0';
  }
  hal_mutex_unlock(s_ota.mutex);
  return status == HAL_OK;
}

bool hal_ota_on_start(hal_ota_on_start_callback_t callback, void *user) {
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  s_ota.on_start = callback;
  s_ota.on_start_user = user;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

bool hal_ota_on_end(hal_ota_on_end_callback_t callback, void *user) {
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  s_ota.on_end = callback;
  s_ota.on_end_user = user;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

bool hal_ota_on_progress(hal_ota_on_progress_callback_t callback, void *user) {
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  s_ota.on_progress = callback;
  s_ota.on_progress_user = user;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

bool hal_ota_on_error(hal_ota_on_error_callback_t callback, void *user) {
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  s_ota.on_error = callback;
  s_ota.on_error_user = user;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

bool hal_ota_begin(void) {
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
    s_ota.started = false;
    hal_mutex_unlock(s_ota.mutex);
    return false;
  }
  if (s_ota.udp != nullptr) {
    hal_udp_socket_close(s_ota.udp);
    s_ota.udp = nullptr;
  }
  update_abort_locked();
  transfer_reset_locked();
  queue_clear_locked();
  s_ota.reboot_pending = false;
  if (s_ota.port == 0u) {
    s_ota.port = kDefaultPort;
  }
  if (s_ota.hostname[0] == '\0') {
    (void)snprintf(s_ota.hostname, sizeof(s_ota.hostname), "%s",
                   HAL_TARGET_NAME);
  }

  hal_udp_socket_t socket = nullptr;
  hal_status_t status = hal_udp_socket_open_ex(&socket);
  if (status == HAL_OK) {
    hal_net_endpoint_t local = {};
    local.family = HAL_NET_AF_INET;
    local.addr_len = HAL_NET_IPV4_ADDR_LEN;
    local.port = s_ota.port;
    status = hal_udp_socket_bind_ex(socket, &local);
  }
  if (status != HAL_OK) {
    if (socket != nullptr) {
      hal_udp_socket_close(socket);
    }
    s_ota.started = false;
    hal_mutex_unlock(s_ota.mutex);
    return false;
  }
  s_ota.udp = socket;
  s_ota.started = true;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

void hal_ota_handle(void) {
  if (!ensure_mutex()) {
    return;
  }
  (void)hal_net_service();
  hal_mutex_lock(s_ota.mutex);
  if (!s_ota.started) {
    hal_mutex_unlock(s_ota.mutex);
    return;
  }
  udp_service_locked();
  if (s_ota.state == ServiceState::kBeginTransfer) {
    begin_transfer_locked();
  } else if (s_ota.state == ServiceState::kReceive) {
    receive_transfer_locked();
  } else if (s_ota.state == ServiceState::kWaitAuth &&
             hal_millis_deadline_expired(s_ota.last_activity_ms,
                                         kConnectTimeoutMs)) {
    transfer_fail_locked(HAL_OTA_ERROR_AUTH, "Authentication Timeout");
  }
  hal_mutex_unlock(s_ota.mutex);

  dispatch_events();
  hal_mutex_lock(s_ota.mutex);
  const bool reboot = s_ota.reboot_pending;
  s_ota.reboot_pending = false;
  hal_mutex_unlock(s_ota.mutex);
  if (reboot) {
    hal_delay_ms(100u);
    esp_restart();
  }
}

bool hal_ota_is_started(void) {
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  const bool started = s_ota.started;
  hal_mutex_unlock(s_ota.mutex);
  return started;
}

hal_status_t hal_ota_confirm_boot_ex(void) {
  if (!ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_ota.mutex);
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == nullptr) {
    hal_mutex_unlock(s_ota.mutex);
    return HAL_ESTATE;
  }
  if (!is_ota_app_partition(running)) {
    hal_mutex_unlock(s_ota.mutex);
    return HAL_OK;
  }
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_result = esp_ota_get_state_partition(running, &state);
  if (state_result == ESP_ERR_NOT_FOUND || state == ESP_OTA_IMG_VALID ||
      state == ESP_OTA_IMG_UNDEFINED) {
    hal_mutex_unlock(s_ota.mutex);
    return HAL_OK;
  }
  if (state_result != ESP_OK) {
    const hal_status_t status = ota_status_from_esp(state_result);
    hal_mutex_unlock(s_ota.mutex);
    return status;
  }
  if (state != ESP_OTA_IMG_NEW && state != ESP_OTA_IMG_PENDING_VERIFY) {
    hal_mutex_unlock(s_ota.mutex);
    return HAL_ESTATE;
  }
  const hal_status_t status =
      ota_status_from_esp(esp_ota_mark_app_valid_cancel_rollback());
  hal_mutex_unlock(s_ota.mutex);
  return status;
}

hal_status_t hal_ota_get_boot_info_ex(hal_ota_boot_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  if (!ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_ota.mutex);
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  hal_ota_boot_info_t info = {};
  info.mode = boot_mode(running, boot);
  info.max_attempts = kEspRollbackAttempts;
  copy_partition_version(running, info.program_version);
  if (!same_partition(boot, running)) {
    copy_partition_version(boot, info.staging_version);
  }
  hal_mutex_unlock(s_ota.mutex);
  *out_info = info;
  return info.mode == HAL_OTA_BOOT_RECOVERY ? HAL_EPROTO : HAL_OK;
}

#endif /* HAL_ENABLE_OTA */
#endif /* HAL_TARGET_IS_ESP32_FAMILY */
