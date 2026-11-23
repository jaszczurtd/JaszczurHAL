#ifndef JH_BUILD_METADATA_H
#define JH_BUILD_METADATA_H

/** @file jh_build_metadata.h
 * Readers of the files a completed build leaves behind, shared by
 * `jh firmware read` and the ESP-IDF backend (`jh esp`): CMakeCache.txt
 * entries, the line structure of text metadata and the integers of
 * flasher_args.json and linker maps. The JSON documents themselves
 * (jh_esp_idf_artifacts.json, flasher_args.json, project descriptions) are
 * read with jh_json_read_value. No function allocates. C11.
 */
#include "jh_failure.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Take the next line of input, without its LF and a CR before it, and
 * advance input past it. False when input is empty. The line borrows input;
 * no terminating NUL is assumed. */
bool jh_metadata_next_line(jb_json_span_t *input, jb_json_span_t *line);

/** Find the CMakeCache.txt entry key:TYPE=VALUE in cache and borrow VALUE
 * into value. found reports whether the entry exists (value is empty when
 * not). HAL_ECONFIG for an entry without '=' or a repeated one, HAL_EPROTO
 * for a value holding NUL; HAL_EINVAL for missing arguments. */
hal_status_t jh_cmake_cache_find(jb_json_span_t cache, const char *key,
                                 jb_json_span_t *value, bool *found);

/** A nonempty decimal or 0x/0X hexadecimal unsigned integer that fits 64
 * bits, without sign, spaces or separators. */
bool jh_metadata_unsigned(jb_json_span_t text, uint64_t *out);

#ifdef __cplusplus
}
#endif
#endif
