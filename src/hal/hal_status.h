#pragma once

/**
 * @file hal_status.h
 * @brief Shared HAL status/result codes for new public APIs.
 *
 * Existing modules keep their current bool/NULL/void and module-local status
 * contracts for compatibility. New APIs may use hal_status_t directly, and
 * future compatibility wrappers can translate these values back to legacy
 * return shapes.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Common HAL operation status.
 *
 * Values are positive on success and negative on failure so callers can use
 * `status == HAL_OK` for success and `status < 0` for generic failure checks.
 * Names intentionally use the HAL_ prefix instead of POSIX errno names to avoid
 * collisions with errno.h and the BSD sockets compatibility layer.
 */
typedef enum {
  HAL_NONE = 0,      /**< No status / uninitialised / abnormal. */
  HAL_OK = 1,        /**< Operation completed successfully. */
  HAL_EINVAL = -1,   /**< Invalid argument or unsupported parameter value. */
  HAL_EBUSY = -2,    /**< Resource or bus is busy. */
  HAL_ETIMEOUT = -3, /**< Operation timed out. */
  HAL_EIO = -4,      /**< Generic device, bus or backend I/O error. */
  HAL_EUNSUPPORTED =
      -5,          /**< Operation is not supported by this target/backend. */
  HAL_ENOENT = -6, /**< Requested object, device or entry was not found. */
  HAL_EAGAIN = -7, /**< Try again later / nonblocking operation would block. */
  HAL_EOVERFLOW = -8, /**< Operation would overflow a buffer or resource. */
  HAL_ENOMEM = -9,    /**< Out of memory or resource slots. */
  HAL_IGNORED = -10,  /**< Operation was ignored (e.g. non-critical error). */
  HAL_EEXIST = -11,   /**< Object already exists (e.g. duplicate creation). */
  HAL_EPERM =
      -12, /**< Operation not permitted (e.g. insufficient privilege). */
  HAL_EINTERNAL = -13, /**< Internal error (e.g. unexpected state). */
  HAL_ECANCELED = -14, /**< Operation was canceled (e.g. by user request). */
  HAL_EPROTO = -15,    /**< Protocol error (e.g. unexpected response). */
  HAL_EAUTH = -16,     /**< Authentication or authorization failure. */
  HAL_EBUS = -17,      /**< Bus error (e.g. I2C/SPI transaction failure). */
  HAL_EHW =
      -18, /**< Hardware error (e.g. peripheral fault or misconfiguration). */
  HAL_ECONFIG = -19, /**< Configuration error (e.g. invalid setup or missing
                        dependency). */
  HAL_ESTATE = -20,  /**< Invalid state for the requested operation. */
  HAL_EUNINIT =
      -21, /**< Operation attempted on uninitialized object or subsystem. */
  HAL_EDEPRECATED = -22, /**< Operation is deprecated and should not be used. */
  HAL_EUNKNOWN = -23     /**< Unknown error. */
} hal_status_t;

/**
 * @brief Return a stable symbolic name for a HAL status code.
 *
 * Unknown numeric values return "HAL_STATUS_UNKNOWN".
 */
static inline const char *hal_status_to_string(hal_status_t status) {
  switch (status) {
  case HAL_NONE:
    return "HAL_NONE";
  case HAL_OK:
    return "HAL_OK";
  case HAL_EINVAL:
    return "HAL_EINVAL";
  case HAL_EBUSY:
    return "HAL_EBUSY";
  case HAL_ETIMEOUT:
    return "HAL_ETIMEOUT";
  case HAL_EIO:
    return "HAL_EIO";
  case HAL_EUNSUPPORTED:
    return "HAL_EUNSUPPORTED";
  case HAL_ENOENT:
    return "HAL_ENOENT";
  case HAL_EAGAIN:
    return "HAL_EAGAIN";
  case HAL_EOVERFLOW:
    return "HAL_EOVERFLOW";
  case HAL_ENOMEM:
    return "HAL_ENOMEM";
  case HAL_IGNORED:
    return "HAL_IGNORED";
  case HAL_EEXIST:
    return "HAL_EEXIST";
  case HAL_EPERM:
    return "HAL_EPERM";
  case HAL_EINTERNAL:
    return "HAL_EINTERNAL";
  case HAL_ECANCELED:
    return "HAL_ECANCELED";
  case HAL_EPROTO:
    return "HAL_EPROTO";
  case HAL_EAUTH:
    return "HAL_EAUTH";
  case HAL_EBUS:
    return "HAL_EBUS";
  case HAL_EHW:
    return "HAL_EHW";
  case HAL_ECONFIG:
    return "HAL_ECONFIG";
  case HAL_ESTATE:
    return "HAL_ESTATE";
  case HAL_EUNINIT:
    return "HAL_EUNINIT";
  case HAL_EDEPRECATED:
    return "HAL_EDEPRECATED";
  case HAL_EUNKNOWN:
    return "HAL_EUNKNOWN";
  default:
    return "HAL_STATUS_UNKNOWN";
  }
}

/** @brief Return true when @p status represents a successful operation. */
static inline bool hal_status_is_ok(hal_status_t status) {
  return status == HAL_OK;
}

/** @brief Return true when @p status represents a failure. */
static inline bool hal_status_is_error(hal_status_t status) {
  return status < HAL_NONE;
}

/** @brief Convert a legacy boolean result into a HAL status code. */
static inline hal_status_t hal_status_from_bool(bool ok,
                                                hal_status_t error_status) {
  return ok ? HAL_OK : error_status;
}

/** @brief Convert a HAL status code into the legacy boolean success shape. */
static inline bool hal_status_to_bool(hal_status_t status) {
  return hal_status_is_ok(status);
}

#ifdef __cplusplus
}
#endif
