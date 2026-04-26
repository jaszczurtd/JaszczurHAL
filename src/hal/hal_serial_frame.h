#pragma once

/**
 * @file hal_serial_frame.h
 * @brief Wire framing for the Fiesta SerialConfigurator (SC) protocol.
 *
 * Frame format (both directions):
 * @code
 *   $SC,<seq>,<payload>*<crc8>\n
 * @endcode
 *
 * - Literal start sentinel: `$SC,`
 * - `<seq>`: ASCII unsigned decimal in [0..65535] (request id; the response
 *   carries the same value so the host can correlate replies).
 * - `<payload>`: free-form ASCII text without `*`, `\r` or `\n`.
 * - Literal `*` separator.
 * - `<crc8>`: two uppercase hex digits, CRC-8/CCITT (poly 0x07, init 0x00,
 *   no reflect, no xor-out) computed over the bytes between (but excluding)
 *   the leading `$` and the `*` (i.e. over `SC,<seq>,<payload>`).
 * - Trailing `\n` line terminator.
 *
 * This header is shared verbatim between firmware and the host
 * SerialConfigurator. The configurator carries a stand-alone copy with
 * matching constants and CRC parameters; do not change one without the
 * other.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Literal frame start including the trailing comma. */
#define HAL_SERIAL_FRAME_PREFIX "$SC,"

/** Length of @ref HAL_SERIAL_FRAME_PREFIX (excluding NUL). */
#define HAL_SERIAL_FRAME_PREFIX_LEN 4u

/** Maximum payload size (without framing overhead, without NUL). */
#define HAL_SERIAL_FRAME_PAYLOAD_MAX 256u

/** Maximum framed line size (includes NUL terminator, excludes `\n`). */
#define HAL_SERIAL_FRAME_LINE_MAX (HAL_SERIAL_FRAME_PAYLOAD_MAX + 32u)

/**
 * @brief Compute CRC-8/CCITT (poly 0x07, init 0x00) over @p data.
 */
static inline uint8_t hal_serial_frame_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00u;
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (uint8_t)((crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u)
                                          : (uint8_t)(crc << 1));
        }
    }
    return crc;
}

/**
 * @brief Build a single framed line `$SC,<seq>,<payload>*<crc>` (no `\n`).
 *
 * The trailing newline is intentionally omitted so callers may append it
 * (or use `hal_serial_println` which already adds it).
 *
 * @param seq          Sequence number to embed (0..65535).
 * @param payload      Payload string (must be NUL-terminated, must not
 *                     contain `*`, `\r` or `\n`).
 * @param out          Output buffer.
 * @param out_size     Size of output buffer in bytes.
 * @param out_len      Optional, receives the number of written bytes
 *                     (excluding the NUL terminator). May be NULL.
 * @return true on success, false on invalid arguments / buffer too small.
 */
static inline bool hal_serial_frame_encode(uint16_t seq,
                                           const char *payload,
                                           char *out,
                                           size_t out_size,
                                           size_t *out_len) {
    if (payload == NULL || out == NULL || out_size < 12u) {
        return false;
    }

    for (const char *p = payload; *p != '\0'; ++p) {
        const char c = *p;
        if (c == '*' || c == '\r' || c == '\n') {
            return false;
        }
    }

    /* Build everything up to (but excluding) the `*`. */
    int written = snprintf(out, out_size, "%s%u,%s",
                           HAL_SERIAL_FRAME_PREFIX, (unsigned)seq, payload);
    if (written < 0 || (size_t)written >= out_size) {
        return false;
    }

    /* CRC is computed over `SC,<seq>,<payload>` (i.e. skip the leading `$`). */
    const uint8_t crc = hal_serial_frame_crc8((const uint8_t *)out + 1,
                                              (size_t)written - 1u);

    const int extra = snprintf(out + written, out_size - (size_t)written,
                               "*%02X", (unsigned)crc);
    if (extra < 0 || (size_t)extra >= out_size - (size_t)written) {
        return false;
    }

    if (out_len != NULL) {
        *out_len = (size_t)(written + extra);
    }
    return true;
}

/**
 * @brief Parse one framed line into (seq, payload).
 *
 * Accepts a trimmed input line (no leading whitespace, no trailing CR/LF).
 * The line must start with @ref HAL_SERIAL_FRAME_PREFIX. CRC and structural
 * validation are performed; on failure the function returns false and
 * @p payload_out is set to an empty string.
 *
 * @param line             NUL-terminated trimmed line to parse.
 * @param seq_out          Receives the parsed sequence number.
 * @param payload_out      Receives the inner payload (NUL-terminated).
 * @param payload_out_size Size of @p payload_out in bytes.
 * @return true on a structurally valid frame with matching CRC.
 */
static inline bool hal_serial_frame_decode(const char *line,
                                           uint16_t *seq_out,
                                           char *payload_out,
                                           size_t payload_out_size) {
    if (line == NULL || seq_out == NULL || payload_out == NULL ||
        payload_out_size == 0u) {
        return false;
    }

    payload_out[0] = '\0';

    if (strncmp(line, HAL_SERIAL_FRAME_PREFIX, HAL_SERIAL_FRAME_PREFIX_LEN) != 0) {
        return false;
    }

    const char *seq_begin = line + HAL_SERIAL_FRAME_PREFIX_LEN;
    const char *seq_end = seq_begin;
    while (*seq_end >= '0' && *seq_end <= '9') {
        ++seq_end;
    }
    if (seq_end == seq_begin || *seq_end != ',') {
        return false;
    }

    /* Parse seq into uint32 first to catch overflow safely. */
    uint32_t seq_acc = 0u;
    for (const char *p = seq_begin; p < seq_end; ++p) {
        seq_acc = seq_acc * 10u + (uint32_t)(*p - '0');
        if (seq_acc > 0xFFFFu) {
            return false;
        }
    }

    const char *payload_begin = seq_end + 1; /* skip ',' */
    const char *star = strchr(payload_begin, '*');
    if (star == NULL) {
        return false;
    }

    /* Expect exactly two hex digits after '*'. */
    if (star[1] == '\0' || star[2] == '\0' || star[3] != '\0') {
        return false;
    }

    uint8_t expected_crc = 0u;
    for (int i = 1; i <= 2; ++i) {
        const char c = star[i];
        uint8_t nibble;
        if (c >= '0' && c <= '9') {
            nibble = (uint8_t)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            nibble = (uint8_t)(10 + (c - 'A'));
        } else if (c >= 'a' && c <= 'f') {
            nibble = (uint8_t)(10 + (c - 'a'));
        } else {
            return false;
        }
        expected_crc = (uint8_t)((expected_crc << 4) | nibble);
    }

    /* CRC is computed over bytes between (and excluding) the leading '$'
     * and the '*' separator. */
    const size_t crc_len = (size_t)(star - (line + 1));
    const uint8_t actual_crc = hal_serial_frame_crc8(
        (const uint8_t *)(line + 1), crc_len);
    if (actual_crc != expected_crc) {
        return false;
    }

    const size_t payload_len = (size_t)(star - payload_begin);
    if (payload_len + 1u > payload_out_size) {
        return false;
    }
    memcpy(payload_out, payload_begin, payload_len);
    payload_out[payload_len] = '\0';

    *seq_out = (uint16_t)seq_acc;
    return true;
}

#ifdef __cplusplus
}
#endif
