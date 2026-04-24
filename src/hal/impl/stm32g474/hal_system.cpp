#include "../../hal_system.h"

#include <string.h>

static uint32_t s_millis = 0u;
static uint32_t s_micros = 0u;
static bool s_watchdog_fed = false;
static bool s_watchdog_caused_reboot = false;
static uint32_t s_free_heap = 0u;
static float s_chip_temp_c = 0.0f;

/* Placeholder UID used until STM32 UID registers are wired in. */
static uint8_t s_device_uid[HAL_DEVICE_UID_BYTES] = {
    0x47, 0x34, 0x74, 0x00, 0x00, 0x00, 0x00, 0x01
};

uint32_t hal_millis(void) {
    return s_millis;
}

uint32_t hal_micros(void) {
    return s_micros;
}

uint64_t hal_micros64(void) {
    return (uint64_t)s_micros;
}

void hal_delay_ms(uint32_t ms) {
    s_millis += ms;
    s_micros += (ms * 1000u);
}

void hal_delay_us(uint32_t us) {
    s_micros += us;
    s_millis = s_micros / 1000u;
}

void hal_watchdog_feed(void) {
    s_watchdog_fed = true;
}

void hal_watchdog_enable(uint32_t ms, bool pause_on_debug) {
    (void)ms;
    (void)pause_on_debug;
    s_watchdog_fed = false;
}

bool hal_watchdog_caused_reboot(void) {
    return s_watchdog_caused_reboot;
}

void hal_idle(void) {
    /* STM32G474 TODO: add low-power wait-for-interrupt / cooperative yield. */
}

uint32_t hal_get_free_heap(void) {
    return s_free_heap;
}

float hal_read_chip_temp(void) {
    return s_chip_temp_c;
}

void hal_enter_bootloader(void) {
    /* STM32G474 TODO: jump to STM32 system bootloader. */
}

void hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]) {
    if (uid == nullptr) {
        return;
    }

    memcpy(uid, s_device_uid, HAL_DEVICE_UID_BYTES);
}

bool hal_get_device_uid_hex(char *buf, size_t buflen) {
    if (buf == nullptr) {
        return false;
    }
    if (buflen < HAL_DEVICE_UID_HEX_BUF_SIZE) {
        return false;
    }

    static const char kHex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < HAL_DEVICE_UID_BYTES; ++i) {
        buf[(i * 2u) + 0u] = kHex[(s_device_uid[i] >> 4) & 0x0Fu];
        buf[(i * 2u) + 1u] = kHex[s_device_uid[i] & 0x0Fu];
    }
    buf[HAL_DEVICE_UID_BYTES * 2u] = '\0';
    return true;
}
