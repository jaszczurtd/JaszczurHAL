/**
 * @file app.c
 * @brief Portable PGA2311 stereo volume example over HAL SPI.
 *
 * Demonstrates:
 * - SPI bus initialization
 * - PGA2311 handle creation
 * - Gain stepping in 0.5 dB units
 * - Mute/unmute control with software fallback (no mute pin)
 */

#include <hal/hal_app.h>
#include <hal/hal_pga2311.h>
#include <hal/hal_serial.h>
#include <hal/hal_spi.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>

#include <stdbool.h>
#include <stdint.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_SPI_MISO 16u
#define EXAMPLE_SPI_MOSI 19u
#define EXAMPLE_SPI_SCK  18u
#define EXAMPLE_PGA_CS   17u
#else
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK  5u
#define EXAMPLE_PGA_CS   4u
#endif

#define ARRAY_LEN(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

static hal_pga2311_t s_pga = NULL;
static uint32_t s_last_step_ms = 0u;
static uint32_t s_gain_idx = 0u;
static bool s_mute_phase = false;

static const int16_t s_gain_half_db_steps[] = {
    -160, -120, -80, -40, -20, 0, 20, 40, 63
};

static void print_u32(uint32_t v) {
    char buf[11];
    int i = (int)sizeof(buf) - 1;
    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0);
    hal_serial_print(&buf[i]);
}

static void print_half_db(int16_t half_db) {
    if (half_db < 0) {
        hal_serial_print("-");
        half_db = (int16_t)(-half_db);
    }
    print_u32((uint32_t)(half_db / 2));
    hal_serial_print((half_db & 1) ? ".5 dB" : ".0 dB");
}

void app_start(void) {
    hal_serial_begin(115200);
    hal_serial_println("");
    hal_serial_println("=== JaszczurHAL PGA2311 example ===");

    hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);

    hal_pga2311_config_t cfg = hal_pga2311_default_config();
    cfg.spi_bus = 0u;
    cfg.cs_pin = EXAMPLE_PGA_CS;
    cfg.mute_pin = HAL_PGA2311_MUTE_PIN_NONE;
    cfg.spi_clock_hz = HAL_PGA2311_SPI_DEFAULT_HZ;
    cfg.start_muted = false;

    s_pga = hal_pga2311_init(&cfg);
    if (!s_pga) {
        hal_serial_println("PGA2311 init FAILED");
        return;
    }

    if (hal_pga2311_set_gain_half_db(s_pga, -40, -40)) {
        hal_serial_println("Initial gain set to -20.0 dB");
    }
}

void app_task0(void) {
    if (!s_pga) {
        hal_delay_ms(1000);
        return;
    }

    const uint32_t now = hal_millis();
    if ((now - s_last_step_ms) < 1000u) {
        return;
    }
    s_last_step_ms = now;

    if (s_mute_phase) {
        if (hal_pga2311_set_mute(s_pga, false)) {
            hal_serial_println("PGA2311 mute OFF");
        }
        s_mute_phase = false;
        return;
    }

    const int16_t gain = s_gain_half_db_steps[s_gain_idx];
    if (hal_pga2311_set_gain_half_db(s_pga, gain, gain)) {
        hal_serial_print("Set gain: ");
        print_half_db(gain);
        hal_serial_println("");
    } else {
        hal_serial_println("Set gain FAILED");
    }

    s_gain_idx++;
    if (s_gain_idx >= ARRAY_LEN(s_gain_half_db_steps)) {
        s_gain_idx = 0u;
        if (hal_pga2311_set_mute(s_pga, true)) {
            hal_serial_println("PGA2311 mute ON");
            s_mute_phase = true;
        }
    }
}
