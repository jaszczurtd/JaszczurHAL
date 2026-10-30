#pragma once

#include "hal/core/hal_layout_check.h"

#include "hal/radio/hal_lora_link.h"

#ifdef HAL_ENABLE_LORA_LINK

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JH_LORA_LINK_FRAME_HEADER_SIZE 25u
#define JH_LORA_LINK_FRAME_TAG_SIZE 16u
#define JH_LORA_LINK_FRAME_MAX_PLAINTEXT                                       \
  (HAL_LORA_RADIO_MAX_PAYLOAD - JH_LORA_LINK_FRAME_HEADER_SIZE)
#define JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST UINT8_C(0x01)
#define JH_LORA_LINK_FRAME_FLAG_ACK UINT8_C(0x02)
#define JH_LORA_LINK_FRAME_FLAG_ENCRYPTED UINT8_C(0x04)
/** @brief Acknowledgement body: the acknowledged session ID and sequence. */
#define JH_LORA_LINK_FRAME_ACK_PAYLOAD_SIZE 8u

typedef struct {
  uint8_t flags;
  uint8_t port;
  uint16_t source;
  uint16_t destination;
  uint32_t session_id;
  uint32_t sequence;
  uint8_t fragment_index;
  uint8_t fragment_count;
  uint16_t message_length;
  uint32_t integrity;
} jh_lora_link_frame_header_t;

size_t jh_lora_link_frame_payload_capacity(bool encrypted);

hal_status_t
jh_lora_link_frame_encode(const jh_lora_link_frame_header_t *header,
                          const uint8_t *payload, size_t payload_length,
                          const uint8_t *key, uint8_t *out_frame,
                          size_t frame_capacity, size_t *out_frame_length);

hal_status_t jh_lora_link_frame_decode(const uint8_t *frame,
                                       size_t frame_length, const uint8_t *key,
                                       jh_lora_link_frame_header_t *out_header,
                                       uint8_t *out_payload,
                                       size_t payload_capacity,
                                       size_t *out_payload_length);

/**
 * @brief Write the body of an acknowledgement for one received message.
 *
 * An acknowledgement is numbered by its own sender like every other frame
 * and names the acknowledged sender session and sequence in this
 * authenticated body, so two acknowledgements never share an AEAD nonce.
 *
 * @param acknowledged_session_id Session ID of the acknowledged message.
 * @param acknowledged_sequence Sequence number of the acknowledged message.
 * @param out_body JH_LORA_LINK_FRAME_ACK_PAYLOAD_SIZE bytes to fill.
 */
void jh_lora_link_frame_encode_ack_body(uint32_t acknowledged_session_id,
                                        uint32_t acknowledged_sequence,
                                        uint8_t *out_body);

/**
 * @brief Read an acknowledgement body written by
 * jh_lora_link_frame_encode_ack_body().
 *
 * @param body Decoded acknowledgement payload.
 * @param body_length Payload length; only
 *        JH_LORA_LINK_FRAME_ACK_PAYLOAD_SIZE is accepted.
 * @param out_acknowledged_session_id Receives the acknowledged session ID.
 * @param out_acknowledged_sequence Receives the acknowledged sequence.
 * @return true when the body has the expected length and was decoded.
 */
bool jh_lora_link_frame_decode_ack_body(const uint8_t *body, size_t body_length,
                                        uint32_t *out_acknowledged_session_id,
                                        uint32_t *out_acknowledged_sequence);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_LORA_LINK */
