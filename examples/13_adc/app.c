#include <hal/analog/hal_adc.h>
#include <hal/analog/hal_external_adc.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#if HAL_TARGET_IS_RP
#define INTERNAL_ADC_PIN_0 26u
#define INTERNAL_ADC_PIN_1 27u
#define ADS_I2C_SDA_PIN 4u
#define ADS_I2C_SCL_PIN 5u
#else
#define INTERNAL_ADC_PIN_0 0u
#define INTERNAL_ADC_PIN_1 1u
#define ADS_I2C_SDA_PIN 25u
#define ADS_I2C_SCL_PIN 24u
#endif

#define ADS1115_ADDRESS 0x48u
#define ADS1115_MV_PER_LSB 0.1875f
#define ADC_REPORT_PERIOD_MS 1000u
#define ADS_RETRY_PERIOD_MS 5000u

static bool s_ads1115_ready = false;
static uint32_t s_last_report_ms = 0u;
static uint32_t s_last_ads_retry_ms = 0u;

static int32_t ads_raw_to_mv(int16_t raw) {
  const float millivolts = (float)raw * ADS1115_MV_PER_LSB;
  return (int32_t)(millivolts + (millivolts >= 0.0f ? 0.5f : -0.5f));
}

static int32_t ads_scaled_to_mv(float volts) {
  const float millivolts = volts * 1000.0f;
  return (int32_t)(millivolts + (millivolts >= 0.0f ? 0.5f : -0.5f));
}

static void init_ads1115(uint32_t now) {
  s_last_ads_retry_ms = now;
  const hal_status_t status =
      hal_ext_adc_init(ADS1115_ADDRESS, ADS1115_MV_PER_LSB);
  s_ads1115_ready = status == HAL_OK;
  if (s_ads1115_ready) {
    deb("ADS1115 ready at address 0x48");
  } else {
    derr("ADS1115 unavailable: %s", hal_status_to_string(status));
  }
}

static void report_internal_adc(void) {
  const int raw0 = hal_adc_read(INTERNAL_ADC_PIN_0);
  const int raw1 = hal_adc_read(INTERNAL_ADC_PIN_1);
  deb("Internal ADC: pin %u raw=%d (~%ld mV), pin %u raw=%d (~%ld mV)",
      (unsigned)INTERNAL_ADC_PIN_0, raw0, (long)(raw0 * 3300L / 4095L),
      (unsigned)INTERNAL_ADC_PIN_1, raw1, (long)(raw1 * 3300L / 4095L));
}

static void report_ads1115(uint32_t now) {
  if (!s_ads1115_ready) {
    if ((uint32_t)(now - s_last_ads_retry_ms) >= ADS_RETRY_PERIOD_MS) {
      init_ads1115(now);
    }
    return;
  }

  for (uint8_t channel = 0u; channel < 4u; ++channel) {
    int16_t raw = 0;
    hal_status_t status = hal_ext_adc_read_ex(channel, &raw);
    if (status != HAL_OK) {
      derr("ADS1115 channel %u failed: %s", (unsigned)channel,
           hal_status_to_string(status));
      s_ads1115_ready = false;
      s_last_ads_retry_ms = now;
      return;
    }

    float scaled_volts = 0.0f;
    status = hal_ext_adc_read_scaled_ex(channel, &scaled_volts);
    if (status != HAL_OK) {
      derr("ADS1115 scaled channel %u failed: %s", (unsigned)channel,
           hal_status_to_string(status));
      s_ads1115_ready = false;
      s_last_ads_retry_ms = now;
      return;
    }

    deb("ADS1115 A%u: raw=%d (~%ld mV), scaled=%.4f V (~%ld mV)",
        (unsigned)channel, (int)raw, (long)ads_raw_to_mv(raw),
        (double)scaled_volts, (long)ads_scaled_to_mv(scaled_volts));
  }
}

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL ADC ===");

  hal_adc_set_resolution(12u);
  const hal_status_t i2c_status =
      hal_i2c_init(ADS_I2C_SDA_PIN, ADS_I2C_SCL_PIN, HAL_I2C_CLOCK_STANDARD_HZ);
  if (i2c_status != HAL_OK) {
    derr("ADS1115 I2C init failed: %s", hal_status_to_string(i2c_status));
  }
  init_ads1115(hal_millis());
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_report_ms) >= ADC_REPORT_PERIOD_MS) {
    s_last_report_ms = now;
    report_internal_adc();
    report_ads1115(now);
  }
  hal_delay_ms(20u);
}
