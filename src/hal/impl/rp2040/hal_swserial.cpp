#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040

#include "../../hal_config.h"
#ifdef HAL_ENABLE_SWSERIAL

#include "../../hal_swserial.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"
#include "drivers/swserial/swserial.pio.h"

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/sync.h>
#include <pico/platform.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * RP2040 software UART.
 *
 * RX and TX are performed entirely by PIO state machines. RX words are moved
 * into a power-of-two raw ring by DMA, so a continuous NMEA burst does not run
 * any bit-timing loop (or any GPIO callback) on either CPU core. Public reads
 * drain and validate the completed words into the API's byte ring.
 *
 * One instance uses two PIO state machines and one DMA channel. The PIO
 * programs are shared by every instance allocated on the same PIO block.
 */

#define HAL_SWSERIAL_RX_BUF_SIZE 64u
#define HAL_SWSERIAL_DMA_WORD_COUNT 64u
#define HAL_SWSERIAL_DMA_RING_BITS 8u /* 64 * sizeof(uint32_t) == 256 */
#define HAL_SWSERIAL_DMA_TRANSFER_COUNT UINT32_MAX
#define HAL_SWSERIAL_DMA_REARM_THRESHOLD (1u << 20u)
#define HAL_SWSERIAL_PIO_PIN_COUNT 32u

static_assert((HAL_SWSERIAL_RX_BUF_SIZE & (HAL_SWSERIAL_RX_BUF_SIZE - 1u)) ==
                  0u,
              "SoftwareSerial RX ring must be a power of two");
static_assert(HAL_SWSERIAL_DMA_WORD_COUNT * sizeof(uint32_t) ==
                  (1u << HAL_SWSERIAL_DMA_RING_BITS),
              "SoftwareSerial DMA ring size/alignment mismatch");

typedef enum {
  HAL_SWSERIAL_PARITY_NONE = 0,
  HAL_SWSERIAL_PARITY_EVEN = 1,
  HAL_SWSERIAL_PARITY_ODD = 2,
} hal_swserial_parity_t;

struct alignas(256) hal_swserial_impl_s {
  uint8_t rx_pin;
  uint8_t tx_pin;
  uint32_t baud;
  uint8_t bits;
  uint8_t stop_bits;
  hal_swserial_parity_t parity;
  bool started;
  bool overflow;

  PIO pio;
  uint8_t pio_index;
  int rx_sm;
  int tx_sm;
  int dma_channel;

  bool rx_dma_active;
  uint64_t dma_epoch_base;
  uint64_t raw_consumed;
  uint8_t rx_buf[HAL_SWSERIAL_RX_BUF_SIZE];
  uint8_t head;
  uint8_t tail;
  hal_mutex_t mutex;

  alignas(256) uint32_t rx_dma_words[HAL_SWSERIAL_DMA_WORD_COUNT];
};

typedef struct {
  bool loaded;
  uint rx_offset;
  uint tx_offset;
  uint8_t users;
} swserial_pio_programs_t;

static hal_swserial_impl_t s_pool[HAL_SWSERIAL_MAX_INSTANCES];
static bool s_used[HAL_SWSERIAL_MAX_INSTANCES];
static hal_mutex_t s_pool_mutex = NULL;
static swserial_pio_programs_t s_programs[NUM_PIOS] = {};

static inline uint8_t next_index(uint8_t index) {
  return (uint8_t)((index + 1u) & (HAL_SWSERIAL_RX_BUF_SIZE - 1u));
}

static bool swserial_pin_valid(uint8_t pin) {
  /* The PIO pin masks used below cover GPIO 0..31. */
  return pin < NUM_BANK0_GPIOS && pin < HAL_SWSERIAL_PIO_PIN_COUNT;
}

static bool swserial_config_valid(uint16_t config) {
  if ((config & (uint16_t)~0x0733u) != 0u) {
    return false;
  }

  const uint16_t data_bits = config & 0x0700u;
  const uint16_t stop_bits = config & 0x0030u;
  const uint16_t parity = config & 0x0003u;
  return (data_bits == HAL_UART_DATA_5 || data_bits == HAL_UART_DATA_6 ||
          data_bits == HAL_UART_DATA_7 || data_bits == HAL_UART_DATA_8) &&
         (stop_bits == HAL_UART_STOP_BIT_1 ||
          stop_bits == HAL_UART_STOP_BIT_2) &&
         (parity == HAL_UART_PARITY_NONE || parity == HAL_UART_PARITY_EVEN ||
          parity == HAL_UART_PARITY_ODD);
}

