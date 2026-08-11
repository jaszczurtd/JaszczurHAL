#include "hal/security/hal_crypto.h"
#include "support/crypto_test_vectors.h"
#include "utils/unity.h"

#include <limits.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_chacha20_poly1305_decrypt_matches_rfc8439_vector(void) {
  uint8_t key[HAL_CHACHA20_KEY_BYTES];
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t aad[12];
  uint8_t ciphertext[265];
  uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES];
  uint8_t plaintext[265];
  uint8_t expected_plaintext[265];

  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_key_hex, key, sizeof(key)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_nonce_hex, nonce, sizeof(nonce)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_aad_hex, aad, sizeof(aad)));
  TEST_ASSERT_TRUE(
      hex_to_bytes(k_rfc8439_ciphertext_hex, ciphertext, sizeof(ciphertext)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_tag_hex, tag, sizeof(tag)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_plaintext_hex, expected_plaintext,
                                sizeof(expected_plaintext)));

  TEST_ASSERT_TRUE(hal_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                                 ciphertext, sizeof(ciphertext),
                                                 tag, plaintext));
  TEST_ASSERT_EQUAL_INT(
      0, memcmp(expected_plaintext, plaintext, sizeof(plaintext)));
}

void test_chacha20_poly1305_encrypt_matches_rfc8439_vector(void) {
  uint8_t key[HAL_CHACHA20_KEY_BYTES];
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t aad[12];
  uint8_t plaintext[265];
  uint8_t ciphertext[265];
  uint8_t expected_ciphertext[265];
  uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES];
  uint8_t expected_tag[HAL_CHACHA20_POLY1305_TAG_BYTES];

  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_key_hex, key, sizeof(key)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_nonce_hex, nonce, sizeof(nonce)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_aad_hex, aad, sizeof(aad)));
  TEST_ASSERT_TRUE(
      hex_to_bytes(k_rfc8439_plaintext_hex, plaintext, sizeof(plaintext)));
  TEST_ASSERT_TRUE(hex_to_bytes(k_rfc8439_ciphertext_hex, expected_ciphertext,
                                sizeof(expected_ciphertext)));
  TEST_ASSERT_TRUE(
      hex_to_bytes(k_rfc8439_tag_hex, expected_tag, sizeof(expected_tag)));

  TEST_ASSERT_TRUE(hal_chacha20_poly1305_encrypt(key, nonce, aad, sizeof(aad),
                                                 plaintext, sizeof(plaintext),
                                                 ciphertext, tag));
  TEST_ASSERT_EQUAL_INT(
      0, memcmp(expected_ciphertext, ciphertext, sizeof(ciphertext)));
  TEST_ASSERT_EQUAL_INT(0, memcmp(expected_tag, tag, sizeof(tag)));
}

void test_chacha20_poly1305_rejects_invalid_tag_and_args(void) {
  uint8_t key[HAL_CHACHA20_KEY_BYTES] = {0};
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {0};
  uint8_t aad[1] = {0};
  uint8_t in[32] = {0};
  uint8_t out[32];
  uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES] = {0};

  memset(out, 0xA5, sizeof(out));
  TEST_ASSERT_TRUE(hal_chacha20_poly1305_encrypt(key, nonce, aad, sizeof(aad),
                                                 in, sizeof(in), out, tag));
  tag[0] ^= 0x01u;
  uint8_t before[sizeof(out)];
  memcpy(before, out, sizeof(out));
  TEST_ASSERT_FALSE(hal_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                                  out, sizeof(out), tag, out));
  TEST_ASSERT_EQUAL_INT(0, memcmp(before, out, sizeof(out)));

  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_encrypt(NULL, nonce, aad, 1u, in, 1u, out, tag));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_encrypt(key, NULL, aad, 1u, in, 1u, out, tag));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_encrypt(key, nonce, NULL, 1u, in, 1u, out, tag));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_encrypt(key, nonce, aad, 1u, NULL, 1u, out, tag));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_encrypt(key, nonce, aad, 1u, in, 1u, NULL, tag));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_encrypt(key, nonce, aad, 1u, in, 1u, out, NULL));

  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_decrypt(NULL, nonce, aad, 1u, out, 1u, tag, in));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_decrypt(key, NULL, aad, 1u, out, 1u, tag, in));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_decrypt(key, nonce, NULL, 1u, out, 1u, tag, in));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_decrypt(key, nonce, aad, 1u, NULL, 1u, tag, in));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_decrypt(key, nonce, aad, 1u, out, 1u, NULL, in));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_decrypt(key, nonce, aad, 1u, out, 1u, tag, NULL));
}

