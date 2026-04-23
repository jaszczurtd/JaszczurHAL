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
