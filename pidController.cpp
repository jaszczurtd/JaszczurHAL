
#include "pidController.h"

void initPIDcontroller(PIDController *c, float kp, float ki, float kd) {
  c->kp = kp;
  c->ki = ki;
  c->kd = kd;
  c->last_time = 0;
}

void updatePIDtime(PIDController *c, float timeDivider) {
  float now = millis();
  c->dt = (now - c->last_time) / timeDivider;
  c->last_time = now;
}

float updatePIDcontroller(PIDController *c, float error) {
  float proportional = error;
  c->integral += error * c->dt;
  float derivative = (error - c->previous) / c->dt;
  c->previous = error;
  return (c->kp * proportional) + (c->ki * c->integral) + (c->kd * derivative);
}

