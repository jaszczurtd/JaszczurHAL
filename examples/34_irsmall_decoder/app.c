#include <hal/hal_app.h>
#include <hal/hal_irsmall_decoder.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#include <stdint.h>

static hal_irsmall_decoder_t ir;

#if HAL_TARGET_IS_RP2040
static const uint8_t IR_INPUT_PIN = 16u; /* GP16 */
#elif HAL_TARGET_IS_STM32G474
static const uint8_t IR_INPUT_PIN = 16u; /* PB0 */
#else
static const uint8_t IR_INPUT_PIN = 16u;
#endif

void app_start(void) {
  debugInit();

  hal_irsmall_decoder_config_t cfg = hal_irsmall_decoder_default_config(
      IR_INPUT_PIN, HAL_IRSMALL_PROTOCOL_NEC);
  cfg.irq_priority = HAL_IRQ_PRIORITY_HIGH;

  if (!hal_irsmall_decoder_init(&ir, &cfg)) {
    derr("IRsmallDecoder init failed\r\n");
    return;
  }

  deb("IRsmallDecoder ready pin=%u protocol=%u\r\n", (unsigned)IR_INPUT_PIN,
      (unsigned)cfg.protocol);
}

void app_task0(void) {
  hal_irsmall_decoder_data_t data;

  if (hal_irsmall_decoder_data_available(&ir, &data)) {
    deb("ir protocol=%u held=%u addr=0x%04X cmd=0x%02X ext=0x%02X bits=%u\r\n",
        (unsigned)data.protocol, data.key_held ? 1u : 0u, (unsigned)data.addr,
        (unsigned)data.cmd, (unsigned)data.ext, (unsigned)data.bits);
  }

  hal_delay_ms(10u);
}
