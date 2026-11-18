#ifndef JH_JSON_DIGEST_H
#define JH_JSON_DIGEST_H

/** @file jh_json_digest.h
 * SHA-256 digests in hex: of bytes, and of compact JSON with sorted keys for
 * configuration identities.
 */
#include <jb/json_tree.h>

#ifdef __cplusplus
extern "C" {
#endif
/** Encode a 32-byte SHA-256 digest as 64 lowercase digits and a terminator.
 * Both buffers are required and must not overlap. */
void jh_sha256_hex(const uint8_t digest[32], char out[65]);
/** SHA-256 of length bytes as 64 lowercase digits and a terminator. data may
 * be NULL only when length is zero; out is required. */
void jh_sha256_data_hex(const void *data, size_t length, char out[65]);
/** Hash a tree without altering its numeric tokens. ensure_ascii selects
 * escaped Unicode (feature registry) or raw UTF-8 (hardware). out is required
 * and changes only on success. */
hal_status_t jh_json_digest(jb_json_tree_view_t value, bool ensure_ascii,
                            char out[65]);
#ifdef __cplusplus
}
#endif
#endif
