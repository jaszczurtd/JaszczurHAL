#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_endian.h"
#include "hal/spi/hal_spi.h"
#include "hal/spi/hal_spi_internal.h"
#include "hal/spi/hal_spi_settings.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#ifdef JH_STM32G474_HW
#include "port/stm32g474_gpio_af.h"
#include "port/stm32g474_regs.h"
#endif

typedef struct {
  bool initialized;
  bool transaction_active;
  uint8_t rx_pin;
  uint8_t tx_pin;
  uint8_t sck_pin;
  hal_spi_settings_t settings;
  uint32_t actual_clock_hz;
  hal_mutex_t mutex;
#ifdef JH_STM32G474_HW
  bool hw_configured;
  uint32_t applied_cr1;
  uint32_t applied_cr2;
  volatile bool dma_tx_active;
  const uint8_t *volatile dma_tx_next;
  volatile size_t dma_tx_remaining;
  volatile uint32_t dma_tx_block_count;
  volatile hal_status_t dma_tx_status;
#endif
} hal_spi_bus_state_t;

static hal_spi_bus_state_t s_spi[2] = {};

static inline uint8_t spi_bus_index(uint8_t bus) {
  HAL_ASSERT(bus <= 1u, "hal_spi: invalid bus index");
  return (bus <= 1u) ? bus : 0u;
}

static inline hal_spi_bus_state_t *spi_state(uint8_t bus) {
  return &s_spi[spi_bus_index(bus)];
}

static void spi_default_pins(uint8_t idx, uint8_t *rx_pin, uint8_t *tx_pin,
                             uint8_t *sck_pin) {
  if (idx == 1u) {
    *rx_pin = (uint8_t)(1u * 16u + 14u);  /* PB14 = SPI2_MISO */
    *tx_pin = (uint8_t)(1u * 16u + 15u);  /* PB15 = SPI2_MOSI */
    *sck_pin = (uint8_t)(1u * 16u + 13u); /* PB13 = SPI2_SCK  */
  } else {
    *rx_pin = 6u;  /* PA6 = SPI1_MISO, Nucleo D12 */
    *tx_pin = 7u;  /* PA7 = SPI1_MOSI, Nucleo D11 */
    *sck_pin = 5u; /* PA5 = SPI1_SCK,  Nucleo D13 */
  }
}

static void spi_ensure_mutex(uint8_t bus) {
  hal_spi_bus_state_t *st = spi_state(bus);
  (void)jh_hal_mutex_create_once(&st->mutex);
}

#ifdef JH_STM32G474_HW
extern "C" uint8_t __jh_stm32_ccm_start;
extern "C" uint8_t __jh_stm32_ccm_end;

/* Reserved by the SPI backend; PWM audio uses DMA1 Channels1-2. */
static constexpr uint8_t kSpiDmaTxChannels[2] = {
    6u, /* SPI1 TX: DMA1 Channel7 */
    7u, /* SPI2 TX: DMA1 Channel8 */
};
static constexpr uint8_t kSpiDmaTxIrqs[2] = {
    DMA1_Channel7_IRQn,
    DMA1_Channel8_IRQn,
};
static constexpr uint32_t kSpiDmaMaxBlockBytes = UINT16_MAX;
static constexpr uint8_t kSpiDmaIrqPriority = 0x80u;

static inline uint32_t spi_hw_base(uint8_t idx) {
  return idx == 1u ? SPI2_BASE : SPI1_BASE;
}

static inline uint8_t spi_dma_tx_channel(uint8_t idx) {
  return kSpiDmaTxChannels[idx];
}

static inline uint32_t spi_dma_tx_request(uint8_t idx) {
  return idx == 1u ? DMA_REQUEST_SPI2_TX : DMA_REQUEST_SPI1_TX;
}

/* SPI kernel clock = APB clock for the controller: SPI1 on APB2 (PCLK2),
 * SPI2 on APB1 (PCLK1). Do NOT use the core clock directly - the two can
 * diverge once the PLL / APB prescalers are configured. */
static inline uint32_t spi_pclk_hz(uint8_t idx) {
  return idx == 1u ? JH_G474_PCLK1_HZ : JH_G474_PCLK2_HZ;
}