static int swserial_parity(uint32_t data) {
  data ^= data >> 4u;
  data &= 0x0Fu;
  return (int)((0x6996u >> data) & 1u);
}

static uint8_t swserial_data_bits(uint16_t config) {
  switch (config & 0x0700u) {
  case HAL_UART_DATA_5:
    return 5u;
  case HAL_UART_DATA_6:
    return 6u;
  case HAL_UART_DATA_7:
    return 7u;
  case HAL_UART_DATA_8:
  default:
    return 8u;
  }
}

static uint8_t swserial_stop_bits(uint16_t config) {
  return ((config & 0x0030u) == HAL_UART_STOP_BIT_2) ? 2u : 1u;
}

static hal_swserial_parity_t swserial_parity_mode(uint16_t config) {
  switch (config & 0x0003u) {
  case HAL_UART_PARITY_EVEN:
    return HAL_SWSERIAL_PARITY_EVEN;
  case HAL_UART_PARITY_ODD:
    return HAL_SWSERIAL_PARITY_ODD;
  default:
    return HAL_SWSERIAL_PARITY_NONE;
  }
}

static PIO swserial_pio(uint8_t index) { return pio_get_instance((uint)index); }

static bool swserial_programs_acquire(uint8_t pio_index) {
  swserial_pio_programs_t *state = &s_programs[pio_index];
  PIO pio = swserial_pio(pio_index);

  if (state->loaded) {
    ++state->users;
    return true;
  }

  if (!pio_can_add_program(pio, &jh_swserial_rx_program)) {
    return false;
  }
  const int rx_offset = pio_add_program(pio, &jh_swserial_rx_program);
  if (rx_offset < 0) {
    return false;
  }

  if (!pio_can_add_program(pio, &jh_swserial_tx_program)) {
    pio_remove_program(pio, &jh_swserial_rx_program, (uint)rx_offset);
    return false;
  }
  const int tx_offset = pio_add_program(pio, &jh_swserial_tx_program);
  if (tx_offset < 0) {
    pio_remove_program(pio, &jh_swserial_rx_program, (uint)rx_offset);
    return false;
  }

  state->loaded = true;
  state->rx_offset = (uint)rx_offset;
  state->tx_offset = (uint)tx_offset;
  state->users = 1u;
  return true;
}

static void swserial_programs_release(uint8_t pio_index) {
  swserial_pio_programs_t *state = &s_programs[pio_index];
  if (!state->loaded || state->users == 0u) {
    return;
  }

  --state->users;
  if (state->users != 0u) {
    return;
  }

  PIO pio = swserial_pio(pio_index);
  pio_remove_program(pio, &jh_swserial_tx_program, state->tx_offset);
  pio_remove_program(pio, &jh_swserial_rx_program, state->rx_offset);
  *state = {};
}

static bool swserial_claim_pio(hal_swserial_t h) {
  for (uint8_t pio_index = 0u; pio_index < (uint8_t)NUM_PIOS; ++pio_index) {
    PIO pio = swserial_pio(pio_index);
#if defined(PICO_PIO_USE_GPIO_BASE) && PICO_PIO_USE_GPIO_BASE
    /* Do not retarget an RP2350B PIO block already serving GPIO 16..47. */
    if (pio_get_gpio_base(pio) != 0u) {
      continue;
    }
#endif
    const int rx_sm = pio_claim_unused_sm(pio, false);
    if (rx_sm < 0) {
      continue;
    }

    const int tx_sm = pio_claim_unused_sm(pio, false);
    if (tx_sm < 0) {
      pio_sm_unclaim(pio, (uint)rx_sm);
      continue;
    }

    if (!swserial_programs_acquire(pio_index)) {
      pio_sm_unclaim(pio, (uint)tx_sm);
      pio_sm_unclaim(pio, (uint)rx_sm);
      continue;
    }

    h->pio = pio;
    h->pio_index = pio_index;
    h->rx_sm = rx_sm;
    h->tx_sm = tx_sm;
    return true;
  }
  return false;
}

