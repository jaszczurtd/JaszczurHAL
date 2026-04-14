#pragma once

/**
 * @file hal_math.h
 * @brief Platform-independent math macros.
 *
 * These helpers are intentionally macro-based and type-independent so they can
 * be used from both C and C++ code through a single API name.
 */

/**
 * @brief Clamp a value to the range [lo, hi] (type-independent).
 *
 * Generic replacement for Arduino constrain().
 * No hardware dependency — safe to include anywhere.
 *
 * @note Arguments may be evaluated more than once. Avoid side effects.
 */
#ifndef hal_constrain
#define hal_constrain(v, lo, hi) (((v) < (lo)) ? (lo) : (((v) > (hi)) ? (hi) : (v)))
#endif

/**
 * @brief Re-map a value from one range to another (type-independent).
 *
 * Generic replacement for Arduino map().
 * No hardware dependency — safe to include anywhere.
 *
 * When @p in_min equals @p in_max the macro returns @p out_min to avoid
 * division by zero (matches the behaviour of mapfloat()).
 *
 * @note Arguments may be evaluated more than once. Avoid side effects.
 */
#ifndef hal_map
#define hal_map(x, in_min, in_max, out_min, out_max) \
    ((in_max) == (in_min) ? (out_min) : \
     (((x) - (in_min)) * ((out_max) - (out_min)) / ((in_max) - (in_min)) + (out_min)))
#endif
