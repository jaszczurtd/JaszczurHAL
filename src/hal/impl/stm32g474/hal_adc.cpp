#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_adc.h"
#include "../../hal_sync.h"

#ifdef JH_STM32G474_HW
#include "port/stm32g474_regs.h"
#endif

static uint8_t s_resolution = 12u;
static hal_mutex_t s_adc_mutex = nullptr;

#ifndef JH_STM32G474_HW
/* Host-sanity build: no registers to touch - keep a benign zero-filled store. */
static int s_adc_values[128] = {};
#endif

static void adc_ensure_mutex(void) {
    if (s_adc_mutex == nullptr) {
        hal_critical_section_enter();
        if (s_adc_mutex == nullptr) {
            s_adc_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

#ifdef JH_STM32G474_HW
/* ── Real ADC1 backend: single-ended, polled, one regular conversion ───────
 * No init entry point exists in the public API, so the first hal_adc_read()
 * lazily brings ADC1 up (clock, regulator, calibration, enable) and routes
 * the requested pin to analog mode on demand. */

static bool s_adc_ready = false;

/* RES field for a requested bit count; anything unsupported falls back to 12. */
static uint32_t adc_res_field(uint8_t bits) {
    switch (bits) {
        case 10u: return 1u;
        case 8u:  return 2u;
        case 6u:  return 3u;
        case 12u:
        default:  return 0u;
    }
}

/* Map a JaszczurHAL pin id (port*16 + pin) to its ADC1 external channel.
 * Returns 0 for pins ADC1 cannot reach (0 is not a valid regular input here).
 * Single-ended ADC1 assignments per RM0440; pending on-silicon validation. */
static uint32_t adc1_channel_for_pin(uint8_t pin) {
    switch (pin) {
        case 0x00: return 1u;   /* PA0  -> ADC1_IN1  */
        case 0x01: return 2u;   /* PA1  -> ADC1_IN2  */
        case 0x02: return 3u;   /* PA2  -> ADC1_IN3  */
        case 0x03: return 4u;   /* PA3  -> ADC1_IN4  */
        case 0x10: return 15u;  /* PB0  -> ADC1_IN15 */
        case 0x11: return 12u;  /* PB1  -> ADC1_IN12 */
        case 0x1B: return 14u;  /* PB11 -> ADC1_IN14 */
        case 0x1C: return 11u;  /* PB12 -> ADC1_IN11 */
        case 0x1E: return 5u;   /* PB14 -> ADC1_IN5  */
        case 0x20: return 6u;   /* PC0  -> ADC1_IN6  */
        case 0x21: return 7u;   /* PC1  -> ADC1_IN7  */
        case 0x22: return 8u;   /* PC2  -> ADC1_IN8  */
        case 0x23: return 9u;   /* PC3  -> ADC1_IN9  */
        default:   return 0u;
    }
}

static void adc_apply_resolution(void) {
    ADC1_CFGR = (ADC1_CFGR & ~ADC_CFGR_RES_MASK) |
                (adc_res_field(s_resolution) << ADC_CFGR_RES_POS);
}

static void adc1_hw_init(void) {
    if (s_adc_ready) {
        return;
    }
    /* Kernel clock = HCLK/1 (synchronous); peripheral bus clock via RCC. */
    RCC_AHB2ENR |= RCC_AHB2ENR_ADC12EN;
    ADC12_CCR = (ADC12_CCR & ~ADC_CCR_CKMODE_MASK) | ADC_CCR_CKMODE_HCLK_DIV1;

    /* Leave deep-power-down, enable the internal regulator, wait tADCVREG_STUP
     * (~20 us; a generous spin at the 16 MHz bring-up clock). */
    ADC1_CR &= ~ADC_CR_DEEPPWD;
    ADC1_CR |= ADC_CR_ADVREGEN;
    for (volatile uint32_t i = 0; i < 4000u; ++i) { }

    /* Single-ended calibration - ADEN must be 0 here (it is after reset). */
    ADC1_CR &= ~ADC_CR_ADCALDIF;
    ADC1_CR |= ADC_CR_ADCAL;
    while (ADC1_CR & ADC_CR_ADCAL) { }

    adc_apply_resolution();

    /* Enable the ADC and wait until it reports ready. */
    ADC1_ISR = ADC_ISR_ADRDY;              /* clear ADRDY by writing 1 */
    ADC1_CR |= ADC_CR_ADEN;
    while (!(ADC1_ISR & ADC_ISR_ADRDY)) { }

    s_adc_ready = true;
}

/* Route @p pin to analog mode and run one polled conversion on its channel. */
static int adc1_hw_read(uint8_t pin) {
    const uint32_t ch = adc1_channel_for_pin(pin);
    if (ch == 0u) {
        return 0;
    }

    const uint32_t port = (uint32_t)(pin >> 4);
    const uint32_t n    = (uint32_t)(pin & 0x0Fu);
    if (port <= 6u) {
        RCC_AHB2ENR |= (1u << port);                       /* GPIOAEN..GPIOGEN */
        GPIO_MODER(port) |= (GPIO_MODE_ANALOG << (n * 2u)); /* analog (11) */
    }

    /* Sample time (3 bits per channel, split across SMPR1/SMPR2 at channel 10). */
    if (ch <= 9u) {
        ADC1_SMPR1 = (ADC1_SMPR1 & ~(0x7u << (ch * 3u))) |
                     (ADC_SMP_247CYCLES << (ch * 3u));
    } else {
        const uint32_t s = ch - 10u;
        ADC1_SMPR2 = (ADC1_SMPR2 & ~(0x7u << (s * 3u))) |
                     (ADC_SMP_247CYCLES << (s * 3u));
    }

    /* One-conversion regular sequence: L = 0 (1 conv), SQ1 = channel. */
    ADC1_SQR1 = (ch << ADC_SQR1_SQ1_POS);

    ADC1_ISR = ADC_ISR_EOC;                /* clear any stale EOC */
    ADC1_CR |= ADC_CR_ADSTART;
    uint32_t to = ADC_POLL_TIMEOUT;
    while (!(ADC1_ISR & ADC_ISR_EOC) && to) { --to; }
    if (to == 0u) {
        return 0;
    }
    return (int)(ADC1_DR & 0xFFFFu);       /* reading DR clears EOC */
}
#endif  // JH_STM32G474_HW

void hal_adc_set_resolution(uint8_t bits) {
    adc_ensure_mutex();
    hal_mutex_lock(s_adc_mutex);
    s_resolution = bits;
#ifdef JH_STM32G474_HW
    if (s_adc_ready) {
        adc_apply_resolution();
    }
#else
    (void)s_resolution;
#endif
    hal_mutex_unlock(s_adc_mutex);
}

int hal_adc_read(uint8_t pin) {
    adc_ensure_mutex();
    hal_mutex_lock(s_adc_mutex);
    int val;
#ifdef JH_STM32G474_HW
    adc1_hw_init();
    val = adc1_hw_read(pin);
#else
    val = (pin < 128u) ? s_adc_values[pin] : 0;
#endif
    hal_mutex_unlock(s_adc_mutex);
    return val;
}

#endif  // HAL_TARGET_IS_STM32G474
