#include "../../hal_target.h"

#if HAL_TARGET_IS_STM32G474
#include "../../hal_config.h"

#if defined(HAL_ENABLE_TLS) || defined(HAL_ENABLE_WIREGUARD) ||                \
    defined(HAL_ENABLE_BLE_STREAM)

#include "../../hal_status.h"
#include "../../hal_sync.h"
#include "../shared/jh_secure_random.h"
#include "stm32g474_secure_random.h"
#if defined(HAL_ENABLE_TLS)
#include "../shared/frameworks/BearSSL/jh_bearssl_provider.h"
#endif
#include "../shared/hal_mutex_once.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(JH_STM32G474_HW)
#include "port/stm32g474_regs.h"
#endif

namespace {

hal_mutex_t s_rng_mutex;

#if defined(JH_STM32G474_HW)
#if defined(HAL_ENABLE_TLS)
hal_mutex_t s_tls_provider_mutex;
bool s_tls_provider_in_use;
alignas(8)
    __attribute__((section(".ccmram"))) jh_bearssl_client_t s_tls_provider;
#endif

hal_status_t rng_initialize(void) {
  RCC_CRRCR |= RCC_CRRCR_HSI48ON;
  uint32_t remaining = RNG_POLL_TIMEOUT;
  while ((RCC_CRRCR & RCC_CRRCR_HSI48RDY) == 0u && remaining > 0u) {
    --remaining;
  }
  if ((RCC_CRRCR & RCC_CRRCR_HSI48RDY) == 0u) {
    return HAL_EHW;
  }

  RCC_CCIPR = (RCC_CCIPR & ~RCC_CCIPR_CLK48SEL_MASK) | RCC_CCIPR_CLK48SEL_HSI48;
  RCC_AHB2ENR |= RCC_AHB2ENR_RNGEN;
  (void)RCC_AHB2ENR;

  RNG_CR &= ~RNG_CR_RNGEN;
  RNG_SR &= ~(RNG_SR_CEIS | RNG_SR_SEIS);
  RNG_CR |= RNG_CR_RNGEN;
  return HAL_OK;
}

hal_status_t rng_read_word(uint32_t *out_word) {
  uint32_t remaining = RNG_POLL_TIMEOUT;
  while (remaining > 0u) {
    const uint32_t status = RNG_SR;
    if ((status & RNG_SR_ERRORS) != 0u) {
      return HAL_EHW;
    }
    if ((status & RNG_SR_DRDY) != 0u) {
      *out_word = RNG_DR;
      return HAL_OK;
    }
    --remaining;
  }
  return HAL_ETIMEOUT;
}
#endif

} // namespace

#if defined(JH_STM32G474_HW) && defined(HAL_ENABLE_TLS)
jh_bearssl_client_t *jh_bearssl_client_allocate(void) {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_tls_provider_mutex);
  if (mutex == nullptr) {
    return nullptr;
  }
  hal_mutex_lock(mutex);
  jh_bearssl_client_t *provider = nullptr;
  if (!s_tls_provider_in_use) {
    s_tls_provider_in_use = true;
    memset(&s_tls_provider, 0, sizeof(s_tls_provider));
    provider = &s_tls_provider;
  }
  hal_mutex_unlock(mutex);
  return provider;
}

void jh_bearssl_client_release(jh_bearssl_client_t *provider) {
  if (provider == nullptr) {
    return;
  }
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_tls_provider_mutex);
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (provider == &s_tls_provider) {
    memset(&s_tls_provider, 0, sizeof(s_tls_provider));
    s_tls_provider_in_use = false;
  }
  hal_mutex_unlock(mutex);
}
#endif

hal_status_t jh_stm32g474_secure_random_bytes(void *buffer, size_t length) {
  if (buffer == nullptr || length == 0u) {
    return HAL_EINVAL;
  }

  uint8_t *bytes = static_cast<uint8_t *>(buffer);
  memset(bytes, 0, length);

#if !defined(JH_STM32G474_HW)
  return HAL_EUNSUPPORTED;
#else
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_rng_mutex);
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);

  hal_status_t status = rng_initialize();
  size_t offset = 0u;
  while (status == HAL_OK && offset < length) {
    uint32_t word = 0u;
    status = rng_read_word(&word);
    const size_t available = length - offset;
    const size_t count = available < sizeof(word) ? available : sizeof(word);
    for (size_t index = 0u; status == HAL_OK && index < count; ++index) {
      bytes[offset + index] = (uint8_t)(word >> (index * 8u));
    }
    offset += count;
  }

  if (status != HAL_OK) {
    RNG_CR &= ~RNG_CR_RNGEN;
    memset(bytes, 0, length);
  }
  hal_mutex_unlock(mutex);
  return status;
#endif
}

extern "C" hal_status_t jh_secure_random_bytes(void *buffer, size_t length) {
  return jh_stm32g474_secure_random_bytes(buffer, length);
}

#endif
#endif
