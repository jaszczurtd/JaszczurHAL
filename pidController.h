#ifndef _PidController_h
#define _PidController_h

#include "libConfig.h"
#include <Arduino.h>
#include <inttypes.h>

enum Direction { FORWARD, BACKWARD };

#define PID_UNINITIALIZED -1234567.0f

class PIDController {
public:
  PIDController(float kp, float ki, float kd, float mi);
  void updatePIDtime(float timeDivider);
  float updatePIDcontroller(float error, float V_supply);
  float updatePIDcontroller(float error);
  void setVoltageCompensation(float kff, float v_nominal);
  float compensateVoltageThreshold(float V_supply);
  void setOutputLimits(float min, float max);
  void reset();
  void setDirection(Direction d);

private:
  float dt;
  float last_time;
  float integral;
  float previous;
  float output;
  float pid_kp;
  float pid_ki;
  float pid_kd;
  float max_integral;
  float outputMin;
  float outputMax;
  int dir;
  float Kff;
  float V_nominal;
  float lastVolts;
};

#endif
