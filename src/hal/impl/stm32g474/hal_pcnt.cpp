#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474
#include "../../hal_config.h"
#ifdef HAL_ENABLE_PCNT

#include "../../hal_pcnt.h"

#ifdef JH_STM32G474_HW
#include "port/stm32g474_regs.h"
#endif

/*
 * STM32G474 hardware pulse counter.
 *
 * Channel 0 -> TIM2 (32-bit) in external-clock mode 1, counting edges on
 * TIM2_CH1 = PA0 (AF1). The counter increments per selected edge with zero
 * CPU involvement. Additional channels (other timers/pins) are a follow-up;
 * hal_pcnt_init() returns false for them today.
 */

#define G474_PCNT_CHANNELS 1

static bool s_init[G474_PCNT_CHANNELS] = {};
#ifndef JH_STM32G474_HW
static uint32_t s_count[G474_PCNT_CHANNELS] = {};   /* host-sanity build */
#endif

bool hal_pcnt_is_supported(void) {
    return true;
}

uint8_t hal_pcnt_channel_count(void) {
    return G474_PCNT_CHANNELS;
}

bool hal_pcnt_init(uint8_t channel, uint8_t pin, hal_pcnt_edge_t edge) {
    if (channel >= G474_PCNT_CHANNELS) {
        return false;
    }
    (void)pin;   /* fixed to TIM2_CH1 = PA0 for channel 0 */
#ifndef JH_STM32G474_HW
    (void)edge;
#endif
#ifdef JH_STM32G474_HW
    /* Clock GPIOA + TIM2. */
    RCC_AHB2ENR  |= RCC_AHB2ENR_GPIOAEN;
    RCC_APB1ENR1 |= RCC_APB1ENR1_TIM2EN;

    /* PA0 -> alternate function AF1 (TIM2_CH1). */
    GPIO_MODER(0) = (GPIO_MODER(0) & ~(0x3u << (0u * 2u))) |
                    (GPIO_MODE_AF << (0u * 2u));
    GPIO_AFRL(0)  = (GPIO_AFRL(0) & ~(0xFu << (0u * 4u))) | (1u << (0u * 4u));

    /* IC1 mapped on TI1. */
    TIM2_CCMR1 = (TIM2_CCMR1 & ~0x3u) | TIM_CCMR1_CC1S_TI1;

    /* Edge polarity (input-capture polarity table). */
    uint32_t ccer = TIM2_CCER & ~(TIM_CCER_CC1P | TIM_CCER_CC1NP);
    if (edge == HAL_PCNT_EDGE_FALLING) {
        ccer |= TIM_CCER_CC1P;
    } else if (edge == HAL_PCNT_EDGE_BOTH) {
        ccer |= TIM_CCER_CC1P | TIM_CCER_CC1NP;
    }
    TIM2_CCER = ccer | TIM_CCER_CC1E;

    /* External clock mode 1, clocked from TI1FP1. */
    TIM2_SMCR = (TIM2_SMCR & ~0x77u) | TIM_SMCR_TS_TI1FP1 | TIM_SMCR_SMS_EXT1;

    TIM2_ARR = 0xFFFFFFFFu;
    TIM2_CNT = 0u;
    TIM2_CR1 |= TIM_CR1_CEN;
#endif
    s_init[channel] = true;
    return true;
}

uint32_t hal_pcnt_read(uint8_t channel) {
    if (channel >= G474_PCNT_CHANNELS || !s_init[channel]) {
        return 0u;
    }
#ifdef JH_STM32G474_HW
    return TIM2_CNT;
#else
    return s_count[channel];
#endif
}

void hal_pcnt_reset(uint8_t channel) {
    if (channel >= G474_PCNT_CHANNELS) {
        return;
    }
#ifdef JH_STM32G474_HW
    TIM2_CNT = 0u;
#else
    s_count[channel] = 0u;
#endif
}

uint32_t hal_pcnt_read_and_reset(uint8_t channel) {
    const uint32_t v = hal_pcnt_read(channel);
    hal_pcnt_reset(channel);
    return v;
}

#endif  // HAL_ENABLE_PCNT
#endif  // HAL_TARGET_IS_STM32G474
