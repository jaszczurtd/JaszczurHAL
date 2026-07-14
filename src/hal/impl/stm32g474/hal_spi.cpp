#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#include "../../hal_spi.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"

#ifdef JH_STM32G474_HW
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

static bool spi_settings_valid(const hal_spi_settings_t *settings) {
  return settings == nullptr || ((settings->bit_order == HAL_SPI_LSBFIRST ||
                                  settings->bit_order == HAL_SPI_MSBFIRST) &&
                                 settings->data_mode <= HAL_SPI_MODE3);
}

static hal_spi_settings_t spi_default_settings(void) {
  hal_spi_settings_t s = {HAL_SPI_CLOCK_DEFAULT_HZ, HAL_SPI_MSBFIRST,
                          HAL_SPI_MODE0};
  return s;
}

static hal_spi_settings_t
spi_normalize_settings(const hal_spi_settings_t *settings) {
  hal_spi_settings_t s = settings ? *settings : spi_default_settings();
  if (s.clock_hz == 0u) {
    s.clock_hz = HAL_SPI_CLOCK_DEFAULT_HZ;
  }
  if (s.bit_order != HAL_SPI_LSBFIRST) {
    s.bit_order = HAL_SPI_MSBFIRST;
  }
  if (s.data_mode > HAL_SPI_MODE3) {
    s.data_mode = HAL_SPI_MODE0;
  }
  return s;
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
static inline uint32_t spi_hw_base(uint8_t idx) {
  return idx == 1u ? SPI2_BASE : SPI1_BASE;
}

/* SPI kernel clock = APB clock for the controller: SPI1 on APB2 (PCLK2),
 * SPI2 on APB1 (PCLK1). Do NOT use the core clock directly - the two can
 * diverge once the PLL / APB prescalers are configured. */
static inline uint32_t spi_pclk_hz(uint8_t idx) {
  return idx == 1u ? JH_G474_PCLK1_HZ : JH_G474_PCLK2_HZ;
}

static inline uint32_t spi_pin_port(uint8_t pin) {
  return (uint32_t)(pin >> 4);
}
static inline uint32_t spi_pin_num(uint8_t pin) {
  return (uint32_t)(pin & 0x0Fu);
}

static void spi_gpio_clock_enable(uint32_t port) {
  if (port <= 6u) {
    RCC_AHB2ENR |= (1u << port);
  }
}

static void spi_hw_clock_enable(uint8_t idx) {
  if (idx == 1u) {
    RCC_APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
  } else {
    RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;
  }
}

static void spi_config_af5_pin(uint8_t pin) {
  const uint32_t port = spi_pin_port(pin);
  const uint32_t n = spi_pin_num(pin);
  if (port > 6u) {
    return;
  }
  spi_gpio_clock_enable(port);
  GPIO_MODER(port) =
      (GPIO_MODER(port) & ~(0x3u << (n * 2u))) | (GPIO_MODE_AF << (n * 2u));
  GPIO_OTYPER(port) &= ~(1u << n);          /* push-pull */
  GPIO_OSPEEDR(port) |= (0x3u << (n * 2u)); /* very high speed */
  GPIO_PUPDR(port) =
      (GPIO_PUPDR(port) & ~(0x3u << (n * 2u))) | (GPIO_PUPD_NONE << (n * 2u));
  if (n < 8u) {
    GPIO_AFRL(port) = (GPIO_AFRL(port) & ~(0xFu << (n * 4u))) |
                      (5u << (n * 4u)); /* AF5 = SPI1/SPI2 */
  } else {
    const uint32_t p = n - 8u;
    GPIO_AFRH(port) =
        (GPIO_AFRH(port) & ~(0xFu << (p * 4u))) | (5u << (p * 4u));
  }
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
  return spi_hw_transfer8(idx, data, out_received);
#else
  (void)data;
  *out_received = 0xFFu;
  return HAL_OK;
#endif
}

uint8_t hal_spi_transfer(uint8_t bus, uint8_t data) {
  uint8_t received = 0xFFu;
  (void)hal_spi_transfer_ex(bus, data, &received);
  return received;
}

hal_status_t hal_spi_transfer16_ex(uint8_t bus, uint16_t data,
                                   uint16_t *out_received) {
  if (bus > 1u || out_received == nullptr) {
    return HAL_EINVAL;
  }
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
  uint16_t in = 0u;
  uint8_t first = 0u;
  uint8_t second = 0u;
  hal_status_t status;
  if (s_spi[idx].settings.bit_order == HAL_SPI_LSBFIRST) {
    status = hal_spi_transfer_ex(idx, (uint8_t)(data & 0xFFu), &first);
    if (hal_status_is_error(status)) {
      return status;
    }
    status = hal_spi_transfer_ex(idx, (uint8_t)(data >> 8), &second);
    in = (uint16_t)(first | ((uint16_t)second << 8));
  } else {
    status = hal_spi_transfer_ex(idx, (uint8_t)(data >> 8), &first);
    if (hal_status_is_error(status)) {
      return status;
    }
    status = hal_spi_transfer_ex(idx, (uint8_t)(data & 0xFFu), &second);
    in = (uint16_t)(((uint16_t)first << 8) | second);
  }
  if (hal_status_is_error(status)) {
    return status;
  }
  *out_received = in;
  return HAL_OK;
}

uint16_t hal_spi_transfer16(uint8_t bus, uint16_t data) {
  uint16_t received = 0xFFFFu;
  (void)hal_spi_transfer16_ex(bus, data, &received);
  return received;
}

hal_status_t hal_spi_transfer_buffer(uint8_t bus, uint8_t *buffer, size_t len) {
  if (bus > 1u || (len > 0u && buffer == nullptr)) {
    return HAL_EINVAL;
  }
  return hal_spi_transfer_txrx(bus, buffer, buffer, len);
}

hal_status_t hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                                   size_t len) {
  if (bus > 1u || (len > 0u && tx == nullptr && rx == nullptr)) {
    return HAL_EINVAL;
  }
  for (size_t i = 0; i < len; ++i) {
    const uint8_t out = tx ? tx[i] : 0xFFu;
    uint8_t in = 0u;
    const hal_status_t status = hal_spi_transfer_ex(bus, out, &in);
    if (hal_status_is_error(status)) {
      return status;
    }
    if (rx) {
      rx[i] = in;
    }
  }
  return HAL_OK;
}

