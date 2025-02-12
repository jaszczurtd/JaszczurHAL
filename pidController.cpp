
#include "pidController.h"

PIDController::PIDController(float kp, float ki, float kd, float mi) {
  setKp(kp);
  setKi(ki);
  setKd(kd);
  setMaxIntegral(mi);
  last_time = millis();
  integral = previous = output = previous_derivative = output = 0;  
  dir = FORWARD;
  dt = 0.001; 

  setOutputLimits(PID_UNINITIALIZED, PID_UNINITIALIZED);
}

void PIDController::setKp(float kp) { pid_kp = kp; }
void PIDController::setKi(float ki) { pid_ki = ki; }
void PIDController::setKd(float kd) { pid_kd = kd; }
void PIDController::setTf(float tf) { Tf = tf; }
void PIDController::setMaxIntegral(float mi) { max_integral = mi; }


void PIDController::updatePIDtime(float timeDivider) {
  float now = millis();

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

  previous = error;

  return output;
}

void PIDController::setOutputLimits(float min, float max) {
  outputMin = min;
  outputMax = max;
}

void PIDController::reset() {
  last_time = millis();
  integral = previous = output = previous_derivative = 0;  
  dt = 0.001; 
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
    stabilityCounter = 0;
    instabilityCounter++;
  }
  return (stabilityCounter >= stabilityThreshold);
}

bool PIDController::isOscillating(float currentError, int windowSize) {
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

