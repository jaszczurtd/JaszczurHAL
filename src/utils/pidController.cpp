
#include "pidController.h"

PIDController::PIDController(float kp, float ki, float kd, float mi) {
  setKp(kp);
  setKi(ki);
  setKd(kd);
  setMaxIntegral(mi);
  last_time_ms = hal_millis();
  integral = previous = output = previous_derivative = 0;
  dir = FORWARD;
  dt = 0.001f;
  errorHistoryHead = 0;
  errorHistoryCount = 0;
  for (float &e : errorHistory)
    e = 0.0f;

  setOutputLimits(PID_UNINITIALIZED, PID_UNINITIALIZED);
}

void PIDController::setKp(float kp) { pid_kp = kp; }
void PIDController::setKi(float ki) { pid_ki = ki; }
void PIDController::setKd(float kd) { pid_kd = kd; }
void PIDController::setTf(float tf) { Tf = tf; }
void PIDController::setMaxIntegral(float mi) { max_integral = mi; }

void PIDController::updatePIDtime(float timeDivider) {
  const uint32_t now_ms = hal_millis();
  const uint32_t elapsed_ms = now_ms - last_time_ms;

  if (timeDivider == 0) {
    dt = 0.001f;
  } else {
    dt = (float)elapsed_ms / timeDivider;
    if (dt <= 0)
      dt = 0.001f;
  }

  last_time_ms = now_ms;
}

float PIDController::filteredDerivative(float sample, float previousSample,
                                        float seconds) const {
  const float raw = (sample - previousSample) / seconds;
  return previous_derivative +
         (seconds / (seconds + Tf)) * (raw - previous_derivative);
}

hal_status_t PIDController::step(float error, float measurement, float seconds,
                                 float deadband, hal_pid_terms_t *terms) {
  if (terms == nullptr || !isfinite(error) || !isfinite(measurement) ||
      !isfinite(seconds) || seconds <= 0.0f || !isfinite(deadband) ||
      deadband < 0.0f || !isfinite(pid_kp) || !isfinite(pid_ki) ||
      !isfinite(pid_kd) || !isfinite(Tf) || Tf < 0.0f ||
      !isfinite(max_integral) || max_integral < 0.0f || !isfinite(outputMin) ||
      !isfinite(outputMax) || outputMin > outputMax ||
      ((outputMin == PID_UNINITIALIZED) != (outputMax == PID_UNINITIALIZED))) {
    return HAL_EINVAL;
  }
  const float integrationError =
      error - hal_constrain(error, -deadband, deadband);
  const float increment = integrationError * seconds;
  const float candidate =
      hal_constrain(integral + increment, -max_integral, max_integral);
  const float derivative =
      measurementInitialized
          ? filteredDerivative(-measurement, -previousMeasurement, seconds)
          : 0.0f;
  hal_pid_terms_t next = {};
  next.proportional = pid_kp * error;
  next.derivative = pid_kd * derivative;
  const float trial = next.proportional + pid_ki * candidate + next.derivative;
  if (!isfinite(increment) || !isfinite(integral + increment) ||
      !isfinite(derivative) || !isfinite(trial)) {
    return HAL_EOVERFLOW;
  }
  const bool limited = outputMax != PID_UNINITIALIZED;
  const float integralChange = pid_ki * (candidate - integral);
  const bool deepenSaturation =
      limited && ((trial > outputMax && integralChange > 0.0f) ||
                  (trial < outputMin && integralChange < 0.0f));
  const float accepted = hal_constrain(deepenSaturation ? integral : candidate,
                                       -max_integral, max_integral);
  next.integral = pid_ki * accepted;
  next.unconstrained = next.proportional + next.integral + next.derivative;
  if (!isfinite(next.unconstrained)) {
    return HAL_EOVERFLOW;
  }
  next.output = limited
                    ? hal_constrain(next.unconstrained, outputMin, outputMax)
                    : next.unconstrained;
  next.saturated_high = limited && next.unconstrained >= outputMax;
  next.saturated_low = limited && next.unconstrained <= outputMin;
  integral = accepted;
  previousMeasurement = measurement;
  measurementInitialized = true;
  previous_derivative = derivative;
  output = next.output;
  *terms = next;
  return HAL_OK;
}

float PIDController::updatePIDcontroller(float error) {
  float proportional = error;

  // Block integration that would deepen saturation, including negative Ki.
  const float increment = error * dt;
  const float integralChange = pid_ki * increment;
  bool saturatedHigh = (outputMax != PID_UNINITIALIZED && output >= outputMax);
  bool saturatedLow = (outputMin != PID_UNINITIALIZED && output <= outputMin);
  bool canIntegrate = !(saturatedHigh && integralChange > 0.0f) &&
                      !(saturatedLow && integralChange < 0.0f);
  if (canIntegrate) {
    integral += increment;
  }

  // Hard clamp as secondary safeguard
  integral = pid_clamp(integral, -max_integral, max_integral);

  float derivative = filteredDerivative(error, previous, dt);
  previous_derivative = derivative;

  output =
      (pid_kp * proportional) + (pid_ki * integral) + (pid_kd * derivative);

  if (outputMax != PID_UNINITIALIZED && outputMin != PID_UNINITIALIZED) {
    output = pid_clamp(output, outputMin, outputMax);
  }

  previous = error;

  return output;
}

void PIDController::setOutputLimits(float min, float max) {
  outputMin = min;
  outputMax = max;
}

void PIDController::reset() {
  measurementInitialized = false;
  previousMeasurement = 0.0f;
  last_time_ms = hal_millis();
  integral = previous = output = previous_derivative = 0;
  dt = 0.001f;
  errorHistoryHead = 0;
  errorHistoryCount = 0;
}

void PIDController::setDirection(Direction d) {
  if (dir != (int)d) {
    pid_kp = -pid_kp;
    pid_ki = -pid_ki;
    pid_kd = -pid_kd;
    dir = d;
  }
}

bool PIDController::isErrorStable(float error, float tolerance,
                                  int stabilityThreshold) {
  if (fabs(error) < tolerance) {
    stabilityCounter++;
    instabilityCounter = 0;
  } else {
    stabilityCounter = 0;
    instabilityCounter++;
  }
  return (stabilityCounter >= stabilityThreshold);
}

bool PIDController::isOscillating(float currentError, int windowSize) {
  if (windowSize > PID_OSCILLATION_WINDOW)
    windowSize = PID_OSCILLATION_WINDOW;

  errorHistory[errorHistoryHead] = currentError;
  errorHistoryHead = (errorHistoryHead + 1) % PID_OSCILLATION_WINDOW;
  if (errorHistoryCount < PID_OSCILLATION_WINDOW)
    errorHistoryCount++;

  int count = (errorHistoryCount < windowSize) ? errorHistoryCount : windowSize;
  int zeroCrossings = 0;
  for (int i = 1; i < count; i++) {
    int a = (errorHistoryHead - count + i - 1 + PID_OSCILLATION_WINDOW) %
            PID_OSCILLATION_WINDOW;
    int b = (errorHistoryHead - count + i + PID_OSCILLATION_WINDOW) %
            PID_OSCILLATION_WINDOW;
    if ((errorHistory[a] < 0 && errorHistory[b] >= 0) ||
        (errorHistory[a] >= 0 && errorHistory[b] < 0)) {
      zeroCrossings++;
    }
  }
  return (zeroCrossings > 3);
}
