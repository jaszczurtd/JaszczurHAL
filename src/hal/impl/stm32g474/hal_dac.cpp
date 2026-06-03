#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474
#include "../../hal_config.h"
#ifdef HAL_ENABLE_DAC

#include "../../hal_dac.h"

#ifdef JH_STM32G474_HW
#include "port/stm32g474_regs.h"
#endif

#ifndef HAL_DAC_VREF_MV
#define HAL_DAC_VREF_MV 3300u
#endif

#define G474_DAC_CHANNELS 2
#define G474_DAC_RES_BITS 12u

static bool s_init[G474_DAC_CHANNELS] = {};
#ifndef JH_STM32G474_HW
/* Host-sanity build: keep written codes in RAM instead of touching registers. */
static uint16_t s_value[G474_DAC_CHANNELS] = {};
#endif

bool hal_dac_is_supported(void) {
    return true;
}

uint8_t hal_dac_resolution_bits(void) {
    return G474_DAC_RES_BITS;
}

uint16_t hal_dac_max_value(void) {
    return (uint16_t)((1u << G474_DAC_RES_BITS) - 1u);
}

bool hal_dac_init(uint8_t channel) {
    if (channel >= G474_DAC_CHANNELS) {
        return false;
    }
#ifdef JH_STM32G474_HW
    /* Clock GPIOA + DAC1; set the output pin to analog; enable the channel.
     * ch0 -> DAC1_OUT1 = PA4, ch1 -> DAC1_OUT2 = PA5. */
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_DAC1EN;
    const uint32_t pin = (channel == 0u) ? 4u : 5u;
    GPIO_MODER(0) |= (GPIO_MODE_ANALOG << (pin * 2u));   /* analog mode (11) */
    DAC1_CR |= (channel == 0u) ? DAC_CR_EN1 : DAC_CR_EN2;
#endif
    s_init[channel] = true;
    return true;
}

void hal_dac_write(uint8_t channel, uint16_t value) {
    if (channel >= G474_DAC_CHANNELS || !s_init[channel]) {
        return;
    }
    const uint16_t max = hal_dac_max_value();
    if (value > max) {
        value = max;
    }
#ifdef JH_STM32G474_HW
    if (channel == 0u) {
        DAC1_DHR12R1 = value;
    } else {
        DAC1_DHR12R2 = value;
    }
#else
    s_value[channel] = value;
#endif
}

void hal_dac_write_millivolts(uint8_t channel, uint16_t millivolts) {
    if (millivolts > HAL_DAC_VREF_MV) {
        millivolts = HAL_DAC_VREF_MV;
    }
    const uint32_t code = ((uint32_t)millivolts * hal_dac_max_value()) / HAL_DAC_VREF_MV;
    hal_dac_write(channel, (uint16_t)code);
}

#endif  // HAL_ENABLE_DAC
#endif  // HAL_TARGET_IS_STM32G474