void test_chacha20_xor_supports_in_place_processing(void) {
  uint8_t key[HAL_CHACHA20_KEY_BYTES];
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t plain[37];
  uint8_t cipher_a[37];
  uint8_t cipher_b[37];

  for (size_t i = 0u; i < HAL_CHACHA20_KEY_BYTES; ++i) {
    key[i] = (uint8_t)(0xA0u + i);
  }
  for (size_t i = 0u; i < HAL_CHACHA20_NONCE_BYTES; ++i) {
    nonce[i] = (uint8_t)(0x10u + i);
  }
  for (size_t i = 0u; i < sizeof(plain); ++i) {
    plain[i] = (uint8_t)(i * 3u + 1u);
  }
  memcpy(cipher_b, plain, sizeof(plain));

  TEST_ASSERT_TRUE(
      hal_chacha20_xor(key, 7u, nonce, plain, sizeof(plain), cipher_a));
  TEST_ASSERT_TRUE(
      hal_chacha20_xor(key, 7u, nonce, cipher_b, sizeof(cipher_b), cipher_b));
  TEST_ASSERT_EQUAL_INT(0, memcmp(cipher_a, cipher_b, sizeof(cipher_a)));

  TEST_ASSERT_TRUE(
      hal_chacha20_xor(key, 7u, nonce, cipher_b, sizeof(cipher_b), cipher_b));
  TEST_ASSERT_EQUAL_INT(0, memcmp(plain, cipher_b, sizeof(plain)));
}

void test_chacha20_poly1305_in_place_roundtrip_and_aad_sensitivity(void) {
  uint8_t key[HAL_CHACHA20_KEY_BYTES];
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t aad[13];
  uint8_t aad_bad[13];
  uint8_t buf[96];
  uint8_t ref_plain[96];
  uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES];
  uint8_t out_on_fail[96];
  uint8_t out_before[96];

  for (size_t i = 0u; i < HAL_CHACHA20_KEY_BYTES; ++i) {
    key[i] = (uint8_t)i;
  }
  for (size_t i = 0u; i < HAL_CHACHA20_NONCE_BYTES; ++i) {
    nonce[i] = (uint8_t)(0xF0u - i);
  }
  for (size_t i = 0u; i < sizeof(aad); ++i) {
    aad[i] = (uint8_t)(0x30u + i);
  }
  memcpy(aad_bad, aad, sizeof(aad));
  aad_bad[5] ^= 0x55u;

  for (size_t i = 0u; i < sizeof(buf); ++i) {
    buf[i] = (uint8_t)(i ^ 0xA5u);
  }
  memcpy(ref_plain, buf, sizeof(buf));

  TEST_ASSERT_TRUE(hal_chacha20_poly1305_encrypt(key, nonce, aad, sizeof(aad),
                                                 buf, sizeof(buf), buf, tag));
  TEST_ASSERT_TRUE(hal_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                                 buf, sizeof(buf), tag, buf));
  TEST_ASSERT_EQUAL_INT(0, memcmp(ref_plain, buf, sizeof(buf)));

  TEST_ASSERT_TRUE(hal_chacha20_poly1305_encrypt(
      key, nonce, aad, sizeof(aad), ref_plain, sizeof(ref_plain), buf, tag));
  memset(out_on_fail, 0xCC, sizeof(out_on_fail));
  memcpy(out_before, out_on_fail, sizeof(out_on_fail));
  TEST_ASSERT_FALSE(
      hal_chacha20_poly1305_decrypt(key, nonce, aad_bad, sizeof(aad_bad), buf,
                                    sizeof(buf), tag, out_on_fail));
  TEST_ASSERT_EQUAL_INT(0,
                        memcmp(out_before, out_on_fail, sizeof(out_on_fail)));
}

