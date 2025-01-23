#ifndef _PidController_h
#define _PidController_h

#include "libConfig.h"
#include <Arduino.h>
#include <inttypes.h>
#include <cfloat>

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
  float updatePIDcontroller(float error, float V_supply);
  float updatePIDcontroller(float error);
  void setVoltageCompensation(float kff, float v_nominal);
  float compensateVoltageThreshold(float V_supply);
  void setOutputLimits(float min, float max);
  void reset();
  void setDirection(Direction d);
  void checkForOscillations(float error);
  bool isErrorStable(float error, float tolerance, int stabilityThreshold);
  bool isOscillating(float tolerance);

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

  float maxOutput = -FLT_MAX;
  float minOutput = FLT_MAX;
  int stabilityCounter = 0;
  int instabilityCounter = 0;
  int zeroCrossings = 0;
};

#endif