static void spi_hw_clock_enable(uint8_t idx) {
  if (idx == 1u) {
    RCC_APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
  } else {
    RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;
  }
}

static void spi_config_af5_pin(uint8_t pin) {
  jh_stm32g474_gpio_set_af(pin, 5u);
}

static uint32_t spi_prescaler_bits(uint32_t requested_hz, uint32_t source_hz,
                                   uint32_t *actual_hz) {
  if (requested_hz == 0u) {
    requested_hz = HAL_SPI_CLOCK_DEFAULT_HZ;
  }
  uint32_t div = 2u;
  uint32_t br = 0u;
  while ((source_hz / div) > requested_hz && br < 7u) {
    div <<= 1u;
    br++;
  }
  if (actual_hz != nullptr) {
    *actual_hz = source_hz / div;
  }
  return br << SPI_CR1_BR_POS;
}

static uint32_t spi_cr1_from_settings(const hal_spi_settings_t *settings,
                                      uint32_t source_hz, uint32_t *actual_hz) {
  uint32_t cr1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
  cr1 |= spi_prescaler_bits(settings->clock_hz, source_hz, actual_hz);
  if (settings->bit_order == HAL_SPI_LSBFIRST) {
    cr1 |= SPI_CR1_LSBFIRST;
  }
  if (settings->data_mode & 0x1u) {
    cr1 |= SPI_CR1_CPHA;
  }
  if (settings->data_mode & 0x2u) {
    cr1 |= SPI_CR1_CPOL;
  }
  return cr1;
}

static hal_status_t spi_wait_not_busy(uint32_t base) {
  uint32_t to = SPI_POLL_TIMEOUT;
  while ((SPI_SR(base) & SPI_SR_BSY) && to) {
    --to;
  }
  return to > 0u ? HAL_OK : HAL_ETIMEOUT;
}

static void spi_hw_drain_rx(uint32_t base) {
  while ((SPI_SR(base) & SPI_SR_RXNE) != 0u) {
    (void)SPI_DR8(base);
  }
  if ((SPI_SR(base) & SPI_SR_OVR) != 0u) {
    (void)SPI_DR(base);
    (void)SPI_SR(base);
  }
}

static bool spi_dma_memory_accessible(const uint8_t *data, size_t len) {
  const uintptr_t start = (uintptr_t)data;
  const uintptr_t end = start + len;
  const uintptr_t ccm_start = (uintptr_t)&__jh_stm32_ccm_start;
  const uintptr_t ccm_end = (uintptr_t)&__jh_stm32_ccm_end;
  if (end < start) {
    return false;
  }
  return end <= ccm_start || start >= ccm_end;
}

static void spi_dma_stop(uint8_t idx, hal_status_t status) {
  hal_spi_bus_state_t *st = &s_spi[idx];
  const uint32_t base = spi_hw_base(idx);
  const uint8_t channel = spi_dma_tx_channel(idx);

  SPI_CR2(base) &= ~SPI_CR2_TXDMAEN;
  DMA_CCR(DMA1_BASE, channel) &= ~DMA_CCR_EN;
  DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(channel);
  if ((SPI_SR(base) & SPI_SR_BSY) == 0u) {
    spi_hw_drain_rx(base);
  }

  st->dma_tx_active = false;
  st->dma_tx_next = nullptr;
  st->dma_tx_remaining = 0u;
  st->dma_tx_block_count = 0u;
  st->dma_tx_status = status;
}

static void spi_dma_reset_channel(uint8_t idx, bool configure_request) {
  const uint8_t channel = spi_dma_tx_channel(idx);
  spi_dma_stop(idx, HAL_OK);
  DMAMUX_CCR(channel) =
      configure_request ? (spi_dma_tx_request(idx) & DMAMUX_CCR_DMAREQ_ID_MASK)
                        : 0u;
}

