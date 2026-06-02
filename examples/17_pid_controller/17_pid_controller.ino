#include <JaszczurHAL.h>
#include <tools.h>

static PIDController pid(1.2f, 0.03f, 0.08f, 100.0f);
static float process_value = 0.0f;
static const float setpoint = 100.0f;
static uint32_t last_update_ms = 0;

void setup() {
  hal_debug_init(115200);

  pid.setOutputLimits(-25.0f, 25.0f);
  pid.setTf(0.05f);
  pid.updatePIDtime(1.0f);
}

void loop() {
  const uint32_t now = hal_millis();
  if (now - last_update_ms < 200u) {
    return;
  }
  last_update_ms = now;

  const float error = setpoint - process_value;
  const float control = pid.updatePIDcontroller(error);
  process_value += control * 0.05f;

  hal_deb("PID: set=%.1f pv=%.2f err=%.2f out=%.2f stable=%d osc=%d",
          setpoint,
          process_value,
          error,
          control,
          pid.isErrorStable(error, 1.0f, 10) ? 1 : 0,
          pid.isOscillating(error, 10) ? 1 : 0);
}
