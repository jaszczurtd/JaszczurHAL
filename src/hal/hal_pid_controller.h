#pragma once

/**
 * @file hal_pid_controller.h
 * @brief C-compatible wrapper for the PIDController utility.
 *
 * This API exposes PID operations as plain C functions and opaque handles,
 * so ECU modules can use PID logic without direct C++ class coupling.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque PID controller implementation type. */
typedef struct hal_pid_controller_impl_s hal_pid_controller_impl_t;

/** @brief Handle to a PID controller instance. */
typedef hal_pid_controller_impl_t *hal_pid_controller_t;

/** @brief PID direction for output response. */
typedef enum {
  HAL_PID_DIRECTION_FORWARD = 0,  /**< Positive error increases output. */
  HAL_PID_DIRECTION_BACKWARD = 1  /**< Positive error decreases output. */
} hal_pid_direction_t;

/**
 * @brief Create a PID controller with default internal settings.
 * @return Controller handle, or NULL on allocation failure.
 */
hal_pid_controller_t hal_pid_controller_create(void);

/**
 * @brief Create a PID controller with initial gains and integral limit.
 * @param kp Proportional gain.
 * @param ki Integral gain.
 * @param kd Derivative gain.
 * @param max_integral Maximum absolute integral value (anti-windup).
 * @return Controller handle, or NULL on allocation failure.
 */
hal_pid_controller_t hal_pid_controller_create_with_gains(float kp,
                                                          float ki,
                                                          float kd,
                                                          float max_integral);

/**
 * @brief Destroy a PID controller instance.
 * @param controller Controller handle. NULL is ignored.
 */
void hal_pid_controller_destroy(hal_pid_controller_t controller);

/** @brief Set proportional gain. */
void hal_pid_controller_set_kp(hal_pid_controller_t controller, float kp);

/** @brief Set integral gain. */
void hal_pid_controller_set_ki(hal_pid_controller_t controller, float ki);

/** @brief Set derivative gain. */
void hal_pid_controller_set_kd(hal_pid_controller_t controller, float kd);

/** @brief Set derivative filter time constant. */
void hal_pid_controller_set_tf(hal_pid_controller_t controller, float tf);

/** @brief Set maximum absolute integral value (anti-windup clamp). */
void hal_pid_controller_set_max_integral(hal_pid_controller_t controller,
                                         float max_integral);

/** @brief Get proportional gain. Returns 0.0f when @p controller is NULL. */
float hal_pid_controller_get_kp(hal_pid_controller_t controller);

/** @brief Get integral gain. Returns 0.0f when @p controller is NULL. */
float hal_pid_controller_get_ki(hal_pid_controller_t controller);

/** @brief Get derivative gain. Returns 0.0f when @p controller is NULL. */
float hal_pid_controller_get_kd(hal_pid_controller_t controller);

/** @brief Get derivative filter time constant. Returns 0.0f when NULL. */
float hal_pid_controller_get_tf(hal_pid_controller_t controller);

/**
 * @brief Update internal PID timestep from elapsed milliseconds / divider.
 * @param controller Controller handle.
 * @param time_divider Divider used to compute internal dt.
 */
void hal_pid_controller_update_time(hal_pid_controller_t controller,
                                    float time_divider);

/**
 * @brief Run one PID iteration.
 * @param controller Controller handle.
 * @param error Current error (setpoint - measurement).
 * @return Controller output, or 0.0f when @p controller is NULL.
 */
float hal_pid_controller_update(hal_pid_controller_t controller, float error);

/**
 * @brief Set output clamping range.
 * @param controller Controller handle.
 * @param min_output Minimum output value.
 * @param max_output Maximum output value.
 */
void hal_pid_controller_set_output_limits(hal_pid_controller_t controller,
                                          float min_output,
                                          float max_output);

/**
 * @brief Reset PID internal state (integrator, derivative, timing, history).
 * @param controller Controller handle.
 */
void hal_pid_controller_reset(hal_pid_controller_t controller);

/**
 * @brief Configure PID output direction.
 * @param controller Controller handle.
 * @param direction Forward or backward response.
 */
void hal_pid_controller_set_direction(hal_pid_controller_t controller,
                                      hal_pid_direction_t direction);

/**
 * @brief Check whether error remains inside tolerance long enough.
 * @param controller Controller handle.
 * @param error Current error value.
 * @param tolerance Absolute error tolerance.
 * @param stability_threshold Required stable sample count.
 * @return true when considered stable, false otherwise.
 */
bool hal_pid_controller_is_error_stable(hal_pid_controller_t controller,
                                        float error,
                                        float tolerance,
                                        int stability_threshold);

/**
 * @brief Detect oscillation from recent error history.
 * @param controller Controller handle.
 * @param current_error Current error value.
 * @param window_size Number of samples to inspect.
 * @return true when oscillation is detected.
 */
bool hal_pid_controller_is_oscillating(hal_pid_controller_t controller,
                                       float current_error,
                                       int window_size);

#ifdef __cplusplus
}
#endif
