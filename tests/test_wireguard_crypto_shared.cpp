#include "utils/unity.h"

#include <stdint.h>
#include <string.h>

extern "C" {
#include "chacha20.h"
#include "chacha20poly1305.h"
#include "crypto.h"
#include "wireguard_replay.h"
}

void setUp(void) {}
void tearDown(void) {}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
  if (hex == NULL || out == NULL) {
    return false;
  }
  if (strlen(hex) != out_len * 2u) {
    return false;
  }

  for (size_t i = 0u; i < out_len; ++i) {
    const int hi = hex_nibble(hex[i * 2u]);
    const int lo = hex_nibble(hex[i * 2u + 1u]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

void test_chacha20_init_ietf_matches_rfc8439_block_vector(void) {
  struct chacha20_ctx ctx;
  uint8_t key[32];
  uint8_t nonce[12];
  uint8_t zero_block[64] = {0};
  uint8_t block[64];
  uint8_t expected[64];

  for (size_t i = 0u; i < sizeof(key); ++i) {
    key[i] = (uint8_t)i;
  }

  TEST_ASSERT_TRUE(
      hex_to_bytes("000000090000004a00000000", nonce, sizeof(nonce)));
  TEST_ASSERT_TRUE(hex_to_bytes("10f1e7e4d13b5915500fdd1fa32071c4"
                                "c7d1f4c733c068030422aa9ac3d46c4e"
                                "d2826446079faa0914c2d705d98b02a2"
                                "b5129cd1de164eb9cbd083e8a2503c4e",
                                expected, sizeof(expected)));

  chacha20_init_ietf(&ctx, key, 1u, nonce);
  chacha20(&ctx, block, zero_block, (uint32_t)sizeof(block));

  TEST_ASSERT_EQUAL_INT(0, memcmp(expected, block, sizeof(block)));
}

void test_crypto_zero_and_equal_behave_as_expected(void) {
  uint8_t secret[16];
  for (size_t i = 0u; i < sizeof(secret); ++i) {
    secret[i] = (uint8_t)(0xA0u + i);
  }

  crypto_zero(secret, sizeof(secret));
  for (size_t i = 0u; i < sizeof(secret); ++i) {
    TEST_ASSERT_EQUAL_UINT8(0u, secret[i]);
  }

  uint8_t a[8] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
  uint8_t b[8];
  memcpy(b, a, sizeof(a));

  TEST_ASSERT_TRUE(crypto_equal(a, b, sizeof(a)));

  b[3] ^= 0x55u;
  TEST_ASSERT_FALSE(crypto_equal(a, b, sizeof(a)));

  TEST_ASSERT_TRUE(crypto_equal(a, b, 0u));
}

void test_blake2s_matches_known_vector_for_abc(void) {
  static const char msg[] = "abc";
  uint8_t digest[32];
  uint8_t expected[32];

  TEST_ASSERT_TRUE(hex_to_bytes("508c5e8c327c14e2e1a72ba34eeb452f"
                                "37458b209ed63a294d999b4c86675982",
                                expected, sizeof(expected)));

  TEST_ASSERT_EQUAL_INT(
      0, blake2s(digest, sizeof(digest), NULL, 0u, msg, sizeof(msg) - 1u));
  TEST_ASSERT_EQUAL_INT(0, memcmp(expected, digest, sizeof(digest)));
}

void test_x25519_matches_rfc7748_vectors(void) {
  uint8_t alice_private[32];
  uint8_t alice_public_expected[32];
  uint8_t bob_private[32];
  uint8_t bob_public_expected[32];
  uint8_t shared_expected[32];

  uint8_t alice_public[32];
  uint8_t bob_public[32];
  uint8_t shared_ab[32];
  uint8_t shared_ba[32];

  TEST_ASSERT_TRUE(hex_to_bytes("77076d0a7318a57d3c16c17251b26645"
                                "df4c2f87ebc0992ab177fba51db92c2a",
                                alice_private, sizeof(alice_private)));
  TEST_ASSERT_TRUE(hex_to_bytes("8520f0098930a754748b7ddcb43ef75a"
                                "0dbf3a0d26381af4eba4a98eaa9b4e6a",
                                alice_public_expected,
                                sizeof(alice_public_expected)));
  TEST_ASSERT_TRUE(hex_to_bytes("5dab087e624a8a4b79e17f8b83800ee6"
                                "6f3bb1292618b6fd1c2f8b27ff88e0eb",
                                bob_private, sizeof(bob_private)));
  TEST_ASSERT_TRUE(hex_to_bytes("de9edb7d7b7dc1b4d35b61c2ece43537"
                                "3f8343c85b78674dadfc7e146f882b4f",
                                bob_public_expected,
                                sizeof(bob_public_expected)));
  TEST_ASSERT_TRUE(hex_to_bytes("4a5d9d5ba4ce2de1728e3bf480350f25"
                                "e07e21c947d19e3376f09b3c1e161742",
                                shared_expected, sizeof(shared_expected)));

  TEST_ASSERT_EQUAL_INT(
      0, x25519(alice_public, alice_private, X25519_BASE_POINT, 1));
  TEST_ASSERT_EQUAL_INT(0,
                        x25519(bob_public, bob_private, X25519_BASE_POINT, 1));
  TEST_ASSERT_EQUAL_INT(
      0, memcmp(alice_public_expected, alice_public, sizeof(alice_public)));
  TEST_ASSERT_EQUAL_INT(
      0, memcmp(bob_public_expected, bob_public, sizeof(bob_public)));

  TEST_ASSERT_EQUAL_INT(0, x25519(shared_ab, alice_private, bob_public, 1));
  TEST_ASSERT_EQUAL_INT(0, x25519(shared_ba, bob_private, alice_public, 1));
  TEST_ASSERT_EQUAL_INT(0,
                        memcmp(shared_expected, shared_ab, sizeof(shared_ab)));
  TEST_ASSERT_EQUAL_INT(0,
                        memcmp(shared_expected, shared_ba, sizeof(shared_ba)));
}

void test_chacha20poly1305_roundtrip_and_tag_validation(void) {
  uint8_t key[32];
  uint8_t aad[11];
  uint8_t plain[48];
  uint8_t cipher[sizeof(plain) + 16u];
  uint8_t decrypted[sizeof(plain)];
  uint8_t tampered[sizeof(cipher)];
  uint8_t unchanged[sizeof(decrypted)];

  for (size_t i = 0u; i < sizeof(key); ++i) {
    key[i] = (uint8_t)(0x11u + i);
  }
  for (size_t i = 0u; i < sizeof(aad); ++i) {
    aad[i] = (uint8_t)(0x70u + i);
  }
  for (size_t i = 0u; i < sizeof(plain); ++i) {
    plain[i] = (uint8_t)(i ^ 0x5Au);
  }

  chacha20poly1305_encrypt(cipher, plain, sizeof(plain), aad, sizeof(aad),
                           0x0123456789ABCDEFULL, key);

  TEST_ASSERT_TRUE(chacha20poly1305_decrypt(decrypted, cipher, sizeof(cipher),
                                            aad, sizeof(aad),
                                            0x0123456789ABCDEFULL, key));
  TEST_ASSERT_EQUAL_INT(0, memcmp(plain, decrypted, sizeof(plain)));

  memcpy(tampered, cipher, sizeof(cipher));
  tampered[sizeof(tampered) - 1u] ^= 0x01u;
  memset(decrypted, 0xCC, sizeof(decrypted));
  memcpy(unchanged, decrypted, sizeof(decrypted));

  TEST_ASSERT_FALSE(chacha20poly1305_decrypt(decrypted, tampered,
                                             sizeof(tampered), aad, sizeof(aad),
                                             0x0123456789ABCDEFULL, key));
  TEST_ASSERT_EQUAL_INT(0, memcmp(unchanged, decrypted, sizeof(decrypted)));
}

void test_chacha20poly1305_ietf_detached_matches_rfc8439_vector(void) {
  uint8_t key[32];
  uint8_t nonce[12];
  uint8_t aad[12];
  uint8_t plaintext[265];
  uint8_t ciphertext[265];
  uint8_t expected_ciphertext[265];
  uint8_t tag[16];
  uint8_t expected_tag[16];
  uint8_t decrypted[265];
  uint8_t bad_tag[16];
  uint8_t untouched[265];

  TEST_ASSERT_TRUE(hex_to_bytes("1c9240a5eb55d38af333888604f6b5f0"
                                "473917c1402b80099dca5cbc207075c0",
                                key, sizeof(key)));
  TEST_ASSERT_TRUE(
      hex_to_bytes("000000000102030405060708", nonce, sizeof(nonce)));
  TEST_ASSERT_TRUE(hex_to_bytes("f33388860000000000004e91", aad, sizeof(aad)));
  TEST_ASSERT_TRUE(hex_to_bytes("496e7465726e65742d44726166747320"
                                "61726520647261667420646f63756d65"
                                "6e74732076616c696420666f72206120"
                                "6d6178696d756d206f6620736978206d"
                                "6f6e74687320616e64206d6179206265"
                                "20757064617465642c207265706c6163"
                                "65642c206f72206f62736f6c65746564"
                                "206279206f7468657220646f63756d65"
                                "6e747320617420616e792074696d652e"
                                "20497420697320696e617070726f7072"
                                "6961746520746f2075736520496e7465"
                                "726e65742d4472616674732061732072"
                                "65666572656e6365206d617465726961"
                                "6c206f7220746f206369746520746865"
                                "6d206f74686572207468616e20617320"
                                "2fe2809c776f726b20696e2070726f67"
                                "726573732e2fe2809d",
                                plaintext, sizeof(plaintext)));
  TEST_ASSERT_TRUE(hex_to_bytes("64a0861575861af460f062c79be643bd"
                                "5e805cfd345cf389f108670ac76c8cb2"
                                "4c6cfc18755d43eea09ee94e382d26b0"
                                "bdb7b73c321b0100d4f03b7f355894cf"
                                "332f830e710b97ce98c8a84abd0b9481"
                                "14ad176e008d33bd60f982b1ff37c855"
                                "9797a06ef4f0ef61c186324e2b350638"
                                "3606907b6a7c02b0f9f6157b53c867e4"
                                "b9166c767b804d46a59b5216cde7a4e9"
                                "9040c5a40433225ee282a1b0a06c523e"
                                "af4534d7f83fa1155b0047718cbc546a"
                                "0d072b04b3564eea1b422273f548271a"
                                "0bb2316053fa76991955ebd63159434e"
                                "cebb4e466dae5a1073a6727627097a10"
                                "49e617d91d361094fa68f0ff77987130"
                                "305beaba2eda04df997b714d6c6f2c29"
                                "a6ad5cb4022b02709b",
                                expected_ciphertext,
                                sizeof(expected_ciphertext)));
  TEST_ASSERT_TRUE(hex_to_bytes("eead9d67890cbb22392336fea1851f38",
                                expected_tag, sizeof(expected_tag)));

  TEST_ASSERT_TRUE(chacha20poly1305_encrypt_ietf_detached(
      ciphertext, tag, plaintext, sizeof(plaintext), aad, sizeof(aad), nonce,
      key));
  TEST_ASSERT_EQUAL_INT(
      0, memcmp(expected_ciphertext, ciphertext, sizeof(ciphertext)));
  TEST_ASSERT_EQUAL_INT(0, memcmp(expected_tag, tag, sizeof(tag)));

  TEST_ASSERT_TRUE(chacha20poly1305_decrypt_ietf_detached(
      decrypted, ciphertext, sizeof(ciphertext), tag, aad, sizeof(aad), nonce,
      key));
  TEST_ASSERT_EQUAL_INT(0, memcmp(plaintext, decrypted, sizeof(plaintext)));

  memcpy(bad_tag, tag, sizeof(bad_tag));
  bad_tag[0] ^= 0x01u;
  memset(decrypted, 0xA5, sizeof(decrypted));
  memcpy(untouched, decrypted, sizeof(untouched));

  TEST_ASSERT_FALSE(chacha20poly1305_decrypt_ietf_detached(
      decrypted, ciphertext, sizeof(ciphertext), bad_tag, aad, sizeof(aad),
      nonce, key));
  TEST_ASSERT_EQUAL_INT(0, memcmp(untouched, decrypted, sizeof(decrypted)));
}

void test_chacha20poly1305_ietf_detached_rejects_invalid_arguments(void) {
  uint8_t key[32] = {0};
  uint8_t nonce[12] = {0};
  uint8_t aad[1] = {0xABu};
  uint8_t plain[1] = {0xCDu};
  uint8_t cipher[1] = {0};
  uint8_t out[1] = {0};
  uint8_t tag[16] = {0};

  TEST_ASSERT_FALSE(chacha20poly1305_encrypt_ietf_detached(
      NULL, tag, plain, 1u, aad, 1u, nonce, key));
  TEST_ASSERT_FALSE(chacha20poly1305_encrypt_ietf_detached(
      cipher, tag, NULL, 1u, aad, 1u, nonce, key));
  TEST_ASSERT_FALSE(chacha20poly1305_encrypt_ietf_detached(
      cipher, tag, plain, 1u, NULL, 1u, nonce, key));
  TEST_ASSERT_FALSE(chacha20poly1305_encrypt_ietf_detached(
      cipher, NULL, plain, 1u, aad, 1u, nonce, key));
  TEST_ASSERT_FALSE(chacha20poly1305_encrypt_ietf_detached(
      cipher, tag, plain, 1u, aad, 1u, NULL, key));
  TEST_ASSERT_FALSE(chacha20poly1305_encrypt_ietf_detached(
      cipher, tag, plain, 1u, aad, 1u, nonce, NULL));

  TEST_ASSERT_FALSE(chacha20poly1305_decrypt_ietf_detached(
      NULL, cipher, 1u, tag, aad, 1u, nonce, key));
  TEST_ASSERT_FALSE(chacha20poly1305_decrypt_ietf_detached(
      out, NULL, 1u, tag, aad, 1u, nonce, key));
  TEST_ASSERT_FALSE(chacha20poly1305_decrypt_ietf_detached(
      out, cipher, 1u, NULL, aad, 1u, nonce, key));
  TEST_ASSERT_FALSE(chacha20poly1305_decrypt_ietf_detached(
      out, cipher, 1u, tag, NULL, 1u, nonce, key));
  TEST_ASSERT_FALSE(chacha20poly1305_decrypt_ietf_detached(out, cipher, 1u, tag,
                                                           aad, 1u, NULL, key));
  TEST_ASSERT_FALSE(chacha20poly1305_decrypt_ietf_detached(
      out, cipher, 1u, tag, aad, 1u, nonce, NULL));

  TEST_ASSERT_TRUE(chacha20poly1305_encrypt_ietf_detached(
      NULL, tag, NULL, 0u, aad, sizeof(aad), nonce, key));
  TEST_ASSERT_TRUE(chacha20poly1305_decrypt_ietf_detached(
      NULL, NULL, 0u, tag, aad, sizeof(aad), nonce, key));
}

void test_wireguard_replay_window_accepts_zero_once_and_tracks_32_bits(void) {
  uint32_t bitmap = 0u;
  uint64_t counter = 0u;

  TEST_ASSERT_TRUE(wireguard_replay_check(&bitmap, &counter, 0u));
  TEST_ASSERT_FALSE(wireguard_replay_check(&bitmap, &counter, 0u));
  TEST_ASSERT_TRUE(wireguard_replay_check(&bitmap, &counter, 32u));
  TEST_ASSERT_TRUE(wireguard_replay_check(&bitmap, &counter, 1u));
  TEST_ASSERT_FALSE(wireguard_replay_check(&bitmap, &counter, 1u));
  TEST_ASSERT_FALSE(wireguard_replay_check(&bitmap, &counter, 0u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_crypto_zero_and_equal_behave_as_expected);
  RUN_TEST(test_blake2s_matches_known_vector_for_abc);
  RUN_TEST(test_x25519_matches_rfc7748_vectors);
  RUN_TEST(test_chacha20_init_ietf_matches_rfc8439_block_vector);
  RUN_TEST(test_chacha20poly1305_roundtrip_and_tag_validation);
  RUN_TEST(test_chacha20poly1305_ietf_detached_matches_rfc8439_vector);
  RUN_TEST(test_chacha20poly1305_ietf_detached_rejects_invalid_arguments);
  RUN_TEST(test_wireguard_replay_window_accepts_zero_once_and_tracks_32_bits);
  return UNITY_END();
}
