
#include "pidController.h"


PIDController::PIDController(float kp, float ki, float kd, float mi) {
  setKp(kp);
  setKi(ki);
  setKd(kd);
  setMaxIntegral(mi);
  last_time = hal_millis();
  integral = previous = output = previous_derivative = 0;
  dir = FORWARD;
  dt = 0.001f;
  errorHistoryHead = 0;
  errorHistoryCount = 0;

  setOutputLimits(PID_UNINITIALIZED, PID_UNINITIALIZED);
}

void PIDController::setKp(float kp) { pid_kp = kp; }
void PIDController::setKi(float ki) { pid_ki = ki; }
void PIDController::setKd(float kd) { pid_kd = kd; }
void PIDController::setTf(float tf) { Tf = tf; }
void PIDController::setMaxIntegral(float mi) { max_integral = mi; }


void PIDController::updatePIDtime(float timeDivider) {
  float now = (float)hal_millis();

  if (timeDivider == 0) {
    dt = 0.001;  
  } else {
    dt = (now - last_time) / timeDivider;
    if (dt <= 0) dt = 0.001;  
  }

  last_time = now;
}

float PIDController::updatePIDcontroller(float error) {
  float proportional = error;

  // Clamping anti-windup: skip integration when output is saturated in the
  // direction of the error (prevents integral windup at output limits).
  bool saturatedHigh = (outputMax != PID_UNINITIALIZED && output >= outputMax);
  bool saturatedLow  = (outputMin != PID_UNINITIALIZED && output <= outputMin);
  bool canIntegrate  = !(saturatedHigh && error > 0.0f) && !(saturatedLow && error < 0.0f);
  if (canIntegrate) {
    integral += error * dt;
  }

  // Hard clamp as secondary safeguard
  integral = pid_clamp(integral, -max_integral, max_integral);

  float raw_derivative = (error - previous) / dt;
  float derivative = previous_derivative + (dt / (dt + Tf)) * (raw_derivative - previous_derivative);
  previous_derivative = derivative;

  output = (pid_kp * proportional) + (pid_ki * integral) + (pid_kd * derivative);

  if(outputMax != PID_UNINITIALIZED &&
     outputMin != PID_UNINITIALIZED) {
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
  last_time = hal_millis();
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

bool PIDController::isErrorStable(float error, float tolerance, int stabilityThreshold) {
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
    if (windowSize > PID_OSCILLATION_WINDOW) windowSize = PID_OSCILLATION_WINDOW;

    errorHistory[errorHistoryHead] = currentError;
    errorHistoryHead = (errorHistoryHead + 1) % PID_OSCILLATION_WINDOW;
    if (errorHistoryCount < PID_OSCILLATION_WINDOW) errorHistoryCount++;

    int count = (errorHistoryCount < windowSize) ? errorHistoryCount : windowSize;
    int zeroCrossings = 0;
    for (int i = 1; i < count; i++) {
        int a = (errorHistoryHead - count + i - 1 + PID_OSCILLATION_WINDOW) % PID_OSCILLATION_WINDOW;
        int b = (errorHistoryHead - count + i     + PID_OSCILLATION_WINDOW) % PID_OSCILLATION_WINDOW;
        if ((errorHistory[a] < 0 && errorHistory[b] >= 0) ||
            (errorHistory[a] >= 0 && errorHistory[b] < 0)) {
            zeroCrossings++;
        }
    }
    return (zeroCrossings > 3);
}

