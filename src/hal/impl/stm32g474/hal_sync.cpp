#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#include "hal/system/hal_sync.h"

#if defined(HAL_ENABLE_FREERTOS)
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>
#endif

#include <new>
#include <stddef.h>
#include <stdint.h>

#if defined(HAL_ENABLE_FREERTOS)
#define JH_STM32_HAL_SYNC_FREERTOS 1
#else
#define JH_STM32_HAL_SYNC_FREERTOS 0
#endif

struct hal_mutex_impl_t {
#if JH_STM32_HAL_SYNC_FREERTOS
  SemaphoreHandle_t handle;
#else
  volatile uint8_t locked;
#endif
};

namespace {
volatile uint32_t s_critical_depth = 0u;

#if defined(__arm__) || defined(__thumb__)
uint32_t s_saved_primask = 0u;

static inline uint32_t stm32_read_primask(void) {
  uint32_t primask;
  __asm__ volatile("MRS %0, primask" : "=r"(primask)::"memory");
  return primask;
}

static inline void stm32_disable_irq(void) {
  __asm__ volatile("cpsid i" ::: "memory");
}

static inline void stm32_enable_irq(void) {
  __asm__ volatile("cpsie i" ::: "memory");
}

#if JH_STM32_HAL_SYNC_FREERTOS
static inline bool stm32_in_isr(void) {
  uint32_t ipsr;
  __asm__ volatile("MRS %0, ipsr" : "=r"(ipsr));
  return (ipsr & 0x1FFu) != 0u;
}
#endif
#else
#if JH_STM32_HAL_SYNC_FREERTOS
static inline bool stm32_in_isr(void) { return false; }
#endif
#endif
} // namespace

#if !JH_STM32_HAL_SYNC_FREERTOS
static inline void hal_sync_relax(void) {
#if defined(__arm__) || defined(__thumb__) || defined(__aarch64__)
  __asm__ volatile("nop");
#endif
}
#endif

extern "C" bool hal_stm32g474_critical_section_active(void) {
  return s_critical_depth > 0u;
}

hal_mutex_t hal_mutex_create(void) {
  hal_mutex_impl_t *m = new (std::nothrow) hal_mutex_impl_t();
  HAL_ASSERT(m != NULL, "hal_mutex_create: allocation failed");
  if (!m) {
    return NULL;
  }

#if JH_STM32_HAL_SYNC_FREERTOS
  m->handle = xSemaphoreCreateMutex();
  if (m->handle == NULL) {
    delete m;
    HAL_ASSERT(false, "hal_mutex_create: FreeRTOS mutex allocation failed");
    return NULL;
  }
#else
  m->locked = 0u;
#endif
  return m;
}

void hal_mutex_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_lock: mutex is NULL");
  if (!mutex) {
    return;
  }

#if JH_STM32_HAL_SYNC_FREERTOS
  HAL_ASSERT(!stm32_in_isr(),
             "hal_mutex_lock: FreeRTOS mutex cannot be locked from ISR");
  if (stm32_in_isr()) {
    return;
  }

  const BaseType_t scheduler_state = xTaskGetSchedulerState();
  const TickType_t wait_ticks =
      (scheduler_state == taskSCHEDULER_RUNNING) ? portMAX_DELAY : 0u;
  const BaseType_t ok = xSemaphoreTake(mutex->handle, wait_ticks);
  HAL_ASSERT(ok == pdTRUE, "hal_mutex_lock: FreeRTOS mutex take failed");
  (void)ok;
#else
  while (__atomic_test_and_set(&mutex->locked, __ATOMIC_ACQUIRE)) {
    hal_sync_relax();
  }
#endif
}

bool hal_mutex_try_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_try_lock: mutex is NULL");
  if (!mutex) {
    return false;
  }

#if JH_STM32_HAL_SYNC_FREERTOS
  if (stm32_in_isr()) {
    return false;
  }
  return xSemaphoreTake(mutex->handle, 0u) == pdTRUE;
#else
  uint8_t expected = 0u;
  return __atomic_compare_exchange_n(&mutex->locked, &expected, 1u, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
#endif
}

void hal_mutex_unlock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_unlock: mutex is NULL");
  if (!mutex) {
    return;
  }

#if JH_STM32_HAL_SYNC_FREERTOS
  HAL_ASSERT(!stm32_in_isr(),
             "hal_mutex_unlock: FreeRTOS mutex cannot be unlocked from ISR");
  if (stm32_in_isr()) {
    return;
  }

  const BaseType_t ok = xSemaphoreGive(mutex->handle);
  HAL_ASSERT(ok == pdTRUE, "hal_mutex_unlock: FreeRTOS mutex give failed");
  (void)ok;
#else
  __atomic_clear(&mutex->locked, __ATOMIC_RELEASE);
#endif
}

void hal_mutex_destroy(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_destroy: mutex is NULL");
  if (!mutex) {
    return;
  }

#if JH_STM32_HAL_SYNC_FREERTOS
  vSemaphoreDelete(mutex->handle);
#endif
  delete mutex;
}

void hal_critical_section_enter(void) {
#if defined(__arm__) || defined(__thumb__)
  const uint32_t primask = stm32_read_primask();
  stm32_disable_irq();
  if (s_critical_depth == 0u) {
    s_saved_primask = primask;
  }
  ++s_critical_depth;
#endif
}

void hal_critical_section_exit(void) {
#if defined(__arm__) || defined(__thumb__)
  if (s_critical_depth == 0u) {
    return;
  }

  --s_critical_depth;
  if ((s_critical_depth == 0u) && ((s_saved_primask & 0x1u) == 0u)) {
    stm32_enable_irq();
  }
#endif
}

#endif // HAL_TARGET_IS_STM32G474
