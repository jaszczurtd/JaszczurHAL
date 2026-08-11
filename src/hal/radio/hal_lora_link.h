#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LORA_LINK

/**
 * @file hal_lora_link.h
 * @brief Reliable private point-to-point messaging over raw LoRa radio.
 *
 * The link owns one configured @ref hal_lora_radio_t for its lifetime. It
 * adds addressing, message sequence numbers, acknowledgements, bounded
 * retransmission, duplicate suppression and fragmentation. Optional
 * ChaCha20-Poly1305 protection is available when HAL_ENABLE_CRYPTO is enabled.
 */

#include "hal/core/hal_status.h"
#include "hal/radio/hal_lora_radio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Reserved address meaning no node. */
#define HAL_LORA_LINK_ADDRESS_NONE UINT16_C(0)

/** @brief Destination accepted by every link using the same modem settings. */
#define HAL_LORA_LINK_ADDRESS_BROADCAST UINT16_MAX

/** @brief Required key size for ChaCha20-Poly1305 protection. */
#define HAL_LORA_LINK_CRYPTO_KEY_BYTES 32u

/** @brief Opaque reliable-link handle. */
typedef struct hal_lora_link_impl_s *hal_lora_link_t;

/** @brief Per-link frame protection policy. */
typedef enum {
  HAL_LORA_LINK_SECURITY_NONE = 0,
  HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305,
} hal_lora_link_security_t;

/** @brief Complete construction descriptor for one reliable link. */
typedef struct {
  hal_lora_radio_t radio;
  uint16_t local_address;
  uint32_t session_id;
  uint32_t initial_sequence;
  uint32_t acknowledgement_timeout_ms;
  uint32_t retry_backoff_ms;
  uint32_t reassembly_timeout_ms;
  uint8_t max_retries;
  hal_lora_link_security_t security;
  const uint8_t *key;
  size_t key_length;
} hal_lora_link_config_t;

/** @brief Stable operating state of one link. */
typedef enum {
  HAL_LORA_LINK_STATE_RECEIVING = 0,
  HAL_LORA_LINK_STATE_TRANSMITTING,
  HAL_LORA_LINK_STATE_WAITING_ACKNOWLEDGEMENT,
  HAL_LORA_LINK_STATE_RETRY_WAIT,
  HAL_LORA_LINK_STATE_SENDING_ACKNOWLEDGEMENT,
  HAL_LORA_LINK_STATE_ERROR,
} hal_lora_link_state_t;

/** @brief Snapshot of the current or most recent send operation. */
typedef struct {
  hal_lora_operation_state_t state;
  hal_status_t result;
  uint32_t sequence;
  uint16_t attempts;
  uint8_t fragment_count;
} hal_lora_link_send_status_t;

/** @brief Metadata copied with one complete reassembled message. */
typedef struct {
  uint16_t source;
  uint16_t destination;
  uint32_t session_id;
  uint32_t sequence;
  uint8_t port;
  uint8_t fragment_count;
  bool encrypted;
  hal_lora_packet_info_t packet;
} hal_lora_link_message_info_t;

/** @brief Per-handle counters and recent protocol observations. */
typedef struct {
  uint32_t transmitted_messages;
  uint32_t received_messages;
  uint32_t transmitted_frames;
  uint32_t received_frames;
  uint32_t acknowledgements_sent;
  uint32_t acknowledgements_received;
  uint32_t retransmissions;
  uint32_t duplicate_messages;
  uint32_t duplicate_fragments;
  uint32_t malformed_frames;
  uint32_t authentication_failures;
  uint32_t integrity_failures;
  uint32_t reassembly_timeouts;
  uint32_t reassembly_drops;
  uint32_t dropped_messages;
  uint32_t send_timeouts;
  uint32_t cancelled_sends;
  uint32_t operation_errors;
  uint32_t last_received_sequence;
  uint32_t last_transmitted_sequence;
  uint16_t last_source;
  uint16_t last_destination;
  int16_t last_rssi_dbm;
  int8_t last_snr_db;
  hal_status_t last_error;
} hal_lora_link_diagnostics_t;

/**
 * @brief Return bounded defaults for a configured radio and local identity.
 *
 * The returned descriptor uses no protection, three retries, a 1500 ms ACK
 * timeout, 200 ms retry backoff and 5000 ms reassembly timeout.
 */
hal_lora_link_config_t hal_lora_link_config_defaults(hal_lora_radio_t radio,
                                                     uint16_t local_address,
                                                     uint32_t session_id);

/**
 * @brief Attach a reliable link to one configured, standby radio.
 *
 * The link starts continuous receive and exclusively owns radio operations
 * until destroyed. The caller retains radio lifecycle ownership and must keep
 * it alive. A non-zero session_id must be unique for each local-address/key
 * session; reuse after reboot breaks replay protection and AEAD nonce safety.
 */
hal_status_t hal_lora_link_create(const hal_lora_link_config_t *config,
                                  hal_lora_link_t *out_link);

/** @brief Detach the link and leave its radio in standby. */
hal_status_t hal_lora_link_destroy(hal_lora_link_t link);

/**
 * @brief Start sending one copied message.
 *
 * Unicast may request an acknowledgement. Broadcast messages must be sent
 * without acknowledgement. The port is application-defined; zero is valid.
 */
hal_status_t hal_lora_link_send_start(hal_lora_link_t link,
                                      uint16_t destination, uint8_t port,
                                      const uint8_t *data, size_t length,
                                      bool acknowledged);

/** @brief Copy the current or most recent asynchronous send status. */
hal_status_t
hal_lora_link_get_send_status(hal_lora_link_t link,
                              hal_lora_link_send_status_t *out_status);

/**
 * @brief Copy and consume one complete reassembled message.
 *
 * HAL_EAGAIN means no message is queued. On HAL_EOVERFLOW, out_length reports
 * the complete message length and the queued message is consumed.
 */
hal_status_t hal_lora_link_receive(hal_lora_link_t link, uint8_t *buffer,
                                   size_t buffer_size, size_t *out_length,
                                   hal_lora_link_message_info_t *out_info);

/**
 * @brief Advance radio I/O, ACK/retry timing and message reassembly.
 *
 * Call regularly from app_task0() or one owning FreeRTOS task. HAL_EAGAIN
 * means no externally visible progress occurred during this call.
 */
hal_status_t hal_lora_link_process(hal_lora_link_t link);

/** @brief Cancel an active application send and resume continuous receive. */
hal_status_t hal_lora_link_cancel(hal_lora_link_t link);

/** @brief Copy the current stable link state. */
hal_status_t hal_lora_link_get_state(hal_lora_link_t link,
                                     hal_lora_link_state_t *out_state);

/** @brief Copy protocol counters and recent observations. */
hal_status_t
hal_lora_link_get_diagnostics(hal_lora_link_t link,
                              hal_lora_link_diagnostics_t *out_diagnostics);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_LORA_LINK */
