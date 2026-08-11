#ifndef JH_CRYPTO_TEST_VECTORS_H
#define JH_CRYPTO_TEST_VECTORS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int hex_nibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

static bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
  if (hex == NULL || out == NULL || strlen(hex) != out_len * 2u) {
    return false;
  }
  for (size_t i = 0u; i < out_len; ++i) {
    const int high = hex_nibble(hex[i * 2u]);
    const int low = hex_nibble(hex[i * 2u + 1u]);
    if (high < 0 || low < 0) {
      return false;
    }
    out[i] = (uint8_t)((high << 4) | low);
  }
  return true;
}

static constexpr char k_rfc8439_key_hex[] = "1c9240a5eb55d38af333888604f6b5f0"
                                            "473917c1402b80099dca5cbc207075c0";
static constexpr char k_rfc8439_nonce_hex[] = "000000000102030405060708";
static constexpr char k_rfc8439_aad_hex[] = "f33388860000000000004e91";
static constexpr char k_rfc8439_plaintext_hex[] =
    "496e7465726e65742d44726166747320"
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
    "726573732e2fe2809d";
static constexpr char k_rfc8439_ciphertext_hex[] =
    "64a0861575861af460f062c79be643bd"
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
    "a6ad5cb4022b02709b";
static constexpr char k_rfc8439_tag_hex[] = "eead9d67890cbb22392336fea1851f38";

#endif
