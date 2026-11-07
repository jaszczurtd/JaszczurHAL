#include "hal/core/hal_array.h"
#include "hal/core/jh_endian.h"
#include "support/crypto_test_vectors.h"
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
  chacha20(&ctx, block, zero_block, sizeof(block));

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

  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_key_hex, key, sizeof(key)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_nonce_hex, nonce, sizeof(nonce)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_aad_hex, aad, sizeof(aad)));
  TEST_ASSERT_TRUE(
      hex_to_bytes(k_rfc8439_plaintext_hex, plaintext, sizeof(plaintext)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_ciphertext_hex, expected_ciphertext,
                                sizeof(expected_ciphertext)));
  TEST_ASSERT_TRUE(
      hex_to_bytes(k_rfc8439_tag_hex, expected_tag, sizeof(expected_tag)));

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

void test_chacha20poly1305_matches_independent_aligned_vectors(void) {
  // BearSSL aca13833b6f9ddffaea2041a01facc76829dc03b, independently
  // checked with OpenSSL 3.0.13 ChaCha20-Poly1305. No HAL code generated these.
  static const struct {
    size_t aad_len;
    size_t text_len;
    const char *tag_hex;
  } vectors[] = {
      {0u, 0u, "a7578beb65f297eeacc41054f27f4ba3"},
      {0u, 16u, "47839c175c04e45bb05126c6926a48e5"},
      {0u, 32u, "887f14da2559c446a0c9d6a4704f97f9"},
      {0u, 64u, "f8f2d40f06b955a78679bfd6200fd6c2"},
      {0u, 128u, "d39436892fe1dd16803d13b5a86413e7"},
      {16u, 0u, "4a12e7c7ffa31f633e95fe2a8ba6ddd2"},
      {16u, 16u, "6e85d8e44c4e1d9ddbefe1f1c1540af8"},
      {16u, 32u, "d1d4975c0b8560b6cb4bbd6ba0dcd588"},
      {16u, 64u, "a36bde39a28b9e286cdfdb6b822c2cff"},
      {16u, 128u, "28a2e755b80371aa791e7d446d4d95d0"},
      {32u, 0u, "8110b6a03a6e36bb950bb83ad1ab868e"},
      {32u, 16u, "6f4b2dd59f92ca895920b4ece4c4536d"},
      {32u, 32u, "ffce2e379a45a029cef0636ed4e63b60"},
      {32u, 64u, "e5995fbdc0da18ce3e0c6aad9a3dac7b"},
      {32u, 128u, "0845609290b0b404764e3232355f6e7c"},
      {64u, 0u, "7e3999f01d8cefc5c03f1834493f0f13"},
      {64u, 16u, "9ae30ba4090fe6067d0be4df56f12d6d"},
      {64u, 32u, "abfdefc11cd8446db38a392c3a932259"},
      {64u, 64u, "fa03f0a8593a1fad6403038ee4c7b39d"},
      {64u, 128u, "19c85fabc07e5d4929f8c206ba7a1a99"},
      {128u, 0u, "d5406c9367c64c160a8abf9a6b0bb6ba"},
      {128u, 16u, "17fb58fbf0ca7dba599c270fae22aebb"},
      {128u, 32u, "d7ce0dd214d9d1499ba60f5854d093e2"},
      {128u, 64u, "2e7e38d32a1d83a4c6546c918bc8a4cc"},
      {128u, 128u, "bdbb11374276ad207f7837c037f7a0e8"},
  };
  static const char ciphertext_hex[] =
      "8225e919eed4fc68c7f2831f655a5462892bbdc6805d61ec3432009cb7d540e6"
      "384705fb1edbd604a9312a792d9eca51533889a29fb6e17f5c0fef0d63f4ef62"
      "97fd43f77c31ecc7c89cd406fcc91fef7b7a87c9206d1fdb4cd26d2b2d1bb76e"
      "8510555c110883f49e168ab311bb7816f5666aaed707cd935bfd543a938926a2";
  const uint8_t key[32] = {0};
  const uint64_t nonce_value = UINT64_C(0x0123456789abcdef);
  uint8_t nonce[12] = {0};
  jh_store_le64(nonce + 4u, nonce_value);
  uint8_t plain[128];
  uint8_t aad[128];
  uint8_t expected_ciphertext[sizeof(plain)];
  TEST_ASSERT_TRUE(hex_to_bytes(ciphertext_hex, expected_ciphertext,
                                sizeof(expected_ciphertext)));
  for (size_t i = 0; i < sizeof(plain); ++i) {
    plain[i] = (uint8_t)i;
  }
  for (size_t i = 0; i < sizeof(aad); ++i) {
    aad[i] = (uint8_t)(0xA0u + i);
  }
  for (size_t i = 0; i < COUNTOF(vectors); ++i) {
    const size_t len = vectors[i].text_len;
    const size_t ad_len = vectors[i].aad_len;
    uint8_t expected[sizeof(plain) + 16u];
    uint8_t legacy[sizeof(plain) + 16u];
    uint8_t ietf[sizeof(plain)];
    uint8_t tag[16];
    uint8_t decoded[sizeof(plain)];
    memcpy(expected, expected_ciphertext, len);
    TEST_ASSERT_TRUE(
        hex_to_bytes(vectors[i].tag_hex, expected + len, sizeof(tag)));
    chacha20poly1305_encrypt(legacy, plain, len, aad, ad_len, nonce_value, key);
    TEST_ASSERT_EQUAL_MEMORY(expected, legacy, len + sizeof(tag));
    TEST_ASSERT_TRUE(chacha20poly1305_encrypt_ietf_detached(
        ietf, tag, plain, len, aad, ad_len, nonce, key));
    TEST_ASSERT_EQUAL_INT(0, memcmp(expected, ietf, len));
    TEST_ASSERT_EQUAL_MEMORY(expected + len, tag, sizeof(tag));
    TEST_ASSERT_TRUE(chacha20poly1305_decrypt(
        decoded, expected, len + sizeof(tag), aad, ad_len, nonce_value, key));
    TEST_ASSERT_EQUAL_INT(0, memcmp(plain, decoded, len));
    memset(decoded, 0xA5, sizeof(decoded));
    TEST_ASSERT_TRUE(chacha20poly1305_decrypt_ietf_detached(
        decoded, expected, len, expected + len, aad, ad_len, nonce, key));
    TEST_ASSERT_EQUAL_INT(0, memcmp(plain, decoded, len));
  }
}

