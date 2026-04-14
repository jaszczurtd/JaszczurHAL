#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

/**
 * @file pidController.h
 * @brief Discrete PID controller with derivative filtering, output limiting,
 *        oscillation detection and error-stability checks.
 */

#include "libConfig.h"
#include <hal/hal_system.h>
#include <hal/hal_math.h>  // hal_constrain (type-independent macro)
#include <inttypes.h>
#include <float.h>
#include <math.h>

/** @brief Size of the error history window for oscillation detection. */
#define PID_OSCILLATION_WINDOW 20

/**
 * @brief Backward-compatible clamp alias.
 * @deprecated Use hal_constrain() directly in new code.
 */
template<typename T>
static inline T pid_clamp(T v, T lo, T hi) { return hal_constrain(v, lo, hi); }

/** @brief PID direction. */
enum Direction { FORWARD, BACKWARD };

/** @brief Sentinel value indicating an uninitialised PID output limit. */
#define PID_UNINITIALIZED -1234567.0f

/** @brief Grouping of PID tuning parameters. */
typedef struct {
  float kP;   /**< Proportional gain. */
  float kI;   /**< Integral gain. */
  float kD;   /**< Derivative gain. */
  float Tf;   /**< Derivative low-pass filter time constant. */
} PIDValues;

/**
 * @brief Discrete PID controller.
 *
 * Supports configurable gains, anti-windup via integral limit and output
 * clamping, first-order derivative filtering, and runtime oscillation /
 * stability analysis.
 */
class PIDController {
public:
  /** @brief Default constructor (all gains zero, limits uninitialised). */
  PIDController() : dt(0.001f), last_time(0.0f), integral(0.0f), previous(0.0f),
                    output(0.0f), pid_kp(0.0f), pid_ki(0.0f), pid_kd(0.0f),
                    max_integral(0.0f), dir(FORWARD),
                    errorHistoryHead(0), errorHistoryCount(0) {
    setOutputLimits(PID_UNINITIALIZED, PID_UNINITIALIZED);
  }

  /**
   * @brief Construct with initial gains and integral limit.
   * @param kp Proportional gain.
   * @param ki Integral gain.
   * @param kd Derivative gain.
   * @param mi Maximum integral magnitude (anti-windup).
   */
  PIDController(float kp, float ki, float kd, float mi);

  /** @brief Set proportional gain. */
  void setKp(float kp); 
  /** @brief Set integral gain. */
  void setKi(float ki);
  /** @brief Set derivative gain. */
  void setKd(float kd);
  /** @brief Set the derivative low-pass filter time constant. */
  void setTf(float tf);
  /** @brief Set the maximum integral magnitude (anti-windup). */
  void setMaxIntegral(float mi);

  /** @brief Get proportional gain. */
  float getKp() { return pid_kp; }
  /** @brief Get integral gain. */
  float getKi() { return pid_ki; }
  /** @brief Get derivative gain. */
  float getKd() { return pid_kd; }
  /** @brief Get derivative filter time constant. */
  float getTf() { return Tf; }

  /**
   * @brief Update the PID time step from a divider value.
   * @param timeDivider Divider applied to compute dt.
   */
  void updatePIDtime(float timeDivider);

  /**
   * @brief Compute one PID iteration.
   * @param error Current error (setpoint - measurement).
   * @return Controller output (clamped to output limits).
   */
  float updatePIDcontroller(float error);

  /**
   * @brief Set the output clamping range.
   * @param min Minimum output value.
   * @param max Maximum output value.
   */
  void setOutputLimits(float min, float max);

  /** @brief Reset internal state (integral, derivative, history). */
  void reset();

  /**
   * @brief Set forward or reverse acting direction.
   * @param d FORWARD or BACKWARD.
   */
  void setDirection(Direction d);

  /**
   * @brief Check whether the error has been stable within tolerance.
   * @param error              Current error value.
   * @param tolerance          Maximum allowed |error| for stability.
   * @param stabilityThreshold Number of consecutive stable samples required.
   * @return true if the error has been within tolerance for the required count.
   */
  bool isErrorStable(float error, float tolerance, int stabilityThreshold);

  /**
   * @brief Detect oscillation in the recent error history.
   * @param currentError Current error value.
   * @param windowSize   Number of history samples to analyse (max PID_OSCILLATION_WINDOW).
   * @return true if oscillation is detected (frequent zero-crossings).
   */
  bool isOscillating(float currentError, int windowSize = 20);

private:
  float dt;
  float last_time;
  float integral;
  float previous;
  float output;
  float pid_kp;
  float pid_ki;
  float pid_kd;
  float max_integral;
  float outputMin;
  float outputMax;
  int dir;

  float errorHistory[PID_OSCILLATION_WINDOW];
  int errorHistoryHead;
  int errorHistoryCount;

  float previous_derivative = 0;
  float Tf = 0.05f;

  int stabilityCounter = 0;
  int instabilityCounter = 0;
  int zeroCrossings = 0;
};

#endif
