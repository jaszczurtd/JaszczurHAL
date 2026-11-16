#ifndef JH_FIRMWARE_INTERNAL_H
#define JH_FIRMWARE_INTERNAL_H
#include "jh_firmware_read.h"

#define JH_FIRMWARE_MAX_BYTES (64u * 1024u * 1024u)
#define JH_FIRMWARE_METADATA_BYTES (16u * 1024u * 1024u)

typedef struct {
  uint64_t begin, end;
} jh_flash_region_t;
typedef struct {
  const jh_firmware_options_t *options;
  jh_failure_t *failure;
  jh_json_location_t *location;
  jb_vector_t strings;
  size_t string_bytes;
  const char *directory, *tool, *target, *interface_file, *target_file;
  bool locate_only;
  jh_flash_region_t regions[96];
  size_t region_count;
  uint32_t address, size;
} jh_firmware_plan_t;

hal_status_t jh_firmware_prepare(jh_firmware_plan_t *plan);
const char *jh_firmware_text(jh_firmware_plan_t *plan, jb_json_span_t value);
const char *jh_firmware_path(jh_firmware_plan_t *plan, const char *base,
                             const char *name);
void jh_firmware_sha256(const void *data, size_t length, char out[65]);
#endif
