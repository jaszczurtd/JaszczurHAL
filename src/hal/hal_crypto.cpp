#include "hal_crypto.h"

#ifdef HAL_ENABLE_CRYPTO

#include <limits.h>
#include <string.h>

extern "C" {
#include "hal/impl/shared/wireguard/crypto/chacha20.h"
#include "hal/impl/shared/wireguard/crypto/chacha20poly1305.h"
#include "hal/impl/shared/wireguard/crypto/crypto.h"
}

namespace {

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int)(c - 'a') + 26;
    if (c >= '0' && c <= '9') return (int)(c - '0') + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static inline bool can_append(size_t index, size_t count, size_t capacity) {
    return (index <= capacity) && (count <= (capacity - index));
}

static const uint32_t kMd5K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

static const uint8_t kMd5S[64] = {
    7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u,
    5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u,
    4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u,
    6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u
};

static inline uint32_t rotl32(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32u - n));
}

static const uint8_t kZeroChaChaBlock[HAL_CHACHA20_BLOCK_BYTES] = {0};

static bool chacha20_counter_will_overflow(uint32_t counter, size_t input_len) {
    uint64_t blocks_needed = (uint64_t)(input_len / HAL_CHACHA20_BLOCK_BYTES)
                           + ((input_len % HAL_CHACHA20_BLOCK_BYTES) ? 1u : 0u);
    return ((uint64_t)counter + blocks_needed) > 0x100000000ULL;
}

static void chacha20_xor_stream_ietf(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                                     uint32_t counter,
                                     const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                                     const uint8_t *input,
                                     size_t input_len,
                                     uint8_t *output) {
    struct chacha20_ctx ctx;
    chacha20_init_ietf(&ctx, key, counter, nonce);

    size_t offset = 0u;
    while (offset < input_len) {
        size_t remaining = input_len - offset;
        uint32_t chunk = (remaining > (size_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)remaining;
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

    chacha20_xor_stream_ietf(key,
                             counter,
                             nonce,
                             kZeroChaChaBlock,
                             HAL_CHACHA20_BLOCK_BYTES,
                             out_block);
    return true;
}

bool hal_chacha20_xor(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                      uint32_t counter,
                      const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                      const uint8_t *input,
                      size_t input_len,
                      uint8_t *output) {
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
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *plaintext,
    size_t text_len,
    uint8_t *ciphertext,
    uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES]) {
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

    return chacha20poly1305_encrypt_ietf_detached(ciphertext,
                                                  tag,
                                                  plaintext,
                                                  text_len,
                                                  aad,
                                                  aad_len,
                                                  nonce,
                                                  key);
}

bool hal_chacha20_poly1305_decrypt(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES],
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *ciphertext,
    size_t text_len,
    const uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES],
    uint8_t *plaintext) {
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

    return chacha20poly1305_decrypt_ietf_detached(plaintext,
                                                  ciphertext,
                                                  text_len,
                                                  tag,
                                                  aad,
                                                  aad_len,
                                                  nonce,
                                                  key);
}

