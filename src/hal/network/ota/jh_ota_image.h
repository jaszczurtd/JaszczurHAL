#pragma once

#include "hal/core/hal_status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JH_OTA_IMAGE_HEADER_SIZE 160u
#define JH_OTA_IMAGE_VERSION 1u
#define JH_OTA_SHA256_BYTES 32u
#define JH_OTA_VERSION_TEXT_SIZE 32u
#define JH_OTA_STATE_RECORD_SIZE 256u
#define JH_OTA_STATE_VERSION 1u

typedef enum {
  JH_OTA_TARGET_UNKNOWN = 0,
  JH_OTA_TARGET_RP2040 = 1,
  JH_OTA_TARGET_RP2350_ARM = 2,
  JH_OTA_TARGET_RP2350_RISCV = 3
} jh_ota_target_t;

typedef struct {
  jh_ota_target_t target;
  uint16_t flags;
  uint32_t program_offset;
  uint32_t payload_size;
  uint32_t generation;
  uint8_t sha256[JH_OTA_SHA256_BYTES];
  char version[JH_OTA_VERSION_TEXT_SIZE];
  uint8_t signature[JH_OTA_SHA256_BYTES];
} jh_ota_image_manifest_t;

typedef enum {
  JH_OTA_BOOT_STABLE = 0,
  JH_OTA_BOOT_PENDING = 1,
  JH_OTA_BOOT_TRIAL = 2,
  JH_OTA_BOOT_ROLLBACK = 3,
  JH_OTA_BOOT_RECOVERY = 4
} jh_ota_boot_mode_t;

typedef struct {
  uint32_t sequence;
  jh_ota_boot_mode_t mode;
  uint8_t attempts;
  uint8_t max_attempts;
  uint32_t program_size;
  uint32_t staging_size;
  uint32_t program_generation;
  uint32_t staging_generation;
  uint8_t program_sha256[JH_OTA_SHA256_BYTES];
  uint8_t staging_sha256[JH_OTA_SHA256_BYTES];
  char program_version[JH_OTA_VERSION_TEXT_SIZE];
  char staging_version[JH_OTA_VERSION_TEXT_SIZE];
} jh_ota_boot_state_t;

hal_status_t
jh_ota_image_manifest_encode(const jh_ota_image_manifest_t *manifest,
                             uint8_t out[JH_OTA_IMAGE_HEADER_SIZE]);

hal_status_t
jh_ota_image_manifest_decode(const uint8_t raw[JH_OTA_IMAGE_HEADER_SIZE],
                             jh_ota_image_manifest_t *out_manifest);

hal_status_t jh_ota_image_manifest_sign(jh_ota_image_manifest_t *manifest,
                                        const uint8_t *key, size_t key_size);

hal_status_t
jh_ota_image_manifest_verify(const jh_ota_image_manifest_t *manifest,
                             const uint8_t *key, size_t key_size);

jh_ota_target_t jh_ota_current_target(void);

hal_status_t jh_ota_boot_state_encode(const jh_ota_boot_state_t *state,
                                      uint8_t out[JH_OTA_STATE_RECORD_SIZE]);

hal_status_t
jh_ota_boot_state_decode(const uint8_t raw[JH_OTA_STATE_RECORD_SIZE],
                         jh_ota_boot_state_t *out_state);

hal_status_t
jh_ota_boot_state_select(const uint8_t first[JH_OTA_STATE_RECORD_SIZE],
                         const uint8_t second[JH_OTA_STATE_RECORD_SIZE],
                         jh_ota_boot_state_t *out_state,
                         uint8_t *out_selected_index);

#ifdef __cplusplus
}
#endif
