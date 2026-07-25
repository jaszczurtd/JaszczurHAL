#include "../../../../hal_target.h"

#if HAL_TARGET_IS_RP && defined(HAL_ENABLE_OTA)

#include "../../../../hal_config.h"
#include "../../../../hal_crypto.h"
#include "rp_flash_storage.h"
#include "rp_ota_storage.h"

#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>

#include <string.h>

namespace {

static_assert(HAL_RP_OTA_MAX_BOOT_ATTEMPTS > 0u &&
                  HAL_RP_OTA_MAX_BOOT_ATTEMPTS <= UINT8_MAX,
              "HAL_RP_OTA_MAX_BOOT_ATTEMPTS must be in range 1..255");

struct OtaWriter {
  bool active;
  bool staging_ready;
  uint32_t container_size;
  uint32_t container_received;
  uint32_t payload_received;
  uint8_t header[JH_OTA_IMAGE_HEADER_SIZE];
  size_t header_used;
  uint8_t page[FLASH_PAGE_SIZE];
  size_t page_used;
  jh_ota_image_manifest_t manifest;
  jh_rp_flash_partition_t staging;
  hal_sha256_context_t sha256;
  uint8_t authentication_key[HAL_MD5_HEX_BUF_SIZE];
  size_t authentication_key_size;
};

OtaWriter s_writer;

extern "C" uint8_t __flash_binary_start;
extern "C" uint8_t __flash_binary_end;

hal_status_t state_partition(uint8_t index,
                             jh_rp_flash_partition_t *out_partition) {
  return jh_rp_flash_storage_partition(index == 0u
                                           ? JH_RP_FLASH_PARTITION_OTA_STATE_A
                                           : JH_RP_FLASH_PARTITION_OTA_STATE_B,
                                       out_partition);
}

hal_status_t read_state_record(uint8_t index,
                               uint8_t out[JH_OTA_STATE_RECORD_SIZE]) {
  jh_rp_flash_partition_t partition = {};
  hal_status_t status = state_partition(index, &partition);
  if (status != HAL_OK) {
    return status;
  }
  return jh_rp_flash_storage_read(&partition, 0u, out,
                                  JH_OTA_STATE_RECORD_SIZE);
}

hal_status_t load_state(jh_ota_boot_state_t *out_state,
                        uint8_t *out_selected_index) {
  uint8_t records[2][JH_OTA_STATE_RECORD_SIZE];
  hal_status_t status = read_state_record(0u, records[0]);
  if (status != HAL_OK) {
    return status;
  }
  status = read_state_record(1u, records[1]);
  if (status != HAL_OK) {
    return status;
  }
  return jh_ota_boot_state_select(records[0], records[1], out_state,
                                  out_selected_index);
}

hal_status_t write_state(const jh_ota_boot_state_t *state,
                         uint8_t current_index) {
  uint8_t encoded[JH_OTA_STATE_RECORD_SIZE];
  hal_status_t status = jh_ota_boot_state_encode(state, encoded);
  if (status != HAL_OK) {
    return status;
  }
  jh_rp_flash_partition_t partition = {};
  status = state_partition((uint8_t)(current_index ^ 1u), &partition);
  if (status != HAL_OK) {
    return status;
  }
  status = jh_rp_flash_storage_erase(&partition, 0u, FLASH_SECTOR_SIZE);
  if (status != HAL_OK) {
    return status;
  }
  return jh_rp_flash_storage_program(&partition, 0u, encoded, sizeof(encoded));
}

hal_status_t digest_partition(const jh_rp_flash_partition_t *partition,
                              uint32_t size,
                              uint8_t out[HAL_SHA256_DIGEST_BYTES]) {
  if (partition == nullptr || out == nullptr || size == 0u ||
      size > partition->size) {
    return HAL_EINVAL;
  }
  hal_sha256_context_t context = {};
  hal_status_t status = hal_sha256_init_ex(&context);
  if (status != HAL_OK) {
    return status;
  }
  uint8_t buffer[1024];
  for (uint32_t offset = 0u; offset < size;) {
    const size_t chunk =
        size - offset < sizeof(buffer) ? size - offset : sizeof(buffer);
    status = jh_rp_flash_storage_read(partition, offset, buffer, chunk);
    if (status != HAL_OK) {
      return status;
    }
    status = hal_sha256_update_ex(&context, buffer, chunk);
    if (status != HAL_OK) {
      return status;
    }
    offset += (uint32_t)chunk;
  }
  return hal_sha256_final_ex(&context, out);
}

hal_status_t initialise_factory_state(jh_ota_boot_state_t *out_state,
                                      uint8_t *out_index) {
  if (out_state == nullptr || out_index == nullptr) {
    return HAL_EINVAL;
  }
  jh_rp_flash_partition_t program = {};
  hal_status_t status = jh_rp_flash_storage_partition(
      JH_RP_FLASH_PARTITION_OTA_PROGRAM, &program);
  if (status != HAL_OK) {
    return status;
  }
  const uintptr_t start = (uintptr_t)&__flash_binary_start;
  const uintptr_t end = (uintptr_t)&__flash_binary_end;
  if (start < (uintptr_t)XIP_BASE + program.flash_offset || end <= start ||
      end - start > program.size) {
    return HAL_ECONFIG;
  }

  jh_ota_boot_state_t state = {};
  state.sequence = 1u;
  state.mode = JH_OTA_BOOT_STABLE;
  state.max_attempts = (uint8_t)HAL_RP_OTA_MAX_BOOT_ATTEMPTS;
  state.program_size = (uint32_t)(end - start);
  memcpy(state.program_version, "factory", 8u);
  memcpy(state.staging_version, "empty", 6u);
  status = digest_partition(&program, state.program_size, state.program_sha256);
  if (status != HAL_OK) {
    return status;
  }
  *out_state = state;
  status = write_state(&state, 1u);
  if (status == HAL_OK) {
    *out_index = 0u;
  }
  return status;
}

hal_status_t load_or_initialise_state(jh_ota_boot_state_t *out_state,
                                      uint8_t *out_index) {
  const hal_status_t status = load_state(out_state, out_index);
  if (status == HAL_OK) {
    return HAL_OK;
  }
  if (status != HAL_ENOENT) {
    return status;
  }
  return initialise_factory_state(out_state, out_index);
}

hal_status_t prepare_staging(void) {
  hal_status_t status =
      jh_ota_image_manifest_decode(s_writer.header, &s_writer.manifest);
  if (status != HAL_OK) {
    return status;
  }
  if (s_writer.manifest.target != jh_ota_current_target() ||
      s_writer.manifest.program_offset != HAL_RP_OTA_PROGRAM_OFFSET ||
      s_writer.manifest.payload_size == 0u ||
      s_writer.manifest.payload_size > HAL_RP_OTA_SLOT_SIZE ||
      s_writer.container_size !=
          JH_OTA_IMAGE_HEADER_SIZE + s_writer.manifest.payload_size) {
    return HAL_ECONFIG;
  }
  status = jh_rp_flash_storage_partition(JH_RP_FLASH_PARTITION_OTA_STAGING,
                                         &s_writer.staging);
  if (status != HAL_OK) {
    return status;
  }
  status = jh_ota_image_manifest_verify(&s_writer.manifest,
                                        s_writer.authentication_key,
                                        s_writer.authentication_key_size);
  if (status != HAL_OK) {
    return status;
  }
  status =
      jh_rp_flash_storage_erase(&s_writer.staging, 0u, s_writer.staging.size);
  if (status != HAL_OK) {
    return status;
  }
  status = hal_sha256_init_ex(&s_writer.sha256);
  if (status != HAL_OK) {
    return status;
  }
  memset(s_writer.page, 0xFF, sizeof(s_writer.page));
  s_writer.staging_ready = true;
  return HAL_OK;
}

hal_status_t flush_page(void) {
  if (s_writer.page_used == 0u) {
    return HAL_OK;
  }
  const uint32_t offset =
      s_writer.payload_received - (uint32_t)s_writer.page_used;
  const uint32_t page_offset = offset - (offset % FLASH_PAGE_SIZE);
  const hal_status_t status = jh_rp_flash_storage_program(
      &s_writer.staging, page_offset, s_writer.page, sizeof(s_writer.page));
  if (status == HAL_OK) {
    s_writer.page_used = 0u;
    memset(s_writer.page, 0xFF, sizeof(s_writer.page));
  }
  return status;
}

hal_status_t consume_payload(const uint8_t *data, size_t size) {
  while (size > 0u) {
    const size_t capacity = sizeof(s_writer.page) - s_writer.page_used;
    const size_t chunk = size < capacity ? size : capacity;
    memcpy(&s_writer.page[s_writer.page_used], data, chunk);
    hal_status_t status = hal_sha256_update_ex(&s_writer.sha256, data, chunk);
    if (status != HAL_OK) {
      return status;
    }
    s_writer.page_used += chunk;
    s_writer.payload_received += (uint32_t)chunk;
    data += chunk;
    size -= chunk;
    if (s_writer.page_used == sizeof(s_writer.page)) {
      status = flush_page();
      if (status != HAL_OK) {
        return status;
      }
    }
  }
  return HAL_OK;
}

} // namespace

