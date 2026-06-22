#include <hal/hal_app.h>
#include <hal/hal_crypto.h>
#include <hal/hal_system.h>
#include <tools_c.h>

static const uint8_t key[HAL_CHACHA20_KEY_BYTES] = {};
static const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {};

static void demoCrypto(void) {
  const uint8_t msg[] = "hello";
  uint8_t cipher[sizeof(msg)] = {};
  uint8_t plain[sizeof(msg)] = {};
  uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES] = {};
  char md5_hex[HAL_MD5_HEX_BUF_SIZE] = {};

  (void)hal_md5_hex(msg, sizeof(msg) - 1u, md5_hex, sizeof(md5_hex));
  (void)hal_chacha20_poly1305_encrypt(key, nonce, NULL, 0u, msg, sizeof(msg),
                                      cipher, tag);
  (void)hal_chacha20_poly1305_decrypt(key, nonce, NULL, 0u, cipher, sizeof(msg),
                                      tag, plain);
}

void app_start(void) {
  debugInit();
  demoCrypto();
}

void app_task0(void) { hal_delay_ms(1000); }