void test_chacha20_poly1305_supports_empty_plaintext(void) {
  uint8_t key[HAL_CHACHA20_KEY_BYTES] = {0};
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {0};
  uint8_t aad[7] = {1u, 2u, 3u, 4u, 5u, 6u, 7u};
  uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES];
  uint8_t bad_tag[HAL_CHACHA20_POLY1305_TAG_BYTES];

  TEST_ASSERT_TRUE(hal_chacha20_poly1305_encrypt(key, nonce, aad, sizeof(aad),
                                                 NULL, 0u, NULL, tag));
  TEST_ASSERT_TRUE(hal_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                                 NULL, 0u, tag, NULL));

  memcpy(bad_tag, tag, sizeof(tag));
  bad_tag[0] ^= 0x80u;
  TEST_ASSERT_FALSE(hal_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                                  NULL, 0u, bad_tag, NULL));
}

void test_chacha20_block_matches_rfc8439_vector(void) {
  uint8_t key[HAL_CHACHA20_KEY_BYTES];
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t block[HAL_CHACHA20_BLOCK_BYTES];
  uint8_t expected[HAL_CHACHA20_BLOCK_BYTES];

  for (size_t i = 0u; i < HAL_CHACHA20_KEY_BYTES; ++i) {
    key[i] = (uint8_t)i;
  }
  TEST_ASSERT_TRUE(
      hex_to_bytes("000000090000004a00000000", nonce, sizeof(nonce)));
  TEST_ASSERT_TRUE(hex_to_bytes("10f1e7e4d13b5915500fdd1fa32071c4"
                                "c7d1f4c733c068030422aa9ac3d46c4e"
                                "d2826446079faa0914c2d705d98b02a2"
                                "b5129cd1de164eb9cbd083e8a2503c4e",
                                expected, sizeof(expected)));

  TEST_ASSERT_TRUE(hal_chacha20_block(key, 1u, nonce, block));
  TEST_ASSERT_EQUAL_INT(0, memcmp(expected, block, sizeof(expected)));
}

void test_chacha20_xor_matches_rfc8439_ciphertext_and_decrypts(void) {
  static const char plaintext[] =
      "Ladies and Gentlemen of the class of '99: If I could offer you only one "
      "tip for the future, sunscreen would be it.";
  const size_t plaintext_len = strlen(plaintext);

  uint8_t key[HAL_CHACHA20_KEY_BYTES];
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t expected[114];
  uint8_t encrypted[114];
  uint8_t decrypted[114];

  for (size_t i = 0u; i < HAL_CHACHA20_KEY_BYTES; ++i) {
    key[i] = (uint8_t)i;
  }
  TEST_ASSERT_TRUE(
      hex_to_bytes("000000000000004a00000000", nonce, sizeof(nonce)));
  TEST_ASSERT_TRUE(hex_to_bytes("6e2e359a2568f98041ba0728dd0d6981"
                                "e97e7aec1d4360c20a27afccfd9fae0b"
                                "f91b65c5524733ab8f593dabcd62b357"
                                "1639d624e65152ab8f530c359f0861d8"
                                "07ca0dbf500d6a6156a38e088a22b65e"
                                "52bc514d16ccf806818ce91ab7793736"
                                "5af90bbf74a35be6b40b8eedf2785e42"
                                "874d",
                                expected, sizeof(expected)));

  TEST_ASSERT_TRUE(hal_chacha20_xor(key, 1u, nonce, (const uint8_t *)plaintext,
                                    plaintext_len, encrypted));
  TEST_ASSERT_EQUAL_INT(0, memcmp(expected, encrypted, sizeof(expected)));

  TEST_ASSERT_TRUE(hal_chacha20_xor(key, 1u, nonce, encrypted,
                                    sizeof(encrypted), decrypted));
  TEST_ASSERT_EQUAL_INT(
      0, memcmp((const uint8_t *)plaintext, decrypted, plaintext_len));
}

