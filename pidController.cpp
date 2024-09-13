
#include "pidController.h"

PIDController::PIDController(float kp, float ki, float kd, float mi) {
  pid_kp = kp;
  pid_ki = ki;
  pid_kd = kd;
  max_integral = mi;
  last_time = millis();
  dt = integral = previous = output = 0;  
  dir = FORWARD;
  setVoltageCompensation(PID_UNINITIALIZED, PID_UNINITIALIZED);
  setOutputLimits(PID_UNINITIALIZED, PID_UNINITIALIZED);
}

void PIDController::updatePIDtime(float timeDivider) {
  float now = millis();
  dt = (now - last_time) / timeDivider;
  if (dt <= 0) dt = 0.001;
  last_time = now;
}

void PIDController::setVoltageCompensation(float kff, float v_nominal) {
  Kff = kff;
  V_nominal = v_nominal;
  lastVolts = 0.0;
}

float PIDController::compensateVoltageThreshold(float V_supply) {
  if (fabs(V_supply - lastVolts) > Kff) {
    lastVolts = V_supply;
  }

  return (V_nominal / lastVolts);    
}

float PIDController::updatePIDcontroller(float error, float V_supply) {
  float proportional = error;
  integral += error * dt;

  // Anti-windup: Clamp the integral term within a range
  integral = constrain(integral, -max_integral, max_integral);

  float derivative = (error - previous) / dt;
  previous = error;

  output = (pid_kp * proportional) + (pid_ki * integral) + (pid_kd * derivative);

  if(outputMax != PID_UNINITIALIZED &&
     outputMin != PID_UNINITIALIZED) {
    output = constrain(output, outputMin, outputMax);
  }

  return output;
}

float PIDController::updatePIDcontroller(float error) {
  return updatePIDcontroller(error, PID_UNINITIALIZED);
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