hal_status_t jh_rp_ota_storage_begin(uint32_t container_size,
                                     const uint8_t *authentication_key,
                                     size_t authentication_key_size) {
  if (container_size <= JH_OTA_IMAGE_HEADER_SIZE || s_writer.active ||
      (authentication_key == nullptr && authentication_key_size > 0u) ||
      authentication_key_size > sizeof(s_writer.authentication_key)) {
    return s_writer.active ? HAL_EBUSY : HAL_EINVAL;
  }
  memset(&s_writer, 0, sizeof(s_writer));
  s_writer.active = true;
  s_writer.container_size = container_size;
  if (authentication_key_size > 0u) {
    memcpy(s_writer.authentication_key, authentication_key,
           authentication_key_size);
  }
  s_writer.authentication_key_size = authentication_key_size;
  return HAL_OK;
}

hal_status_t jh_rp_ota_storage_write(const uint8_t *data, size_t size,
                                     size_t *out_written) {
  if (!s_writer.active || data == nullptr || size == 0u ||
      out_written == nullptr ||
      size > s_writer.container_size - s_writer.container_received) {
    return HAL_EINVAL;
  }
  const size_t input_size = size;
  if (s_writer.header_used < sizeof(s_writer.header)) {
    const size_t remaining = sizeof(s_writer.header) - s_writer.header_used;
    const size_t chunk = size < remaining ? size : remaining;
    memcpy(&s_writer.header[s_writer.header_used], data, chunk);
    s_writer.header_used += chunk;
    s_writer.container_received += (uint32_t)chunk;
    data += chunk;
    size -= chunk;
    if (s_writer.header_used == sizeof(s_writer.header)) {
      const hal_status_t status = prepare_staging();
      if (status != HAL_OK) {
        return status;
      }
    }
  }
  if (size > 0u) {
    if (!s_writer.staging_ready ||
        s_writer.payload_received + size > s_writer.manifest.payload_size) {
      return HAL_EOVERFLOW;
    }
    const hal_status_t status = consume_payload(data, size);
    if (status != HAL_OK) {
      return status;
    }
    s_writer.container_received += (uint32_t)size;
  }
  *out_written = input_size;
  return HAL_OK;
}