bool hal_md5(const uint8_t *input,
             size_t input_len,
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

    uint32_t state[4] = {
        0x67452301u,
        0xefcdab89u,
        0x98badcfeu,
        0x10325476u
    };

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

bool hal_md5_hex(const uint8_t *input,
                 size_t input_len,
                 char *output,
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

    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0u; i < HAL_MD5_DIGEST_BYTES; ++i) {
        output[i * 2u] = kHex[digest[i] >> 4];
        output[i * 2u + 1u] = kHex[digest[i] & 0x0Fu];
    }
    output[HAL_MD5_HEX_BUF_SIZE - 1u] = '\0';
    return true;
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

bool hal_base64_encode(const uint8_t *input,
                       size_t input_len,
                       char *output,
                       size_t out_size,
                       size_t *out_len) {
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
        uint32_t triple =
            ((uint32_t)input[in_i] << 16) |
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
            ((uint32_t)input[in_i] << 16) |
            ((uint32_t)input[in_i + 1u] << 8);
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

bool hal_base64_decode(const char *input,
                       size_t input_len,
                       uint8_t *output,
                       size_t out_size,
                       size_t *out_len) {
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

constexpr size_t kSha256BlockBytes = 64u;

struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[kSha256BlockBytes];
    size_t buffer_len;
};

constexpr uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

inline uint32_t rotr32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

inline void sha256_init(Sha256Ctx &ctx) {
    ctx.state[0] = 0x6a09e667u;
    ctx.state[1] = 0xbb67ae85u;
    ctx.state[2] = 0x3c6ef372u;
    ctx.state[3] = 0xa54ff53au;
    ctx.state[4] = 0x510e527fu;
    ctx.state[5] = 0x9b05688cu;
    ctx.state[6] = 0x1f83d9abu;
    ctx.state[7] = 0x5be0cd19u;
    ctx.bit_count = 0u;
    ctx.buffer_len = 0u;
}

inline void sha256_compress(Sha256Ctx &ctx, const uint8_t block[kSha256BlockBytes]) {
    uint32_t w[64];
    for (size_t i = 0u; i < 16u; ++i) {
        const size_t base = i * 4u;
        w[i] = ((uint32_t)block[base] << 24) |
               ((uint32_t)block[base + 1u] << 16) |
               ((uint32_t)block[base + 2u] << 8) |
               (uint32_t)block[base + 3u];
    }
    for (size_t i = 16u; i < 64u; ++i) {
        const uint32_t s0 = rotr32(w[i - 15u], 7) ^ rotr32(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        const uint32_t s1 = rotr32(w[i - 2u], 17) ^ rotr32(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    uint32_t a = ctx.state[0];
    uint32_t b = ctx.state[1];
    uint32_t c = ctx.state[2];
    uint32_t d = ctx.state[3];
    uint32_t e = ctx.state[4];
    uint32_t f = ctx.state[5];
    uint32_t g = ctx.state[6];
    uint32_t h = ctx.state[7];

    for (size_t i = 0u; i < 64u; ++i) {
        const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
        const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
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

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

inline void sha256_update(Sha256Ctx &ctx, const uint8_t *data, size_t len) {
    if (len == 0u) {
        return;
    }
    ctx.bit_count += (uint64_t)len * 8u;

    if (ctx.buffer_len > 0u) {
        const size_t needed = kSha256BlockBytes - ctx.buffer_len;
        const size_t take = (len < needed) ? len : needed;
        memcpy(&ctx.buffer[ctx.buffer_len], data, take);
        ctx.buffer_len += take;
        data += take;
        len -= take;
        if (ctx.buffer_len == kSha256BlockBytes) {
            sha256_compress(ctx, ctx.buffer);
            ctx.buffer_len = 0u;
        }
    }

    while (len >= kSha256BlockBytes) {
        sha256_compress(ctx, data);
        data += kSha256BlockBytes;
        len -= kSha256BlockBytes;
    }

    if (len > 0u) {
        memcpy(ctx.buffer, data, len);
        ctx.buffer_len = len;
    }
}

inline void sha256_final(Sha256Ctx &ctx, uint8_t out_digest[HAL_SHA256_DIGEST_BYTES]) {
    const uint64_t bit_count = ctx.bit_count;
    const uint8_t pad = 0x80u;
    sha256_update(ctx, &pad, 1u);

    const uint8_t zero = 0x00u;
    while (ctx.buffer_len != (kSha256BlockBytes - 8u)) {
        sha256_update(ctx, &zero, 1u);
    }

    uint8_t length_be[8];
    for (size_t i = 0u; i < 8u; ++i) {
        length_be[i] = (uint8_t)(bit_count >> (56u - i * 8u));
    }
    sha256_update(ctx, length_be, sizeof(length_be));

    for (size_t i = 0u; i < 8u; ++i) {
        const uint32_t v = ctx.state[i];
        out_digest[i * 4u] = (uint8_t)(v >> 24);
        out_digest[i * 4u + 1u] = (uint8_t)(v >> 16);
        out_digest[i * 4u + 2u] = (uint8_t)(v >> 8);
        out_digest[i * 4u + 3u] = (uint8_t)v;
    }
}

inline void bytes_to_hex_lower(const uint8_t *bytes,
                               size_t bytes_len,
                               char *output,
                               size_t out_size) {
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

}  // namespace

bool hal_sha256(const uint8_t *input,
                size_t input_len,
                uint8_t out_digest[HAL_SHA256_DIGEST_BYTES]) {
    if (out_digest == nullptr) {
        return false;
    }
    if (input == nullptr && input_len > 0u) {
        return false;
    }
    Sha256Ctx ctx;
    sha256_init(ctx);
    sha256_update(ctx, input, input_len);
    sha256_final(ctx, out_digest);
    return true;
}

bool hal_sha256_hex(const uint8_t *input,
                    size_t input_len,
                    char *output,
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

bool hal_hmac_sha256(const uint8_t *key,
                     size_t key_len,
                     const uint8_t *message,
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

    Sha256Ctx inner_ctx;
    sha256_init(inner_ctx);
    sha256_update(inner_ctx, inner_pad, sizeof(inner_pad));
    sha256_update(inner_ctx, message, message_len);
    uint8_t inner_digest[HAL_SHA256_DIGEST_BYTES];
    sha256_final(inner_ctx, inner_digest);

    Sha256Ctx outer_ctx;
    sha256_init(outer_ctx);
    sha256_update(outer_ctx, outer_pad, sizeof(outer_pad));
    sha256_update(outer_ctx, inner_digest, sizeof(inner_digest));
    sha256_final(outer_ctx, out_mac);
    return true;
}

bool hal_hmac_sha256_hex(const uint8_t *key,
                         size_t key_len,
                         const uint8_t *message,
                         size_t message_len,
                         char *output,
                         size_t out_size) {
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
