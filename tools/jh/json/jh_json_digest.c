#include "jh_json_digest.h"
#include "hal/security/jh_sha256.h"

static hal_status_t sha_sink(void *user, const char *data, size_t length) {
  jh_sha256_update(user, (const uint8_t *)data, length);
  return HAL_OK;
}

void jh_sha256_hex(const uint8_t digest[JH_SHA256_DIGEST_BYTES], char out[65]) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < JH_SHA256_DIGEST_BYTES; ++i) {
    out[2 * i] = hex[digest[i] >> 4];
    out[2 * i + 1] = hex[digest[i] & 15];
  }
  out[64] = '\0';
}

/* The digest of a tree value: compact, sorted keys, ASCII escapes. */
hal_status_t jh_json_digest(jb_json_tree_view_t value, bool ensure_ascii,
                            char out[65]) {
  if (out == NULL)
    return HAL_EINVAL;
  jh_sha256_context_t context;
  jh_sha256_init(&context);
  const jb_json_tree_format_t format = {SIZE_MAX, 0, true, ensure_ascii};
  size_t written = 0;
  const hal_status_t status =
      jb_json_tree_format(value, &format, sha_sink, &context, &written);
  uint8_t digest[JH_SHA256_DIGEST_BYTES];
  jh_sha256_final(&context, digest);
  if (status == HAL_OK)
    jh_sha256_hex(digest, out);
  return status;
}