void test_chacha20_rejects_invalid_arguments(void) {
  uint8_t key[HAL_CHACHA20_KEY_BYTES] = {0};
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {0};
  uint8_t in[65] = {0};
  uint8_t out[65] = {0};
  uint8_t block[HAL_CHACHA20_BLOCK_BYTES] = {0};

  TEST_ASSERT_FALSE(hal_chacha20_block(NULL, 0u, nonce, block));
  TEST_ASSERT_FALSE(hal_chacha20_block(key, 0u, NULL, block));
  TEST_ASSERT_FALSE(hal_chacha20_block(key, 0u, nonce, NULL));

  TEST_ASSERT_FALSE(hal_chacha20_xor(NULL, 0u, nonce, in, sizeof(in), out));
  TEST_ASSERT_FALSE(hal_chacha20_xor(key, 0u, NULL, in, sizeof(in), out));
  TEST_ASSERT_FALSE(hal_chacha20_xor(key, 0u, nonce, NULL, 1u, out));
  TEST_ASSERT_FALSE(hal_chacha20_xor(key, 0u, nonce, in, 1u, NULL));

  /* Counter overflow after first block should be rejected. */
  TEST_ASSERT_FALSE(
      hal_chacha20_xor(key, UINT32_MAX, nonce, in, sizeof(in), out));
  TEST_ASSERT_TRUE(hal_chacha20_xor(key, UINT32_MAX, nonce, in, 64u, out));
  TEST_ASSERT_TRUE(hal_chacha20_xor(key, 0u, nonce, NULL, 0u, NULL));
}

void test_chacha20_and_aead_reject_wraparound_for_very_large_lengths(void) {
  const size_t too_large_len =
      ((size_t)UINT32_MAX * HAL_CHACHA20_BLOCK_BYTES) + 1u;

  uint8_t key[HAL_CHACHA20_KEY_BYTES] = {0};
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {0};
  uint8_t aad[1] = {0x11u};
  uint8_t input[1] = {0x42u};
  uint8_t output[1] = {0xA5u};
  uint8_t ciphertext[1] = {0x33u};
  uint8_t plaintext[1] = {0xCCu};
  uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES];
  uint8_t tag_before[HAL_CHACHA20_POLY1305_TAG_BYTES];
  uint8_t ciphertext_before[sizeof(ciphertext)];
  uint8_t plaintext_before[sizeof(plaintext)];

  memset(tag, 0x5Au, sizeof(tag));

  TEST_ASSERT_FALSE(
      hal_chacha20_xor(key, 1u, nonce, input, too_large_len, output));
  TEST_ASSERT_EQUAL_UINT8(0xA5u, output[0]);

  memcpy(tag_before, tag, sizeof(tag_before));
  memcpy(ciphertext_before, ciphertext, sizeof(ciphertext_before));
  TEST_ASSERT_FALSE(hal_chacha20_poly1305_encrypt(
      key, nonce, aad, sizeof(aad), input, too_large_len, ciphertext, tag));
  TEST_ASSERT_EQUAL_INT(
      0, memcmp(ciphertext_before, ciphertext, sizeof(ciphertext)));
  TEST_ASSERT_EQUAL_INT(0, memcmp(tag_before, tag, sizeof(tag)));

  memcpy(plaintext_before, plaintext, sizeof(plaintext_before));
  TEST_ASSERT_FALSE(hal_chacha20_poly1305_decrypt(
      key, nonce, aad, sizeof(aad), ciphertext, too_large_len, tag, plaintext));
  TEST_ASSERT_EQUAL_INT(0,
                        memcmp(plaintext_before, plaintext, sizeof(plaintext)));
}

