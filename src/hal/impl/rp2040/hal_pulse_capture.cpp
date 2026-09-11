#include "hal/core/hal_config.h"
#if HAL_TARGET_IS_RP && defined(HAL_ENABLE_PULSE_CAPTURE)
#include "hal/analog/jh_pulse_capture_backend.h"
#include "hal/core/hal_array.h"
#include "hal/system/hal_system.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "jh_pulse_capture_program.h"

namespace {
/* Every high/low loop decrements X once per eight system clocks. The
 * high->low path adds two cycles and low->high removes two; consecutive
 * falling timestamps therefore differ by exactly 8 * unsigned(Xold-Xnew).
 * x-- always targets the next instruction, including when X wraps.
 * PUSH NOBLOCK keeps time running if DMA fails; RXSTALL makes that a fault.
 */
const pio_program program = [] {
  pio_program value = {};
  value.instructions = jh_rp_capture_instructions;
  value.length = (uint8_t)COUNTOF(jh_rp_capture_instructions);
  value.origin = -1;
  return value;
}();
alignas(4096) volatile uint32_t ring[1024];
constexpr uint32_t transfer_count = 0x0ffff000U;
uint32_t reload_count = transfer_count;
PIO pio;
int sm = -1, dma = -1, reload_dma = -1;
uint offset, pin, saved_inover;
uint32_t last_remaining, produced, consumed, frequency;
uint64_t started_us;
uint32_t last_poll_us;
bool first_capture;

hal_status_t update_produced(void) {
  const uint32_t remaining = dma_hw->ch[dma].transfer_count;
  produced += remaining <= last_remaining
                  ? last_remaining - remaining
                  : last_remaining + transfer_count - remaining;
  last_remaining = remaining;
  // Keep one slot free while a DMA write may still be in flight.
  return produced - consumed >= COUNTOF(ring) ? HAL_EOVERFLOW : HAL_OK;
}

void release_dma(int *channel) {
  if (*channel < 0)
    return;
  dma_channel_abort((uint)*channel);
  dma_channel_unclaim((uint)*channel);
  *channel = -1;
}
} // namespace

hal_status_t jh_pulse_capture_start(const hal_pulse_capture_config_t *config,
                                    uint32_t *clock_hz, uint16_t *stride) {
  /* GPIOs 0..29 are shared by all RP packages and need no GPIO-base remap. */
  if (config->pin > 29U)
    return HAL_EINVAL;
  frequency = clock_get_hz(clk_sys) / 8U;
  if (frequency < 2000000U || clock_get_hz(clk_sys) % 8U != 0U)
    return HAL_ECONFIG;
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(
          &program, &pio, (uint *)&sm, &offset, config->pin, 1U, true))
    return HAL_EBUSY;
  dma = dma_claim_unused_channel(false);
  reload_dma = dma_claim_unused_channel(false);
  if (dma < 0 || reload_dma < 0) {
    release_dma(&dma);
    release_dma(&reload_dma);
    pio_remove_program_and_unclaim_sm(&program, pio, (uint)sm, offset);
    sm = -1;
    return HAL_EBUSY;
  }
  pin = config->pin;
  saved_inover =
      (io_bank0_hw->io[pin].ctrl & IO_BANK0_GPIO0_CTRL_INOVER_BITS) >>
      IO_BANK0_GPIO0_CTRL_INOVER_LSB;
  gpio_set_input_enabled(pin, true);
  gpio_set_inover(pin, config->falling ? GPIO_OVERRIDE_NORMAL
                                       : GPIO_OVERRIDE_INVERT);
  pio_sm_config cfg = pio_get_default_sm_config();
  sm_config_set_wrap(&cfg, offset, offset + program.length - 1U);
  sm_config_set_jmp_pin(&cfg, pin);
  sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_RX);
  pio_sm_init(pio, (uint)sm, offset, &cfg);
  pio_sm_exec(pio, (uint)sm, pio_encode_mov_not(pio_x, pio_null));
  pio->fdebug = 1U << (uint)sm;

  dma_channel_config dc = dma_channel_get_default_config((uint)dma);
  channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
  channel_config_set_read_increment(&dc, false);
  channel_config_set_write_increment(&dc, true);
  channel_config_set_ring(&dc, true, 12);
  channel_config_set_dreq(&dc, pio_get_dreq(pio, (uint)sm, false));
  channel_config_set_chain_to(&dc, (uint)reload_dma);
  dma_channel_configure((uint)dma, &dc, ring, &pio->rxf[sm], transfer_count,
                        false);
  dma_channel_config rc = dma_channel_get_default_config((uint)reload_dma);
  channel_config_set_transfer_data_size(&rc, DMA_SIZE_32);
  channel_config_set_read_increment(&rc, false);
  channel_config_set_write_increment(&rc, false);
  dma_channel_configure((uint)reload_dma, &rc,
                        &dma_hw->ch[dma].al1_transfer_count_trig, &reload_count,
                        1, false);
  last_remaining = transfer_count;
  produced = consumed = 0;
  first_capture = true;
  dma_start_channel_mask(1U << (uint)dma);
  const uint32_t irq = save_and_disable_interrupts();
  started_us = hal_micros64();
  pio_sm_set_enabled(pio, (uint)sm, true);
  restore_interrupts(irq);
  last_poll_us = hal_micros();
  *clock_hz = frequency;
  *stride = 32;
  return HAL_OK;
}

hal_status_t jh_pulse_capture_stop(void) {
  if (sm < 0)
    return HAL_OK;
  pio_sm_set_enabled(pio, (uint)sm, false);
  /* Break chaining before aborting either channel. */
  hw_write_masked(&dma_hw->ch[dma].ctrl_trig,
                  (uint32_t)dma << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB,
                  DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
  release_dma(&reload_dma);
  release_dma(&dma);
  gpio_set_inover(pin, saved_inover);
  pio_remove_program_and_unclaim_sm(&program, pio, (uint)sm, offset);
  sm = -1;
  return HAL_OK;
}

hal_status_t jh_pulse_capture_next(jh_pulse_capture_edge_t *edge) {
  const uint32_t polled_us = hal_micros();
  if (hal_elapsed_u32(polled_us, last_poll_us, 10000U))
    return HAL_EOVERFLOW;
  last_poll_us = polled_us;
  if (clock_get_hz(clk_sys) != frequency * 8U)
    return HAL_ECONFIG;
  if ((pio->fdebug & (1U << (uint)sm)) != 0U)
    return HAL_EOVERFLOW;
  if ((dma_hw->ch[dma].ctrl_trig & DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS) != 0U)
    return HAL_EHW;
  if (update_produced() != HAL_OK)
    return HAL_EOVERFLOW;
  /* DMA retains every edge. Only read every 32nd timestamp on the CPU.
   * Skip the possible synthetic falling edge when starting with a low input. */
  const uint32_t needed = first_capture ? 2U : 32U;
  if (produced - consumed < needed)
    return HAL_EAGAIN;
  __dmb();
  const uint32_t ticks = ~ring[(consumed + needed - 1U) % COUNTOF(ring)];
  if (update_produced() != HAL_OK)
    return HAL_EOVERFLOW;
  consumed += needed;
  first_capture = false;
  const uint64_t now = hal_micros64();
  const uint32_t current_ticks =
      (uint32_t)(((now - started_us + 2U) * frequency) / 1000000U);
  const uint32_t age_us =
      (uint32_t)(((uint64_t)(current_ticks - ticks) * 1000000U) / frequency);
  *edge = {ticks, (uint32_t)now - age_us - 1U};
  return HAL_OK;
}
#endif