static void swserial_release_pio(hal_swserial_t h) {
  if (h->pio == NULL) {
    return;
  }
  if (h->rx_sm >= 0) {
    pio_sm_set_enabled(h->pio, (uint)h->rx_sm, false);
    pio_sm_unclaim(h->pio, (uint)h->rx_sm);
  }
  if (h->tx_sm >= 0) {
    pio_sm_set_enabled(h->pio, (uint)h->tx_sm, false);
    pio_sm_unclaim(h->pio, (uint)h->tx_sm);
  }
  swserial_programs_release(h->pio_index);
  h->pio = NULL;
  h->rx_sm = -1;
  h->tx_sm = -1;
}

static void swserial_reset_handle(hal_swserial_t h, uint8_t rx_pin,
                                  uint8_t tx_pin) {
  memset(h, 0, sizeof(*h));
  h->rx_pin = rx_pin;
  h->tx_pin = tx_pin;
  h->bits = 8u;
  h->stop_bits = 1u;
  h->parity = HAL_SWSERIAL_PARITY_NONE;
  h->rx_sm = -1;
  h->tx_sm = -1;
  h->dma_channel = -1;
}

static bool swserial_clock_divider(uint32_t baud, float *out_divider) {
  if (baud == 0u || out_divider == NULL) {
    return false;
  }
  const float divider = (float)clock_get_hz(clk_sys) / (8.0f * (float)baud);
  if (divider < 1.0f || divider >= 65536.0f) {
    return false;
  }
  *out_divider = divider;
  return true;
}

static uint8_t swserial_rx_sample_bits(const hal_swserial_t h) {
  return (uint8_t)(h->bits + (h->parity == HAL_SWSERIAL_PARITY_NONE ? 0u : 1u));
}

static uint8_t swserial_tx_shift_bits(const hal_swserial_t h) {
  return (uint8_t)(swserial_rx_sample_bits(h) + h->stop_bits);
}

static void swserial_stop_dma(hal_swserial_t h) {
  if (!h->rx_dma_active || h->dma_channel < 0) {
    return;
  }
  dma_channel_abort((uint)h->dma_channel);
  h->rx_dma_active = false;
}

static void swserial_configure_rx_dma(hal_swserial_t h) {
  dma_channel_config config =
      dma_channel_get_default_config((uint)h->dma_channel);
  channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
  channel_config_set_read_increment(&config, false);
  channel_config_set_write_increment(&config, true);
  channel_config_set_dreq(&config, pio_get_dreq(h->pio, (uint)h->rx_sm, false));
  channel_config_set_ring(&config, true, HAL_SWSERIAL_DMA_RING_BITS);

  const size_t write_index =
      (size_t)(h->dma_epoch_base & (HAL_SWSERIAL_DMA_WORD_COUNT - 1u));
  dma_channel_configure((uint)h->dma_channel, &config,
                        &h->rx_dma_words[write_index], &h->pio->rxf[h->rx_sm],
                        HAL_SWSERIAL_DMA_TRANSFER_COUNT, true);
  h->rx_dma_active = true;
}

static void swserial_configure_rx(hal_swserial_t h, float divider) {
  const uint sm = (uint)h->rx_sm;
  const uint offset = s_programs[h->pio_index].rx_offset;

  pio_sm_set_enabled(h->pio, sm, false);
  pio_sm_clear_fifos(h->pio, sm);
  pio_sm_restart(h->pio, sm);
  pio_sm_clkdiv_restart(h->pio, sm);

  pio_sm_set_consecutive_pindirs(h->pio, sm, h->rx_pin, 1u, false);
  pio_gpio_init(h->pio, h->rx_pin);
  gpio_pull_up(h->rx_pin);

  pio_sm_config config = jh_swserial_rx_program_get_default_config(offset);
  sm_config_set_in_pins(&config, h->rx_pin);
  sm_config_set_jmp_pin(&config, h->rx_pin);
  sm_config_set_in_shift(&config, true, false, 32u);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
  sm_config_set_clkdiv(&config, divider);
  pio_sm_init(h->pio, sm, offset, &config);

  const uint8_t sample_bits = swserial_rx_sample_bits(h);
  pio_sm_exec(h->pio, sm, pio_encode_set(pio_y, sample_bits - 1u));
  pio_sm_exec(h->pio, sm, pio_encode_mov(pio_isr, pio_null));

  swserial_configure_rx_dma(h);
  pio_sm_set_enabled(h->pio, sm, true);
}

static void swserial_configure_tx(hal_swserial_t h, float divider) {
  const uint sm = (uint)h->tx_sm;
  const uint offset = s_programs[h->pio_index].tx_offset;
  const uint32_t pin_mask = 1u << h->tx_pin;

  pio_sm_set_enabled(h->pio, sm, false);
  pio_sm_clear_fifos(h->pio, sm);
  pio_sm_restart(h->pio, sm);
  pio_sm_clkdiv_restart(h->pio, sm);

  pio_sm_set_pins_with_mask(h->pio, sm, pin_mask, pin_mask);
  pio_sm_set_pindirs_with_mask(h->pio, sm, pin_mask, pin_mask);
  pio_gpio_init(h->pio, h->tx_pin);

  pio_sm_config config = jh_swserial_tx_program_get_default_config(offset);
  sm_config_set_out_shift(&config, true, false, 32u);
  sm_config_set_out_pins(&config, h->tx_pin, 1u);
  sm_config_set_sideset_pins(&config, h->tx_pin);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv(&config, divider);
  pio_sm_init(h->pio, sm, offset, &config);

  const uint8_t shift_bits = swserial_tx_shift_bits(h);
  pio_sm_exec(h->pio, sm, pio_encode_set(pio_y, shift_bits - 1u));
  pio_sm_set_enabled(h->pio, sm, true);
}

static uint64_t swserial_dma_produced(const hal_swserial_t h) {
  if (!h->rx_dma_active) {
    return h->dma_epoch_base;
  }
  const uint32_t remaining =
      dma_channel_hw_addr((uint)h->dma_channel)->transfer_count;
  return h->dma_epoch_base +
         ((uint64_t)HAL_SWSERIAL_DMA_TRANSFER_COUNT - remaining);
}

static void swserial_push_decoded(hal_swserial_t h, uint8_t value) {
  const uint8_t next = next_index(h->tail);
  if (next == h->head) {
    h->overflow = true;
    return;
  }
  h->rx_buf[h->tail] = value;
  h->tail = next;
}

static bool swserial_decode_word(const hal_swserial_t h, uint32_t raw,
                                 uint8_t *out_value) {
  const uint8_t sample_bits = swserial_rx_sample_bits(h);
  const uint32_t samples = raw >> (32u - sample_bits);
  const uint32_t data_mask = (1u << h->bits) - 1u;
  const uint32_t value = samples & data_mask;

  if (h->parity != HAL_SWSERIAL_PARITY_NONE) {
    const bool parity_bit = ((samples >> h->bits) & 1u) != 0u;
    const int calculated = swserial_parity(value);
    if ((h->parity == HAL_SWSERIAL_PARITY_EVEN &&
         calculated != (int)parity_bit) ||
        (h->parity == HAL_SWSERIAL_PARITY_ODD &&
         calculated == (int)parity_bit)) {
      return false;
    }
  }

  *out_value = (uint8_t)value;
  return true;
}

static void swserial_consume_rx(hal_swserial_t h, uint64_t produced) {
  uint64_t pending = produced - h->raw_consumed;
  if (pending > HAL_SWSERIAL_DMA_WORD_COUNT) {
    h->overflow = true;
    h->raw_consumed = produced - HAL_SWSERIAL_DMA_WORD_COUNT;
    pending = HAL_SWSERIAL_DMA_WORD_COUNT;
  }

  __dmb();
  while (pending > 0u) {
    const size_t raw_index =
        (size_t)(h->raw_consumed & (HAL_SWSERIAL_DMA_WORD_COUNT - 1u));
    const uint32_t raw = h->rx_dma_words[raw_index];
    ++h->raw_consumed;
    --pending;

    uint8_t value = 0u;
    if (swserial_decode_word(h, raw, &value)) {
      swserial_push_decoded(h, value);
    }
  }
}

