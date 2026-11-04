#include "hal/security/hal_crypto.h"

#ifdef HAL_ENABLE_CRYPTO

#include "hal/core/hal_text.h"

#include <limits.h>
#include <string.h>

extern "C" {
#include "hal/network/wireguard/core/crypto/chacha20.h"
#include "hal/network/wireguard/core/crypto/chacha20poly1305.h"
#include "hal/network/wireguard/core/crypto/crypto.h"
}

namespace {

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline int base64_value(char c) {
  if (c >= 'A' && c <= 'Z')
    return (int)(c - 'A');
  if (c >= 'a' && c <= 'z')
    return (int)(c - 'a') + 26;
  if (c >= '0' && c <= '9')
    return (int)(c - '0') + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

static inline bool can_append(size_t index, size_t count, size_t capacity) {
  return (index <= capacity) && (count <= (capacity - index));
}

static const uint32_t kMd5K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu,
    0x4787c62au, 0xa8304613u, 0xfd469501u, 0x698098d8u, 0x8b44f7afu,
    0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu,
    0x49b40821u, 0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u, 0x21e1cde6u,
    0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u,
    0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u,
    0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u,
    0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u, 0xf4292244u, 0x432aff97u,
    0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du,
    0x85845dd1u, 0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};

static const uint8_t kMd5S[64] = {
    7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u,
    5u, 9u,  14u, 20u, 5u, 9u,  14u, 20u, 5u, 9u,  14u, 20u, 5u, 9u,  14u, 20u,
    4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u,
    6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u};

static inline uint32_t rotl32(uint32_t x, uint32_t n) {
  return (x << n) | (x >> (32u - n));
}

static const uint8_t kZeroChaChaBlock[HAL_CHACHA20_BLOCK_BYTES] = {};

static bool chacha20_counter_will_overflow(uint32_t counter, size_t input_len) {
  uint64_t blocks_needed = (uint64_t)(input_len / HAL_CHACHA20_BLOCK_BYTES) +
                           ((input_len % HAL_CHACHA20_BLOCK_BYTES) ? 1u : 0u);
  return ((uint64_t)counter + blocks_needed) > 0x100000000ULL;
}

static void chacha20_xor_stream_ietf(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES], uint32_t counter,
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES], const uint8_t *input,
    size_t input_len, uint8_t *output) {
  struct chacha20_ctx ctx;
  chacha20_init_ietf(&ctx, key, counter, nonce);

  size_t offset = 0u;
  while (offset < input_len) {
    size_t remaining = input_len - offset;
    uint32_t chunk =
        (remaining > (size_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)remaining;
    chacha20(&ctx, output + offset, input + offset, chunk);
    offset += (size_t)chunk;
  }

  crypto_zero(&ctx, sizeof(ctx));
}

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t m[16];

  for (size_t i = 0u; i < 16u; ++i) {
    m[i] = U8TO32_LITTLE(block + (i * 4u));
  }

  for (uint32_t i = 0u; i < 64u; ++i) {
    uint32_t f = 0u;
    uint32_t g = 0u;

    if (i < 16u) {
      f = (b & c) | ((~b) & d);
      g = i;
    } else if (i < 32u) {
      f = (d & b) | ((~d) & c);
      g = (5u * i + 1u) & 0x0Fu;
    } else if (i < 48u) {
      f = b ^ c ^ d;
      g = (3u * i + 5u) & 0x0Fu;
    } else {
      f = c ^ (b | (~d));
      g = (7u * i) & 0x0Fu;
    }

    uint32_t tmp = d;
    d = c;
    c = b;
    uint32_t sum = a + f + kMd5K[i] + m[g];
    b = b + rotl32(sum, kMd5S[i]);
    a = tmp;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

} // namespace

bool hal_chacha20_block(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                        uint32_t counter,
                        const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                        uint8_t out_block[HAL_CHACHA20_BLOCK_BYTES]) {
  if (key == nullptr || nonce == nullptr || out_block == nullptr) {
    return false;
  }

  chacha20_xor_stream_ietf(key, counter, nonce, kZeroChaChaBlock,
                           HAL_CHACHA20_BLOCK_BYTES, out_block);
  return true;
}

bool hal_chacha20_xor(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                      uint32_t counter,
                      const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                      const uint8_t *input, size_t input_len, uint8_t *output) {
  if (key == nullptr || nonce == nullptr) {
    return false;
  }
  if (input_len == 0u) {
    return true;
  }
  if (input == nullptr || output == nullptr) {
    return false;
  }

  if (chacha20_counter_will_overflow(counter, input_len)) {
    return false;
  }

  chacha20_xor_stream_ietf(key, counter, nonce, input, input_len, output);

  return true;
}

bool hal_chacha20_poly1305_encrypt(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES],
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES], const uint8_t *aad,
    size_t aad_len, const uint8_t *plaintext, size_t text_len,
    uint8_t *ciphertext, uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES]) {
  if (key == nullptr || nonce == nullptr || tag == nullptr) {
    return false;
  }
  if (aad_len > 0u && aad == nullptr) {
    return false;
  }
  if (text_len > 0u && (plaintext == nullptr || ciphertext == nullptr)) {
    return false;
  }
  if (chacha20_counter_will_overflow(1u, text_len)) {
    return false;
  }

  return chacha20poly1305_encrypt_ietf_detached(
      ciphertext, tag, plaintext, text_len, aad, aad_len, nonce, key);
}

