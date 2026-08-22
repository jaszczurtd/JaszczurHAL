#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_OTA

#include "hal/core/hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_ota.h
 * @brief Thread-safe OTA service with HAL socket transport and callback
 * dispatch from hal_ota_handle().
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  HAL_OTA_COMMAND_SKETCH = 0,
  HAL_OTA_COMMAND_FILESYSTEM = 1,
  HAL_OTA_COMMAND_UNKNOWN = 255
} hal_ota_command_t;

typedef enum {
  HAL_OTA_ERROR_AUTH = 1,
  HAL_OTA_ERROR_BEGIN = 2,
  HAL_OTA_ERROR_CONNECT = 3,
  HAL_OTA_ERROR_RECEIVE = 4,
  HAL_OTA_ERROR_END = 5,
  HAL_OTA_ERROR_UNKNOWN = 255
} hal_ota_error_t;

typedef void (*hal_ota_on_start_callback_t)(hal_ota_command_t command,
                                            void *user);
typedef void (*hal_ota_on_end_callback_t)(void *user);
typedef void (*hal_ota_on_progress_callback_t)(uint32_t progress,
                                               uint32_t total, void *user);
typedef void (*hal_ota_on_error_callback_t)(hal_ota_error_t error, void *user);

#define HAL_OTA_VERSION_TEXT_SIZE 32u

typedef enum {
  HAL_OTA_BOOT_STABLE = 0,
  HAL_OTA_BOOT_PENDING = 1,
  HAL_OTA_BOOT_TRIAL = 2,
  HAL_OTA_BOOT_ROLLBACK = 3,
  HAL_OTA_BOOT_RECOVERY = 4
} hal_ota_boot_mode_t;

typedef struct {
  hal_ota_boot_mode_t mode;
  uint8_t attempts;
  uint8_t max_attempts;
  uint32_t program_generation;
  uint32_t staging_generation;
  char program_version[HAL_OTA_VERSION_TEXT_SIZE];
  char staging_version[HAL_OTA_VERSION_TEXT_SIZE];
} hal_ota_boot_info_t;

/**
 * @brief Set OTA UDP port.
 * @param port OTA port (>0).
 * @return true when accepted.
 */
bool hal_ota_set_port(uint16_t port);

/**
 * @brief Set OTA hostname.
 * @param hostname Null-terminated hostname.
 * @return true when accepted.
 */
bool hal_ota_set_hostname(const char *hostname);

/**
 * @brief Set the password used by AUTH2 and target image authentication.
 *
 * Omit this call or pass an empty string to use the explicitly
 * unauthenticated development flow.
 *
 * @param password Null-terminated password string (may be empty).
 * @return true when accepted; false on invalid input or an internal failure.
 */
bool hal_ota_set_password(const char *password);

/**
 * @brief Register start callback (pass NULL to unregister).
 * @param callback Callback function pointer.
 * @param user Opaque user pointer passed to callback.
 * @return true when accepted.
 */
bool hal_ota_on_start(hal_ota_on_start_callback_t callback, void *user);

/**
 * @brief Register end callback (pass NULL to unregister).
 * @param callback Callback function pointer.
 * @param user Opaque user pointer passed to callback.
 * @return true when accepted.
 */
bool hal_ota_on_end(hal_ota_on_end_callback_t callback, void *user);

/**
 * @brief Register progress callback (pass NULL to unregister).
 * @param callback Callback function pointer.
 * @param user Opaque user pointer passed to callback.
 * @return true when accepted.
 */
bool hal_ota_on_progress(hal_ota_on_progress_callback_t callback, void *user);

/**
 * @brief Register error callback (pass NULL to unregister).
 * @param callback Callback function pointer.
 * @param user Opaque user pointer passed to callback.
 * @return true when accepted.
 */
bool hal_ota_on_error(hal_ota_on_error_callback_t callback, void *user);

/** @brief Initialize OTA service and publish its hostname via mDNS.
 *
 * The configured name becomes reachable as `<hostname>.local` on native RP
 * CYW43 builds. The hostname must fit in one DNS label (at most 63 bytes).
 *  @return true when service is started.
 */
bool hal_ota_begin(void);

/** @brief Poll OTA service and dispatch pending callbacks. */
void hal_ota_handle(void);

/** @brief Return true when OTA service was started. */
bool hal_ota_is_started(void);

/**
 * @brief Confirm that a native trial image booted successfully.
 *
 * Call this only after the application has completed its own startup checks.
 * @return HAL_OK when confirmed or already stable.
 */
hal_status_t hal_ota_confirm_boot_ex(void);

/**
 * @brief Read the native OTA boot/rollback state.
 * @param out_info Destination for the current state.
 */
hal_status_t hal_ota_get_boot_info_ex(hal_ota_boot_info_t *out_info);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_OTA */
