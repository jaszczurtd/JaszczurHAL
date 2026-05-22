#include "../../hal_config.h"
#ifndef HAL_DISABLE_DS18B20

#include "../../hal_ds18b20.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "../../hal_timer.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#if defined(ARDUINO_ARCH_RP2040)
  #include <hardware/clocks.h>
  #include <hardware/pio.h>
#endif

static constexpr uint8_t kCmdSkipRom     = 0xCC;
static constexpr uint8_t kCmdMatchRom    = 0x55;
static constexpr uint8_t kCmdConvertT    = 0x44;
static constexpr uint8_t kCmdReadScratch = 0xBE;

enum ds18b20_state_t {
    DS18B20_STATE_IDLE = 0,
    DS18B20_STATE_CONVERTING,
};

struct hal_ds18b20_impl_s {
    bool            in_use;
    uint8_t         pin;
    bool            use_rom;
    uint8_t         rom[8];
    uint32_t        conversion_time_us;
    uint64_t        conversion_deadline_us;
    ds18b20_state_t state;
    bool            sample_valid;
    bool            sample_fresh;
    float           last_temp_c;
    hal_mutex_t     mutex;

    hal_timer_t     conversion_timer;
    bool            conversion_timer_armed;

#if defined(ARDUINO_ARCH_RP2040)
    bool            pio_enabled;
    PIO             pio;
    uint            sm;
    uint            program_offset;
#endif
};

static hal_ds18b20_impl_t s_pool[HAL_DS18B20_MAX_INSTANCES];

#if defined(ARDUINO_ARCH_RP2040)
static hal_mutex_t s_pio_claim_mutex = NULL;