void test_chacha20poly1305_legacy_matches_ietf(void) {
  const uint8_t key[32] = {0};
  const uint64_t nonce_value = UINT64_C(0x0123456789abcdef);
  uint8_t nonce[12] = {0};
  jh_store_le64(nonce + 4u, nonce_value);
  uint8_t plain[129];
  uint8_t aad[17];
  for (size_t i = 0; i < sizeof(plain); ++i) {
    plain[i] = (uint8_t)i;
  }
  for (size_t i = 0; i < sizeof(aad); ++i) {
    aad[i] = (uint8_t)(0xA0u + i);
  }
  const size_t lengths[] = {0u,  1u,  15u,  16u,  17u, 63u,
                            64u, 65u, 127u, 128u, 129u};
  const size_t aad_lengths[] = {0u, 1u, 15u, 16u, 17u};
  for (size_t i = 0; i < COUNTOF(lengths); ++i) {
    for (size_t j = 0; j < COUNTOF(aad_lengths); ++j) {
      const size_t len = lengths[i];
      const size_t ad_len = aad_lengths[j];
      uint8_t legacy[sizeof(plain) + 16u];
      uint8_t ietf[sizeof(plain)];
      uint8_t tag[16];
      uint8_t decoded[sizeof(plain)];
      chacha20poly1305_encrypt(legacy, plain, len, aad, ad_len, nonce_value,
                               key);
      TEST_ASSERT_TRUE(chacha20poly1305_encrypt_ietf_detached(
          ietf, tag, plain, len, aad, ad_len, nonce, key));
      TEST_ASSERT_EQUAL_INT(0, memcmp(ietf, legacy, len));
      TEST_ASSERT_EQUAL_MEMORY(tag, legacy + len, sizeof(tag));
      TEST_ASSERT_TRUE(chacha20poly1305_decrypt(
          decoded, legacy, len + sizeof(tag), aad, ad_len, nonce_value, key));
      TEST_ASSERT_EQUAL_INT(0, memcmp(plain, decoded, len));
    }
  }
}

void test_wireguard_replay_window_accepts_zero_once_and_tracks_reordering(
    void) {
  wireguard_replay_state_t replay = {};

  TEST_ASSERT_TRUE(wireguard_replay_check(&replay, 0u));
  TEST_ASSERT_FALSE(wireguard_replay_check(&replay, 0u));
  TEST_ASSERT_TRUE(wireguard_replay_check(&replay, 9000u));
  TEST_ASSERT_TRUE(wireguard_replay_check(&replay, 1000u));
  TEST_ASSERT_FALSE(wireguard_replay_check(&replay, 1000u));
  TEST_ASSERT_FALSE(wireguard_replay_check(&replay, 839u));
  TEST_ASSERT_TRUE(wireguard_replay_check(&replay, 840u));
}

void test_wireguard_replay_window_handles_word_wrap_and_protocol_limit(void) {
  wireguard_replay_state_t replay = {};

  for (uint64_t sequence = 1u; sequence <= 8300u; ++sequence) {
    if (sequence != 140u) {
      TEST_ASSERT_TRUE(wireguard_replay_check(&replay, sequence));
    }
  }
  TEST_ASSERT_FALSE(wireguard_replay_check(&replay, 1u));
  TEST_ASSERT_TRUE(wireguard_replay_check(&replay, 140u));
  TEST_ASSERT_FALSE(wireguard_replay_check(&replay, 140u));
  TEST_ASSERT_FALSE(
      wireguard_replay_check(&replay, WIREGUARD_REPLAY_REJECT_AFTER_MESSAGES));
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
  RUN_TEST(test_chacha20poly1305_matches_independent_aligned_vectors);
  RUN_TEST(test_chacha20poly1305_legacy_matches_ietf);
  RUN_TEST(
      test_wireguard_replay_window_accepts_zero_once_and_tracks_reordering);
  RUN_TEST(test_wireguard_replay_window_handles_word_wrap_and_protocol_limit);
  return UNITY_END();
}
