#pragma once

/**
 * @file jh_endian.h
 * @brief Unaligned byte-order and integer byte-swap helpers.
 *
 * Load and store helpers intentionally do not validate pointers. Callers must
 * provide storage large enough for the selected integer width, matching the
 * semantics of direct byte indexing used by the firmware previously.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint16_t jh_bswap16(uint16_t value) {
  return (uint16_t)((uint16_t)(value << 8u) | (uint16_t)(value >> 8u));
}

static inline uint32_t jh_bswap32(uint32_t value) {
  return ((value & UINT32_C(0x000000FF)) << 24u) |
         ((value & UINT32_C(0x0000FF00)) << 8u) |
         ((value & UINT32_C(0x00FF0000)) >> 8u) |
         ((value & UINT32_C(0xFF000000)) >> 24u);
}

static inline uint64_t jh_bswap64(uint64_t value) {
  return ((value & UINT64_C(0x00000000000000FF)) << 56u) |
         ((value & UINT64_C(0x000000000000FF00)) << 40u) |
         ((value & UINT64_C(0x0000000000FF0000)) << 24u) |
         ((value & UINT64_C(0x00000000FF000000)) << 8u) |
         ((value & UINT64_C(0x000000FF00000000)) >> 8u) |
         ((value & UINT64_C(0x0000FF0000000000)) >> 24u) |
         ((value & UINT64_C(0x00FF000000000000)) >> 40u) |
         ((value & UINT64_C(0xFF00000000000000)) >> 56u);
}

/** @brief Return the most significant byte of a 16-bit value. */
static inline uint8_t jh_u16_msb(uint16_t value) {
  return (uint8_t)(value >> 8u);
}

/** @brief Return the least significant byte of a 16-bit value. */
static inline uint8_t jh_u16_lsb(uint16_t value) { return (uint8_t)value; }

static inline uint16_t jh_load_le16(const uint8_t *input) {
  return (uint16_t)input[0] | (uint16_t)((uint16_t)input[1] << 8u);
}

static inline uint32_t jh_load_le32(const uint8_t *input) {
  return (uint32_t)input[0] | ((uint32_t)input[1] << 8u) |
         ((uint32_t)input[2] << 16u) | ((uint32_t)input[3] << 24u);
}

static inline uint64_t jh_load_le64(const uint8_t *input) {
  return (uint64_t)jh_load_le32(input) |
         ((uint64_t)jh_load_le32(input + 4u) << 32u);
}

static inline void jh_store_le16(uint8_t *output, uint16_t value) {
  output[0] = (uint8_t)value;
  output[1] = (uint8_t)(value >> 8u);
}

static inline void jh_store_le32(uint8_t *output, uint32_t value) {
  output[0] = (uint8_t)value;
  output[1] = (uint8_t)(value >> 8u);
  output[2] = (uint8_t)(value >> 16u);
  output[3] = (uint8_t)(value >> 24u);
}

static inline void jh_store_le64(uint8_t *output, uint64_t value) {
  jh_store_le32(output, (uint32_t)value);
  jh_store_le32(output + 4u, (uint32_t)(value >> 32u));
}

static inline uint16_t jh_load_be16(const uint8_t *input) {
  return (uint16_t)((uint16_t)input[0] << 8u) | (uint16_t)input[1];
}

/** @brief Build a 16-bit value from explicit most/least significant bytes. */
static inline uint16_t jh_u16_from_bytes(uint8_t msb, uint8_t lsb) {
  const uint8_t bytes[] = {msb, lsb};
  return jh_load_be16(bytes);
}

static inline uint32_t jh_load_be32(const uint8_t *input) {
  return ((uint32_t)input[0] << 24u) | ((uint32_t)input[1] << 16u) |
         ((uint32_t)input[2] << 8u) | (uint32_t)input[3];
}

static inline uint64_t jh_load_be64(const uint8_t *input) {
  return ((uint64_t)jh_load_be32(input) << 32u) |
         (uint64_t)jh_load_be32(input + 4u);
}

static inline void jh_store_be16(uint8_t *output, uint16_t value) {
  output[0] = (uint8_t)(value >> 8u);
  output[1] = (uint8_t)value;
}

static inline void jh_store_be32(uint8_t *output, uint32_t value) {
  output[0] = (uint8_t)(value >> 24u);
  output[1] = (uint8_t)(value >> 16u);
  output[2] = (uint8_t)(value >> 8u);
  output[3] = (uint8_t)value;
}

static inline void jh_store_be64(uint8_t *output, uint64_t value) {
  jh_store_be32(output, (uint32_t)(value >> 32u));
  jh_store_be32(output + 4u, (uint32_t)value);
}

/** @name Source-compatible 16-bit helpers */
/** @{ */
uint8_t(MSB)(unsigned short value);
uint8_t(LSB)(unsigned short value);
/** @} */

#ifdef __cplusplus
}
#endif

/* Public compatibility names used by existing firmware. Inline functions make
 * each macro argument evaluate exactly once. */
#ifndef MSB
#define MSB(value) jh_u16_msb((uint16_t)(value))
#endif

#ifndef LSB
#define LSB(value) jh_u16_lsb((uint16_t)(value))
#endif
