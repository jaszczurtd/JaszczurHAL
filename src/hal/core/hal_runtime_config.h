#pragma once

/**
 * @file hal_runtime_config.h
 * @brief Runtime configuration API for JaszczurHAL.
 */

/**
 * @brief Runtime HAL configuration.
 *
 * All fields are initialised to the compile-time defaults by
 * hal_config_defaults(). The application may override individual fields and
 * pass the struct to hal_setup() before using any HAL create functions.
 *
 * Values must not exceed the compile-time #define maximums (the static arrays
 * are sized at compile time). hal_setup() caps oversized values automatically.
 *
 * @code
 *   hal_config_t cfg = hal_config_defaults();
 *   cfg.pwm_freq_max_channels = 4;
 *   cfg.can_max_instances = 1;
 *   hal_setup(&cfg);
 * @endcode
 */
typedef struct {
  int pwm_freq_max_channels;  /**< Effective PWM-freq channel limit. */
  int can_max_instances;      /**< Effective CAN instance limit. */
  int uart_max_instances;     /**< Effective hardware UART limit. */
  int swserial_max_instances; /**< Effective software UART limit. */
  int mock_can_max_inst;      /**< Mock CAN instance limit. */
  int mock_can_buf_size;      /**< Mock CAN ring-buffer depth. */
  int mock_max_alarms;        /**< Mock timer alarm limit. */
} hal_config_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Return a configuration populated with the compile-time defaults. */
hal_config_t hal_config_defaults(void);

/**
 * @brief Initialise the HAL with the given configuration.
 *
 * Must be called before any hal_*_create() function. If never called,
 * compile-time defaults are used. Values exceeding the compile-time maximum
 * are silently capped.
 *
 * @param cfg Pointer to the configuration struct.
 */
void hal_setup(const hal_config_t *cfg);

/**
 * @brief Get the active HAL configuration.
 * @return Read-only pointer to the internal configuration.
 */
const hal_config_t *hal_get_config(void);

#ifdef __cplusplus
}
#endif
