#ifndef _PidController_h
#define _PidController_h

#include "libConfig.h"
#include <Arduino.h>
#include <inttypes.h>

typedef struct {
  float dt;
  float last_time;
  float integral;
  float previous;
  float output;
  float kp;
  float ki;
  float kd;
} PIDController;

void initPIDcontroller(PIDController *c, float kp, float ki, float kd);
void updatePIDtime(PIDController *c, float timeDivider);
float updatePIDcontroller(PIDController *c, float error);

#endif