static void ds18b20_ensure_pio_claim_mutex(void) {
    if (!s_pio_claim_mutex) {
        hal_critical_section_enter();
        if (!s_pio_claim_mutex) {
            s_pio_claim_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

static inline uint16_t ow_make_idle_instruction(void) {
    // Equivalent to the old raw opcode 0xA042 (MOV y, y) without any delay.
    return (uint16_t)(pio_encode_mov(pio_y, pio_y) | pio_encode_delay(0));
}

static inline pio_program_t ow_make_single_instruction_program(const uint16_t *instruction_ptr) {
    pio_program_t program = {
        instruction_ptr,
        1,
        -1,
        0,
#if PICO_PIO_VERSION > 0
        0,
#endif
    };
    return program;
}
#endif

static hal_ds18b20_resolution_t normalize_resolution(hal_ds18b20_resolution_t r) {
    switch (r) {
        case HAL_DS18B20_RES_9_BIT:
        case HAL_DS18B20_RES_10_BIT:
        case HAL_DS18B20_RES_11_BIT:
        case HAL_DS18B20_RES_12_BIT:
            return r;
        default:
            return HAL_DS18B20_RES_12_BIT;
    }
}

static uint32_t conversion_time_us_from_resolution(hal_ds18b20_resolution_t r) {
    switch (normalize_resolution(r)) {
        case HAL_DS18B20_RES_9_BIT:  return 93750u;
        case HAL_DS18B20_RES_10_BIT: return 187500u;
        case HAL_DS18B20_RES_11_BIT: return 375000u;
        default:                     return 750000u;
    }
}

static uint32_t conversion_time_us_from_cfg(uint8_t cfg) {
    switch ((cfg >> 5) & 0x03u) {
        case 0u: return 93750u;
        case 1u: return 187500u;
        case 2u: return 375000u;
        default: return 750000u;
    }
}

#if defined(ARDUINO_ARCH_RP2040)

static inline uint ow_delay_field_from_us(uint32_t us_cycles) {
    // PIO instruction consumes 1 cycle + delay field [0..31].
    if (us_cycles <= 1u) {
        return 0u;
    }
    if (us_cycles > 32u) {
        return pio_encode_delay(31u);
    }
    return pio_encode_delay(us_cycles - 1u);
}

static inline void ow_pio_exec(hal_ds18b20_impl_t *h, uint instr) {
    pio_sm_exec_wait_blocking(h->pio, h->sm, instr);
}

static inline void ow_pio_delay_us(hal_ds18b20_impl_t *h, uint32_t us) {
    while (us > 0u) {
        uint32_t chunk = (us > 32u) ? 32u : us;
        ow_pio_exec(h, pio_encode_nop() | ow_delay_field_from_us(chunk));
        us -= chunk;
    }
}

static inline void ow_pio_drive_low(hal_ds18b20_impl_t *h) {
    // Pin output value is kept at 0; toggling pindir emulates open-drain.
    ow_pio_exec(h, pio_encode_set(pio_pindirs, 1));
}

static inline void ow_pio_release(hal_ds18b20_impl_t *h) {
    ow_pio_exec(h, pio_encode_set(pio_pindirs, 0));
}

static inline bool ow_pio_read_line(hal_ds18b20_impl_t *h) {
    // Clear ISR, shift in one pin bit, push and read back.
    ow_pio_exec(h, pio_encode_mov(pio_isr, pio_null));
    ow_pio_exec(h, pio_encode_in(pio_pins, 1));
    ow_pio_exec(h, pio_encode_push(false, true));
    return pio_sm_get_blocking(h->pio, h->sm) != 0u;
}

static bool ow_pio_claim(hal_ds18b20_impl_t *h) {
    ds18b20_ensure_pio_claim_mutex();
    hal_mutex_lock(s_pio_claim_mutex);

    const uint16_t program_instruction = ow_make_idle_instruction();
    const pio_program_t program = ow_make_single_instruction_program(&program_instruction);

    PIO pio = NULL;
    uint sm = 0u;
    uint offset = 0u;
    const bool ok = pio_claim_free_sm_and_add_program_for_gpio_range(
        &program, &pio, &sm, &offset, h->pin, 1u, true
    );

    if (!ok || !pio) {
        hal_mutex_unlock(s_pio_claim_mutex);
        return false;
    }

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset);
    sm_config_set_set_pins(&c, h->pin, 1);
    sm_config_set_in_pins(&c, h->pin);
    sm_config_set_in_shift(&c, false, false, 32);

    const float pio_div = (float)clock_get_hz(clk_sys) / 1000000.0f;
    sm_config_set_clkdiv(&c, pio_div);

    const int rc_init = pio_sm_init(pio, sm, offset, &c);
    if (rc_init != PICO_OK) {
        pio_remove_program_and_unclaim_sm(&program, pio, sm, offset);
        hal_mutex_unlock(s_pio_claim_mutex);
        return false;
    }

    h->pio = pio;
    h->sm = sm;
    h->program_offset = offset;
    h->pio_enabled = true;

    pio_gpio_init(pio, h->pin);
    gpio_pull_up(h->pin);
    pio_sm_set_enabled(pio, sm, true);
    // Ensure line low value is latched and line starts released.
    ow_pio_exec(h, pio_encode_set(pio_pins, 0));
    ow_pio_release(h);

    hal_mutex_unlock(s_pio_claim_mutex);
    return true;
}

static void ow_pio_release_engine(hal_ds18b20_impl_t *h) {
    if (!h->pio_enabled) {
        return;
    }

    ds18b20_ensure_pio_claim_mutex();
    hal_mutex_lock(s_pio_claim_mutex);

    const uint16_t program_instruction = ow_make_idle_instruction();
    const pio_program_t program = ow_make_single_instruction_program(&program_instruction);

    pio_sm_set_enabled(h->pio, h->sm, false);
    pio_remove_program_and_unclaim_sm(&program, h->pio, h->sm, h->program_offset);

    h->pio_enabled = false;
    h->pio = NULL;
    h->sm = 0u;
    h->program_offset = 0u;

    hal_mutex_unlock(s_pio_claim_mutex);
}

static bool ow_reset_presence(hal_ds18b20_impl_t *h) {
    ow_pio_drive_low(h);
    ow_pio_delay_us(h, 480u);
    ow_pio_release(h);
    ow_pio_delay_us(h, 70u);
    const bool present = !ow_pio_read_line(h);
    ow_pio_delay_us(h, 410u);
    return present;
}

static void ow_write_bit(hal_ds18b20_impl_t *h, bool bit) {
    // 1-Wire write-1 slot has a 6 us low pulse - if the CPU is preempted
    // between drive_low and the PIO delay submission, the low time extends
    // and the slave may latch a 0 instead of a 1. Hold interrupts off for
    // the whole slot to keep timing tight on RP2040.
    hal_critical_section_enter();
    ow_pio_drive_low(h);
    if (bit) {
        // Keep the low pulse short for write-1.
        ow_pio_delay_us(h, 6u);
        ow_pio_release(h);
        ow_pio_delay_us(h, 64u);
    } else {
        ow_pio_delay_us(h, 60u);
        ow_pio_release(h);
        ow_pio_delay_us(h, 10u);
    }
    hal_critical_section_exit();
}

static bool ow_read_bit(hal_ds18b20_impl_t *h) {
    // Read slot: master must sample within ~15 us of slot start. The slave
    // releases the line shortly after, so any preemption between the 9 us
    // post-release delay and the actual sample produces a wrong bit.
    // A critical section keeps the sample window deterministic.
    hal_critical_section_enter();
    ow_pio_drive_low(h);
    ow_pio_delay_us(h, 6u);
    ow_pio_release(h);
    ow_pio_delay_us(h, 9u);
    const bool bit = ow_pio_read_line(h);
    ow_pio_delay_us(h, 55u);
    hal_critical_section_exit();
    return bit;
}

#else

static inline void ow_drive_low(uint8_t pin) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

static inline void ow_release(uint8_t pin) {
    pinMode(pin, INPUT_PULLUP);
}

static inline bool ow_read_line(uint8_t pin) {
    return digitalRead(pin) == HIGH;
}

static bool ow_reset_presence(hal_ds18b20_impl_t *h) {
    hal_critical_section_enter();
    ow_drive_low(h->pin);
    delayMicroseconds(480);
    ow_release(h->pin);
    delayMicroseconds(70);
    const bool present = !ow_read_line(h->pin);
    delayMicroseconds(410);
    hal_critical_section_exit();
    return present;
}

static void ow_write_bit(hal_ds18b20_impl_t *h, bool bit) {
    hal_critical_section_enter();
    ow_drive_low(h->pin);
    if (bit) {
        delayMicroseconds(6);
        ow_release(h->pin);
        delayMicroseconds(64);
    } else {
        delayMicroseconds(60);
        ow_release(h->pin);
        delayMicroseconds(10);
    }
    hal_critical_section_exit();
}

static bool ow_read_bit(hal_ds18b20_impl_t *h) {
    hal_critical_section_enter();
    ow_drive_low(h->pin);
    delayMicroseconds(6);
    ow_release(h->pin);
    delayMicroseconds(9);
    const bool bit = ow_read_line(h->pin);
    delayMicroseconds(55);
    hal_critical_section_exit();
    return bit;
}

#endif

static void ow_write_byte(hal_ds18b20_impl_t *h, uint8_t b) {
    for (int i = 0; i < 8; ++i) {
        ow_write_bit(h, (b & 0x01u) != 0u);
        b >>= 1;
    }
}

static uint8_t ow_read_byte(hal_ds18b20_impl_t *h) {
    uint8_t v = 0u;
    for (int i = 0; i < 8; ++i) {
        if (ow_read_bit(h)) {
            v |= (uint8_t)(1u << i);
        }
    }
    return v;
}

static uint8_t ow_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0u;
    for (size_t i = 0; i < len; ++i) {
        uint8_t in = data[i];
        for (uint8_t b = 0; b < 8u; ++b) {
            const uint8_t mix = (crc ^ in) & 0x01u;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8Cu;
            }
            in >>= 1;
        }
    }
    return crc;
}