void test_md5_hex_matches_known_vectors(void) {
  char out[HAL_MD5_HEX_BUF_SIZE];

  TEST_ASSERT_TRUE(hal_md5_hex(NULL, 0u, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("d41d8cd98f00b204e9800998ecf8427e", out);

  TEST_ASSERT_TRUE(hal_md5_hex((const uint8_t *)"a", 1u, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("0cc175b9c0f1b6a831c399e269772661", out);

  TEST_ASSERT_TRUE(hal_md5_hex((const uint8_t *)"abc", 3u, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("900150983cd24fb0d6963f7d28e17f72", out);

  TEST_ASSERT_TRUE(
      hal_md5_hex((const uint8_t *)"message digest", 14u, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("f96b697d7cb7938d525a2f31aaf161d0", out);

  TEST_ASSERT_TRUE(hal_md5_hex((const uint8_t *)"abcdefghijklmnopqrstuvwxyz",
                               26u, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("c3fcd3d76192e4007dfb496cca67e13b", out);
}

void test_md5_raw_digest_matches_expected_for_abc(void) {
  uint8_t digest[HAL_MD5_DIGEST_BYTES] = {0};
  static const uint8_t expected[HAL_MD5_DIGEST_BYTES] = {
      0x90u, 0x01u, 0x50u, 0x98u, 0x3Cu, 0xD2u, 0x4Fu, 0xB0u,
      0xD6u, 0x96u, 0x3Fu, 0x7Du, 0x28u, 0xE1u, 0x7Fu, 0x72u};

  TEST_ASSERT_TRUE(hal_md5((const uint8_t *)"abc", 3u, digest));
  TEST_ASSERT_EQUAL_INT(0, memcmp(expected, digest, sizeof(expected)));
}

void test_md5_rejects_invalid_arguments(void) {
  char hex_out[HAL_MD5_HEX_BUF_SIZE];
  uint8_t digest[HAL_MD5_DIGEST_BYTES];

  TEST_ASSERT_FALSE(hal_md5((const uint8_t *)"abc", 3u, NULL));
  TEST_ASSERT_FALSE(hal_md5(NULL, 1u, digest));

  TEST_ASSERT_FALSE(hal_md5_hex((const uint8_t *)"abc", 3u, NULL, 0u));
  TEST_ASSERT_FALSE(hal_md5_hex((const uint8_t *)"abc", 3u, hex_out,
                                HAL_MD5_HEX_BUF_SIZE - 1u));
  TEST_ASSERT_FALSE(hal_md5_hex(NULL, 1u, hex_out, sizeof(hex_out)));
}

void test_md5_hex_matches_million_a_vector(void) {
  static uint8_t input[1000000];
  char out[HAL_MD5_HEX_BUF_SIZE];
  memset(input, 'a', sizeof(input));

  TEST_ASSERT_TRUE(hal_md5_hex(input, sizeof(input), out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("7707d6ae4e027c70eea2a935c2296f21", out);
}

void test_length_helpers_return_expected_values(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)hal_base64_encoded_len(0u));
  TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)hal_base64_encoded_len(1u));
  TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)hal_base64_encoded_len(2u));
  TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)hal_base64_encoded_len(3u));
  TEST_ASSERT_EQUAL_UINT32(8u, (uint32_t)hal_base64_encoded_len(4u));

  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)hal_base64_decoded_max_len(0u));
  TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)hal_base64_decoded_max_len(4u));
  TEST_ASSERT_EQUAL_UINT32(6u, (uint32_t)hal_base64_decoded_max_len(8u));
}

void test_encode_handles_padding_variants(void) {
  char out[16];
  size_t out_len = 0u;

  TEST_ASSERT_TRUE(
      hal_base64_encode((const uint8_t *)"M", 1u, out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_STRING("TQ==", out);
  TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)out_len);

  TEST_ASSERT_TRUE(
      hal_base64_encode((const uint8_t *)"Ma", 2u, out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_STRING("TWE=", out);
  TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)out_len);

  TEST_ASSERT_TRUE(hal_base64_encode((const uint8_t *)"Man", 3u, out,
                                     sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_STRING("TWFu", out);
  TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)out_len);
}