static void spi_dma_start_block(uint8_t idx) {
  hal_spi_bus_state_t *st = &s_spi[idx];
  const uint32_t base = spi_hw_base(idx);
  const uint8_t channel = spi_dma_tx_channel(idx);
  const size_t block_size = st->dma_tx_remaining < kSpiDmaMaxBlockBytes
                                ? st->dma_tx_remaining
                                : (size_t)kSpiDmaMaxBlockBytes;
  const bool needs_chain_interrupt = st->dma_tx_remaining > block_size;

  SPI_CR2(base) &= ~SPI_CR2_TXDMAEN;
  DMA_CCR(DMA1_BASE, channel) &= ~DMA_CCR_EN;
  DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(channel);
  DMA_CPAR(DMA1_BASE, channel) = (uint32_t)(uintptr_t)&SPI_DR8(base);
  DMA_CMAR(DMA1_BASE, channel) = (uint32_t)(uintptr_t)st->dma_tx_next;
  DMA_CNDTR(DMA1_BASE, channel) = (uint32_t)block_size;
  DMA_CCR(DMA1_BASE, channel) = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_PL_HIGH;
  if (needs_chain_interrupt) {
    DMA_CCR(DMA1_BASE, channel) |= DMA_CCR_TCIE | DMA_CCR_TEIE;
  }

  st->dma_tx_next += block_size;
  st->dma_tx_remaining -= block_size;
  st->dma_tx_block_count = (uint32_t)block_size;
  st->dma_tx_active = true;

  DMA_CCR(DMA1_BASE, channel) |= DMA_CCR_EN;
  SPI_CR2(base) |= SPI_CR2_TXDMAEN;
}

static bool spi_dma_service_locked(uint8_t idx) {
  hal_spi_bus_state_t *st = &s_spi[idx];
  if (!st->dma_tx_active) {
    return false;
  }

  const uint8_t channel = spi_dma_tx_channel(idx);
  const uint32_t flags = DMA_ISR(DMA1_BASE);
  if ((flags & DMA_FLAG_TEIF(channel)) != 0u) {
    spi_dma_stop(idx, HAL_EIO);
    return false;
  }
  if (st->dma_tx_block_count == 0u) {
    if ((SPI_SR(spi_hw_base(idx)) & SPI_SR_BSY) != 0u) {
      return true;
    }
    spi_dma_stop(idx, HAL_OK);
    return false;
  }
  if ((flags & DMA_FLAG_TCIF(channel)) == 0u) {
    return true;
  }
  if (st->dma_tx_remaining > 0u) {
    spi_dma_start_block(idx);
    return true;
  }
  SPI_CR2(spi_hw_base(idx)) &= ~SPI_CR2_TXDMAEN;
  DMA_CCR(DMA1_BASE, channel) &= ~DMA_CCR_EN;
  DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(channel);
  st->dma_tx_block_count = 0u;
  if ((SPI_SR(spi_hw_base(idx)) & SPI_SR_BSY) != 0u) {
    return true;
  }

  spi_dma_stop(idx, HAL_OK);
  return false;
}

static bool spi_dma_service(uint8_t idx) {
  hal_critical_section_enter();
  const bool active = spi_dma_service_locked(idx);
  hal_critical_section_exit();
  return active;
}

static void spi_dma_stop_if_stalled(uint8_t idx, size_t observed_remaining) {
  hal_critical_section_enter();
  hal_spi_bus_state_t *st = &s_spi[idx];
  if (st->dma_tx_active && st->dma_tx_remaining == observed_remaining) {
    spi_dma_stop(idx, HAL_ETIMEOUT);
  }
  hal_critical_section_exit();
}

static uint32_t spi_dma_block_timeout_us(const hal_spi_bus_state_t *st) {
  const uint32_t clock_hz =
      st->actual_clock_hz > 0u ? st->actual_clock_hz : HAL_SPI_CLOCK_DEFAULT_HZ;
  const uint64_t transfer_us =
      (((uint64_t)st->dma_tx_block_count * 8u * 1000000u) + clock_hz - 1u) /
      clock_hz;
  const uint64_t timeout_us = transfer_us * 2u + 100000u;
  return timeout_us < UINT32_MAX ? (uint32_t)timeout_us : UINT32_MAX;
}