static void ow_select_sensor(hal_ds18b20_impl_t *h) {
    if (h->use_rom) {
        ow_write_byte(h, kCmdMatchRom);
        for (int i = 0; i < 8; ++i) {
            ow_write_byte(h, h->rom[i]);
        }
    } else {
        ow_write_byte(h, kCmdSkipRom);
    }
}

static void ds18b20_conversion_timer_noop_cb(hal_timer_t, void *) {
}

static bool ds18b20_read_scratchpad(hal_ds18b20_impl_t *h, uint8_t scratch[9]) {
    if (!ow_reset_presence(h)) {
        return false;
    }
    ow_select_sensor(h);
    ow_write_byte(h, kCmdReadScratch);
    for (int i = 0; i < 9; ++i) {
        scratch[i] = ow_read_byte(h);
    }
    return ow_crc8(scratch, 8u) == scratch[8];
}

hal_ds18b20_t hal_ds18b20_init(const hal_ds18b20_config_t *cfg) {
    if (!cfg) return NULL;

    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < HAL_DS18B20_MAX_INSTANCES; ++i) {
        if (!s_pool[i].in_use) {
            slot = i;
            s_pool[i].in_use = true;
            break;
        }
    }
    hal_critical_section_exit();

    HAL_ASSERT(slot >= 0, "hal_ds18b20: pool exhausted - increase HAL_DS18B20_MAX_INSTANCES");
    if (slot < 0) return NULL;

    hal_ds18b20_impl_t *h = &s_pool[slot];
    memset(h, 0, sizeof(*h));
    h->in_use = true;
    h->pin = cfg->data_pin;
    h->use_rom = cfg->use_rom;
    memcpy(h->rom, cfg->rom_code, sizeof(h->rom));
    h->conversion_time_us = conversion_time_us_from_resolution(cfg->resolution_hint);
    h->state = DS18B20_STATE_IDLE;
    h->sample_valid = false;
    h->sample_fresh = false;
    h->last_temp_c = NAN;
    h->mutex = hal_mutex_create();
    if (!h->mutex) {
        h->in_use = false;
        return NULL;
    }
    h->conversion_timer = NULL;
    h->conversion_timer_armed = false;

    hal_timer_t conversion_timer = NULL;
    const hal_timer_result_t timer_create_result = hal_timer_create(HAL_TIMER_POOL_DEFAULT,
                                                                     h->conversion_time_us,
                                                                     false,
                                                                     ds18b20_conversion_timer_noop_cb,
                                                                     NULL,
                                                                     &conversion_timer);
    if (timer_create_result == HAL_TIMER_OK) {
        h->conversion_timer = conversion_timer;
    }

