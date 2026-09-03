#pragma once

/**
 * @file hal_array.h
 * @brief Compile-time helpers for fixed-size C and C++ arrays.
 */

/**
 * @def COUNTOF(arr)
 * @brief Number of elements in a statically allocated array.
 *
 * This compatibility macro is the primary JaszczurHAL spelling used by
 * firmware and downstream applications.
 *
 * @note Works only for actual arrays. Passing a pointer yields an incorrect
 *       result.
 */
#ifndef COUNTOF
#define COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif
