/**
 * @file stm32g474_system.cpp
 * @brief STM32G474 SoC-specific system services (host-stub implementation).
 *
 * See @c stm32g474_system.h for the contract. This file compiles on both
 * the host (CMake @c build_stm32_host gate) and on a real ARM toolchain;
 * Cortex-M specific bits are guarded by @c __arm__ / @c __thumb__.
 */

#include "stm32g474_system.h"

#include <string.h>

namespace {

uint32_t g_millis = 0u;
uint32_t g_micros = 0u;
bool     g_watchdog_fed = false;
bool     g_watchdog_caused_reboot = false;
uint32_t g_free_heap = 0u;
float    g_chip_temp_c = 0.0f;

/* Placeholder UID used until the STM32 @c UID_BASE read is wired in. */
uint8_t g_device_uid[8] = {
    0x47, 0x34, 0x74, 0x00, 0x00, 0x00, 0x00, 0x01
};

} // namespace

uint32_t stm32g474_system_millis(void) {
    return g_millis;
}

uint32_t stm32g474_system_micros(void) {
    return g_micros;
}

uint64_t stm32g474_system_micros64(void) {
    return (uint64_t)g_micros;
}

void stm32g474_system_delay_ms(uint32_t ms) {
    g_millis += ms;
    g_micros += (ms * 1000u);
}

void stm32g474_system_delay_us(uint32_t us) {
    g_micros += us;
    g_millis = g_micros / 1000u;
}

void stm32g474_system_watchdog_feed(void) {
    g_watchdog_fed = true;
}

void stm32g474_system_watchdog_enable(uint32_t ms, bool pause_on_debug) {
    (void)ms;
    (void)pause_on_debug;
    g_watchdog_fed = false;
}

bool stm32g474_system_watchdog_caused_reboot(void) {
    return g_watchdog_caused_reboot;
}

void stm32g474_system_idle(void) {
    /* STM32G474 TODO: __WFI(). */
}

bool stm32g474_system_in_isr(void) {
#if defined(__arm__) || defined(__thumb__) || defined(__aarch64__)
    /* On Cortex-M, IPSR is zero in Thread mode and equal to the active
     * exception number in Handler mode. Mask to the documented 9-bit
     * exception-number field. */
    uint32_t ipsr;
    __asm__ __volatile__("MRS %0, ipsr" : "=r"(ipsr));
    return (ipsr & 0x1FFu) != 0u;
#else
    /* Host/sanity builds of stm32_lib use a desktop compiler and cannot
     * execute Cortex-M specific instructions. */
    return false;
#endif
}

uint32_t stm32g474_system_get_free_heap(void) {
    return g_free_heap;
}

float stm32g474_system_read_chip_temp(void) {
    return g_chip_temp_c;
}

void stm32g474_system_enter_bootloader(void) {
    /* STM32G474 TODO: deinit + jump to STM32 system bootloader. */
}

void stm32g474_system_get_device_uid(uint8_t *uid) {
    if (uid == nullptr) {
        return;
    }
    memcpy(uid, g_device_uid, 8u);
}

bool stm32g474_system_get_device_uid_hex(char *buf, size_t buflen) {
    constexpr size_t kUidBytes = 8u;
    constexpr size_t kHexBufSize = (kUidBytes * 2u) + 1u;
    if (buf == nullptr) {
        return false;
    }
    if (buflen < kHexBufSize) {
        return false;
    }
    uint8_t uid[kUidBytes];
    stm32g474_system_get_device_uid(uid);
    static const char kHex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < kUidBytes; ++i) {
        buf[(i * 2u) + 0u] = kHex[(uid[i] >> 4) & 0x0Fu];
        buf[(i * 2u) + 1u] = kHex[uid[i] & 0x0Fu];
    }
    buf[kUidBytes * 2u] = '\0';
    return true;
}