static void swserial_service_rx(hal_swserial_t h) {
  if (!h->rx_dma_active) {
    return;
  }

  const uint32_t remaining =
      dma_channel_hw_addr((uint)h->dma_channel)->transfer_count;
  if (remaining > HAL_SWSERIAL_DMA_REARM_THRESHOLD) {
    swserial_consume_rx(h, swserial_dma_produced(h));
    return;
  }

  /*
   * A DMA transfer count is finite even though the raw destination is a ring.
   * Rearm it well before exhaustion. The PIO RX FIFO buffers characters while
   * the channel is stopped, and the monotonic epoch keeps the ring position
   * continuous across channel runs.
   */
  dma_channel_abort((uint)h->dma_channel);
  h->rx_dma_active = false;
  const uint32_t final_remaining =
      dma_channel_hw_addr((uint)h->dma_channel)->transfer_count;
  const uint64_t final_produced =
      h->dma_epoch_base +
      ((uint64_t)HAL_SWSERIAL_DMA_TRANSFER_COUNT - final_remaining);
  swserial_consume_rx(h, final_produced);
  h->dma_epoch_base = final_produced;
  swserial_configure_rx_dma(h);
}

static uint32_t swserial_encode_tx(const hal_swserial_t h, uint8_t byte) {
  const uint32_t data_mask = (1u << h->bits) - 1u;
  uint32_t value = (uint32_t)byte & data_mask;
  uint8_t shift = h->bits;

  if (h->parity == HAL_SWSERIAL_PARITY_EVEN) {
    value |= (uint32_t)(swserial_parity(value) != 0) << shift;
    ++shift;
  } else if (h->parity == HAL_SWSERIAL_PARITY_ODD) {
    value |= (uint32_t)(swserial_parity(value) == 0) << shift;
    ++shift;
  }

  for (uint8_t stop = 0u; stop < h->stop_bits; ++stop) {
    value |= 1u << shift;
    ++shift;
  }
  return value;
}

static void swserial_wait_tx(hal_swserial_t h) {
  const uint32_t tx_stall_mask = 1u
                                 << (PIO_FDEBUG_TXSTALL_LSB + (uint)h->tx_sm);
  h->pio->fdebug = tx_stall_mask;
  while ((h->pio->fdebug & tx_stall_mask) == 0u) {
    tight_loop_contents();
  }
}

static size_t swserial_write_locked(hal_swserial_t h, const uint8_t *data,
                                    size_t len) {
  for (size_t i = 0u; i < len; ++i) {
    pio_sm_put_blocking(h->pio, (uint)h->tx_sm, swserial_encode_tx(h, data[i]));
  }
  swserial_wait_tx(h);
  return len;
}

hal_status_t hal_swserial_create_ex(uint8_t rx_pin, uint8_t tx_pin,
                                    hal_swserial_t *out_handle) {
  if (out_handle == NULL) {
    return HAL_EINVAL;
  }
  *out_handle = NULL;
  if (!swserial_pin_valid(rx_pin) || !swserial_pin_valid(tx_pin) ||
      rx_pin == tx_pin) {
    return HAL_EINVAL;
  }
  if (jh_hal_mutex_create_once(&s_pool_mutex) == NULL) {
    return HAL_ENOMEM;
  }

  hal_mutex_lock(s_pool_mutex);
  for (int i = 0; i < hal_get_config()->swserial_max_instances; ++i) {
    if (s_used[i]) {
      continue;
    }

    hal_swserial_t h = &s_pool[i];
    swserial_reset_handle(h, rx_pin, tx_pin);
    if (!swserial_claim_pio(h)) {
      hal_mutex_unlock(s_pool_mutex);
      return HAL_ENOMEM;
    }

    h->dma_channel = dma_claim_unused_channel(false);
    if (h->dma_channel < 0) {
      swserial_release_pio(h);
      hal_mutex_unlock(s_pool_mutex);
      return HAL_ENOMEM;
    }

    h->mutex = hal_mutex_create();
    if (h->mutex == NULL) {
      dma_channel_unclaim((uint)h->dma_channel);
      h->dma_channel = -1;
      swserial_release_pio(h);
      hal_mutex_unlock(s_pool_mutex);
      return HAL_ENOMEM;
    }

    s_used[i] = true;
    *out_handle = h;
    hal_mutex_unlock(s_pool_mutex);
    return HAL_OK;
  }
  hal_mutex_unlock(s_pool_mutex);
  return HAL_ENOMEM;
}

hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin) {
  hal_swserial_t h = NULL;
  (void)hal_swserial_create_ex(rx_pin, tx_pin, &h);
  return h;
}

hal_status_t hal_swserial_set_rx_ex(hal_swserial_t h, uint8_t rx_pin) {
  if (h == NULL || h->mutex == NULL || !swserial_pin_valid(rx_pin)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  if (rx_pin == h->rx_pin) {
    hal_mutex_unlock(h->mutex);
    return HAL_OK;
  }
  if (rx_pin == h->tx_pin) {
    hal_mutex_unlock(h->mutex);
    return HAL_EINVAL;
  }
  if (h->started) {
    hal_mutex_unlock(h->mutex);
    return HAL_ESTATE;
  }
  h->rx_pin = rx_pin;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

bool hal_swserial_set_rx(hal_swserial_t h, uint8_t rx_pin) {
  return hal_status_to_bool(hal_swserial_set_rx_ex(h, rx_pin));
}

hal_status_t hal_swserial_set_tx_ex(hal_swserial_t h, uint8_t tx_pin) {
  if (h == NULL || h->mutex == NULL || !swserial_pin_valid(tx_pin)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  if (tx_pin == h->tx_pin) {
    hal_mutex_unlock(h->mutex);
    return HAL_OK;
  }
  if (tx_pin == h->rx_pin) {
    hal_mutex_unlock(h->mutex);
    return HAL_EINVAL;
  }
  if (h->started) {
    hal_mutex_unlock(h->mutex);
    return HAL_ESTATE;
  }
  h->tx_pin = tx_pin;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

bool hal_swserial_set_tx(hal_swserial_t h, uint8_t tx_pin) {
  return hal_status_to_bool(hal_swserial_set_tx_ex(h, tx_pin));
}

hal_status_t hal_swserial_begin(hal_swserial_t h, uint32_t baud,
                                uint16_t config) {
  if (h == NULL || h->mutex == NULL || h->pio == NULL || h->dma_channel < 0 ||
      !swserial_config_valid(config)) {
    return HAL_EINVAL;
  }

  float divider = 0.0f;
  if (!swserial_clock_divider(baud, &divider)) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  if (!swserial_pin_valid(h->rx_pin) || !swserial_pin_valid(h->tx_pin) ||
      h->rx_pin == h->tx_pin) {
    hal_mutex_unlock(h->mutex);
    return HAL_EINVAL;
  }
  swserial_stop_dma(h);
  pio_sm_set_enabled(h->pio, (uint)h->rx_sm, false);
  pio_sm_set_enabled(h->pio, (uint)h->tx_sm, false);

  h->baud = baud;
  h->bits = swserial_data_bits(config);
  h->stop_bits = swserial_stop_bits(config);
  h->parity = swserial_parity_mode(config);
  h->head = 0u;
  h->tail = 0u;
  h->dma_epoch_base = 0u;
  h->raw_consumed = 0u;
  h->overflow = false;
  memset(h->rx_dma_words, 0, sizeof(h->rx_dma_words));

  swserial_configure_tx(h, divider);
  swserial_configure_rx(h, divider);
  h->started = true;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

int hal_swserial_available(hal_swserial_t h) {
  if (h == NULL || h->mutex == NULL) {
    return 0;
  }
  hal_mutex_lock(h->mutex);
  if (!h->started) {
    hal_mutex_unlock(h->mutex);
    return 0;
  }
  swserial_service_rx(h);
  const int available = (int)((h->tail + HAL_SWSERIAL_RX_BUF_SIZE - h->head) &
                              (HAL_SWSERIAL_RX_BUF_SIZE - 1u));
  hal_mutex_unlock(h->mutex);
  return available;
}

hal_status_t hal_swserial_read_ex(hal_swserial_t h, uint8_t *out_value) {
  if (out_value == NULL) {
    return HAL_EINVAL;
  }
  *out_value = 0u;
  if (h == NULL || h->mutex == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  if (!h->started) {
    hal_mutex_unlock(h->mutex);
    return HAL_EUNINIT;
  }
  swserial_service_rx(h);
  if (h->head == h->tail) {
    hal_mutex_unlock(h->mutex);
    return HAL_EAGAIN;
  }
  *out_value = h->rx_buf[h->head];
  h->head = next_index(h->head);
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

int hal_swserial_read(hal_swserial_t h) {
  uint8_t value = 0u;
  return hal_status_to_bool(hal_swserial_read_ex(h, &value)) ? (int)value : -1;
}

hal_status_t hal_swserial_write_ex(hal_swserial_t h, const uint8_t *data,
                                   size_t len, size_t *out_written) {
  if (out_written != NULL) {
    *out_written = 0u;
  }
  if (h == NULL || h->mutex == NULL || (len > 0u && data == NULL)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  if (!h->started) {
    hal_mutex_unlock(h->mutex);
    return HAL_EUNINIT;
  }
  if (len == 0u) {
    hal_mutex_unlock(h->mutex);
    return HAL_OK;
  }
  const size_t written = swserial_write_locked(h, data, len);
  hal_mutex_unlock(h->mutex);
  if (out_written != NULL) {
    *out_written = written;
  }
  return HAL_OK;
}

size_t hal_swserial_write(hal_swserial_t h, const uint8_t *data, size_t len) {
  size_t written = 0u;
  (void)hal_swserial_write_ex(h, data, len, &written);
  return written;
}

hal_status_t hal_swserial_println_ex(hal_swserial_t h, const char *s,
                                     size_t *out_written) {
  if (out_written != NULL) {
    *out_written = 0u;
  }
  if (h == NULL || h->mutex == NULL) {
    return HAL_EINVAL;
  }
  const char *text = (s != NULL) ? s : "";
  const size_t len = strlen(text);
  static const uint8_t crlf[] = {'\r', '\n'};

  hal_mutex_lock(h->mutex);
  if (!h->started) {
    hal_mutex_unlock(h->mutex);
    return HAL_EUNINIT;
  }
  if (len > 0u) {
    (void)swserial_write_locked(h, (const uint8_t *)text, len);
  }
  (void)swserial_write_locked(h, crlf, sizeof(crlf));
  hal_mutex_unlock(h->mutex);
  if (out_written != NULL) {
    *out_written = len;
  }
  return HAL_OK;
}

size_t hal_swserial_println(hal_swserial_t h, const char *s) {
  size_t written = 0u;
  (void)hal_swserial_println_ex(h, s, &written);
  return written;
}

hal_status_t hal_swserial_flush(hal_swserial_t h) {
  if (h == NULL || h->mutex == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  if (!h->started) {
    hal_mutex_unlock(h->mutex);
    return HAL_EUNINIT;
  }
  swserial_wait_tx(h);
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

void hal_swserial_destroy(hal_swserial_t h) {
  if (h == NULL) {
    return;
  }
  if (jh_hal_mutex_create_once(&s_pool_mutex) == NULL) {
    return;
  }

  hal_mutex_lock(s_pool_mutex);
  for (int i = 0; i < hal_get_config()->swserial_max_instances; ++i) {
    if (h != &s_pool[i] || !s_used[i]) {
      continue;
    }

    hal_mutex_lock(h->mutex);
    const bool was_started = h->started;
    swserial_stop_dma(h);
    h->started = false;

    if (h->dma_channel >= 0) {
      dma_channel_unclaim((uint)h->dma_channel);
      h->dma_channel = -1;
    }
    swserial_release_pio(h);

    if (was_started) {
      gpio_set_dir(h->rx_pin, GPIO_IN);
      gpio_pull_up(h->rx_pin);
      gpio_set_function(h->rx_pin, GPIO_FUNC_SIO);
      gpio_put(h->tx_pin, true);
      gpio_set_dir(h->tx_pin, GPIO_OUT);
      gpio_set_function(h->tx_pin, GPIO_FUNC_SIO);
    }

    hal_mutex_t mutex = h->mutex;
    s_used[i] = false;
    hal_mutex_unlock(mutex);
    hal_mutex_destroy(mutex);
    memset(h, 0, sizeof(*h));
    hal_mutex_unlock(s_pool_mutex);
    return;
  }
  hal_mutex_unlock(s_pool_mutex);
}

#endif /* HAL_ENABLE_SWSERIAL */
#endif /* HAL_TARGET_IS_RP2040 */
