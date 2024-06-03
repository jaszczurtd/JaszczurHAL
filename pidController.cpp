
#include "pidController.h"

PIDController::PIDController(float kp, float ki, float kd, float mi) {
  pid_kp = kp;
  pid_ki = ki;
  pid_kd = kd;
  max_integral = mi;
  dt = last_time = integral = previous = output = 0;  
}

void PIDController::updatePIDtime(float timeDivider) {
  float now = millis();
  dt = (now - last_time) / timeDivider;
  last_time = now;
}

float PIDController::updatePIDcontroller(float error) {
  float proportional = error;
  integral += error * dt;

  // Anti-windup: Clamp the integral term within a range
  if (integral > max_integral) integral = max_integral;
  else if (integral < -max_integral) integral = -max_integral;

  float derivative = (error - previous) / dt;
  previous = error;
  output = (pid_kp * proportional) + (pid_ki * integral) + (pid_kd * derivative);
  return output;
}