static hal_status_t spi_hw_apply(uint8_t idx) {
  hal_spi_bus_state_t *st = &s_spi[idx];
  if (!st->initialized) {
    return HAL_EUNINIT;
  }
  const uint32_t base = spi_hw_base(idx);
  uint32_t actual_hz = 0u;
  const uint32_t cr1 =
      spi_cr1_from_settings(&st->settings, spi_pclk_hz(idx), &actual_hz);
  const uint32_t cr2 = SPI_CR2_DS_8BIT | SPI_CR2_FRXTH;

  if (st->hw_configured && st->applied_cr1 == cr1 && st->applied_cr2 == cr2) {
    st->actual_clock_hz = actual_hz;
    return HAL_OK;
  }

  const hal_status_t wait_status = spi_wait_not_busy(base);
  if (hal_status_is_error(wait_status)) {
    return wait_status;
  }
  SPI_CR1(base) &= ~SPI_CR1_SPE;
  (void)SPI_DR(base);
  (void)SPI_SR(base);
  SPI_CR1(base) = cr1;
  SPI_CR2(base) = cr2;
  SPI_CR1(base) = cr1 | SPI_CR1_SPE;

  st->actual_clock_hz = actual_hz;
  st->applied_cr1 = cr1;
  st->applied_cr2 = cr2;
  st->hw_configured = true;
  return HAL_OK;
}

static hal_status_t spi_hw_init(uint8_t idx) {
  hal_spi_bus_state_t *st = &s_spi[idx];
  spi_hw_clock_enable(idx);
  RCC_AHB1ENR |= RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN;
  (void)RCC_AHB1ENR;
  spi_dma_reset_channel(idx, true);
  const uint8_t irq = kSpiDmaTxIrqs[idx];
  NVIC_IPR8(irq) = kSpiDmaIrqPriority;
  NVIC_ICPR(irq / 32u) = 1u << (irq % 32u);
  NVIC_ISER(irq / 32u) = 1u << (irq % 32u);
  spi_config_af5_pin(st->rx_pin);
  spi_config_af5_pin(st->tx_pin);
  spi_config_af5_pin(st->sck_pin);
  st->hw_configured = false;
  return spi_hw_apply(idx);
}

static hal_status_t spi_hw_transfer8(uint8_t idx, uint8_t data,
                                     uint8_t *out_received) {
  const uint32_t base = spi_hw_base(idx);
  uint32_t to = SPI_POLL_TIMEOUT;
  while (!(SPI_SR(base) & SPI_SR_TXE) && to) {
    --to;
  }
  if (to == 0u) {
    return HAL_ETIMEOUT;
  }

  SPI_DR8(base) = data;

  to = SPI_POLL_TIMEOUT;
  while (!(SPI_SR(base) & SPI_SR_RXNE) && to) {
    --to;
  }
  if (to == 0u) {
    return HAL_ETIMEOUT;
  }

  *out_received = SPI_DR8(base);
  return HAL_OK;
}
#endif /* JH_STM32G474_HW */

hal_status_t hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin,
                          uint8_t sck_pin) {
  if (bus > 1u) {
    return HAL_EINVAL;
  }
  uint8_t idx = spi_bus_index(bus);
  hal_spi_bus_state_t *st = &s_spi[idx];

#ifdef JH_STM32G474_HW
  if (st->initialized) {
    const hal_status_t wait_status = hal_spi_write_dma_async_wait_ex(idx);
    if (hal_status_is_error(wait_status)) {
      return wait_status;
    }
  }
#endif
  spi_ensure_mutex(idx);
  if (st->mutex == nullptr) {
    return HAL_ENOMEM;
  }
  st->rx_pin = rx_pin;
  st->tx_pin = tx_pin;
  st->sck_pin = sck_pin;
  st->settings = spi_default_settings();
  st->transaction_active = false;
  st->initialized = true;
#ifdef JH_STM32G474_HW
  const hal_status_t status = spi_hw_init(idx);
  if (hal_status_is_error(status)) {
    st->initialized = false;
  }
  return status;
#else
  return HAL_OK;
#endif
}