void test_encode_supports_empty_input(void) {
  char out[1] = {'X'};
  size_t out_len = 123u;

  TEST_ASSERT_TRUE(hal_base64_encode(NULL, 0u, out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)out_len);
  TEST_ASSERT_EQUAL_STRING("", out);
}

void test_encode_rejects_invalid_arguments(void) {
  char out[8];

  TEST_ASSERT_FALSE(hal_base64_encode(NULL, 1u, out, sizeof(out), NULL));
  TEST_ASSERT_FALSE(
      hal_base64_encode((const uint8_t *)"A", 1u, NULL, 0u, NULL));
  TEST_ASSERT_FALSE(hal_base64_encode((const uint8_t *)"A", 1u, out, 4u,
                                      NULL)); /* needs 5 incl. NUL */
}

void test_decode_basic_vectors(void) {
  uint8_t out[8];
  size_t out_len = 0u;

  TEST_ASSERT_TRUE(hal_base64_decode("TQ==", 4u, out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)out_len);
  TEST_ASSERT_EQUAL_UINT8('M', out[0]);

  TEST_ASSERT_TRUE(hal_base64_decode("TWE=", 4u, out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)out_len);
  TEST_ASSERT_EQUAL_UINT8('M', out[0]);
  TEST_ASSERT_EQUAL_UINT8('a', out[1]);

  TEST_ASSERT_TRUE(hal_base64_decode("TWFu", 4u, out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)out_len);
  TEST_ASSERT_EQUAL_UINT8('M', out[0]);
  TEST_ASSERT_EQUAL_UINT8('a', out[1]);
  TEST_ASSERT_EQUAL_UINT8('n', out[2]);
}

void test_decode_supports_length_query_mode(void) {
  size_t out_len = 0u;
  TEST_ASSERT_TRUE(hal_base64_decode("SGVsbG8=", 8u, NULL, 0u, &out_len));
  TEST_ASSERT_EQUAL_UINT32(5u, (uint32_t)out_len);
}

void test_decode_rejects_invalid_input(void) {
  uint8_t out[8];
  size_t out_len = 0u;

  TEST_ASSERT_FALSE(hal_base64_decode("AAA", 3u, out, sizeof(out),
                                      &out_len)); /* len % 4 != 0 */
  TEST_ASSERT_FALSE(hal_base64_decode("AA*A", 4u, out, sizeof(out),
                                      &out_len)); /* invalid char */
  TEST_ASSERT_FALSE(hal_base64_decode("AA=A", 4u, out, sizeof(out),
                                      &out_len)); /* bad padding */
  TEST_ASSERT_FALSE(hal_base64_decode("AA==AA==", 8u, out, sizeof(out),
                                      &out_len)); /* padding not final */
  TEST_ASSERT_FALSE(
      hal_base64_decode("TWFu", 4u, out, 2u, &out_len)); /* output too small */
  TEST_ASSERT_FALSE(
      hal_base64_decode(NULL, 4u, out, sizeof(out), &out_len)); /* null input */
  TEST_ASSERT_FALSE(hal_base64_decode("TQ==", 4u, NULL, 1u,
                                      &out_len)); /* null output with size */
}