hal_status_t hal_spi_write(uint8_t bus, const uint8_t *data, size_t len) {
  if (bus > 1u || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  return hal_spi_transfer_txrx(bus, data, nullptr, len);
}

hal_status_t hal_spi_write_dma_ex(uint8_t bus, const uint8_t *data,
                                  size_t len) {
  hal_status_t status = hal_spi_write_dma_async_start_ex(bus, data, len);
  if (hal_status_is_error(status)) {
    return status;
  }
  return hal_spi_write_dma_async_wait_ex(bus);
}

bool hal_spi_write_dma(uint8_t bus, const uint8_t *data, size_t len) {
  return hal_status_to_bool(hal_spi_write_dma_ex(bus, data, len));
}

hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus, const uint8_t *data,
                                              size_t len) {
  if (bus > 1u || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }
  return hal_spi_write(bus, data, len);
}

bool hal_spi_write_dma_async_start(uint8_t bus, const uint8_t *data,
                                   size_t len) {
  return hal_status_to_bool(hal_spi_write_dma_async_start_ex(bus, data, len));
}

bool hal_spi_write_dma_async_busy(uint8_t bus) {
  (void)bus;
  return false;
}

hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus) {
  return bus <= 1u ? HAL_OK : HAL_EINVAL;
}

bool hal_spi_write_dma_async_wait(uint8_t bus) {
  return hal_status_to_bool(hal_spi_write_dma_async_wait_ex(bus));
}

#endif // HAL_TARGET_IS_STM32G474
