/**
 * @file main.c
 * @brief STM32G474 (Nucleo-G474RE) bare-metal entry shim for portable_blink.
 *
 * The blink logic is the shared, portable blink_app (../blink_app.c). This
 * file only provides the bare-metal entry point and the one capability that is
 * not yet behind a portable hal_* facade: the detailed Cortex-M fault dump.
 */

#include "../blink_app.h"
#include "port/exception_info.h"

int main(void) {
    blink_app_setup();

    /* Platform extra: if the previous reset was a fault, dump the captured
     * register frame (the portable reset reason is already printed by
     * blink_app_setup()). */
    exception_info_report_last();

    for (;;) {
        blink_app_step();
    }
}
