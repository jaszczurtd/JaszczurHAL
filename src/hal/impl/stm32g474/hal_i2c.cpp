#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#ifdef HAL_ENABLE_I2C

#include "../../hal_i2c.h"
#include "../../hal_sync.h"

#include <string.h>

#ifdef JH_STM32G474_HW
#include "port/stm32g474_regs.h"
#endif

#define STM32_I2C_BUF_SIZE 255

typedef struct {
    uint8_t rx_buf[STM32_I2C_BUF_SIZE];
    int rx_len;
    int rx_pos;
    uint8_t tx_buf[STM32_I2C_BUF_SIZE];
    int tx_len;
    uint8_t cur_addr;
    uint8_t last_error;      /* result of the last end_transmission */
    bool initialized;
    uint32_t clock_hz;
    uint32_t transaction_count;
    uint32_t bus_clear_count;
    hal_mutex_t mutex;
} i2c_bus_state_t;

static i2c_bus_state_t s_i2c[2] = {};

static inline uint8_t i2c_bus_index(uint8_t bus) {
    return bus == 1u ? 1u : 0u;
}

static inline i2c_bus_state_t *i2c_state(uint8_t bus) {
    return &s_i2c[i2c_bus_index(bus)];
}

static void i2c_ensure_mutex(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    if (st->mutex == NULL) {
        hal_critical_section_enter();
        if (st->mutex == NULL) {
            st->mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

/* ── Real I2C1 backend (bus 0 only): SCL=PB8, SDA=PB9, AF4, 100 kHz ───────── */
#ifdef JH_STM32G474_HW
#define I2C_TIMEOUT 200000u

static void gpiob_i2c_pin(uint32_t pin) {
    GPIO_MODER(1)  = (GPIO_MODER(1) & ~(0x3u << (pin * 2u))) | (GPIO_MODE_AF << (pin * 2u));
    GPIO_OTYPER(1)  |= (1u << pin);                          /* open-drain */
    GPIO_OSPEEDR(1) |= (0x3u << (pin * 2u));                 /* high speed */
    GPIO_PUPDR(1)  = (GPIO_PUPDR(1) & ~(0x3u << (pin * 2u))) | (GPIO_PUPD_UP << (pin * 2u));
    const uint32_t p = pin - 8u;                             /* AFRH covers 8..15 */
    GPIO_AFRH(1)   = (GPIO_AFRH(1) & ~(0xFu << (p * 4u))) | (4u << (p * 4u));  /* AF4 */
}

static void i2c1_hw_init(void) {
    RCC_AHB2ENR  |= RCC_AHB2ENR_GPIOBEN;
    RCC_APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    gpiob_i2c_pin(8u);   /* PB8 = SCL */
    gpiob_i2c_pin(9u);   /* PB9 = SDA */
    I2C1_CR1     &= ~I2C_CR1_PE;
    I2C1_TIMINGR  = I2C_TIMINGR_100K_16MHZ;
    I2C1_CR1     |= I2C_CR1_PE;
}

/* Master write of @p len bytes (AUTOEND). Returns 0 ok / 2 NACK / 4 timeout. */
static uint8_t i2c1_hw_write(uint8_t addr, const uint8_t *buf, int len) {
    I2C1_ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
    I2C1_CR2 = ((uint32_t)addr << 1) | ((uint32_t)(uint8_t)len << 16) |
               I2C_CR2_AUTOEND | I2C_CR2_START;
    for (int i = 0; i < len; ++i) {
        uint32_t to = I2C_TIMEOUT;
        while (!(I2C1_ISR & (I2C_ISR_TXIS | I2C_ISR_NACKF)) && to) { --to; }
        if (to == 0u) { return 4u; }
        if (I2C1_ISR & I2C_ISR_NACKF) { break; }
        I2C1_TXDR = buf[i];
    }
    uint32_t to = I2C_TIMEOUT;
    while (!(I2C1_ISR & I2C_ISR_STOPF) && to) { --to; }
    if (to == 0u) { return 4u; }
    const bool nack = (I2C1_ISR & I2C_ISR_NACKF) != 0u;
    I2C1_ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
    return nack ? 2u : 0u;
}

/* Master read of @p len bytes (AUTOEND). Returns number of bytes received. */
static int i2c1_hw_read(uint8_t addr, uint8_t *buf, int len) {
    if (len <= 0) { return 0; }
    I2C1_ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
    I2C1_CR2 = ((uint32_t)addr << 1) | ((uint32_t)(uint8_t)len << 16) |
               I2C_CR2_RD_WRN | I2C_CR2_AUTOEND | I2C_CR2_START;
    int got = 0;
    for (int i = 0; i < len; ++i) {
        uint32_t to = I2C_TIMEOUT;
        while (!(I2C1_ISR & (I2C_ISR_RXNE | I2C_ISR_NACKF)) && to) { --to; }
        if (to == 0u || (I2C1_ISR & I2C_ISR_NACKF)) { break; }
        buf[i] = (uint8_t)I2C1_RXDR;
        ++got;
    }
    uint32_t to = I2C_TIMEOUT;
    while (!(I2C1_ISR & I2C_ISR_STOPF) && to) { --to; }
    I2C1_ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
    return got;
}

/* Zero-byte probe: returns true if the device ACKs (is present). */
static bool i2c1_hw_ack(uint8_t addr) {
    I2C1_ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
    I2C1_CR2 = ((uint32_t)addr << 1) | I2C_CR2_AUTOEND | I2C_CR2_START;
    uint32_t to = I2C_TIMEOUT;
    while (!(I2C1_ISR & I2C_ISR_STOPF) && to) { --to; }
    const bool nack = (I2C1_ISR & I2C_ISR_NACKF) != 0u;
    I2C1_ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
    return (to != 0u) && !nack;
}

static inline bool i2c_hw_bus(uint8_t bus) { return i2c_bus_index(bus) == 0u; }
#endif /* JH_STM32G474_HW */

void hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
    hal_i2c_init_bus(0, sda_pin, scl_pin, clock_hz);
}

void hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
    (void)sda_pin;   /* fixed to PB8/PB9 on the hardware backend */
    (void)scl_pin;

    i2c_ensure_mutex(bus);

    i2c_bus_state_t *st = i2c_state(bus);
    st->rx_len = 0;
    st->rx_pos = 0;
    st->tx_len = 0;
    st->cur_addr = 0u;
    st->last_error = 0u;
    st->clock_hz = clock_hz;
    st->transaction_count = 0u;
    st->bus_clear_count = 0u;
    st->initialized = true;

#ifdef JH_STM32G474_HW
    if (i2c_hw_bus(bus)) {
        i2c1_hw_init();   /* clock_hz honored as standard-mode 100 kHz */
    }
#endif
}

void hal_i2c_set_clock(uint32_t clock_hz) {
    hal_i2c_set_clock_bus(0, clock_hz);
}

void hal_i2c_set_clock_bus(uint8_t bus, uint32_t clock_hz) {
    i2c_ensure_mutex(bus);
    hal_mutex_lock(i2c_state(bus)->mutex);
    i2c_state(bus)->clock_hz = clock_hz;
    hal_mutex_unlock(i2c_state(bus)->mutex);
}

void hal_i2c_deinit(void) {
    hal_i2c_deinit_bus(0);
}

void hal_i2c_deinit_bus(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    st->initialized = false;
    st->rx_len = 0;
    st->rx_pos = 0;
    st->tx_len = 0;
    st->cur_addr = 0u;
#ifdef JH_STM32G474_HW
    if (i2c_hw_bus(bus)) {
        I2C1_CR1 &= ~I2C_CR1_PE;
    }
#endif
}

void hal_i2c_lock(void) {
    hal_i2c_lock_bus(0);
}

void hal_i2c_lock_bus(uint8_t bus) {
    i2c_ensure_mutex(bus);
    hal_mutex_lock(i2c_state(bus)->mutex);
}

void hal_i2c_unlock(void) {
    hal_i2c_unlock_bus(0);
}

void hal_i2c_unlock_bus(uint8_t bus) {
    i2c_ensure_mutex(bus);
    hal_mutex_unlock(i2c_state(bus)->mutex);
}

void hal_i2c_begin_transmission(uint8_t address) {
    hal_i2c_begin_transmission_bus(0, address);
}

void hal_i2c_begin_transmission_bus(uint8_t bus, uint8_t address) {
    hal_i2c_lock_bus(bus);
    i2c_state(bus)->cur_addr = address;
    i2c_state(bus)->tx_len = 0;
}

size_t hal_i2c_write(uint8_t data) {
    return hal_i2c_write_bus(0, data);
}

size_t hal_i2c_write_bus(uint8_t bus, uint8_t data) {
    i2c_bus_state_t *st = i2c_state(bus);
    if (st->tx_len >= STM32_I2C_BUF_SIZE) {
        return 0u;
    }
    st->tx_buf[st->tx_len++] = data;
    return 1u;
}

uint8_t hal_i2c_end_transmission(void) {
    return hal_i2c_end_transmission_bus(0);
}

uint8_t hal_i2c_end_transmission_bus(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    uint8_t err = 0u;
#ifdef JH_STM32G474_HW
    if (i2c_hw_bus(bus) && st->initialized) {
        err = i2c1_hw_write(st->cur_addr, st->tx_buf, st->tx_len);
    }
#endif
    st->last_error = err;
    st->tx_len = 0;
    st->transaction_count++;
    hal_i2c_unlock_bus(bus);
    return err;
}

uint8_t hal_i2c_write_byte(uint8_t address, uint8_t data, bool *outWriteOk) {
    return hal_i2c_write_byte_bus(0, address, data, outWriteOk);
}

uint8_t hal_i2c_write_byte_bus(uint8_t bus, uint8_t address, uint8_t data, bool *outWriteOk) {
    hal_i2c_begin_transmission_bus(bus, address);
    size_t written = hal_i2c_write_bus(bus, data);
    if (outWriteOk != NULL) {
        *outWriteOk = (written == 1u);
    }
    return hal_i2c_end_transmission_bus(bus);
}

uint8_t hal_i2c_read_byte(uint8_t address, bool *outReadOk) {
    return hal_i2c_read_byte_bus(0, address, outReadOk);
}

uint8_t hal_i2c_read_byte_bus(uint8_t bus, uint8_t address, bool *outReadOk) {
    uint8_t got = hal_i2c_request_from_bus(bus, address, 1u);
    int v = hal_i2c_read_bus(bus);
    if (outReadOk != NULL) {
        *outReadOk = (got == 1u) && (v >= 0);
    }
    return (v >= 0) ? (uint8_t)v : 0u;
}

uint8_t hal_i2c_request_from(uint8_t address, uint8_t count) {
    return hal_i2c_request_from_bus(0, address, count);
}

uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count) {
    i2c_bus_state_t *st = i2c_state(bus);
    (void)address;

    hal_i2c_lock_bus(bus);
    /* count is uint8_t (<=255) and the rx buffer holds 255 bytes, so it always fits. */
    int got = 0;
#ifdef JH_STM32G474_HW
    if (i2c_hw_bus(bus) && st->initialized) {
        got = i2c1_hw_read(address, st->rx_buf, (int)count);
    } else {
        got = (int)count;   /* bus 1 / host-stub: accept request */
    }
#else
    got = (int)count;
#endif
    st->rx_len = got;
    st->rx_pos = 0;
    st->transaction_count++;
    hal_i2c_unlock_bus(bus);
    return (uint8_t)got;
}