bool hal_chacha20_poly1305_decrypt(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES],
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES], const uint8_t *aad,
    size_t aad_len, const uint8_t *ciphertext, size_t text_len,
    const uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES], uint8_t *plaintext) {
  if (key == nullptr || nonce == nullptr || tag == nullptr) {
    return false;
  }
  if (aad_len > 0u && aad == nullptr) {
    return false;
  }
  if (text_len > 0u && (ciphertext == nullptr || plaintext == nullptr)) {
    return false;
  }
  if (chacha20_counter_will_overflow(1u, text_len)) {
    return false;
  }

  return chacha20poly1305_decrypt_ietf_detached(plaintext, ciphertext, text_len,
                                                tag, aad, aad_len, nonce, key);
}

bool hal_md5(const uint8_t *input, size_t input_len,
             uint8_t out_digest[HAL_MD5_DIGEST_BYTES]) {
  if (out_digest == nullptr) {
    return false;
  }
  if (input_len > 0u && input == nullptr) {
    return false;
  }
  if (input_len > (UINT64_MAX / 8u)) {
    return false;
  }

  uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};

  size_t full_blocks = input_len / 64u;
  for (size_t i = 0u; i < full_blocks; ++i) {
    md5_transform(state, input + (i * 64u));
  }

  size_t rem = input_len - (full_blocks * 64u);
  uint8_t tail[128];
  memset(tail, 0, sizeof(tail));
  if (rem > 0u) {
    memcpy(tail, input + (full_blocks * 64u), rem);
  }
  tail[rem] = 0x80u;

  size_t padded_len = (rem < 56u) ? 64u : 128u;
  uint64_t bit_len = (uint64_t)input_len * 8u;
  for (size_t i = 0u; i < 8u; ++i) {
    tail[padded_len - 8u + i] = (uint8_t)((bit_len >> (8u * i)) & 0xFFu);
  }

  md5_transform(state, tail);
  if (padded_len == 128u) {
    md5_transform(state, tail + 64u);
  }

  U32TO8_LITTLE(&out_digest[0], state[0]);
  U32TO8_LITTLE(&out_digest[4], state[1]);
  U32TO8_LITTLE(&out_digest[8], state[2]);
  U32TO8_LITTLE(&out_digest[12], state[3]);
  return true;
}