hal_status_t jh_rp_ota_storage_finish(void) {
  if (!s_writer.active || !s_writer.staging_ready ||
      s_writer.container_received != s_writer.container_size ||
      s_writer.payload_received != s_writer.manifest.payload_size) {
    return HAL_ESTATE;
  }
  hal_status_t status = flush_page();
  if (status != HAL_OK) {
    return status;
  }
  uint8_t digest[HAL_SHA256_DIGEST_BYTES];
  status = hal_sha256_final_ex(&s_writer.sha256, digest);
  if (status != HAL_OK ||
      memcmp(digest, s_writer.manifest.sha256, sizeof(digest)) != 0) {
    return status == HAL_OK ? HAL_EAUTH : status;
  }
  uint8_t flash_digest[HAL_SHA256_DIGEST_BYTES];
  status = digest_partition(&s_writer.staging, s_writer.manifest.payload_size,
                            flash_digest);
  if (status != HAL_OK || memcmp(flash_digest, s_writer.manifest.sha256,
                                 sizeof(flash_digest)) != 0) {
    return status == HAL_OK ? HAL_EIO : status;
  }

  jh_ota_boot_state_t state = {};
  uint8_t current_index = 0u;
  status = load_or_initialise_state(&state, &current_index);
  if (status != HAL_OK || state.mode != JH_OTA_BOOT_STABLE) {
    return status == HAL_OK ? HAL_ESTATE : status;
  }
  jh_rp_flash_partition_t phase = {};
  status =
      jh_rp_flash_storage_partition(JH_RP_FLASH_PARTITION_OTA_PHASE, &phase);
  if (status != HAL_OK) {
    return status;
  }
  status = jh_rp_flash_storage_erase(&phase, 0u, phase.size);
  if (status != HAL_OK) {
    return status;
  }
  state.sequence++;
  state.mode = JH_OTA_BOOT_PENDING;
  state.attempts = 0u;
  state.staging_size = s_writer.manifest.payload_size;
  state.staging_generation = s_writer.manifest.generation;
  memcpy(state.staging_sha256, s_writer.manifest.sha256,
         sizeof(state.staging_sha256));
  memcpy(state.staging_version, s_writer.manifest.version,
         sizeof(state.staging_version));
  status = write_state(&state, current_index);
  if (status == HAL_OK) {
    memset(&s_writer, 0, sizeof(s_writer));
  }
  return status;
}

void jh_rp_ota_storage_abort(void) { memset(&s_writer, 0, sizeof(s_writer)); }

hal_status_t jh_rp_ota_storage_confirm_boot(void) {
  jh_ota_boot_state_t state = {};
  uint8_t current_index = 0u;
  hal_status_t status = load_state(&state, &current_index);
  if (status != HAL_OK) {
    return status;
  }
  if (state.mode == JH_OTA_BOOT_STABLE) {
    return HAL_OK;
  }
  if (state.mode != JH_OTA_BOOT_TRIAL) {
    return HAL_ESTATE;
  }
  state.sequence++;
  state.mode = JH_OTA_BOOT_STABLE;
  state.attempts = 0u;
  return write_state(&state, current_index);
}

hal_status_t jh_rp_ota_storage_get_state(jh_ota_boot_state_t *out_state) {
  if (out_state == nullptr) {
    return HAL_EINVAL;
  }
  uint8_t selected = 0u;
  return load_state(out_state, &selected);
}

#endif
