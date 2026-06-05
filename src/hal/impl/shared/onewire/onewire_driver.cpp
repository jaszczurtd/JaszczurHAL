/*
 * 1-Wire timing, ROM search and CRC logic is modeled after Paul Stoffregen's
 * OneWire Arduino library, originally by Jim Studt. This implementation was
 * rewritten as an Arduino-free JaszczurHAL shared driver: it keeps the proven
 * reset, write/read slot timing, search state machine and Dallas/Maxim CRC
 * routines, but uses only HAL GPIO, delay and critical-section primitives.
 *
 * OneWire library
 *
 * Copyright (c) 2007, Jim Studt
 * Copyright (c) 2010, Paul Stoffregen
 * Copyright (c) 2013, Robin James
 * Copyright (c) 2013, Glenn Trewitt
 * Copyright (c) 2013, Jason Dangel
 * Copyright (c) 2014, Matthijs Kooijman
 * Copyright (c) 2014, Sean Hickey
 * Copyright (c) 2014, Josh Larios
 * Copyright (c) 2014, Guillermo Lovato
 * Copyright (c) 2015, Michael Markstaller
 * Copyright (c) 2015, Roger Clark
 * Copyright (c) 2015, Love Nystrom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "../../../hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "../../../hal_config.h"
#ifdef HAL_ENABLE_ONEWIRE

#include "onewire_driver.h"

#include "../../../hal_gpio.h"
#include "../../../hal_sync.h"
#include "../../../hal_system.h"

#include <stddef.h>

#define JH_ONEWIRE_CMD_MATCH_ROM        (0x55u)
#define JH_ONEWIRE_CMD_SKIP_ROM         (0xCCu)
#define JH_ONEWIRE_CMD_SEARCH_ROM       (0xF0u)
#define JH_ONEWIRE_CMD_CONDITIONAL_ROM  (0xECu)

static inline void onewire_release(uint8_t pin) {
    hal_gpio_set_mode(pin, HAL_GPIO_INPUT);
    hal_gpio_write(pin, false);
}

static inline void onewire_drive_low(uint8_t pin) {
    hal_gpio_write(pin, false);
    hal_gpio_set_mode(pin, HAL_GPIO_OUTPUT);
}

static inline void onewire_drive_high(uint8_t pin) {
    hal_gpio_write(pin, true);
    hal_gpio_set_mode(pin, HAL_GPIO_OUTPUT);
}

JHOneWire::JHOneWire()
    : pin_(0),
      rom_no_{0, 0, 0, 0, 0, 0, 0, 0},
      last_discrepancy_(0),
      last_family_discrepancy_(0),
      last_device_flag_(false) {
}

JHOneWire::JHOneWire(uint8_t pin) : JHOneWire() {
    begin(pin);
}

void JHOneWire::begin(uint8_t pin) {
    pin_ = pin;
    onewire_release(pin_);
    reset_search();
}

uint8_t JHOneWire::reset(void) {
    uint8_t retries = 125u;

    hal_critical_section_enter();
    onewire_release(pin_);
    hal_critical_section_exit();

    do {
        if (--retries == 0u) {
            return 0u;
        }
        hal_delay_us(2u);
    } while (!hal_gpio_read(pin_));

    hal_critical_section_enter();
    onewire_drive_low(pin_);
    hal_critical_section_exit();

    hal_delay_us(480u);

    hal_critical_section_enter();
    onewire_release(pin_);
    hal_delay_us(70u);
    const uint8_t present = (uint8_t)(!hal_gpio_read(pin_));
    hal_critical_section_exit();

    hal_delay_us(410u);
    return present;
}

void JHOneWire::write_bit(uint8_t value) {
    if (value & 1u) {
        hal_critical_section_enter();
        onewire_drive_low(pin_);
        hal_delay_us(10u);
        onewire_drive_high(pin_);
        hal_critical_section_exit();
        hal_delay_us(55u);
    } else {
        hal_critical_section_enter();
        onewire_drive_low(pin_);
        hal_delay_us(65u);
        onewire_drive_high(pin_);
        hal_critical_section_exit();
        hal_delay_us(5u);
    }
}

uint8_t JHOneWire::read_bit(void) {
    hal_critical_section_enter();
    hal_gpio_set_mode(pin_, HAL_GPIO_OUTPUT);
    hal_gpio_write(pin_, false);
    hal_delay_us(3u);
    hal_gpio_set_mode(pin_, HAL_GPIO_INPUT);
    hal_delay_us(10u);
    const uint8_t value = (uint8_t)(hal_gpio_read(pin_) ? 1u : 0u);
    hal_critical_section_exit();
    hal_delay_us(53u);
    return value;
}

void JHOneWire::write(uint8_t value, uint8_t power) {
    for (uint8_t bit_mask = 0x01u; bit_mask != 0u; bit_mask <<= 1u) {
        write_bit((uint8_t)((bit_mask & value) ? 1u : 0u));
    }

    if (!power) {
        hal_critical_section_enter();
        onewire_release(pin_);
        hal_critical_section_exit();
    }
}

void JHOneWire::write_bytes(const uint8_t *buf, uint16_t count, bool power) {
    if (buf == NULL) {
        return;
    }

    for (uint16_t i = 0u; i < count; ++i) {
        write(buf[i]);
    }

    if (!power) {
        hal_critical_section_enter();
        onewire_release(pin_);
        hal_critical_section_exit();
    }
}

uint8_t JHOneWire::read(void) {
    uint8_t value = 0u;

    for (uint8_t bit_mask = 0x01u; bit_mask != 0u; bit_mask <<= 1u) {
        if (read_bit()) {
            value |= bit_mask;
        }
    }
    return value;
}

void JHOneWire::read_bytes(uint8_t *buf, uint16_t count) {
    if (buf == NULL) {
        return;
    }

    for (uint16_t i = 0u; i < count; ++i) {
        buf[i] = read();
    }
}

void JHOneWire::select(const uint8_t rom[8]) {
    if (rom == NULL) {
        return;
    }

    write(JH_ONEWIRE_CMD_MATCH_ROM);
    for (uint8_t i = 0u; i < 8u; ++i) {
        write(rom[i]);
    }
}

void JHOneWire::skip(void) {
    write(JH_ONEWIRE_CMD_SKIP_ROM);
}

void JHOneWire::depower(void) {
    hal_critical_section_enter();
    hal_gpio_set_mode(pin_, HAL_GPIO_INPUT);
    hal_critical_section_exit();
}

void JHOneWire::reset_search(void) {
    last_discrepancy_ = 0u;
    last_device_flag_ = false;
    last_family_discrepancy_ = 0u;
    for (int i = 7; ; --i) {
        rom_no_[i] = 0u;
        if (i == 0) {
            break;
        }
    }
}

void JHOneWire::target_search(uint8_t family_code) {
    rom_no_[0] = family_code;
    for (uint8_t i = 1u; i < 8u; ++i) {
        rom_no_[i] = 0u;
    }
    last_discrepancy_ = 64u;
    last_family_discrepancy_ = 0u;
    last_device_flag_ = false;
}

bool JHOneWire::search(uint8_t *new_addr, bool search_mode) {
    uint8_t id_bit_number = 1u;
    uint8_t last_zero = 0u;
    uint8_t rom_byte_number = 0u;
    uint8_t rom_byte_mask = 1u;
    bool search_result = false;
    uint8_t id_bit = 0u;
    uint8_t cmp_id_bit = 0u;
    uint8_t search_direction = 0u;

    if (new_addr == NULL) {
        return false;
    }

    if (!last_device_flag_) {
        if (!reset()) {
            last_discrepancy_ = 0u;
            last_device_flag_ = false;
            last_family_discrepancy_ = 0u;
            return false;
        }

        write(search_mode ? JH_ONEWIRE_CMD_SEARCH_ROM
                          : JH_ONEWIRE_CMD_CONDITIONAL_ROM);

        do {
            id_bit = read_bit();
            cmp_id_bit = read_bit();

            if ((id_bit == 1u) && (cmp_id_bit == 1u)) {
                break;
            } else {
                if (id_bit != cmp_id_bit) {
                    search_direction = id_bit;
                } else {
                    if (id_bit_number < last_discrepancy_) {
                        search_direction =
                            (uint8_t)((rom_no_[rom_byte_number] & rom_byte_mask) > 0u);
                    } else {
                        search_direction =
                            (uint8_t)(id_bit_number == last_discrepancy_);
                    }

                    if (search_direction == 0u) {
                        last_zero = id_bit_number;
                        if (last_zero < 9u) {
                            last_family_discrepancy_ = last_zero;
                        }
                    }
                }

                if (search_direction == 1u) {
                    rom_no_[rom_byte_number] |= rom_byte_mask;
                } else {
                    rom_no_[rom_byte_number] &= (uint8_t)~rom_byte_mask;
                }

                write_bit(search_direction);

                ++id_bit_number;
                rom_byte_mask <<= 1u;

                if (rom_byte_mask == 0u) {
                    ++rom_byte_number;
                    rom_byte_mask = 1u;
                }
            }
        } while (rom_byte_number < 8u);

        if (!(id_bit_number < 65u)) {
            last_discrepancy_ = last_zero;
            if (last_discrepancy_ == 0u) {
                last_device_flag_ = true;
            }
            search_result = true;
        }
    }

    if (!search_result || !rom_no_[0]) {
        last_discrepancy_ = 0u;
        last_device_flag_ = false;
        last_family_discrepancy_ = 0u;
        search_result = false;
    } else {
        for (uint8_t i = 0u; i < 8u; ++i) {
            new_addr[i] = rom_no_[i];
        }
    }

    return search_result;
}

uint8_t JHOneWire::crc8(const uint8_t *addr, uint8_t len) {
    static const uint8_t dscrc2x16_table[] = {
        0x00, 0x5E, 0xBC, 0xE2, 0x61, 0x3F, 0xDD, 0x83,
        0xC2, 0x9C, 0x7E, 0x20, 0xA3, 0xFD, 0x1F, 0x41,
        0x00, 0x9D, 0x23, 0xBE, 0x46, 0xDB, 0x65, 0xF8,
        0x8C, 0x11, 0xAF, 0x32, 0xCA, 0x57, 0xE9, 0x74
    };

    if (addr == NULL) {
        return 0u;
    }

    uint8_t crc = 0u;
    while (len--) {
        crc = (uint8_t)(*addr++ ^ crc);
        crc = (uint8_t)(dscrc2x16_table[crc & 0x0Fu] ^
                        dscrc2x16_table[16u + ((crc >> 4u) & 0x0Fu)]);
    }
    return crc;
}

bool JHOneWire::check_crc16(const uint8_t *input,
                            uint16_t len,
                            const uint8_t *inverted_crc,
                            uint16_t crc) {
    if (input == NULL || inverted_crc == NULL) {
        return false;
    }

    crc = (uint16_t)~crc16(input, len, crc);
    return ((crc & 0xFFu) == inverted_crc[0]) &&
           ((crc >> 8u) == inverted_crc[1]);
}

uint16_t JHOneWire::crc16(const uint8_t *input, uint16_t len, uint16_t crc) {
    static const uint8_t oddparity[16] = {
        0u, 1u, 1u, 0u, 1u, 0u, 0u, 1u,
        1u, 0u, 0u, 1u, 0u, 1u, 1u, 0u
    };

    if (input == NULL) {
        return crc;
    }

    for (uint16_t i = 0u; i < len; ++i) {
        uint16_t cdata = input[i];
        cdata = (uint16_t)((cdata ^ crc) & 0xFFu);
        crc >>= 8u;

        if (oddparity[cdata & 0x0Fu] ^ oddparity[cdata >> 4u]) {
            crc ^= 0xC001u;
        }

        cdata <<= 6u;
        crc ^= cdata;
        cdata <<= 1u;
        crc ^= cdata;
    }

    return crc;
}

#endif /* HAL_ENABLE_ONEWIRE */
#endif /* supported target */