bool hal_md5_hex(const uint8_t *input, size_t input_len, char *output,
                 size_t out_size) {
  if (output == nullptr) {
    return false;
  }
  if (out_size < HAL_MD5_HEX_BUF_SIZE) {
    if (out_size > 0u) {
      output[0] = '\0';
    }
    return false;
  }

  uint8_t digest[HAL_MD5_DIGEST_BYTES];
  if (!hal_md5(input, input_len, digest)) {
    output[0] = '\0';
    return false;
  }

  return hal_text_format_hex_ex(digest, sizeof(digest), false, output,
                                out_size) == HAL_OK;
}

size_t hal_base64_encoded_len(size_t input_len) {
  if (input_len > (SIZE_MAX - 2u)) {
    return 0u;
  }
  size_t quads = (input_len + 2u) / 3u;
  if (quads > (SIZE_MAX / 4u)) {
    return 0u;
  }
  return quads * 4u;
}

size_t hal_base64_decoded_max_len(size_t input_len) {
  size_t quads = input_len / 4u;
  if (quads > (SIZE_MAX / 3u)) {
    return 0u;
  }
  return quads * 3u;
}

bool hal_base64_encode(const uint8_t *input, size_t input_len, char *output,
                       size_t out_size, size_t *out_len) {
  if (out_len != nullptr) {
    *out_len = 0u;
  }

  if (output == nullptr) {
    return false;
  }

  size_t encoded_len = hal_base64_encoded_len(input_len);
  if ((input_len > 0u && encoded_len == 0u) || encoded_len > (SIZE_MAX - 1u)) {
    output[0] = '\0';
    return false;
  }

  if (out_size < (encoded_len + 1u)) {
    if (out_size > 0u) {
      output[0] = '\0';
    }
    return false;
  }

  if (input_len > 0u && input == nullptr) {
    output[0] = '\0';
    return false;
  }

  size_t in_i = 0u;
  size_t out_i = 0u;

  while ((input_len - in_i) >= 3u) {
    uint32_t triple = ((uint32_t)input[in_i] << 16) |
                      ((uint32_t)input[in_i + 1u] << 8) |
                      (uint32_t)input[in_i + 2u];

    output[out_i++] = kBase64Table[(triple >> 18) & 0x3Fu];
    output[out_i++] = kBase64Table[(triple >> 12) & 0x3Fu];
    output[out_i++] = kBase64Table[(triple >> 6) & 0x3Fu];
    output[out_i++] = kBase64Table[triple & 0x3Fu];
    in_i += 3u;
  }

  size_t remaining = input_len - in_i;
  if (remaining == 1u) {
    uint32_t triple = ((uint32_t)input[in_i] << 16);
    output[out_i++] = kBase64Table[(triple >> 18) & 0x3Fu];
    output[out_i++] = kBase64Table[(triple >> 12) & 0x3Fu];
    output[out_i++] = '=';
    output[out_i++] = '=';
  } else if (remaining == 2u) {
    uint32_t triple =
        ((uint32_t)input[in_i] << 16) | ((uint32_t)input[in_i + 1u] << 8);
    output[out_i++] = kBase64Table[(triple >> 18) & 0x3Fu];
    output[out_i++] = kBase64Table[(triple >> 12) & 0x3Fu];
    output[out_i++] = kBase64Table[(triple >> 6) & 0x3Fu];
    output[out_i++] = '=';
  }

  output[out_i] = '\0';
  if (out_len != nullptr) {
    *out_len = out_i;
  }
  return true;
}

