#pragma once

#include "../../../../hal_status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JH_OTA_MD5_HEX_CHARS 32u
#define JH_OTA_MD5_HEX_BUFFER_SIZE (JH_OTA_MD5_HEX_CHARS + 1u)

typedef struct {
  uint16_t command;
  uint16_t tcp_port;
  uint32_t image_size;
  char image_md5[JH_OTA_MD5_HEX_BUFFER_SIZE];
} jh_ota_invitation_t;

typedef struct {
  char client_nonce[JH_OTA_MD5_HEX_BUFFER_SIZE];
  char response[JH_OTA_MD5_HEX_BUFFER_SIZE];
} jh_ota_auth_response_t;

hal_status_t jh_ota_parse_invitation(const uint8_t *data, size_t size,
                                     jh_ota_invitation_t *out_invitation);
hal_status_t jh_ota_parse_auth_response(const uint8_t *data, size_t size,
                                        jh_ota_auth_response_t *out_response);
bool jh_ota_hex_equal(const char *left, const char *right);

#ifdef __cplusplus
}
#endif
