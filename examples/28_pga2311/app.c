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
#include <hal/hal_spi.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#include <stdbool.h>
#include <stdint.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_SPI_MISO 16u
#define EXAMPLE_SPI_MOSI 19u
#define EXAMPLE_SPI_SCK 18u
#define EXAMPLE_PGA_CS 17u
#else
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_PGA_CS 4u
#endif

#define ARRAY_LEN(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

static hal_pga2311_t s_pga = NULL;
static uint32_t s_last_step_ms = 0u;
static uint32_t s_gain_idx = 0u;
static bool s_mute_phase = false;

static const int16_t s_gain_half_db_steps[] = {-160, -120, -80, -40, -20,
                                               0,    20,   40,  63};

static void log_half_db(const char *prefix, int16_t half_db) {
  const char *sign = "";
  if (half_db < 0) {
    sign = "-";
    half_db = (int16_t)(-half_db);
  }
  deb("%s%s%u.%u dB", prefix, sign, (unsigned)(half_db / 2),
      (half_db & 1) ? 5u : 0u);
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL PGA2311 example ===");

  hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);

  hal_pga2311_config_t cfg = hal_pga2311_default_config();
  cfg.spi_bus = 0u;
  cfg.cs_pin = EXAMPLE_PGA_CS;
  cfg.mute_pin = HAL_PGA2311_MUTE_PIN_NONE;
  cfg.spi_clock_hz = HAL_PGA2311_SPI_DEFAULT_HZ;
  cfg.start_muted = false;

  if (hal_pga2311_init_ex(&cfg, &s_pga) != HAL_OK) {
    derr("PGA2311 init FAILED");
    return;
  }

  if (hal_pga2311_set_gain_half_db_ex(s_pga, -40, -40) == HAL_OK) {
    deb("Initial gain set to -20.0 dB");
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
    if (hal_pga2311_set_mute_ex(s_pga, false) == HAL_OK) {
      deb("PGA2311 mute OFF");
    }
    s_mute_phase = false;
    return;
  }

  const int16_t gain = s_gain_half_db_steps[s_gain_idx];
  if (hal_pga2311_set_gain_half_db_ex(s_pga, gain, gain) == HAL_OK) {
    log_half_db("Set gain: ", gain);
  } else {
    derr("Set gain FAILED");
  }

  s_gain_idx++;
  if (s_gain_idx >= ARRAY_LEN(s_gain_half_db_steps)) {
    s_gain_idx = 0u;
    if (hal_pga2311_set_mute_ex(s_pga, true) == HAL_OK) {
      deb("PGA2311 mute ON");
      s_mute_phase = true;
    }
  }
}
