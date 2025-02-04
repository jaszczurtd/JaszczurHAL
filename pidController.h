#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include "libConfig.h"
#include <Arduino.h>
#include <inttypes.h>
#include <cfloat>
#include <deque>

enum Direction { FORWARD, BACKWARD };

#define PID_UNINITIALIZED -1234567.0f

typedef struct {
  float kP;
  float kI;
  float kD;
  float Tf;
} PIDValues;

class PIDController {
public:
  PIDController(float kp, float ki, float kd, float mi);

  void setKp(float kp); 
  void setKi(float ki);
  void setKd(float kd);
  void setTf(float tf);

  float getKp() { return pid_kp; }
  float getKi() { return pid_ki; }
  float getKd() { return pid_kd; }
  float getTf() { return Tf; }

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

  std::deque<float> errorHistory;

  float previous_derivative = 0;
  float Tf = 0.05;

  int stabilityCounter = 0;
  int instabilityCounter = 0;
  int zeroCrossings = 0;
};

#endif
