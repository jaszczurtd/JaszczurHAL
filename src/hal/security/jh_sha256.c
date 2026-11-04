#include "hal/security/jh_sha256.h"

#include "hal/core/jh_endian.h"

#include <string.h>

static const uint32_t jh_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t jh_sha256_rotr(uint32_t x, unsigned n) {
  return (x >> n) | (x << (32u - n));
}

static void jh_sha256_compress(jh_sha256_context_t *context,
                               const uint8_t block[JH_SHA256_BLOCK_BYTES]) {
  uint32_t w[64];
  for (size_t i = 0u; i < 16u; ++i) {
    w[i] = jh_load_be32(&block[i * 4u]);
  }
  for (size_t i = 16u; i < 64u; ++i) {
    const uint32_t s0 = jh_sha256_rotr(w[i - 15u], 7) ^
                        jh_sha256_rotr(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
    const uint32_t s1 = jh_sha256_rotr(w[i - 2u], 17) ^
                        jh_sha256_rotr(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  uint32_t a = context->state[0];
  uint32_t b = context->state[1];
  uint32_t c = context->state[2];
  uint32_t d = context->state[3];
  uint32_t e = context->state[4];
  uint32_t f = context->state[5];
  uint32_t g = context->state[6];
  uint32_t h = context->state[7];

  for (size_t i = 0u; i < 64u; ++i) {
    const uint32_t S1 =
        jh_sha256_rotr(e, 6) ^ jh_sha256_rotr(e, 11) ^ jh_sha256_rotr(e, 25);
    const uint32_t ch = (e & f) ^ ((~e) & g);
    const uint32_t temp1 = h + S1 + ch + jh_sha256_k[i] + w[i];
    const uint32_t S0 =
        jh_sha256_rotr(a, 2) ^ jh_sha256_rotr(a, 13) ^ jh_sha256_rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = S0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  context->state[0] += a;
  context->state[1] += b;
  context->state[2] += c;
  context->state[3] += d;
  context->state[4] += e;
  context->state[5] += f;
  context->state[6] += g;
  context->state[7] += h;
}

void jh_sha256_init(jh_sha256_context_t *context) {
  context->state[0] = 0x6a09e667u;
  context->state[1] = 0xbb67ae85u;
  context->state[2] = 0x3c6ef372u;
  context->state[3] = 0xa54ff53au;
  context->state[4] = 0x510e527fu;
  context->state[5] = 0x9b05688cu;
  context->state[6] = 0x1f83d9abu;
  context->state[7] = 0x5be0cd19u;
  context->bit_count = 0u;
  context->buffer_len = 0u;
  context->initialized = true;
}

void jh_sha256_update(jh_sha256_context_t *context, const uint8_t *data,
                      size_t length) {
  if (length == 0u) {
    return;
  }
  context->bit_count += (uint64_t)length * 8u;

  if (context->buffer_len > 0u) {
    const size_t needed = JH_SHA256_BLOCK_BYTES - context->buffer_len;
    const size_t take = (length < needed) ? length : needed;
    memcpy(&context->buffer[context->buffer_len], data, take);
    context->buffer_len += take;
    data += take;
    length -= take;
    if (context->buffer_len == JH_SHA256_BLOCK_BYTES) {
      jh_sha256_compress(context, context->buffer);
      context->buffer_len = 0u;
    }
  }

  while (length >= JH_SHA256_BLOCK_BYTES) {
    jh_sha256_compress(context, data);
    data += JH_SHA256_BLOCK_BYTES;
    length -= JH_SHA256_BLOCK_BYTES;
  }

  if (length > 0u) {
    memcpy(context->buffer, data, length);
    context->buffer_len = length;
  }
}

void jh_sha256_final(jh_sha256_context_t *context,
                     uint8_t digest[JH_SHA256_DIGEST_BYTES]) {
  const uint64_t bit_count = context->bit_count;
  const uint8_t pad = 0x80u;
  jh_sha256_update(context, &pad, 1u);

  const uint8_t zero = 0x00u;
  while (context->buffer_len != (JH_SHA256_BLOCK_BYTES - 8u)) {
    jh_sha256_update(context, &zero, 1u);
  }

  uint8_t length_be[8];
  jh_store_be64(length_be, bit_count);
  jh_sha256_update(context, length_be, sizeof(length_be));

  for (size_t i = 0u; i < 8u; ++i) {
    jh_store_be32(&digest[i * 4u], context->state[i]);
  }
  context->initialized = false;
}
