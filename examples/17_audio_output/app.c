/*
 * Generate PWM audio with an ADC-controlled frequency and adjust PGA2311 gain.
 * DACless uses DMA by default; polling mode must keep servicing the output.
 */

#include <hal/audio/hal_dacless.h>
#include <hal/audio/hal_pga2311.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_array.h>
#include <hal/core/hal_target.h>
#include <hal/serial/hal_serial.h>
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_system.h>

#include <stdint.h>

#if HAL_TARGET_IS_RP
#define EXAMPLE_PGA_BUS 0u
#define EXAMPLE_PGA_MISO 16u
#define EXAMPLE_PGA_MOSI 19u
#define EXAMPLE_PGA_SCK 18u
#define EXAMPLE_PGA_CS 17u
#define EXAMPLE_AUDIO_PWM 6u
#define EXAMPLE_AUDIO_ADC 26u
#elif HAL_TARGET_IS_STM32G474
/* SPI1 uses Nucleo D13/D12/D11/D10. PWM audio uses the separate PB0 pin. */
#define EXAMPLE_PGA_BUS 0u
#define EXAMPLE_PGA_MISO 6u
#define EXAMPLE_PGA_MOSI 7u
#define EXAMPLE_PGA_SCK 5u
#define EXAMPLE_PGA_CS 22u
#define EXAMPLE_AUDIO_PWM 16u
#define EXAMPLE_AUDIO_ADC 0u
#else
#define EXAMPLE_PGA_BUS 1u
#define EXAMPLE_PGA_MISO 30u
#define EXAMPLE_PGA_MOSI 31u
#define EXAMPLE_PGA_SCK 29u
#define EXAMPLE_PGA_CS 28u
#define EXAMPLE_AUDIO_PWM 6u
#define EXAMPLE_AUDIO_ADC 0u
#endif

#define EXAMPLE_AUDIO_BLOCK_SIZE 64u

static hal_dacless_t s_audio = NULL;
static hal_pga2311_t s_pga = NULL;
static uint32_t s_phase = 0u;
static uint32_t s_phase_increment = 90000u;
static uint32_t s_last_report_ms = 0u;
static uint32_t s_last_gain_ms = 0u;
static uint32_t s_gain_index = 0u;

static const int16_t kGainHalfDb[] = {-80, -40, -20, 0, 20, 40};

static void fill_audio_block(void *context, uint16_t *buffer,
                             uint16_t sample_count) {
  (void)context;
  uint16_t control = 0u;
  if (s_audio != NULL) {
    (void)hal_dacless_get_adc(s_audio, 0u, &control);
  }
  s_phase_increment = 50000u + ((uint32_t)control * 80u);
  for (uint16_t i = 0u; i < sample_count; ++i) {
    buffer[i] = (uint16_t)((s_phase >> 20u) & 0x0FFFu);
    s_phase += s_phase_increment;
  }
}

static void start_pga2311(void) {
  (void)hal_spi_init(EXAMPLE_PGA_BUS, EXAMPLE_PGA_MISO, EXAMPLE_PGA_MOSI,
                     EXAMPLE_PGA_SCK);
  hal_pga2311_config_t config = hal_pga2311_default_config();
  config.spi_bus = EXAMPLE_PGA_BUS;
  config.cs_pin = EXAMPLE_PGA_CS;
  config.mute_pin = HAL_PGA2311_MUTE_PIN_NONE;
  config.start_muted = false;
  if (hal_pga2311_init_ex(&config, &s_pga) != HAL_OK) {
    derr("PGA2311 not detected");
    return;
  }
  (void)hal_pga2311_set_gain_half_db_ex(s_pga, -40, -40);
  deb("PGA2311 ready at -20.0 dB");
}

static void start_dacless(void) {
  hal_dacless_config_t config = hal_dacless_default_config();
  config.pwm_pin = EXAMPLE_AUDIO_PWM;
  config.pwm_bits = 12u;
  config.block_size = EXAMPLE_AUDIO_BLOCK_SIZE;
  config.adc_input_count = 1u;
  config.use_dma = true;
  config.adc_pins[0] = EXAMPLE_AUDIO_ADC;

  hal_status_t status = hal_dacless_create(&config, &s_audio);
  if (status == HAL_OK) {
    status = hal_dacless_set_block_callback(s_audio, fill_audio_block, NULL);
  }
  if (status == HAL_OK) {
    status = hal_dacless_begin(s_audio);
  }
  if (status == HAL_OK) {
    status = hal_dacless_unmute(s_audio);
  }
  if (status != HAL_OK) {
    derr("DACless audio unavailable: %s", hal_status_to_string(status));
    if (s_audio != NULL) {
      (void)hal_dacless_destroy(s_audio);
      s_audio = NULL;
    }
    return;
  }

  float sample_rate = 0.0f;
  hal_dacless_state_t state = {0};
  (void)hal_dacless_get_sample_rate(s_audio, &sample_rate);
  (void)hal_dacless_get_state(s_audio, &state);
  deb("DACless ready rate=%.2f Hz dma=%u", (double)sample_rate,
      state.dma_active ? 1u : 0u);
}

void app_start(void) {
  hal_debug_init_default();
  deb("=== JaszczurHAL audio output: PGA2311 + DACless PWM ===");
  start_pga2311();
  start_dacless();
}

void app_task0(void) {
  if (s_audio != NULL) {
    (void)hal_dacless_service(s_audio);
  }

  const uint32_t now = hal_millis();
  if (s_pga != NULL && (uint32_t)(now - s_last_gain_ms) >= 1000u) {
    s_last_gain_ms = now;
    const int16_t gain = kGainHalfDb[s_gain_index];
    (void)hal_pga2311_set_gain_half_db_ex(s_pga, gain, gain);
    deb("PGA2311 gain=%d.%u dB", (int)(gain / 2),
        (unsigned)((gain < 0 ? -gain : gain) & 1) * 5u);
    s_gain_index = (s_gain_index + 1u) % COUNTOF(kGainHalfDb);
  }

  if ((uint32_t)(now - s_last_report_ms) >= 500u) {
    s_last_report_ms = now;
    uint16_t adc = 0u;
    if (s_audio != NULL) {
      (void)hal_dacless_get_adc(s_audio, 0u, &adc);
    }
    deb("audio adc=%u phase_increment=%lu", (unsigned)adc,
        (unsigned long)s_phase_increment);
  }

  /* DMA refills buffers in its completion callback, so this loop may wait.
   * In polling mode, keep calling service() without adding a delay. */
  hal_dacless_state_t state = {0};
  const bool dma_active = s_audio != NULL &&
                          hal_dacless_get_state(s_audio, &state) == HAL_OK &&
                          state.dma_active;
  if (s_audio == NULL || dma_active) {
    hal_delay_ms(1u);
  }
}
