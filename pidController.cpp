
#include "pidController.h"

PIDController::PIDController(float kp, float ki, float kd) {
  pid_kp = kp;
  pid_ki = ki;
  pid_kd = kd;
  last_time = 0;
}

void PIDController::updatePIDtime(float timeDivider) {
  float now = millis();
  dt = (now - last_time) / timeDivider;
  last_time = now;
}

float PIDController::updatePIDcontroller(float error) {
  float proportional = error;
  integral += error * dt;
  float derivative = (error - previous) / dt;
  previous = error;
  return (pid_kp * proportional) + (pid_ki * integral) + (pid_kd * derivative);
}

