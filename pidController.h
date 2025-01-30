#ifndef _PidController_h
#define _PidController_h

#include "libConfig.h"
#include <Arduino.h>
#include <inttypes.h>
#include <cfloat>
#include <deque>

enum Direction { FORWARD, BACKWARD };

#define PID_UNINITIALIZED -1234567.0f

class PIDController {
public:
  PIDController(float kp, float ki, float kd, float mi);

  void setKp(float kp); 
  void setKi(float ki);
  void setKd(float kd);

  float getKp() { return pid_kp; }
  float getKi() { return pid_ki; }
  float getKd() { return pid_kd; }

  void updatePIDtime(float timeDivider);
  float updatePIDcontroller(float error);
  void setOutputLimits(float min, float max);
  void reset();
  void setDirection(Direction d);
  bool isErrorStable(float error, float tolerance, int stabilityThreshold);
  bool isOscillating(float currentError, int windowSize = 20);

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

  int stabilityCounter = 0;
  int instabilityCounter = 0;
  int zeroCrossings = 0;
};

#endif