void hal_spi_deinit(uint8_t bus) {
  const uint8_t idx = spi_bus_index(bus);
  hal_spi_bus_state_t *st = &s_spi[idx];
#ifdef JH_STM32G474_HW
  (void)hal_spi_write_dma_async_wait_ex(idx);
  const uint8_t irq = kSpiDmaTxIrqs[idx];
  NVIC_ICER(irq / 32u) = 1u << (irq % 32u);
  NVIC_ICPR(irq / 32u) = 1u << (irq % 32u);
  spi_dma_reset_channel(idx, false);
  SPI_CR1(spi_hw_base(idx)) &= ~SPI_CR1_SPE;
  st->hw_configured = false;
#endif
  st->transaction_active = false;
  st->initialized = false;
}

void hal_spi_lock(uint8_t bus) {
  uint8_t idx = spi_bus_index(bus);
  spi_ensure_mutex(idx);
  hal_mutex_lock(s_spi[idx].mutex);
}

void hal_spi_unlock(uint8_t bus) {
  uint8_t idx = spi_bus_index(bus);
  spi_ensure_mutex(idx);
  hal_mutex_unlock(s_spi[idx].mutex);
}

hal_status_t hal_spi_begin_transaction(uint8_t bus,
                                       const hal_spi_settings_t *settings) {
  if (bus > 1u || !spi_settings_valid(settings)) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  hal_spi_bus_state_t *st = &s_spi[idx];
  if (!st->initialized) {
    uint8_t rx = 0u, tx = 0u, sck = 0u;
    spi_default_pins(idx, &rx, &tx, &sck);
    const hal_status_t status = hal_spi_init(idx, rx, tx, sck);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
#ifdef JH_STM32G474_HW
  if (st->dma_tx_active) {
    return HAL_EBUSY;
  }
#endif
  st->settings = spi_normalize_settings(settings);
#ifdef JH_STM32G474_HW
  const hal_status_t status = spi_hw_apply(idx);
  if (hal_status_is_error(status)) {
    return status;
  }
#endif
  st->transaction_active = true;
  return HAL_OK;
}

hal_status_t hal_spi_end_transaction(uint8_t bus) {
  if (bus > 1u) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
#ifdef JH_STM32G474_HW
  const hal_status_t dma_status = hal_spi_write_dma_async_wait_ex(idx);
  if (hal_status_is_error(dma_status)) {
    return dma_status;
  }
  if (s_spi[idx].initialized) {
    const hal_status_t status = spi_wait_not_busy(spi_hw_base(idx));
    if (hal_status_is_error(status)) {
      return status;
    }
  }
#endif
  s_spi[idx].transaction_active = false;
  return HAL_OK;
}

hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                 uint8_t *out_received) {
  if (bus > 1u || out_received == nullptr) {
    return HAL_EINVAL;
  }
  const uint8_t idx = spi_bus_index(bus);
  if (!s_spi[idx].initialized) {
    uint8_t rx = 0u, tx = 0u, sck = 0u;
    spi_default_pins(idx, &rx, &tx, &sck);
    const hal_status_t status = hal_spi_init(idx, rx, tx, sck);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
#ifdef JH_STM32G474_HW
  if (s_spi[idx].dma_tx_active) {
    return HAL_EBUSY;
  }
  return spi_hw_transfer8(idx, data, out_received);
#else
  (void)data;
  *out_received = 0xFFu;
  return HAL_OK;
#endif
}

hal_status_t jh_hal_spi_transfer16_provider(uint8_t bus, uint16_t data,
                                            uint16_t *out_received) {
  const uint8_t idx = spi_bus_index(bus);
  if (!s_spi[idx].initialized) {
    /* Initialise before reading bit_order so the byte order matches the
     * default settings the byte transfers below will apply, not the
     * zero-initialised (LSBFIRST) state. */
    uint8_t rx = 0u, tx = 0u, sck = 0u;
    spi_default_pins(idx, &rx, &tx, &sck);
    const hal_status_t status = hal_spi_init(idx, rx, tx, sck);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
  uint8_t tx[2];
  uint8_t rx[2] = {};
  hal_status_t status;
  if (s_spi[idx].settings.bit_order == HAL_SPI_LSBFIRST) {
    jh_store_le16(tx, data);
    status = hal_spi_transfer_ex(idx, tx[0], &rx[0]);
    if (hal_status_is_error(status)) {
      return status;
    }
    status = hal_spi_transfer_ex(idx, tx[1], &rx[1]);
  } else {
    jh_store_be16(tx, data);
    status = hal_spi_transfer_ex(idx, tx[0], &rx[0]);
    if (hal_status_is_error(status)) {
      return status;
    }
    status = hal_spi_transfer_ex(idx, tx[1], &rx[1]);
  }
  if (hal_status_is_error(status)) {
    return status;
  }
  *out_received = s_spi[idx].settings.bit_order == HAL_SPI_LSBFIRST
                      ? jh_load_le16(rx)
                      : jh_load_be16(rx);
  return HAL_OK;
}

hal_status_t hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                                   size_t len) {
  return jh_hal_spi_transfer_txrx_generic(bus, tx, rx, len);
}

hal_status_t jh_hal_spi_write_provider(uint8_t bus, const uint8_t *data,
                                       size_t len) {
  return hal_spi_transfer_txrx(bus, data, nullptr, len);
}

hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus, const uint8_t *data,
                                              size_t len) {
  if (bus > 1u || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }
  const uint8_t idx = spi_bus_index(bus);
  if (!s_spi[idx].initialized) {
    uint8_t rx = 0u, tx = 0u, sck = 0u;
    spi_default_pins(idx, &rx, &tx, &sck);
    const hal_status_t init_status = hal_spi_init(idx, rx, tx, sck);
    if (hal_status_is_error(init_status)) {
      return init_status;
    }
  }
#ifdef JH_STM32G474_HW
  hal_spi_bus_state_t *st = &s_spi[idx];
  if (st->dma_tx_active) {
    return HAL_EBUSY;
  }
  st->dma_tx_status = HAL_OK;
  /* STM32G474 CCM SRAM is CPU-only, so preserve API correctness with the
   * polling path for buffers placed there. */
  if (!spi_dma_memory_accessible(data, len)) {
    return hal_spi_write(bus, data, len);
  }

  const uint32_t base = spi_hw_base(idx);
  const hal_status_t idle_status = spi_wait_not_busy(base);
  if (hal_status_is_error(idle_status)) {
    return idle_status;
  }
  spi_hw_drain_rx(base);
  st->dma_tx_next = data;
  st->dma_tx_remaining = len;
  spi_dma_start_block(idx);
  return HAL_OK;
#else
  return hal_spi_write(bus, data, len);
#endif
}

