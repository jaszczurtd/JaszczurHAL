#include "hal_pid_controller.h"

#include <new>

#include <utils/pidController.h>

struct hal_pid_controller_impl_s {
  PIDController controller;

  hal_pid_controller_impl_s() : controller() {}

  hal_pid_controller_impl_s(float kp, float ki, float kd, float max_integral)
      : controller(kp, ki, kd, max_integral) {}
};

static PIDController *hal_pid_get(hal_pid_controller_t controller) {
  return (controller != nullptr) ? &controller->controller : nullptr;
}

hal_pid_controller_t hal_pid_controller_create(void) {
  return new (std::nothrow) hal_pid_controller_impl_s();
}

hal_pid_controller_t hal_pid_controller_create_with_gains(float kp,
                                                          float ki,
                                                          float kd,
                                                          float max_integral) {
  return new (std::nothrow) hal_pid_controller_impl_s(kp, ki, kd, max_integral);
}

void hal_pid_controller_destroy(hal_pid_controller_t controller) {
  delete controller;
}

void hal_pid_controller_set_kp(hal_pid_controller_t controller, float kp) {
  PIDController *pid = hal_pid_get(controller);
  if (pid != nullptr) {
    pid->setKp(kp);
  }
}

void hal_pid_controller_set_ki(hal_pid_controller_t controller, float ki) {
  PIDController *pid = hal_pid_get(controller);
  if (pid != nullptr) {
    pid->setKi(ki);
  }
}

void hal_pid_controller_set_kd(hal_pid_controller_t controller, float kd) {
  PIDController *pid = hal_pid_get(controller);
  if (pid != nullptr) {
    pid->setKd(kd);
  }
}

void hal_pid_controller_set_tf(hal_pid_controller_t controller, float tf) {
  PIDController *pid = hal_pid_get(controller);
  if (pid != nullptr) {
    pid->setTf(tf);
  }
}

void hal_pid_controller_set_max_integral(hal_pid_controller_t controller,
                                         float max_integral) {
  PIDController *pid = hal_pid_get(controller);
  if (pid != nullptr) {
    pid->setMaxIntegral(max_integral);
  }
}

float hal_pid_controller_get_kp(hal_pid_controller_t controller) {
  PIDController *pid = hal_pid_get(controller);
  return (pid != nullptr) ? pid->getKp() : 0.0f;
}

float hal_pid_controller_get_ki(hal_pid_controller_t controller) {
  PIDController *pid = hal_pid_get(controller);
  return (pid != nullptr) ? pid->getKi() : 0.0f;
}

float hal_pid_controller_get_kd(hal_pid_controller_t controller) {
  PIDController *pid = hal_pid_get(controller);
  return (pid != nullptr) ? pid->getKd() : 0.0f;
}

float hal_pid_controller_get_tf(hal_pid_controller_t controller) {
  PIDController *pid = hal_pid_get(controller);
  return (pid != nullptr) ? pid->getTf() : 0.0f;
}

void hal_pid_controller_update_time(hal_pid_controller_t controller,
                                    float time_divider) {
  PIDController *pid = hal_pid_get(controller);
  if (pid != nullptr) {
    pid->updatePIDtime(time_divider);
  }
}

float hal_pid_controller_update(hal_pid_controller_t controller, float error) {
  PIDController *pid = hal_pid_get(controller);
  return (pid != nullptr) ? pid->updatePIDcontroller(error) : 0.0f;
}

void hal_pid_controller_set_output_limits(hal_pid_controller_t controller,
                                          float min_output,
                                          float max_output) {
  PIDController *pid = hal_pid_get(controller);
  if (pid != nullptr) {
    pid->setOutputLimits(min_output, max_output);
  }
}

void hal_pid_controller_reset(hal_pid_controller_t controller) {
  PIDController *pid = hal_pid_get(controller);
  if (pid != nullptr) {
    pid->reset();
  }
}

void hal_pid_controller_set_direction(hal_pid_controller_t controller,
                                      hal_pid_direction_t direction) {
  PIDController *pid = hal_pid_get(controller);
  if (pid != nullptr) {
    const Direction native_direction =
        (direction == HAL_PID_DIRECTION_BACKWARD) ? BACKWARD : FORWARD;
    pid->setDirection(native_direction);
  }
}

bool hal_pid_controller_is_error_stable(hal_pid_controller_t controller,
                                        float error,
                                        float tolerance,
                                        int stability_threshold) {
  PIDController *pid = hal_pid_get(controller);
  return (pid != nullptr)
             ? pid->isErrorStable(error, tolerance, stability_threshold)
             : false;
}

bool hal_pid_controller_is_oscillating(hal_pid_controller_t controller,
                                       float current_error,
                                       int window_size) {
  PIDController *pid = hal_pid_get(controller);
  return (pid != nullptr)
             ? pid->isOscillating(current_error, window_size)
             : false;
}
