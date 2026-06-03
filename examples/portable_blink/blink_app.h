#pragma once

/**
 * @file blink_app.h
 * @brief Portable blink application logic (backend-agnostic).
 *
 * The whole app is written against the portable `hal_*` API only, so the same
 * two functions drive the LED on every backend. Each target provides a thin
 * entry shim:
 *   - RP2040 / Arduino : portable_blink.ino  -> setup()/loop()
 *   - STM32G474        : g474/main.c         -> main() + super-loop
 *   - host mock        : unit tests          -> call directly
 */

#ifdef __cplusplus
extern "C" {
#endif

/** One-time init: serial console, device id + reset reason, LED pin. */
void blink_app_setup(void);

/** One blink iteration: toggle LED, print uptime, wait ~500 ms. */
void blink_app_step(void);

#ifdef __cplusplus
}
#endif
