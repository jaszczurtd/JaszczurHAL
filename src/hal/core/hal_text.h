#pragma once

/** @file Portable bounded text and fixed-field helpers. */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Format an integer as an 8-, 16-, or 32-bit binary string.
 *
 * Positive values use the smallest supported width. Negative values use the
 * native `int` width. A short buffer receives a terminated low-bit suffix.
 *
 * @param value Integer to format.
 * @param buffer Destination buffer.
 * @param buffer_size Destination capacity including the terminator.
 * @return @p buffer, including when it is NULL.
 */
char *hal_text_format_binary_int(int value, char *buffer, size_t buffer_size);

/**
 * @brief Concatenate two strings into a bounded destination.
 * @param destination Destination buffer.
 * @param destination_size Capacity including the terminator.
 * @param first First input string.
 * @param second Second input string.
 * @return HAL_OK, HAL_EINVAL for invalid input, or HAL_EOVERFLOW when the
 * result does not fit. Overflow leaves @p destination unchanged.
 */
hal_status_t hal_text_concat_ex(char *destination, size_t destination_size,
                                const char *first, const char *second);

/**
 * @brief Concatenate two strings and report success as a boolean.
 * @param destination Destination buffer.
 * @param destination_size Capacity including the terminator.
 * @param first First input string.
 * @param second Second input string.
 * @return true when hal_text_concat_ex() returns HAL_OK.
 */
bool hal_text_concat(char *destination, size_t destination_size,
                     const char *first, const char *second);

/**
 * @brief Validate a non-empty, terminated string containing text or whitespace.
 * @param text String to validate.
 * @param maximum_size Maximum number of bytes inspected.
 * @return true when a terminator is found within the limit and all preceding
 * bytes satisfy `isprint()` or `isspace()`.
 */
bool hal_text_is_printable(const char *text, size_t maximum_size);

/**
 * @brief Decode two hexadecimal characters into one byte.
 * @param high Most significant hexadecimal digit.
 * @param low Least significant hexadecimal digit.
 * @param out_value Receives the decoded byte.
 * @return HAL_OK, or HAL_EINVAL for a NULL output or invalid digit.
 */
hal_status_t hal_text_hex_pair_to_byte_ex(char high, char low,
                                          uint8_t *out_value);

/**
 * @brief Decode two characters using the established unchecked conversion.
 * @param high Most significant character.
 * @param low Least significant character.
 * @return Combined byte. Use hal_text_hex_pair_to_byte_ex() when validation is
 * required.
 */
uint8_t hal_text_hex_pair_to_byte(char high, char low);

/**
 * @brief Decode percent escapes and plus-as-space URL form encoding.
 *
 * Malformed percent sequences are copied unchanged. On overflow, the decoded
 * prefix is terminated and its length is reported.
 *
 * @param source Null-terminated encoded input.
 * @param destination Destination buffer.
 * @param destination_size Capacity including the terminator.
 * @param out_length Optional output for the number of decoded bytes.
 * @return HAL_OK, HAL_EINVAL for invalid input, or HAL_EOVERFLOW.
 */
hal_status_t hal_text_url_decode_ex(const char *source, char *destination,
                                    size_t destination_size,
                                    size_t *out_length);

/**
 * @brief Decode URL form text into a caller-sized destination.
 * @param source Null-terminated encoded input.
 * @param destination Destination with at least `strlen(source) + 1` bytes.
 */
void hal_text_url_decode(const char *source, char *destination);

/**
 * @brief Remove all `isspace()` characters from a string in place.
 * @param text Mutable null-terminated string; NULL is ignored.
 */
void hal_text_remove_whitespace(char *text);

/**
 * @brief Parse consecutive decimal digits and advance an input cursor.
 * @param text In/out cursor into a null-terminated string.
 * @return Parsed non-negative integer, or zero when no digit is present or the
 * cursor is invalid.
 */
int hal_text_parse_number(const char **text);

/**
 * @brief Test whether a string starts with a prefix.
 * @param text String to inspect.
 * @param prefix Prefix to match; an empty prefix matches every string.
 * @return true on a match, or false for NULL input.
 */
bool hal_text_starts_with(const char *text, const char *prefix);

/**
 * @brief Copy ASCII text and transliterate supported Polish UTF-8 letters.
 *
 * Unsupported non-ASCII input is omitted. The output is always terminated
 * when @p output_size is non-zero, including on overflow.
 *
 * @param input Null-terminated UTF-8 input.
 * @param output Destination buffer.
 * @param output_size Capacity including the terminator.
 * @return HAL_OK, HAL_EINVAL for invalid input, or HAL_EOVERFLOW.
 */
hal_status_t hal_text_transliterate_ascii_ex(const char *input, char *output,
                                             size_t output_size);

/**
 * @brief Convenience form of hal_text_transliterate_ascii_ex().
 * @param input Null-terminated UTF-8 input.
 * @param output Destination buffer.
 * @param output_size Capacity including the terminator.
 */
void hal_text_transliterate_ascii(const char *input, char *output,
                                  size_t output_size);

/**
 * @brief Pack text into a fixed-width byte field.
 * @param buffer Destination field.
 * @param width Exact number of bytes written.
 * @param text Null-terminated text, truncated when longer than @p width.
 * @param padding Byte used for unused positions.
 * @return HAL_OK, or HAL_EINVAL for invalid input or zero width.
 */
hal_status_t hal_text_pack_field_ex(uint8_t *buffer, size_t width,
                                    const char *text, uint8_t padding);

/**
 * @brief Pack a fixed-width field with a custom byte.
 * @param buffer Destination field.
 * @param text Null-terminated text.
 * @param width Exact number of bytes written; non-positive values are ignored.
 * @param padding Byte used for unused positions.
 */
void hal_text_pack_field_pad(uint8_t *buffer, const char *text, int width,
                             uint8_t padding);

/**
 * @brief Pack a fixed-width field and fill unused bytes with zeroes.
 * @param buffer Destination field.
 * @param text Null-terminated text.
 * @param width Exact number of bytes written; non-positive values are ignored.
 */
void hal_text_pack_field(uint8_t *buffer, const char *text, int width);

#ifdef __cplusplus
}
#endif