bool hal_spi_write_dma_async_busy(uint8_t bus) {
  if (bus > 1u) {
    return false;
  }
#ifdef JH_STM32G474_HW
  return spi_dma_service(spi_bus_index(bus));
#else
  (void)bus;
  return false;
#endif
}

hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus) {
  if (bus > 1u) {
    return HAL_EINVAL;
  }
#ifdef JH_STM32G474_HW
  const uint8_t idx = spi_bus_index(bus);
  hal_spi_bus_state_t *st = &s_spi[idx];
  size_t observed_remaining = st->dma_tx_remaining;
  uint32_t block_started_us = hal_micros();
  uint32_t timeout_us = spi_dma_block_timeout_us(st);

  while (spi_dma_service(idx)) {
    if (st->dma_tx_remaining != observed_remaining) {
      observed_remaining = st->dma_tx_remaining;
      block_started_us = hal_micros();
      timeout_us = spi_dma_block_timeout_us(st);
    } else if (hal_elapsed_u32(hal_micros(), block_started_us, timeout_us)) {
      spi_dma_stop_if_stalled(idx, observed_remaining);
    }
  }

  const hal_status_t status = st->dma_tx_status;
  st->dma_tx_status = HAL_OK;
  return status;
#else
  return HAL_OK;
#endif
}

#ifdef JH_STM32G474_HW
extern "C" void DMA1_Channel7_IRQHandler(void) {
  (void)spi_dma_service_locked(0u);
}

extern "C" void DMA1_Channel8_IRQHandler(void) {
  (void)spi_dma_service_locked(1u);
}
#endif

#endif // HAL_TARGET_IS_STM32G474
