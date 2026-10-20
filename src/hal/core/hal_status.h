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
 */

#include <jb/status.h>
