#pragma once

#include "hal/network/hal_net.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JH_OTA_MD5_HEX_CHARS 32u
#define JH_OTA_MD5_HEX_BUFFER_SIZE (JH_OTA_MD5_HEX_CHARS + 1u)
#define JH_OTA_AUTH_TAG_HEX_CHARS 64u
#define JH_OTA_AUTH_TAG_HEX_BUFFER_SIZE (JH_OTA_AUTH_TAG_HEX_CHARS + 1u)
#define JH_OTA_AUTH_TRANSCRIPT_BUFFER_SIZE 192u

typedef struct {
  uint16_t command;
  uint16_t tcp_port;
  uint32_t image_size;
  char image_md5[JH_OTA_MD5_HEX_BUFFER_SIZE];
} jh_ota_invitation_t;

typedef struct {
  char client_nonce[JH_OTA_MD5_HEX_BUFFER_SIZE];
  char response[JH_OTA_AUTH_TAG_HEX_BUFFER_SIZE];
} jh_ota_auth_response_t;

hal_status_t jh_ota_parse_invitation(const uint8_t *data, size_t size,
                                     jh_ota_invitation_t *out_invitation);
hal_status_t jh_ota_parse_auth_response(const uint8_t *data, size_t size,
                                        jh_ota_auth_response_t *out_response);
/** Derive the lowercase ASCII MD5 key used by AUTH2 and RP image HMAC. */
hal_status_t
jh_ota_derive_password_key(const char *password,
                           char out_key[JH_OTA_MD5_HEX_BUFFER_SIZE]);
hal_status_t jh_ota_format_auth_transcript(
    const jh_ota_invitation_t *invitation,
    const char device_nonce[JH_OTA_MD5_HEX_BUFFER_SIZE],
    const char client_nonce[JH_OTA_MD5_HEX_BUFFER_SIZE], char *out,
    size_t out_size, size_t *out_length);
bool jh_ota_hex_equal(const char *left, const char *right);
bool jh_ota_auth_tag_equal(const char *left, const char *right);
bool jh_ota_endpoint_equal(const hal_net_endpoint_t *left,
                           const hal_net_endpoint_t *right);

#ifdef __cplusplus
}
#endif
