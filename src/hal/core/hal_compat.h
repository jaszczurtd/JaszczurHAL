#pragma once

/**
 * @file hal_compat.h
 * @brief Portable source-compatibility helpers for JaszczurHAL consumers.
 */

/**
 * @def PROGMEM
 * @brief No-op on platforms without a separate flash address space.
 *
 * JaszczurHAL targets use a unified address space, so this expands to nothing.
 */
#ifndef PROGMEM
#define PROGMEM /* no-op on platforms without separate flash address space */
#endif

/**
 * @def F(s)
 * @brief No-op identity macro for flash-string literals.
 *
 * JaszczurHAL targets use a unified address space, so this returns the string
 * pointer unchanged.
 */
#ifndef F
#define F(s) (s)
#endif

/**
 * @def hal_min(a, b)
 * @brief Type-generic minimum of two values.
 *
 * Safe drop-in for Arduino's min() macro. Arguments with side effects should
 * not be passed because this macro may evaluate one argument twice.
 */
#ifndef hal_min
#define hal_min(a, b) (((a) < (b)) ? (a) : (b))
#endif

/**
 * @def hal_max(a, b)
 * @brief Type-generic maximum of two values.
 *
 * Safe drop-in for Arduino's max() macro. Arguments with side effects should
 * not be passed because this macro may evaluate one argument twice.
 */
#ifndef hal_max
#define hal_max(a, b) (((a) > (b)) ? (a) : (b))
#endif