#if defined(ARDUINO_ARCH_RP2040)
    if (!ow_pio_claim(h)) {
        if (h->conversion_timer) {
            (void)hal_timer_destroy(h->conversion_timer);
            h->conversion_timer = NULL;
        }
        hal_mutex_destroy(h->mutex);
        h->mutex = NULL;
        h->in_use = false;
        return NULL;
    }
#else
    ow_release(h->pin);
#endif

    if (!ow_reset_presence(h)) {
#if defined(ARDUINO_ARCH_RP2040)
        ow_pio_release_engine(h);
#endif
        if (h->conversion_timer) {
            (void)hal_timer_destroy(h->conversion_timer);
            h->conversion_timer = NULL;
        }
        hal_mutex_destroy(h->mutex);
        h->mutex = NULL;
        h->in_use = false;
        return NULL;
    }

    uint8_t scratch[9] = {};
    if (ds18b20_read_scratchpad(h, scratch)) {
        h->conversion_time_us = conversion_time_us_from_cfg(scratch[4]);
    }
    return h;
}

void hal_ds18b20_deinit(hal_ds18b20_t h) {
    if (!h) return;

    hal_timer_t timer_to_destroy = NULL;
    hal_mutex_lock(h->mutex);

    if (h->conversion_timer) {
        (void)hal_timer_stop(h->conversion_timer);
        timer_to_destroy = h->conversion_timer;
        h->conversion_timer = NULL;
    }
    h->conversion_timer_armed = false;

#if defined(ARDUINO_ARCH_RP2040)
    ow_pio_release(h);
#else
    ow_release(h->pin);
#endif

    h->in_use = false;
    hal_mutex_t m = h->mutex;
    h->mutex = NULL;
    hal_mutex_unlock(m);
    hal_mutex_destroy(m);

#if defined(ARDUINO_ARCH_RP2040)
    ow_pio_release_engine(h);
#endif

    if (timer_to_destroy) {
        (void)hal_timer_destroy(timer_to_destroy);
    }
}

