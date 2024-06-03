#ifndef _PidController_h
#define _PidController_h

#include "libConfig.h"
#include <Arduino.h>
#include <inttypes.h>


class PIDController {
public:
  PIDController(float kp, float ki, float kd);
  void updatePIDtime(float timeDivider);
  float updatePIDcontroller(float error);

private:
  float dt;
  float last_time;
  float integral;
  float previous;
  float output;
  float pid_kp;
  float pid_ki;
  float pid_kd;
};

#endif
