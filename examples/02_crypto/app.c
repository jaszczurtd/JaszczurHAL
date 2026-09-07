/*
 * Demonstrate MD5 and ChaCha20-Poly1305 calls on a small fixed message.
 * This example does not check return codes or verify the decrypted result.
 */

#include <hal/core/hal_app.h>
#include <hal/security/hal_crypto.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

/* Fixed zero key and nonce: API demonstration only, never real-data encryption.
 */
static const uint8_t key[HAL_CHACHA20_KEY_BYTES] = {};
static const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {};

static void demoCrypto(void) {
  const uint8_t msg[] = "hello";
  uint8_t cipher[sizeof(msg)] = {};
  uint8_t plain[sizeof(msg)] = {};
  uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES] = {};
  char md5_hex[HAL_MD5_HEX_BUF_SIZE] = {};

  /* Hash only the text; encryption below includes the terminating NUL byte. */
  (void)hal_md5_hex(msg, sizeof(msg) - 1u, md5_hex, sizeof(md5_hex));
  deb("MD5(\"%s\") = %s", msg, md5_hex);

  (void)hal_chacha20_poly1305_encrypt(key, nonce, NULL, 0u, msg, sizeof(msg),
                                      cipher, tag);
  (void)hal_chacha20_poly1305_decrypt(key, nonce, NULL, 0u, cipher, sizeof(msg),
                                      tag, plain);
  deb("ChaCha20-Poly1305: \"%s\" -> %02X%02X%02X%02X%02X -> \"%s\"", msg,
      cipher[0], cipher[1], cipher[2], cipher[3], cipher[4], plain);
}

void app_start(void) {
  hal_debug_init_default();
  demoCrypto();
}

void app_task0(void) { hal_delay_ms(1000); }
