#include "hal/radio/jh_lora_link_frame.h"

#ifdef HAL_ENABLE_LORA_LINK

#ifdef HAL_ENABLE_CRYPTO
#include "hal/security/hal_crypto.h"
#endif

#include <string.h>

#define JH_LORA_LINK_MAGIC_0 UINT8_C(0x4A)
#define JH_LORA_LINK_MAGIC_1 UINT8_C(0x4C)
#define JH_LORA_LINK_PROTOCOL_VERSION UINT8_C(1)
#define JH_LORA_LINK_FRAME_FLAGS                                               \
  (JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST | JH_LORA_LINK_FRAME_FLAG_ACK |         \
   JH_LORA_LINK_FRAME_FLAG_ENCRYPTED)

static void put_u16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)(value >> 8u);
  out[1] = (uint8_t)value;
}

static void put_u32(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)(value >> 24u);
  out[1] = (uint8_t)(value >> 16u);
  out[2] = (uint8_t)(value >> 8u);
  out[3] = (uint8_t)value;
}

static uint16_t get_u16(const uint8_t *input) {
  return (uint16_t)(((uint16_t)input[0] << 8u) | input[1]);
}

static uint32_t get_u32(const uint8_t *input) {
  return ((uint32_t)input[0] << 24u) | ((uint32_t)input[1] << 16u) |
         ((uint32_t)input[2] << 8u) | input[3];
}

static bool is_ack(const jh_lora_link_frame_header_t *header) {
  return (header->flags & JH_LORA_LINK_FRAME_FLAG_ACK) != 0u;
}

static bool is_encrypted(const jh_lora_link_frame_header_t *header) {
  return (header->flags & JH_LORA_LINK_FRAME_FLAG_ENCRYPTED) != 0u;
}

size_t jh_lora_link_frame_payload_capacity(bool encrypted) {
  const size_t overhead = JH_LORA_LINK_FRAME_HEADER_SIZE +
                          (encrypted ? JH_LORA_LINK_FRAME_TAG_SIZE : 0u);
  return HAL_LORA_RADIO_MAX_PAYLOAD - overhead;
}

static void encode_header(const jh_lora_link_frame_header_t *header,
                          uint8_t *out) {
  out[0] = JH_LORA_LINK_MAGIC_0;
  out[1] = JH_LORA_LINK_MAGIC_1;
  out[2] = JH_LORA_LINK_PROTOCOL_VERSION;
  out[3] = header->flags;
  out[4] = header->port;
  put_u16(&out[5], header->source);
  put_u16(&out[7], header->destination);
  put_u32(&out[9], header->session_id);
  put_u32(&out[13], header->sequence);
  out[17] = header->fragment_index;
  out[18] = header->fragment_count;
  put_u16(&out[19], header->message_length);
  put_u32(&out[21], header->integrity);
}

static void decode_header(const uint8_t *input,
                          jh_lora_link_frame_header_t *out) {
  out->flags = input[3];
  out->port = input[4];
  out->source = get_u16(&input[5]);
  out->destination = get_u16(&input[7]);
  out->session_id = get_u32(&input[9]);
  out->sequence = get_u32(&input[13]);
  out->fragment_index = input[17];
  out->fragment_count = input[18];
  out->message_length = get_u16(&input[19]);
  out->integrity = get_u32(&input[21]);
}

static bool header_common_valid(const jh_lora_link_frame_header_t *header) {
  const bool ack = is_ack(header);
  if ((header->flags & ~JH_LORA_LINK_FRAME_FLAGS) != 0u ||
      header->source == HAL_LORA_LINK_ADDRESS_NONE ||
      header->source == HAL_LORA_LINK_ADDRESS_BROADCAST ||
      header->destination == HAL_LORA_LINK_ADDRESS_NONE ||
      header->session_id == 0u ||
      (ack && header->destination == HAL_LORA_LINK_ADDRESS_BROADCAST) ||
      (ack && (header->flags & JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST) != 0u) ||
      (!ack && header->destination == HAL_LORA_LINK_ADDRESS_BROADCAST &&
       (header->flags & JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST) != 0u)) {
    return false;
  }
  if (ack) {
    return header->port == 0u && header->fragment_index == 0u &&
           header->fragment_count == 0u && header->message_length == 0u &&
           header->integrity != 0u;
  }
  return header->fragment_count > 0u &&
         header->fragment_index < header->fragment_count &&
         header->message_length > 0u;
}

static bool payload_shape_valid(const jh_lora_link_frame_header_t *header,
                                size_t payload_length) {
  if (is_ack(header)) {
    return payload_length == 0u;
  }
  const size_t capacity =
      jh_lora_link_frame_payload_capacity(is_encrypted(header));
  const size_t expected_fragments =
      ((size_t)header->message_length + capacity - 1u) / capacity;
  if (expected_fragments != header->fragment_count ||
      expected_fragments > 32u) {
    return false;
  }
  const size_t offset = (size_t)header->fragment_index * capacity;
  const size_t remaining = (size_t)header->message_length - offset;
  const size_t expected_payload = remaining < capacity ? remaining : capacity;
  return payload_length == expected_payload;
}

