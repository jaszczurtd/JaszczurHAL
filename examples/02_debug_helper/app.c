#include <hal/hal_app.h>
#include <hal/hal_serial.h>

void app_start(void) {
    hal_debug_init(115200, NULL);
    hal_deb_set_prefix("EXAMPLE");
    hal_deb("ready");
}

void app_task0(void) {
}
