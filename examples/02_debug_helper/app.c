#include <hal/hal_app.h>
#include <tools_c.h>

void app_start(void) {
  debugInit();
  hal_deb_set_prefix("EXAMPLE");
  deb("ready");
}

void app_task0(void) {}
