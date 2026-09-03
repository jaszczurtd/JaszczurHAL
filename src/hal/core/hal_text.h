#pragma once

/** @file Portable bounded text and fixed-field helpers. */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

char *hal_text_format_binary_int(int value, char *buffer, size_t buffer_size);
hal_status_t hal_text_concat_ex(char *destination, size_t destination_size,
                                const char *first, const char *second);
bool hal_text_concat(char *destination, size_t destination_size,
                     const char *first, const char *second);
bool hal_text_is_printable(const char *text, size_t maximum_size);
hal_status_t hal_text_hex_pair_to_byte_ex(char high, char low,
                                          uint8_t *out_value);
uint8_t hal_text_hex_pair_to_byte(char high, char low);
hal_status_t hal_text_url_decode_ex(const char *source, char *destination,
                                    size_t destination_size,
                                    size_t *out_length);
void hal_text_url_decode(const char *source, char *destination);
void hal_text_remove_whitespace(char *text);
int hal_text_parse_number(const char **text);
bool hal_text_starts_with(const char *text, const char *prefix);
hal_status_t hal_text_transliterate_ascii_ex(const char *input, char *output,
                                             size_t output_size);
void hal_text_transliterate_ascii(const char *input, char *output,
                                  size_t output_size);
hal_status_t hal_text_pack_field_ex(uint8_t *buffer, size_t width,
                                    const char *text, uint8_t padding);

/** Pack a fixed-width field with a custom byte; invalid input is ignored. */
void hal_text_pack_field_pad(uint8_t *buffer, const char *text, int width,
                             uint8_t padding);

/** Pack a fixed-width field and fill unused bytes with zeroes. */
void hal_text_pack_field(uint8_t *buffer, const char *text, int width);

#ifdef __cplusplus
}
#endif
