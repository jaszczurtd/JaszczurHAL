#include "../../hal_system.h"
#include <Arduino.h>
#include <pico/bootrom.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>
#include <hardware/watchdog.h>
#include <string.h>

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

namespace {
// Latched once, during C++ static initialization -- i.e. BEFORE setup() runs
// and therefore before any hal_watchdog_enable() call can rewrite watchdog
// scratch register 4. This lets us tell apart a genuine application watchdog
// *timeout* (the watchdog was armed via watchdog_enable(), which writes
// WATCHDOG_NON_REBOOT_MAGIC into scratch[4]) from a *commanded* reboot through
// watchdog_reboot() -- used by picotool upload, BOOTSEL/UF2 relaunch and
// rp2040.reboot() -- which sets scratch[4] to 0xb007c0d3 or 0. watchdog_reboot
// also uses the watchdog hardware, so plain watchdog_caused_reboot() reports
// true for a fresh flash and makes every upload look like a watchdog
// starvation. watchdog_enable_caused_reboot() additionally checks the scratch
// marker, so it is true ONLY for a real timeout after watchdog_enable().
bool g_watchdog_timeout_boot = false;

__attribute__((constructor)) void hal_watchdog_latch_boot_reason(void) {
    g_watchdog_timeout_boot = watchdog_enable_caused_reboot();
}
} // namespace

bool hal_watchdog_caused_reboot(void) {
    return g_watchdog_timeout_boot;
}

void hal_idle(void) {
    tight_loop_contents();
}

bool hal_in_isr(void) {
    /* RP2040 is Cortex-M0+. IPSR is zero in Thread mode and equal to the
     * active exception number in Handler mode. */
    uint32_t ipsr;
    __asm__ __volatile__("MRS %0, ipsr" : "=r"(ipsr));
    return (ipsr & 0x1FFu) != 0u;
}

uint32_t hal_get_free_heap(void) {
    return rp2040.getFreeHeap();
}

float hal_read_chip_temp(void) {
    return analogReadTemp();
}

void hal_enter_bootloader(void) {
    reset_usb_boot(0, 0);
    while (true) {
        tight_loop_contents();
    }
}

void hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]) {
    if (uid == nullptr) {
        return;
    }
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    /* PICO_UNIQUE_BOARD_ID_SIZE_BYTES is defined as 8. */
    memcpy(uid, id.id, HAL_DEVICE_UID_BYTES);
}

bool hal_get_device_uid_hex(char *buf, size_t buflen) {
    if (buf == nullptr) {
        return false;
    }
    if (buflen < HAL_DEVICE_UID_HEX_BUF_SIZE) {
        return false;
    }
    uint8_t uid[HAL_DEVICE_UID_BYTES];
    hal_get_device_uid(uid);
    static const char kHex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < HAL_DEVICE_UID_BYTES; ++i) {
        buf[(i * 2u) + 0u] = kHex[(uid[i] >> 4) & 0x0Fu];
        buf[(i * 2u) + 1u] = kHex[uid[i] & 0x0Fu];
    }
    buf[HAL_DEVICE_UID_BYTES * 2u] = '\0';
    return true;
}
