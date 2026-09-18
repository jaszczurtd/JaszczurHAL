#pragma once

/**
 * @file hal_pid_controller.h
 * @brief C-compatible wrapper for the PIDController utility.
 *
 * This API exposes PID operations as plain C functions and opaque handles,
 * so ECU modules can use PID logic without direct C++ class coupling.
 */

#include <hal/core/hal_status.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque PID controller implementation type. */
typedef struct hal_pid_controller_impl_s hal_pid_controller_impl_t;

typedef hal_pid_controller_impl_t *hal_pid_controller_t;

/** @brief PID direction for output response. */
typedef enum {
  HAL_PID_DIRECTION_FORWARD = 0, /**< Positive error increases output. */
  HAL_PID_DIRECTION_BACKWARD = 1 /**< Positive error decreases output. */
} hal_pid_direction_t;

/**
 * @brief Create a PID controller with default internal settings.
 * @note Legacy elapsed-time measurement starts at creation.
 * @return Controller handle, or NULL on allocation failure.
 */
hal_pid_controller_t hal_pid_controller_create(void);

/**
 * @brief Create a PID controller with initial gains and integral limit.
 * @note Legacy elapsed-time measurement starts at creation.
 * @param kp Proportional gain.
 * @param ki Integral gain.
 * @param kd Derivative gain.
 * @param max_integral Maximum absolute integral value (anti-windup).
 * @return Controller handle, or NULL on allocation failure.
 */
hal_pid_controller_t hal_pid_controller_create_with_gains(float kp, float ki,
                                                          float kd,
                                                          float max_integral);

/**
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
 * @param controller Controller handle. NULL is ignored.
 * @param time_divider Divides elapsed milliseconds; 1000 gives seconds.
 * @note Time starts at creation, reset or the previous time update. Integer
 * subtraction handles clock wrap for intervals shorter than 2^32 ms before
 * conversion to float. A zero divider or nonpositive computed dt uses 0.001.
 */
void hal_pid_controller_update_time(hal_pid_controller_t controller,
                                    float time_divider);

/**
 * @brief Run one PID iteration.
 * @note Uses derivative on error and the last dt from update_time().
 * Anti-windup checks the direction of Ki * error * dt against the previous
 * output's limits, including negative Ki and BACKWARD direction.
 * @param controller Controller handle.
 * @param error Current error (setpoint - measurement).
 * @return Controller output, or 0.0f when @p controller is NULL.
 */
float hal_pid_controller_update(hal_pid_controller_t controller, float error);

/** @brief Terms from one explicit-time PID step, in controller output units. */
typedef struct {
  float proportional;  /**< Proportional contribution. */
  float integral;      /**< Integral contribution used for this output. */
  float derivative;    /**< Filtered derivative-on-measurement contribution. */
  float unconstrained; /**< Sum before output limits. */
  float output;        /**< Command after output limits. */
  bool saturated_high; /**< Sum reaches the upper limit, regardless of error
                          sign. */
  bool saturated_low;  /**< Sum reaches the lower limit, regardless of error
                          sign. */
} hal_pid_terms_t;

/**
 * @brief Step with elapsed seconds, derivative on measurement and conditional
 * I.
 * @param controller Non-NULL instance; serialize all access to each instance.
 * @param error Setpoint minus measurement, in measurement units.
 * @param measurement Current measurement in the same units.
 * @param dt_s Positive finite elapsed time in seconds; no clock is read.
 * @param integral_deadband Nonnegative finite dead zone in measurement units.
 * Integration uses sign(error) * max(abs(error) - deadband, 0); P and D remain
 * continuous. Integration that would deepen current output saturation is
 * skipped. Set output limits to the available actuator command minus any
 * feedforward.
 * @param terms Non-NULL output receiving the terms on success.
 * @return HAL_OK, HAL_EINVAL for invalid arguments/settings, or HAL_EOVERFLOW
 * for arithmetic overflow. Errors leave controller state and terms unchanged.
 * @note Gains use seconds; max integral is in measurement-unit seconds. The
 * first step after reset seeds the derivative with zero. Reset before switching
 * between this API and the legacy error-derivative update API.
 */
hal_status_t hal_pid_controller_step_ex(hal_pid_controller_t controller,
                                        float error, float measurement,
                                        float dt_s, float integral_deadband,
                                        hal_pid_terms_t *terms);

/**
 * @brief Set output clamping range.
 * @param controller Controller handle.
 * @param min_output Minimum output value.
 * @param max_output Maximum output value.
 */
void hal_pid_controller_set_output_limits(hal_pid_controller_t controller,
                                          float min_output, float max_output);

/**
 * @brief Reset PID internal state (integrator, derivative, timing, history).
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
                                        float error, float tolerance,
                                        int stability_threshold);

/**
 * @brief Detect oscillation from recent error history.
 * @param controller Controller handle.
 * @param current_error Current error value.
 * @param window_size Number of samples to inspect.
 * @return true when oscillation is detected.
 */
bool hal_pid_controller_is_oscillating(hal_pid_controller_t controller,
                                       float current_error, int window_size);

#ifdef __cplusplus
}
#endif
