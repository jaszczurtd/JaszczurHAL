#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_OTA

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
 * @brief Set OTA password.
 * @param password Null-terminated password string (may be empty).
 * @return true when accepted.
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

/** @brief Initialize OTA service.
 *  @return true when service is started.
 */
bool hal_ota_begin(void);

/** @brief Poll OTA service and dispatch pending callbacks. */
void hal_ota_handle(void);

/** @brief Return true when OTA service was started. */
bool hal_ota_is_started(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_OTA */
