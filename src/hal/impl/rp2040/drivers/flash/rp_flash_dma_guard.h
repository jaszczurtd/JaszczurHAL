#pragma once

/* DMA rule of the flash safe zone. While a flash operation runs, XIP is
 * disabled: a DMA channel reading or writing the XIP window stalls or moves
 * garbage. A busy channel whose addresses both lie outside that window, such
 * as a peripheral-to-RAM ring (ADC scan, audio), is unaffected and keeps
 * running; refusing every busy channel would deny persistence to any firmware
 * with a continuous DMA stream. Everything here is forced inline: the caller
 * runs from RAM with XIP disabled and must not call into flash. */

#include <hardware/dma.h>
#include <hardware/regs/addressmap.h>
#include <hardware/structs/dma.h>
#include <pico/platform.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether an address lies in the XIP window: the flash mapping and, on
 * RP2040, the XIP cache SRAM, which is unreachable with XIP disabled too. */
static __force_inline bool jh_rp_flash_address_is_xip(uintptr_t value) {
#if defined(PICO_RP2350)
  return value >= (uintptr_t)XIP_BASE &&
         value < (uintptr_t)XIP_NOCACHE_NOALLOC_NOTRANSLATE_END;
#else
  return (value >= (uintptr_t)XIP_BASE && value < (uintptr_t)XIP_CTRL_BASE) ||
         (value >= (uintptr_t)XIP_SRAM_BASE && value < (uintptr_t)XIP_SRAM_END);
#endif
}

/* Whether one channel is busy with a transfer that touches the XIP window. */
static __force_inline bool jh_rp_flash_dma_channel_blocks(uint channel) {
  if ((dma_hw->ch[channel].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) == 0u) {
    return false;
  }
  const uintptr_t read_addr = (uintptr_t)dma_hw->ch[channel].read_addr;
  const uintptr_t write_addr = (uintptr_t)dma_hw->ch[channel].write_addr;
  return jh_rp_flash_address_is_xip(read_addr) ||
         jh_rp_flash_address_is_xip(write_addr);
}

/* Whether any channel blocks a flash operation right now. */
static __force_inline bool jh_rp_flash_dma_blocks(void) {
  for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
    if (jh_rp_flash_dma_channel_blocks(channel)) {
      return true;
    }
  }
  return false;
}

#ifdef __cplusplus
}
#endif