bool hal_base64_decode(const char *input, size_t input_len, uint8_t *output,
                       size_t out_size, size_t *out_len) {
  if (out_len != nullptr) {
    *out_len = 0u;
  }

  if (input_len == 0u) {
    if (output == nullptr && out_size != 0u) {
      return false;
    }
    if (out_len != nullptr) {
      *out_len = 0u;
    }
    return true;
  }

  if (input == nullptr) {
    return false;
  }

  if ((input_len % 4u) != 0u) {
    return false;
  }

  if (output == nullptr && out_size != 0u) {
    return false;
  }

  size_t out_i = 0u;
  for (size_t in_i = 0u; in_i < input_len; in_i += 4u) {
    char c0 = input[in_i];
    char c1 = input[in_i + 1u];
    char c2 = input[in_i + 2u];
    char c3 = input[in_i + 3u];
    bool is_last = (in_i + 4u == input_len);

    int v0 = base64_value(c0);
    int v1 = base64_value(c1);
    if (v0 < 0 || v1 < 0) {
      return false;
    }

    if (c2 == '=') {
      if (!is_last || c3 != '=') {
        return false;
      }
      if (out_i > (SIZE_MAX - 1u)) {
        return false;
      }
      if (output != nullptr) {
        if (!can_append(out_i, 1u, out_size)) {
          return false;
        }
        output[out_i] = (uint8_t)((v0 << 2) | (v1 >> 4));
      }
      out_i += 1u;
      continue;
    }

    int v2 = base64_value(c2);
    if (v2 < 0) {
      return false;
    }

    if (c3 == '=') {
      if (!is_last) {
        return false;
      }
      if (out_i > (SIZE_MAX - 2u)) {
        return false;
      }
      if (output != nullptr) {
        if (!can_append(out_i, 2u, out_size)) {
          return false;
        }
        output[out_i] = (uint8_t)((v0 << 2) | (v1 >> 4));
        output[out_i + 1u] = (uint8_t)(((v1 & 0x0F) << 4) | (v2 >> 2));
      }
      out_i += 2u;
      continue;
    }

    int v3 = base64_value(c3);
    if (v3 < 0) {
      return false;
    }

    if (out_i > (SIZE_MAX - 3u)) {
      return false;
    }
    if (output != nullptr) {
      if (!can_append(out_i, 3u, out_size)) {
        return false;
      }
      output[out_i] = (uint8_t)((v0 << 2) | (v1 >> 4));
      output[out_i + 1u] = (uint8_t)(((v1 & 0x0F) << 4) | (v2 >> 2));
      output[out_i + 2u] = (uint8_t)(((v2 & 0x03) << 6) | v3);
    }
    out_i += 3u;
  }

  if (out_len != nullptr) {
    *out_len = out_i;
  }
  return true;
}

/* ── SHA-256 / HMAC-SHA256 (FIPS 180-4 + RFC 2104) ───────────────────────── */

namespace {

inline void bytes_to_hex_lower(const uint8_t *bytes, size_t bytes_len,
                               char *output, size_t out_size) {
  static const char hex[] = "0123456789abcdef";
  if (output == nullptr || out_size == 0u) {
    return;
  }
  if (bytes_len * 2u + 1u > out_size) {
    output[0] = '\0';
    return;
  }
  for (size_t i = 0u; i < bytes_len; ++i) {
    output[i * 2u] = hex[(bytes[i] >> 4) & 0x0Fu];
    output[i * 2u + 1u] = hex[bytes[i] & 0x0Fu];
  }
  output[bytes_len * 2u] = '\0';
}

} // namespace

hal_status_t hal_sha256_init_ex(hal_sha256_context_t *context) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  jh_sha256_init(context);
  return HAL_OK;
}

hal_status_t hal_sha256_update_ex(hal_sha256_context_t *context,
                                  const uint8_t *input, size_t input_len) {
  if (context == nullptr || !context->initialized ||
      (input == nullptr && input_len > 0u)) {
    return HAL_EINVAL;
  }
  jh_sha256_update(context, input, input_len);
  return HAL_OK;
}

