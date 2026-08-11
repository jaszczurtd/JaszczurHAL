#pragma once

#include "hal/radio/hal_lora_link.h"

#ifdef HAL_ENABLE_LORA_LINK

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JH_LORA_LINK_FRAME_HEADER_SIZE 25u
#define JH_LORA_LINK_FRAME_TAG_SIZE 16u
#define JH_LORA_LINK_FRAME_MAX_PLAINTEXT                                       \
  (HAL_LORA_RADIO_MAX_PAYLOAD - JH_LORA_LINK_FRAME_HEADER_SIZE)
#define JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST UINT8_C(0x01)
#define JH_LORA_LINK_FRAME_FLAG_ACK UINT8_C(0x02)
#define JH_LORA_LINK_FRAME_FLAG_ENCRYPTED UINT8_C(0x04)

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

#endif /* HAL_ENABLE_LORA_LINK */
