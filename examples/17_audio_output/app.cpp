#include <hal/hal_app.h>
#include <hal/hal_dacless.h>
#include <hal/hal_pga2311.h>
#include <hal/hal_spi.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools.h>

#include <new>
#include <stdint.h>

#if HAL_TARGET_IS_RP
#define EXAMPLE_PGA_BUS 0u
#define EXAMPLE_PGA_MISO 16u
#define EXAMPLE_PGA_MOSI 19u
#define EXAMPLE_PGA_SCK 18u
#define EXAMPLE_PGA_CS 17u
#define EXAMPLE_AUDIO_PWM 6u
#define EXAMPLE_AUDIO_ADC 26u
#else
/* SPI2 on PB14/PB15/PB13 keeps PA6 free for the PWM audio output. */
#define EXAMPLE_PGA_BUS 1u
#define EXAMPLE_PGA_MISO 30u
#define EXAMPLE_PGA_MOSI 31u
#define EXAMPLE_PGA_SCK 29u
#define EXAMPLE_PGA_CS 28u
#define EXAMPLE_AUDIO_PWM 6u
#define EXAMPLE_AUDIO_ADC 0u
#endif

#define EXAMPLE_AUDIO_BLOCK_SIZE 64u

alignas(
    DAClessAudio) static unsigned char s_audio_storage[sizeof(DAClessAudio)];
static DAClessAudio *s_audio = nullptr;
static hal_pga2311_t s_pga = nullptr;
static uint32_t s_phase = 0u;
static uint32_t s_phase_increment = 90000u;
static uint32_t s_last_report_ms = 0u;
static uint32_t s_last_gain_ms = 0u;
static uint32_t s_gain_index = 0u;

static const int16_t kGainHalfDb[] = {-80, -40, -20, 0, 20, 40};

static void fill_audio_block(void *, uint16_t *buffer) {
  const uint16_t control = s_audio != nullptr ? s_audio->getADC(0u) : 0u;
  s_phase_increment = 50000u + ((uint32_t)control * 80u);
  for (uint16_t i = 0u; i < EXAMPLE_AUDIO_BLOCK_SIZE; ++i) {
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
  DAClessConfig config;
  config.pinPWM = EXAMPLE_AUDIO_PWM;
  config.pwmBits = 12u;
  config.blockSize = EXAMPLE_AUDIO_BLOCK_SIZE;
  config.nAdcInputs = 1u;
  config.useDma = true;
  config.adcPins[0] = EXAMPLE_AUDIO_ADC;

  s_audio = new (s_audio_storage) DAClessAudio(config);
  s_audio->setBlockCallback(fill_audio_block, nullptr);
  if (!s_audio->begin()) {
    derr("DACless audio unavailable");
    s_audio = nullptr;
    return;
  }
  s_audio->unmute();
  deb("DACless ready rate=%.2f Hz dma=%u", (double)s_audio->getSampleRate(),
      s_audio->isDmaActive() ? 1u : 0u);
}

extern "C" void app_start(void) {
  debugInit();
  deb("=== JaszczurHAL audio output: PGA2311 + DACless PWM ===");
  start_pga2311();
  start_dacless();
}

extern "C" void app_task0(void) {
  if (s_audio != nullptr) {
    s_audio->service();
  }

  const uint32_t now = hal_millis();
  if (s_pga != nullptr && (uint32_t)(now - s_last_gain_ms) >= 1000u) {
    s_last_gain_ms = now;
    const int16_t gain = kGainHalfDb[s_gain_index];
    (void)hal_pga2311_set_gain_half_db_ex(s_pga, gain, gain);
    deb("PGA2311 gain=%d.%u dB", (int)(gain / 2),
        (unsigned)((gain < 0 ? -gain : gain) & 1) * 5u);
    s_gain_index =
        (s_gain_index + 1u) % (sizeof(kGainHalfDb) / sizeof(kGainHalfDb[0]));
  }

  if ((uint32_t)(now - s_last_report_ms) >= 500u) {
    s_last_report_ms = now;
    deb("audio adc=%u phase_increment=%lu",
        s_audio != nullptr ? (unsigned)s_audio->getADC(0u) : 0u,
        (unsigned long)s_phase_increment);
  }

  /* DMA refills buffers from its completion callback; polling must stay hot. */
  if (s_audio == nullptr || s_audio->isDmaActive()) {
    hal_delay_ms(1u);
  }
}
