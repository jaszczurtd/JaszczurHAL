#include "../../hal_system.h"
#include "hal_mock.h"

#include <string.h>

static uint32_t s_millis         = 0;
static uint32_t s_micros         = 0;
static bool     s_watchdog_fed   = false;
static bool     s_caused_reboot  = false;
static uint32_t s_free_heap      = 256 * 1024;
static bool     s_bootloader_requested = false;

/* Default deterministic mock UID so tests can rely on a stable value. */
static uint8_t s_device_uid[HAL_DEVICE_UID_BYTES] = {
    0xE6, 0x61, 0xA4, 0xD1, 0x23, 0x45, 0x67, 0xAB
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
    s_micros += ms * 1000;
}

void hal_delay_us(uint32_t us) {
    s_micros += us;
}

void hal_watchdog_feed(void) {
    s_watchdog_fed = true;
}

void hal_watchdog_enable(uint32_t ms, bool pause_on_debug) {
    (void)ms; (void)pause_on_debug;
}

bool hal_watchdog_caused_reboot(void) {
    return s_caused_reboot;
}

void hal_idle(void) {
    // no-op
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

void hal_mock_set_millis(uint32_t ms) {
    s_millis = ms;
    s_micros = ms * 1000;
}

void hal_mock_advance_millis(uint32_t ms) {
    s_millis += ms;
    s_micros += ms * 1000;
}

void hal_mock_set_micros(uint32_t us) {
    s_micros = us;
    s_millis = us / 1000;
}

void hal_mock_advance_micros(uint32_t us) {
    s_micros += us;
    s_millis = s_micros / 1000;
}

bool hal_mock_watchdog_was_fed(void) {
    return s_watchdog_fed;
}

void hal_mock_watchdog_reset_flag(void) {
    s_watchdog_fed = false;
}

void hal_mock_set_caused_reboot(bool val) {
    s_caused_reboot = val;
}

uint32_t hal_get_free_heap(void) {
    return s_free_heap;
}

void hal_mock_set_free_heap(uint32_t bytes) {
    s_free_heap = bytes;
}

static float s_chip_temp = 25.0f;

float hal_read_chip_temp(void) {
    return s_chip_temp;
}

void hal_mock_set_chip_temp(float celsius) {
    s_chip_temp = celsius;
}

void hal_enter_bootloader(void) {
    s_bootloader_requested = true;
}

bool hal_mock_bootloader_was_requested(void) {
    return s_bootloader_requested;
}

void hal_mock_bootloader_reset_flag(void) {
    s_bootloader_requested = false;
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

void hal_mock_set_device_uid(const uint8_t uid[HAL_DEVICE_UID_BYTES]) {
    if (uid == nullptr) {
        return;
    }
    memcpy(s_device_uid, uid, HAL_DEVICE_UID_BYTES);
}

void hal_mock_reset_device_uid(void) {
    static const uint8_t kDefault[HAL_DEVICE_UID_BYTES] = {
        0xE6, 0x61, 0xA4, 0xD1, 0x23, 0x45, 0x67, 0xAB
    };
    memcpy(s_device_uid, kDefault, HAL_DEVICE_UID_BYTES);
}
