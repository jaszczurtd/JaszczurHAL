#include "../../hal_system.h"
#include <Arduino.h>
#include <pico/stdlib.h>
#include <hardware/watchdog.h>

uint32_t hal_millis(void) {
    return millis();
}

uint32_t hal_micros(void) {
    return micros();
}

uint64_t hal_micros64(void) {
    return time_us_64();
}

void hal_delay_ms(uint32_t ms) {
    delay(ms);
}

void hal_delay_us(uint32_t us) {
    delayMicroseconds(us);
}

void hal_watchdog_feed(void) {
    watchdog_update();
}

void hal_watchdog_enable(uint32_t ms, bool pause_on_debug) {
    watchdog_enable(ms, pause_on_debug);
}

bool hal_watchdog_caused_reboot(void) {
    return watchdog_caused_reboot();
}

void hal_idle(void) {
    tight_loop_contents();
}

uint32_t hal_get_free_heap(void) {
    return rp2040.getFreeHeap();
}

float hal_read_chip_temp(void) {
    return analogReadTemp();
}
