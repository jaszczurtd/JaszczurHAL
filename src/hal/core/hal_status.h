#pragma once

/**
 * @file hal_status.h
 * @brief Shared HAL status/result codes for new public APIs.
 *
 * hal_status_t, its HAL_* values and the conversion helpers are defined once
 * in jhbase (jb/status.h), shared with the jh tools; this header keeps the HAL
 * include path. The type is 32 bits wide on every compiler and target.
 * Existing modules keep their current bool/NULL/void and module-local status
 * contracts for compatibility.
 *
 * The header also brings JB_STATIC_ASSERT(condition, message) from jb/abi.h:
 * the compile-time assertion for code shared between C and C++. Use it
 * instead of a local static_assert/_Static_assert switch; plain C++ sources
 * may keep static_assert.
 */

#include <jb/abi.h>
#include <jb/status.h>