int hal_i2c_available(void) {
    return hal_i2c_available_bus(0);
}

int hal_i2c_available_bus(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    return st->rx_len - st->rx_pos;
}

int hal_i2c_read(void) {
    return hal_i2c_read_bus(0);
}

int hal_i2c_read_bus(uint8_t bus) {
    i2c_bus_state_t *st = i2c_state(bus);
    if (st->rx_pos < st->rx_len) {
        return st->rx_buf[st->rx_pos++];
    }
    return -1;
}

bool hal_i2c_is_busy(uint8_t address) {
    return hal_i2c_is_busy_bus(0, address);
}

bool hal_i2c_is_busy_bus(uint8_t bus, uint8_t address) {
#ifdef JH_STM32G474_HW
    if (i2c_hw_bus(bus) && i2c_state(bus)->initialized) {
        hal_i2c_lock_bus(bus);
        bool ack = i2c1_hw_ack(address);
        hal_i2c_unlock_bus(bus);
        return !ack;   /* busy/absent == did NOT ACK */
    }
#endif
    (void)address;
    return false;
}

uint32_t hal_i2c_get_transaction_count(void) {
    return hal_i2c_get_transaction_count_bus(0);
}

uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus) {
    return i2c_state(bus)->transaction_count;
}

void hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin) {
    hal_i2c_bus_clear_bus(0, sda_pin, scl_pin);
}

void hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin) {
    (void)sda_pin;
    (void)scl_pin;
    /* GPIO-level 9-clock recovery is a follow-up on this backend. */
    i2c_state(bus)->bus_clear_count++;
}

#endif /* HAL_ENABLE_I2C */

#endif  // HAL_TARGET_IS_STM32G474
