#pragma once

/**
 * @file hal_bits.h
 * @brief Bit-manipulation macros.
 *
 * This header intentionally exposes type-independent macros (not fixed-width
 * typed functions) to keep backward compatibility with mixed 8/16/32-bit
 * project variables.
 */

/** @def is_set
 *  @brief True when any bit from @p mask is set in @p x.
 *  @note Macro arguments may be evaluated more than once.
 */
#ifndef is_set
#define is_set(x, mask)      (((x) & (mask)) != 0u)
#endif

/** @def set_bit
 *  @brief Set masked bits in-place.
 *  @note Macro arguments may be evaluated more than once.
 */
#ifndef set_bit
#define set_bit(var, mask)   do { (var) |=  (mask); } while (0)
#endif

/** @def clr_bit
 *  @brief Clear masked bits in-place.
 *  @note Macro arguments may be evaluated more than once.
 */
#ifndef clr_bit
#define clr_bit(var, mask)   do { (var) &= ~(mask); } while (0)
#endif

/** @def bitSet
 *  @brief Arduino-compatible alias: set single bit in-place.
 *  @note Macro arguments may be evaluated more than once.
 */
#ifndef bitSet
#define bitSet(var, bit)     do { (var) |=  (1u << (bit)); } while (0)
#endif

/** @def bitClear
 *  @brief Arduino-compatible alias: clear single bit in-place.
 *  @note Macro arguments may be evaluated more than once.
 */
#ifndef bitClear
#define bitClear(var, bit)   do { (var) &= ~(1u << (bit)); } while (0)
#endif

/** @def bitRead
 *  @brief Arduino-compatible alias: read single bit (0/1).
 *  @note Macro arguments may be evaluated more than once.
 */
#ifndef bitRead
#define bitRead(var, bit)    (((var) >> (bit)) & 1u)
#endif

/** @def set_bit_v
 *  @brief Set masked bits through a volatile pointer.
 *  @note Macro arguments may be evaluated more than once.
 */
#ifndef set_bit_v
#define set_bit_v(reg, mask) do { (*(reg)) |=  (mask); } while (0)
#endif

/** @def clr_bit_v
 *  @brief Clear masked bits through a volatile pointer.
 *  @note Macro arguments may be evaluated more than once.
 */
#ifndef clr_bit_v
#define clr_bit_v(reg, mask) do { (*(reg)) &= ~(mask); } while (0)
#endif
