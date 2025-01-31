
#include "pidController.h"

PIDController::PIDController(float kp, float ki, float kd, float mi) {
  setKp(kp);
  setKi(ki);
  setKd(kd);
  max_integral = mi;
  last_time = millis();
  dt = integral = previous = output = 0;  
  dir = FORWARD;
  setOutputLimits(PID_UNINITIALIZED, PID_UNINITIALIZED);
}

void PIDController::setKp(float kp) { pid_kp = kp; }
void PIDController::setKi(float ki) { pid_ki = ki; }
void PIDController::setKd(float kd) { pid_kd = kd; }

void PIDController::updatePIDtime(float timeDivider) {
  float now = millis();
  dt = (now - last_time) / timeDivider;
  if (dt <= 0) dt = 0.001;
  last_time = now;
}

float PIDController::updatePIDcontroller(float error) {
  float proportional = error;
  integral += error * dt;

  // Anti-windup: Clamp the integral term within a range
  integral = constrain(integral, -max_integral, max_integral);

  float raw_derivative = (error - previous) / dt;
  float derivative = previous_derivative + (dt / (dt + Tf)) * (raw_derivative - previous_derivative);
  previous_derivative = derivative;

  output = (pid_kp * proportional) + (pid_ki * integral) + (pid_kd * derivative);

  if(outputMax != PID_UNINITIALIZED &&
     outputMin != PID_UNINITIALIZED) {
    output = constrain(output, outputMin, outputMax);
  }

  return output;
}

void PIDController::setOutputLimits(float min, float max) {
  outputMin = min;
  outputMax = max;
}

void PIDController::reset() {
  integral = 0;
  previous = 0;
  output = 0;
  dt = last_time = 0;
}

void PIDController::setDirection(Direction d) {
  dir = d;
  if (dir == BACKWARD) {
    pid_kp = -pid_kp;
    pid_ki = -pid_ki;
    pid_kd = -pid_kd;
  }
}

bool PIDController::isErrorStable(float error, float tolerance, int stabilityThreshold) {

  if (fabs(error) < tolerance) {
    stabilityCounter++;
    instabilityCounter = 0;  
  } else {
    instabilityCounter++;
    stabilityCounter = 0;    
  }

  if (instabilityCounter > stabilityThreshold) {
    return false;
  }

  return true;
}

bool PIDController::isOscillating(float currentError, int windowSize) {
    static std::deque<float> errorHistory;
    errorHistory.push_back(fabs(currentError)); 
    
    if (errorHistory.size() > windowSize) {
        errorHistory.pop_front();
    }

    int zeroCrossings = 0;
    for (size_t i = 1; i < errorHistory.size(); ++i) {
        if ((errorHistory[i-1] < 0 && errorHistory[i] >= 0) || 
            (errorHistory[i-1] >= 0 && errorHistory[i] < 0)) {
            zeroCrossings++;
        }
    }

    return (zeroCrossings > 3); 
}