#ifdef HAL_ENABLE_CRYPTO
static void build_nonce(const jh_lora_link_frame_header_t *header,
                        uint8_t nonce[HAL_CHACHA20_NONCE_BYTES]) {
  put_u32(&nonce[0], header->session_id);
  put_u16(&nonce[4], header->source);
  put_u32(&nonce[6], header->sequence);
  nonce[10] = header->fragment_index;
  nonce[11] = is_ack(header) ? 1u : 0u;
}
#endif

hal_status_t
jh_lora_link_frame_encode(const jh_lora_link_frame_header_t *header,
                          const uint8_t *payload, size_t payload_length,
                          const uint8_t *key, uint8_t *out_frame,
                          size_t frame_capacity, size_t *out_frame_length) {
  if (out_frame_length != NULL) {
    *out_frame_length = 0u;
  }
  if (header == NULL || out_frame == NULL || out_frame_length == NULL ||
      (payload_length > 0u && payload == NULL) ||
      !header_common_valid(header) ||
      !payload_shape_valid(header, payload_length)) {
    return HAL_EINVAL;
  }
  const bool encrypted = is_encrypted(header);
  const size_t frame_length = JH_LORA_LINK_FRAME_HEADER_SIZE + payload_length +
                              (encrypted ? JH_LORA_LINK_FRAME_TAG_SIZE : 0u);
  if (frame_length > frame_capacity ||
      frame_length > HAL_LORA_RADIO_MAX_PAYLOAD) {
    return HAL_EOVERFLOW;
  }
  encode_header(header, out_frame);
  if (!encrypted) {
    if (payload_length > 0u) {
      memcpy(&out_frame[JH_LORA_LINK_FRAME_HEADER_SIZE], payload,
             payload_length);
    }
    *out_frame_length = frame_length;
    return HAL_OK;
  }
  if (key == NULL) {
    return HAL_EINVAL;
  }
#ifdef HAL_ENABLE_CRYPTO
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {};
  build_nonce(header, nonce);
  uint8_t *ciphertext = &out_frame[JH_LORA_LINK_FRAME_HEADER_SIZE];
  uint8_t *tag = &ciphertext[payload_length];
  if (!hal_chacha20_poly1305_encrypt(key, nonce, out_frame,
                                     JH_LORA_LINK_FRAME_HEADER_SIZE, payload,
                                     payload_length, ciphertext, tag)) {
    return HAL_EAUTH;
  }
  *out_frame_length = frame_length;
  return HAL_OK;
#else
  (void)payload;
  return HAL_EUNSUPPORTED;
#endif
}

hal_status_t jh_lora_link_frame_decode(const uint8_t *frame,
                                       size_t frame_length, const uint8_t *key,
                                       jh_lora_link_frame_header_t *out_header,
                                       uint8_t *out_payload,
                                       size_t payload_capacity,
                                       size_t *out_payload_length) {
  if (out_payload_length != NULL) {
    *out_payload_length = 0u;
  }
  if (frame == NULL || out_header == NULL || out_payload_length == NULL ||
      frame_length < JH_LORA_LINK_FRAME_HEADER_SIZE ||
      frame_length > HAL_LORA_RADIO_MAX_PAYLOAD ||
      frame[0] != JH_LORA_LINK_MAGIC_0 || frame[1] != JH_LORA_LINK_MAGIC_1 ||
      frame[2] != JH_LORA_LINK_PROTOCOL_VERSION) {
    return HAL_EPROTO;
  }
  decode_header(frame, out_header);
  if (!header_common_valid(out_header)) {
    return HAL_EPROTO;
  }
  const bool encrypted = is_encrypted(out_header);
  const size_t overhead = JH_LORA_LINK_FRAME_HEADER_SIZE +
                          (encrypted ? JH_LORA_LINK_FRAME_TAG_SIZE : 0u);
  if (frame_length < overhead) {
    return HAL_EPROTO;
  }
  const size_t payload_length = frame_length - overhead;
  if (!payload_shape_valid(out_header, payload_length)) {
    return HAL_EPROTO;
  }
  if (payload_length > payload_capacity ||
      (payload_length > 0u && out_payload == NULL)) {
    return HAL_EOVERFLOW;
  }
  if (!encrypted) {
    if (payload_length > 0u) {
      memcpy(out_payload, &frame[JH_LORA_LINK_FRAME_HEADER_SIZE],
             payload_length);
    }
    *out_payload_length = payload_length;
    return HAL_OK;
  }
  if (key == NULL) {
    return HAL_EAUTH;
  }
#ifdef HAL_ENABLE_CRYPTO
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {};
  build_nonce(out_header, nonce);
  const uint8_t *ciphertext = &frame[JH_LORA_LINK_FRAME_HEADER_SIZE];
  const uint8_t *tag = &ciphertext[payload_length];
  if (!hal_chacha20_poly1305_decrypt(key, nonce, frame,
                                     JH_LORA_LINK_FRAME_HEADER_SIZE, ciphertext,
                                     payload_length, tag, out_payload)) {
    return HAL_EAUTH;
  }
  *out_payload_length = payload_length;
  return HAL_OK;
#else
  return HAL_EUNSUPPORTED;
#endif
}

#endif /* HAL_ENABLE_LORA_LINK */