hal_status_t hal_sha256_final_ex(hal_sha256_context_t *context,
                                 uint8_t out_digest[HAL_SHA256_DIGEST_BYTES]) {
  if (context == nullptr || !context->initialized || out_digest == nullptr) {
    return HAL_EINVAL;
  }
  jh_sha256_final(context, out_digest);
  return HAL_OK;
}

bool hal_sha256(const uint8_t *input, size_t input_len,
                uint8_t out_digest[HAL_SHA256_DIGEST_BYTES]) {
  hal_sha256_context_t ctx;
  return hal_sha256_init_ex(&ctx) == HAL_OK &&
         hal_sha256_update_ex(&ctx, input, input_len) == HAL_OK &&
         hal_sha256_final_ex(&ctx, out_digest) == HAL_OK;
}

bool hal_sha256_hex(const uint8_t *input, size_t input_len, char *output,
                    size_t out_size) {
  if (output == nullptr || out_size < HAL_SHA256_HEX_BUF_SIZE) {
    if (output != nullptr && out_size > 0u) {
      output[0] = '\0';
    }
    return false;
  }
  uint8_t digest[HAL_SHA256_DIGEST_BYTES];
  if (!hal_sha256(input, input_len, digest)) {
    output[0] = '\0';
    return false;
  }
  bytes_to_hex_lower(digest, sizeof(digest), output, out_size);
  return true;
}

bool hal_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *message,
                     size_t message_len,
                     uint8_t out_mac[HAL_SHA256_DIGEST_BYTES]) {
  if (out_mac == nullptr) {
    return false;
  }
  if (key == nullptr && key_len > 0u) {
    return false;
  }
  if (message == nullptr && message_len > 0u) {
    return false;
  }

  uint8_t key_block[HAL_HMAC_SHA256_BLOCK_BYTES];
  memset(key_block, 0, sizeof(key_block));

  if (key_len > HAL_HMAC_SHA256_BLOCK_BYTES) {
    if (!hal_sha256(key, key_len, key_block)) {
      return false;
    }
  } else if (key_len > 0u) {
    memcpy(key_block, key, key_len);
  }

  uint8_t inner_pad[HAL_HMAC_SHA256_BLOCK_BYTES];
  uint8_t outer_pad[HAL_HMAC_SHA256_BLOCK_BYTES];
  for (size_t i = 0u; i < HAL_HMAC_SHA256_BLOCK_BYTES; ++i) {
    inner_pad[i] = key_block[i] ^ 0x36u;
    outer_pad[i] = key_block[i] ^ 0x5cu;
  }

  hal_sha256_context_t inner_ctx;
  jh_sha256_init(&inner_ctx);
  jh_sha256_update(&inner_ctx, inner_pad, sizeof(inner_pad));
  jh_sha256_update(&inner_ctx, message, message_len);
  uint8_t inner_digest[HAL_SHA256_DIGEST_BYTES];
  jh_sha256_final(&inner_ctx, inner_digest);

  hal_sha256_context_t outer_ctx;
  jh_sha256_init(&outer_ctx);
  jh_sha256_update(&outer_ctx, outer_pad, sizeof(outer_pad));
  jh_sha256_update(&outer_ctx, inner_digest, sizeof(inner_digest));
  jh_sha256_final(&outer_ctx, out_mac);
  return true;
}

bool hal_hmac_sha256_hex(const uint8_t *key, size_t key_len,
                         const uint8_t *message, size_t message_len,
                         char *output, size_t out_size) {
  if (output == nullptr || out_size < HAL_SHA256_HEX_BUF_SIZE) {
    if (output != nullptr && out_size > 0u) {
      output[0] = '\0';
    }
    return false;
  }
  uint8_t mac[HAL_SHA256_DIGEST_BYTES];
  if (!hal_hmac_sha256(key, key_len, message, message_len, mac)) {
    output[0] = '\0';
    return false;
  }
  bytes_to_hex_lower(mac, sizeof(mac), output, out_size);
  return true;
}

#endif /* HAL_ENABLE_CRYPTO */
