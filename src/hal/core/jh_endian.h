#pragma once

/**
 * @file jh_endian.h
 * @brief Unaligned byte-order and integer byte-swap helpers.
 *
 * The helpers are defined once in jhbase (jb/endian.h), shared with the jh
 * tools; this header keeps the HAL names as aliases of those definitions, the
 * way hal_status.h includes jb/status.h. Loads and stores access single bytes,
 * so buffers need no alignment. They do not validate pointers: callers provide
 * storage large enough for the selected integer width.
 */

#include <jb/endian.h>
#include <stdint.h>

/** @name HAL names of the jhbase byte-order helpers */
/** @{ */
/** @brief Reverse the byte order of a 16-bit value; alias of jb_bswap16(). */
#define jh_bswap16 jb_bswap16
/** @brief Reverse the byte order of a 32-bit value; alias of jb_bswap32(). */
#define jh_bswap32 jb_bswap32
/** @brief Reverse the byte order of a 64-bit value; alias of jb_bswap64(). */
#define jh_bswap64 jb_bswap64
/** @brief Bits 15..8 of a 16-bit value; alias of jb_u16_msb(). */
#define jh_u16_msb jb_u16_msb
/** @brief Bits 7..0 of a 16-bit value; alias of jb_u16_lsb(). */
#define jh_u16_lsb jb_u16_lsb
/** @brief Join the high and low byte into a 16-bit value; alias of
 * jb_u16_from_bytes(). */
#define jh_u16_from_bytes jb_u16_from_bytes
/** @brief Load a little-endian 16-bit value from any address; alias of
 * jb_load_le16(). */
#define jh_load_le16 jb_load_le16
/** @brief Load a little-endian 32-bit value from any address; alias of
 * jb_load_le32(). */
#define jh_load_le32 jb_load_le32
/** @brief Load a little-endian 64-bit value from any address; alias of
 * jb_load_le64(). */
#define jh_load_le64 jb_load_le64
/** @brief Store a 16-bit value in little-endian order at any address; alias
 * of jb_store_le16(). */
#define jh_store_le16 jb_store_le16
/** @brief Store a 32-bit value in little-endian order at any address; alias
 * of jb_store_le32(). */
#define jh_store_le32 jb_store_le32
/** @brief Store a 64-bit value in little-endian order at any address; alias
 * of jb_store_le64(). */
#define jh_store_le64 jb_store_le64
/** @brief Load a big-endian 16-bit value from any address; alias of
 * jb_load_be16(). */
#define jh_load_be16 jb_load_be16
/** @brief Load a big-endian 32-bit value from any address; alias of
 * jb_load_be32(). */
#define jh_load_be32 jb_load_be32
/** @brief Load a big-endian 64-bit value from any address; alias of
 * jb_load_be64(). */
#define jh_load_be64 jb_load_be64
/** @brief Store a 16-bit value in big-endian order at any address; alias of
 * jb_store_be16(). */
#define jh_store_be16 jb_store_be16
/** @brief Store a 32-bit value in big-endian order at any address; alias of
 * jb_store_be32(). */
#define jh_store_be32 jb_store_be32
/** @brief Store a 64-bit value in big-endian order at any address; alias of
 * jb_store_be64(). */
#define jh_store_be64 jb_store_be64
/** @} */

#ifdef __cplusplus
extern "C" {
#endif

/** @name Source-compatible 16-bit helpers */
/** @{ */
/**
 * @brief Linkable form of MSB().
 * @param value Input value; only its least significant 16 bits are used.
 * @return Bits 15..8 of @p value.
 */
uint8_t(MSB)(unsigned short value);
/**
 * @brief Linkable form of LSB().
 * @param value Input value; only its least significant 16 bits are used.
 * @return Bits 7..0 of @p value.
 */
uint8_t(LSB)(unsigned short value);
/** @} */

#ifdef __cplusplus
}
#endif

/**
 * @def MSB(value)
 * @brief Return the most significant byte of a 16-bit value.
 * @param value Integer expression converted to `uint16_t` and evaluated once.
 * @return Bits 15..8 of @p value.
 */
#ifndef MSB
#define MSB(value) jh_u16_msb((uint16_t)(value))
#endif

/**
 * @def LSB(value)
 * @brief Return the least significant byte of a 16-bit value.
 * @param value Integer expression converted to `uint16_t` and evaluated once.
 * @return Bits 7..0 of @p value.
 */
#ifndef LSB
#define LSB(value) jh_u16_lsb((uint16_t)(value))
#endif
