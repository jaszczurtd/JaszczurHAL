#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "rp2040_cyw43_gspi.h"

#include "hal/system/hal_system.h"
#include "rp2040_cyw43_gspi_clock.h"
#include "rp2040_cyw43_pio_program.h"

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <pico/platform.h>

#include <stdint.h>
#include <string.h>

namespace {

constexpr uint32_t kTransferTimeoutUs = 1000000u;

struct rp2040_gspi_context_t {
  jh_rp2040_cyw43_gspi_config_t config;
  jh_rp2040_cyw43_gspi_clock_t clock;
  PIO pio;
  const pio_program *program_definition;
  uint pio_offset;
  uint pio_offset_lp1_end;
  uint pio_offset_end;
  int pio_sm;
  int dma_out;
  int dma_in;
  jh_cyw43_gspi_host_wake_callback_t host_wake_callback;
  void *host_wake_callback_context;
};

rp2040_gspi_context_t s_context{};
jh_cyw43_gspi_transport_t s_transport{};

bool config_valid(const jh_rp2040_cyw43_gspi_config_t *config) {
  if (config == nullptr || config->pin_chip_select >= NUM_BANK0_GPIOS ||
      config->pin_clock >= NUM_BANK0_GPIOS ||
      config->pin_wl_on >= NUM_BANK0_GPIOS ||
      config->pin_data >= NUM_BANK0_GPIOS || config->target_gspi_hz == 0u ||
      (config->pio_clock_div_override_x256 != 0u &&
       config->pio_clock_div_override_x256 < 256u) ||
      config->max_transaction_bytes < 8u ||
      (config->max_transaction_bytes & 3u) != 0u) {
    return false;
  }
  return config->pin_chip_select != config->pin_clock &&
         config->pin_chip_select != config->pin_wl_on &&
         config->pin_chip_select != config->pin_data &&
         config->pin_clock != config->pin_wl_on &&
         config->pin_clock != config->pin_data &&
         config->pin_wl_on != config->pin_data;
}

bool deadline_expired(uint32_t started_us) {
  return hal_elapsed_u32(hal_micros(), started_us, kTransferTimeoutUs);
}

void stop_comms(rp2040_gspi_context_t *context) {
  gpio_put(context->config.pin_chip_select, true);
  pio_sm_set_enabled(context->pio, (uint)context->pio_sm, false);
  pio_sm_exec(context->pio, (uint)context->pio_sm,
              pio_encode_mov(pio_pins, pio_null));
  gpio_set_function(context->config.pin_data, GPIO_FUNC_SIO);
  gpio_set_dir(context->config.pin_data, GPIO_IN);
  gpio_pull_down(context->config.pin_data);
}

hal_status_t claim_pio(rp2040_gspi_context_t *context) {
  if (context->program_definition == nullptr) {
    return HAL_ECONFIG;
  }
  const PIO candidates[] = {pio0, pio1};
  for (PIO candidate : candidates) {
    if (!pio_can_add_program(candidate, context->program_definition)) {
      continue;
    }
    const int sm = pio_claim_unused_sm(candidate, false);
    if (sm < 0) {
      continue;
    }
    context->pio = candidate;
    context->pio_sm = sm;
    context->pio_offset =
        pio_add_program(candidate, context->program_definition);
    return HAL_OK;
  }
  return HAL_EBUSY;
}

void release_pio(rp2040_gspi_context_t *context) {
  if (context->pio == nullptr || context->pio_sm < 0) {
    return;
  }
  pio_sm_set_enabled(context->pio, (uint)context->pio_sm, false);
  pio_remove_program(context->pio, context->program_definition,
                     context->pio_offset);
  pio_sm_unclaim(context->pio, (uint)context->pio_sm);
  context->pio = nullptr;
  context->pio_sm = -1;
}

bool configure_clock(rp2040_gspi_context_t *context) {
  if (!jh_rp2040_cyw43_gspi_clock_calculate(
          clock_get_hz(clk_sys), context->config.target_gspi_hz,
          context->config.pio_clock_div_override_x256, &context->clock)) {
    return false;
  }

  if (context->clock.program == JH_RP_CYW43_PIO_PROGRAM_LOW_SPEED) {
    context->program_definition = &jh_cyw43_pio_low_speed_program;
    context->pio_offset_lp1_end = jh_cyw43_pio_low_speed_offset_lp1_end;
    context->pio_offset_end = jh_cyw43_pio_low_speed_offset_end;
  } else {
    context->program_definition = &jh_cyw43_pio_high_speed_program;
    context->pio_offset_lp1_end = jh_cyw43_pio_high_speed_offset_lp1_end;
    context->pio_offset_end = jh_cyw43_pio_high_speed_offset_end;
  }
  return true;
}

hal_status_t platform_initialize(void *opaque_context) {
  auto *context = static_cast<rp2040_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  if (!configure_clock(context)) {
    return HAL_ECONFIG;
  }
  hal_status_t status = claim_pio(context);
  if (status != HAL_OK) {
    return status;
  }

  context->dma_out = dma_claim_unused_channel(false);
  context->dma_in = dma_claim_unused_channel(false);
  if (context->dma_out < 0 || context->dma_in < 0) {
    if (context->dma_out >= 0) {
      dma_channel_unclaim((uint)context->dma_out);
      context->dma_out = -1;
    }
    if (context->dma_in >= 0) {
      dma_channel_unclaim((uint)context->dma_in);
      context->dma_in = -1;
    }
    release_pio(context);
    return HAL_EBUSY;
  }

  pio_sm_config sm_config =
      context->clock.program == JH_RP_CYW43_PIO_PROGRAM_LOW_SPEED
          ? jh_cyw43_pio_low_speed_program_get_default_config(
                context->pio_offset)
          : jh_cyw43_pio_high_speed_program_get_default_config(
                context->pio_offset);
  sm_config_set_clkdiv_int_frac8(&sm_config, context->clock.divider_int,
                                 context->clock.divider_frac8);
  sm_config_set_out_pins(&sm_config, context->config.pin_data, 1u);
  sm_config_set_in_pins(&sm_config, context->config.pin_data);
  sm_config_set_set_pins(&sm_config, context->config.pin_data, 1u);
  sm_config_set_sideset_pins(&sm_config, context->config.pin_clock);
  sm_config_set_in_shift(&sm_config, false, true, 32u);
  sm_config_set_out_shift(&sm_config, false, true, 32u);
  context->pio->input_sync_bypass |= 1u << context->config.pin_data;
  pio_sm_set_config(context->pio, (uint)context->pio_sm, &sm_config);
  pio_sm_set_consecutive_pindirs(context->pio, (uint)context->pio_sm,
                                 context->config.pin_clock, 1u, true);

  gpio_init(context->config.pin_wl_on);
  gpio_set_dir(context->config.pin_wl_on, GPIO_OUT);
  gpio_put(context->config.pin_wl_on, false);
  gpio_init(context->config.pin_chip_select);
  gpio_set_dir(context->config.pin_chip_select, GPIO_OUT);
  gpio_put(context->config.pin_chip_select, true);
  gpio_init(context->config.pin_clock);
  gpio_set_dir(context->config.pin_clock, GPIO_OUT);
  gpio_put(context->config.pin_clock, false);
  gpio_set_drive_strength(context->config.pin_clock, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_slew_rate(context->config.pin_clock, GPIO_SLEW_RATE_FAST);
  gpio_init(context->config.pin_data);
  gpio_set_dir(context->config.pin_data, GPIO_OUT);
  gpio_put(context->config.pin_data, false);
  return HAL_OK;
}

hal_status_t platform_deinitialize(void *opaque_context) {
  auto *context = static_cast<rp2040_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  if (context->dma_out >= 0) {
    dma_channel_cleanup((uint)context->dma_out);
    dma_channel_unclaim((uint)context->dma_out);
    context->dma_out = -1;
  }
  if (context->dma_in >= 0) {
    dma_channel_cleanup((uint)context->dma_in);
    dma_channel_unclaim((uint)context->dma_in);
    context->dma_in = -1;
  }
  release_pio(context);
  context->host_wake_callback = nullptr;
  context->host_wake_callback_context = nullptr;
  return HAL_OK;
}

hal_status_t platform_set_power(void *opaque_context, bool enabled) {
  auto *context = static_cast<rp2040_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  if (!enabled) {
    gpio_put(context->config.pin_chip_select, true);
    gpio_put(context->config.pin_clock, false);
    gpio_set_function(context->config.pin_data, GPIO_FUNC_SIO);
    gpio_set_dir(context->config.pin_data, GPIO_OUT);
    gpio_put(context->config.pin_data, false);
  }
  gpio_put(context->config.pin_wl_on, enabled);
  return HAL_OK;
}

hal_status_t platform_release_data(void *opaque_context) {
  auto *context = static_cast<rp2040_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  gpio_set_function(context->config.pin_data, GPIO_FUNC_SIO);
  gpio_set_dir(context->config.pin_data, GPIO_IN);
  gpio_pull_down(context->config.pin_data);
  return HAL_OK;
}

hal_status_t platform_transfer(void *opaque_context, const uint8_t *tx,
                               size_t tx_length, uint8_t *rx,
                               size_t rx_length) {
  auto *context = static_cast<rp2040_gspi_context_t *>(opaque_context);
  if (context == nullptr || tx == nullptr || tx_length == 0u ||
      (tx_length & 3u) != 0u || (rx_length & 3u) != 0u ||
      (rx_length != 0u && rx == nullptr) || ((uintptr_t)tx & 3u) != 0u ||
      (rx != nullptr && ((uintptr_t)rx & 3u) != 0u)) {
    return HAL_EINVAL;
  }
  if (clock_get_hz(clk_sys) != context->clock.clk_sys_hz) {
    return HAL_ECONFIG;
  }

  const uint sm = (uint)context->pio_sm;
  const auto pio_function = (gpio_function_t)pio_get_funcsel(context->pio);
  gpio_set_function(context->config.pin_data, pio_function);
  gpio_set_function(context->config.pin_clock, pio_function);
  gpio_pull_down(context->config.pin_clock);
  gpio_put(context->config.pin_chip_select, false);

  pio_sm_set_enabled(context->pio, sm, false);
  pio_sm_set_wrap(context->pio, sm, context->pio_offset,
                  context->pio_offset +
                      (rx_length == 0u ? context->pio_offset_lp1_end
                                       : context->pio_offset_end) -
                      1u);
  pio_sm_clear_fifos(context->pio, sm);
  pio_sm_set_pindirs_with_mask(context->pio, sm, 1u << context->config.pin_data,
                               1u << context->config.pin_data);
  pio_sm_restart(context->pio, sm);
  pio_sm_clkdiv_restart(context->pio, sm);
  pio_sm_put(context->pio, sm, (uint32_t)(tx_length * 8u - 1u));
  pio_sm_exec(context->pio, sm, pio_encode_out(pio_x, 32u));
  pio_sm_put(context->pio, sm,
             rx_length == 0u ? 0u : (uint32_t)(rx_length * 8u - 1u));
  pio_sm_exec(context->pio, sm, pio_encode_out(pio_y, 32u));
  pio_sm_exec(context->pio, sm, pio_encode_jmp(context->pio_offset));

  dma_channel_abort((uint)context->dma_out);
  dma_channel_config out_config =
      dma_channel_get_default_config((uint)context->dma_out);
  channel_config_set_bswap(&out_config, true);
  channel_config_set_dreq(&out_config, pio_get_dreq(context->pio, sm, true));
  dma_channel_configure((uint)context->dma_out, &out_config,
                        &context->pio->txf[sm], tx, tx_length / 4u, true);

  if (rx_length != 0u) {
    dma_channel_abort((uint)context->dma_in);
    dma_channel_config in_config =
        dma_channel_get_default_config((uint)context->dma_in);
    channel_config_set_bswap(&in_config, true);
    channel_config_set_dreq(&in_config, pio_get_dreq(context->pio, sm, false));
    channel_config_set_write_increment(&in_config, true);
    channel_config_set_read_increment(&in_config, false);
    dma_channel_configure((uint)context->dma_in, &in_config, rx,
                          &context->pio->rxf[sm], rx_length / 4u, true);
  }

  pio_sm_set_enabled(context->pio, sm, true);
  __compiler_memory_barrier();
  const uint32_t started_us = hal_micros();
  while (dma_channel_is_busy((uint)context->dma_out)) {
    if (deadline_expired(started_us)) {
      dma_channel_abort((uint)context->dma_out);
      if (rx_length != 0u) {
        dma_channel_abort((uint)context->dma_in);
      }
      stop_comms(context);
      return HAL_ETIMEOUT;
    }
    tight_loop_contents();
  }
  if (rx_length != 0u) {
    while (dma_channel_is_busy((uint)context->dma_in)) {
      if (deadline_expired(started_us)) {
        dma_channel_abort((uint)context->dma_in);
        stop_comms(context);
        return HAL_ETIMEOUT;
      }
      tight_loop_contents();
    }
  } else {
    const uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    context->pio->fdebug = stall_mask;
    while ((context->pio->fdebug & stall_mask) == 0u) {
      if (deadline_expired(started_us)) {
        stop_comms(context);
        return HAL_ETIMEOUT;
      }
      tight_loop_contents();
    }
  }
  __compiler_memory_barrier();
  stop_comms(context);
  return HAL_OK;
}

hal_status_t
platform_host_wake_attach(void *opaque_context,
                          jh_cyw43_gspi_host_wake_callback_t callback,
                          void *callback_context) {
  auto *context = static_cast<rp2040_gspi_context_t *>(opaque_context);
  if (context == nullptr || callback == nullptr ||
      context->host_wake_callback != nullptr) {
    return context != nullptr && callback != nullptr ? HAL_EBUSY : HAL_EINVAL;
  }
  context->host_wake_callback = callback;
  context->host_wake_callback_context = callback_context;
  return HAL_OK;
}

hal_status_t platform_host_wake_detach(void *opaque_context) {
  auto *context = static_cast<rp2040_gspi_context_t *>(opaque_context);
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  context->host_wake_callback = nullptr;
  context->host_wake_callback_context = nullptr;
  return HAL_OK;
}

void platform_host_wake_mask(void *) {}

hal_status_t platform_host_wake_rearm(void *opaque_context, bool *asserted) {
  auto *context = static_cast<rp2040_gspi_context_t *>(opaque_context);
  if (context == nullptr || asserted == nullptr) {
    return HAL_EINVAL;
  }
  gpio_set_function(context->config.pin_data, GPIO_FUNC_SIO);
  gpio_set_dir(context->config.pin_data, GPIO_IN);
  gpio_pull_down(context->config.pin_data);
  *asserted = gpio_get(context->config.pin_data);
  return HAL_OK;
}

void platform_delay_ms(void *, uint32_t delay_ms) { hal_delay_ms(delay_ms); }

const jh_cyw43_gspi_platform_ops_t kPlatformOps = {
    platform_initialize,       platform_deinitialize,
    platform_set_power,        platform_release_data,
    platform_transfer,         platform_host_wake_attach,
    platform_host_wake_detach, platform_host_wake_mask,
    platform_host_wake_rearm,  platform_delay_ms,
};

} // namespace

extern "C" hal_status_t
jh_rp2040_cyw43_gspi_init(const jh_rp2040_cyw43_gspi_config_t *config) {
  if (!config_valid(config)) {
    return config == nullptr ? HAL_EINVAL : HAL_ECONFIG;
  }
  if (s_transport.initialized) {
    return HAL_EEXIST;
  }
  memset(&s_context, 0, sizeof(s_context));
  s_context.config = *config;
  s_context.pio_sm = -1;
  s_context.dma_out = -1;
  s_context.dma_in = -1;
  return jh_cyw43_gspi_transport_init(&s_transport, &kPlatformOps, &s_context,
                                      config->max_transaction_bytes);
}

extern "C" hal_status_t jh_rp2040_cyw43_gspi_deinit(void) {
  return jh_cyw43_gspi_transport_deinit(&s_transport);
}

extern "C" hal_status_t
jh_rp2040_cyw43_gspi_get_clock(jh_rp2040_cyw43_gspi_clock_t *clock_config) {
  if (clock_config == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_transport.initialized) {
    return HAL_EUNINIT;
  }
  *clock_config = s_context.clock;
  return HAL_OK;
}

extern "C" jh_cyw43_gspi_transport_t *jh_rp2040_cyw43_gspi_transport(void) {
  return s_transport.initialized ? &s_transport : nullptr;
}

#endif