void test_roundtrip_binary_payload(void) {
  static const uint8_t src[] = {0x00u, 0x01u, 0x02u, 0x10u, 0x20u,
                                0x30u, 0x7Fu, 0x80u, 0xFFu, 0x55u};

  char b64[32];
  uint8_t out[16];
  size_t b64_len = 0u;
  size_t out_len = 0u;

  TEST_ASSERT_TRUE(
      hal_base64_encode(src, sizeof(src), b64, sizeof(b64), &b64_len));
  TEST_ASSERT_TRUE(hal_base64_decode(b64, b64_len, out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(src), (uint32_t)out_len);
  TEST_ASSERT_EQUAL_INT(0, memcmp(src, out, sizeof(src)));
}

void test_base64_encode_decode_without_optional_length_output(void) {
  const uint8_t src[] = {0x00u, 0x00u, 0x00u, 0xFFu, 0xEEu, 0xDDu, 0xCCu};
  char b64[32];
  uint8_t out[16];
  size_t out_len = 0u;

  TEST_ASSERT_TRUE(hal_base64_encode(src, sizeof(src), b64, sizeof(b64), NULL));
  TEST_ASSERT_TRUE(
      hal_base64_decode(b64, strlen(b64), out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(src), (uint32_t)out_len);
  TEST_ASSERT_EQUAL_INT(0, memcmp(src, out, sizeof(src)));
}

void test_sha256_incremental_matches_one_shot(void) {
  static const uint8_t payload[] = "JaszczurHAL incremental OTA image digest";
  uint8_t expected[HAL_SHA256_DIGEST_BYTES];
  uint8_t actual[HAL_SHA256_DIGEST_BYTES];
  hal_sha256_context_t context = {};

  TEST_ASSERT_TRUE(hal_sha256(payload, sizeof(payload) - 1u, expected));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sha256_init_ex(&context));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sha256_update_ex(&context, payload, 7u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sha256_update_ex(&context, payload + 7u,
                                                     sizeof(payload) - 8u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sha256_final_ex(&context, actual));
  TEST_ASSERT_EQUAL_MEMORY(expected, actual, sizeof(actual));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_sha256_update_ex(&context, payload, 1u));
}

void test_sha256_incremental_rejects_invalid_arguments(void) {
  hal_sha256_context_t context = {};
  uint8_t digest[HAL_SHA256_DIGEST_BYTES];

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_sha256_init_ex(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_sha256_update_ex(&context, NULL, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sha256_init_ex(&context));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_sha256_update_ex(&context, NULL, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sha256_update_ex(&context, NULL, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_sha256_final_ex(&context, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sha256_final_ex(&context, digest));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_chacha20_poly1305_decrypt_matches_rfc8439_vector);
  RUN_TEST(test_chacha20_poly1305_encrypt_matches_rfc8439_vector);
  RUN_TEST(test_chacha20_poly1305_rejects_invalid_tag_and_args);
  RUN_TEST(test_chacha20_xor_supports_in_place_processing);
  RUN_TEST(test_chacha20_poly1305_in_place_roundtrip_and_aad_sensitivity);
  RUN_TEST(test_chacha20_poly1305_supports_empty_plaintext);
  RUN_TEST(test_chacha20_block_matches_rfc8439_vector);
  RUN_TEST(test_chacha20_xor_matches_rfc8439_ciphertext_and_decrypts);
  RUN_TEST(test_chacha20_rejects_invalid_arguments);
  RUN_TEST(test_chacha20_and_aead_reject_wraparound_for_very_large_lengths);
  RUN_TEST(test_md5_hex_matches_known_vectors);
  RUN_TEST(test_md5_raw_digest_matches_expected_for_abc);
  RUN_TEST(test_md5_rejects_invalid_arguments);
  RUN_TEST(test_md5_hex_matches_million_a_vector);
  RUN_TEST(test_length_helpers_return_expected_values);
  RUN_TEST(test_encode_handles_padding_variants);
  RUN_TEST(test_encode_supports_empty_input);
  RUN_TEST(test_encode_rejects_invalid_arguments);
  RUN_TEST(test_decode_basic_vectors);
  RUN_TEST(test_decode_supports_length_query_mode);
  RUN_TEST(test_decode_rejects_invalid_input);
  RUN_TEST(test_roundtrip_binary_payload);
  RUN_TEST(test_base64_encode_decode_without_optional_length_output);
  RUN_TEST(test_sha256_incremental_matches_one_shot);
  RUN_TEST(test_sha256_incremental_rejects_invalid_arguments);
  return UNITY_END();
}
