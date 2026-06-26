/**
 * @file app.cpp
 * @brief Portable DACless PWM-audio example over JaszczurHAL.
 *
 * Wiring:
 *   PWM audio out -> EXAMPLE_DACLESS_PWM_PIN through an RC low-pass filter
 *   ADC control   -> EXAMPLE_DACLESS_ADC0_PIN
 */

#include <hal/hal_app.h>
#include <hal/hal_dacless.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools.h>

#include <new>
#include <stdint.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_DACLESS_PWM_PIN 6u
#define EXAMPLE_DACLESS_ADC0_PIN 26u
#else
/* STM32 pin id = port * 16 + pin: PA6 PWM, PA0 ADC. */
#define EXAMPLE_DACLESS_PWM_PIN 6u
#define EXAMPLE_DACLESS_ADC0_PIN 0u
#endif

#define EXAMPLE_DACLESS_BLOCK_SIZE 64u

#ifndef DACLESS_EXAMPLE_USE_DMA
#define DACLESS_EXAMPLE_USE_DMA 1
#endif

alignas(
    DAClessAudio) static unsigned char s_audio_storage[sizeof(DAClessAudio)];
static DAClessAudio *s_audio = nullptr;
static uint32_t s_last_log_ms = 0u;
static uint32_t s_phase = 0u;
static uint32_t s_phase_increment = 90000u;

static void fill_audio_block(void *, uint16_t *buffer) {
  const uint16_t adc0 = s_audio ? s_audio->getADC(0u) : 0u;
  s_phase_increment = 50000u + ((uint32_t)adc0 * 80u);

  for (uint16_t i = 0u; i < EXAMPLE_DACLESS_BLOCK_SIZE; ++i) {
    buffer[i] = (uint16_t)((s_phase >> 20u) & 0x0FFFu);
    s_phase += s_phase_increment;
  }
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL DACless audio example ===");
  deb("PWM pin: %u  ADC0 pin: %u", (unsigned)EXAMPLE_DACLESS_PWM_PIN,
      (unsigned)EXAMPLE_DACLESS_ADC0_PIN);
  deb("Audio path: %s", DACLESS_EXAMPLE_USE_DMA ? "DMA" : "polling");

  DAClessConfig cfg;
  cfg.pinPWM = EXAMPLE_DACLESS_PWM_PIN;
  cfg.pwmBits = 12u;
  cfg.blockSize = EXAMPLE_DACLESS_BLOCK_SIZE;
  cfg.nAdcInputs = 1u;
  cfg.useDma = (DACLESS_EXAMPLE_USE_DMA != 0);
  cfg.adcPins[0] = EXAMPLE_DACLESS_ADC0_PIN;

  s_audio = new (s_audio_storage) DAClessAudio(cfg);
  s_audio->setBlockCallback(fill_audio_block, nullptr);
  s_audio->begin();
  s_audio->unmute();

  deb("DACless sample rate: %.2f Hz", (double)s_audio->getSampleRate());
  deb("DMA active: %u", s_audio->isDmaActive() ? 1u : 0u);
}

void app_task0(void) {
  if (s_audio == nullptr) {
    hal_delay_ms(1000u);
    return;
  }

  s_audio->service();

  const uint32_t now = hal_millis();
  if ((now - s_last_log_ms) < 500u) {
    return;
  }
  s_last_log_ms = now;

  deb("ADC0=%u phase_inc=%lu", (unsigned)s_audio->getADC(0u),
      (unsigned long)s_phase_increment);
}