bool hal_ds18b20_request(hal_ds18b20_t h) {
    if (!h) return false;

    hal_mutex_lock(h->mutex);
    if (h->state == DS18B20_STATE_CONVERTING) {
        hal_mutex_unlock(h->mutex);
        return false;
    }

    if (!ow_reset_presence(h)) {
        hal_mutex_unlock(h->mutex);
        return false;
    }

    ow_select_sensor(h);
    ow_write_byte(h, kCmdConvertT);

    h->conversion_deadline_us = hal_micros64() + h->conversion_time_us;
    h->conversion_timer_armed = false;

    if (h->conversion_timer) {
        (void)hal_timer_set_period_us(h->conversion_timer, h->conversion_time_us, false);
        hal_timer_result_t start_result = hal_timer_start(h->conversion_timer);
        if (start_result == HAL_TIMER_ERR_ALREADY_RUNNING) {
            (void)hal_timer_stop(h->conversion_timer);
            start_result = hal_timer_start(h->conversion_timer);
        }
        h->conversion_timer_armed = (start_result == HAL_TIMER_OK);
    }

    h->state = DS18B20_STATE_CONVERTING;
    hal_mutex_unlock(h->mutex);
    return true;
}

void hal_ds18b20_poll(hal_ds18b20_t h) {
    if (!h) return;

    hal_mutex_lock(h->mutex);
    if (h->state != DS18B20_STATE_CONVERTING) {
        hal_mutex_unlock(h->mutex);
        return;
    }

    const uint64_t now = hal_micros64();
    bool conversion_ready = false;
    if (h->conversion_timer_armed && h->conversion_timer) {
        const hal_timer_state_t timer_state = hal_timer_get_state(h->conversion_timer);
        if (timer_state == HAL_TIMER_STATE_RUNNING) {
            conversion_ready = (now >= h->conversion_deadline_us);
        } else {
            conversion_ready = true;
        }
    } else {
        conversion_ready = (now >= h->conversion_deadline_us);
    }

    if (!conversion_ready) {
        hal_mutex_unlock(h->mutex);
        return;
    }

    uint8_t scratch[9] = {};
    if (ds18b20_read_scratchpad(h, scratch)) {
        const int16_t raw = (int16_t)(((uint16_t)scratch[1] << 8) | scratch[0]);
        h->last_temp_c = (float)raw / 16.0f;
        h->sample_valid = true;
        h->sample_fresh = true;
        h->conversion_time_us = conversion_time_us_from_cfg(scratch[4]);
    }

    h->conversion_timer_armed = false;
    h->state = DS18B20_STATE_IDLE;
    hal_mutex_unlock(h->mutex);
}

bool hal_ds18b20_is_busy(hal_ds18b20_t h) {
    if (!h) return false;
    hal_mutex_lock(h->mutex);
    const bool busy = (h->state == DS18B20_STATE_CONVERTING);
    hal_mutex_unlock(h->mutex);
    return busy;
}

bool hal_ds18b20_take_latest(hal_ds18b20_t h, float *temp_c, bool *fresh) {
    if (!h || !temp_c) return false;
    hal_mutex_lock(h->mutex);
    if (!h->sample_valid) {
        hal_mutex_unlock(h->mutex);
        return false;
    }
    *temp_c = h->last_temp_c;
    if (fresh) {
        *fresh = h->sample_fresh;
    }
    h->sample_fresh = false;
    hal_mutex_unlock(h->mutex);
    return true;
}

#endif /* HAL_DISABLE_DS18B20 */
