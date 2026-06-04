/*
 * portable_blink - RP2040 / Arduino entry shim.
 *
 * The actual logic lives in blink_app.c (portable hal_* only). This sketch is
 * just the Arduino-style entry point; the same blink_app is driven from a
 * bare-metal main() on STM32G474 (see g474/main.c).
 *
 * Build: open this folder in VS Code (Ctrl+Shift+B) or
 *   arduino-cli compile --fqbn rp2040:rp2040:rpipico .
 * LED: Pico onboard LED (GP25). Console: USB serial @ 115200.
 */

/* Pull in the library umbrella so arduino-cli attaches JaszczurHAL and puts
 * its src/ on the include path - that is what lets blink_app.c resolve its
 * <hal/...> includes (every other example relies on this too). */
#include <JaszczurHAL.h>

#include "blink_app.h"

void setup() {
    blink_app_setup();
}

void loop() {
    blink_app_step();
}
