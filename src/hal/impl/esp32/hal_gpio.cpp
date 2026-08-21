#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/system/hal_sync.h"
#include "jh_esp32_gpio.h"
#include "jh_esp32_status.h"

#include <driver/gpio.h>
#include <esp_intr_alloc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/soc_caps.h>

#include <stddef.h>
#include <stdint.h>

namespace {

void (*s_callbacks[SOC_GPIO_PIN_COUNT])(void) = {};
bool s_irq_attached[SOC_GPIO_PIN_COUNT] = {};
uint8_t s_irq_owner[SOC_GPIO_PIN_COUNT] = {};
gpio_int_type_t s_irq_type[SOC_GPIO_PIN_COUNT] = {};
hal_mutex_t s_irq_mutex;
uint8_t s_service_owner_core = HAL_GPIO_IRQ_CORE_NONE;
size_t s_attached_count = 0u;
hal_irq_priority_t s_irq_priority = HAL_IRQ_PRIORITY_DEFAULT;

bool gpio_mode_valid(hal_gpio_mode_t mode) {
  return mode >= HAL_GPIO_INPUT && mode <= HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH;
}

bool gpio_mode_is_output(hal_gpio_mode_t mode) {
  return mode == HAL_GPIO_OUTPUT || mode == HAL_GPIO_OUTPUT_LOW ||
         mode == HAL_GPIO_OUTPUT_HIGH || mode == HAL_GPIO_OUTPUT_OPEN_DRAIN ||
         mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW ||
         mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH;
}

bool gpio_irq_mode_valid(hal_gpio_irq_mode_t mode) {
  return mode >= HAL_GPIO_IRQ_FALLING && mode <= HAL_GPIO_IRQ_CHANGE;
}

gpio_int_type_t gpio_irq_type(hal_gpio_irq_mode_t mode) {
  switch (mode) {
  case HAL_GPIO_IRQ_FALLING:
    return GPIO_INTR_NEGEDGE;
  case HAL_GPIO_IRQ_RISING:
    return GPIO_INTR_POSEDGE;
  case HAL_GPIO_IRQ_CHANGE:
  default:
    return GPIO_INTR_ANYEDGE;
  }
}

uint8_t current_core(void) { return (uint8_t)xPortGetCoreID(); }

int irq_allocation_flags(hal_irq_priority_t priority) {
  switch (priority) {
  case HAL_IRQ_PRIORITY_HIGHEST:
    return ESP_INTR_FLAG_LEVEL3;
  case HAL_IRQ_PRIORITY_HIGH:
    return ESP_INTR_FLAG_LEVEL2;
  case HAL_IRQ_PRIORITY_LOW:
  case HAL_IRQ_PRIORITY_DEFAULT:
  default:
    return ESP_INTR_FLAG_LEVEL1;
  }
}

hal_mutex_t irq_mutex(void) { return jh_hal_mutex_create_once(&s_irq_mutex); }

void gpio_irq_dispatch(void *argument) {
  const uintptr_t raw_pin = (uintptr_t)argument;
  if (raw_pin >= SOC_GPIO_PIN_COUNT) {
    return;
  }
  void (*callback)(void) =
      __atomic_load_n(&s_callbacks[raw_pin], __ATOMIC_ACQUIRE);
  if (callback != nullptr) {
    callback();
  }
}

esp_err_t install_irq_service(uint8_t owner_core, hal_irq_priority_t priority) {
  if (current_core() != owner_core) {
    return ESP_ERR_INVALID_STATE;
  }
  const esp_err_t result =
      gpio_install_isr_service(irq_allocation_flags(priority));
  if (result == ESP_OK) {
    s_service_owner_core = owner_core;
  }
  return result;
}

esp_err_t add_attached_handlers(void) {
  for (uint8_t pin = 0u; pin < SOC_GPIO_PIN_COUNT; ++pin) {
    if (!s_irq_attached[pin]) {
      continue;
    }
    const esp_err_t result = gpio_isr_handler_add(
        (gpio_num_t)pin, gpio_irq_dispatch, (void *)(uintptr_t)pin);
    if (result != ESP_OK) {
      return result;
    }
  }
  return ESP_OK;
}

} // namespace

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
  if (!jh_esp32_gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: inaccessible or invalid board pin");
    return;
  }
  if (!gpio_mode_valid(mode)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid mode");
    return;
  }
  if (gpio_mode_is_output(mode) && !jh_esp32_gpio_output_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: pin is input-only");
    return;
  }

  gpio_mode_t idf_mode = GPIO_MODE_INPUT;
  gpio_pullup_t pull_up = GPIO_PULLUP_DISABLE;
  gpio_pulldown_t pull_down = GPIO_PULLDOWN_DISABLE;
  bool initial_level = false;
  bool set_initial_level = false;

  switch (mode) {
  case HAL_GPIO_OUTPUT:
  case HAL_GPIO_OUTPUT_LOW:
    idf_mode = GPIO_MODE_INPUT_OUTPUT;
    set_initial_level = true;
    break;
  case HAL_GPIO_OUTPUT_HIGH:
    idf_mode = GPIO_MODE_INPUT_OUTPUT;
    initial_level = true;
    set_initial_level = true;
    break;
  case HAL_GPIO_INPUT_PULLUP:
    pull_up = GPIO_PULLUP_ENABLE;
    break;
  case HAL_GPIO_INPUT_PULLDOWN:
    pull_down = GPIO_PULLDOWN_ENABLE;
    break;
  case HAL_GPIO_OUTPUT_OPEN_DRAIN:
  case HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH:
    idf_mode = GPIO_MODE_INPUT_OUTPUT_OD;
    initial_level = true;
    set_initial_level = true;
    break;
  case HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW:
    idf_mode = GPIO_MODE_INPUT_OUTPUT_OD;
    set_initial_level = true;
    break;
  case HAL_GPIO_INPUT:
  default:
    break;
  }

  if (set_initial_level) {
    const esp_err_t result =
        gpio_set_level((gpio_num_t)pin, initial_level ? 1u : 0u);
    HAL_ASSERT(result == ESP_OK,
               "hal_gpio_set_mode: initial output level failed");
    if (result != ESP_OK) {
      return;
    }
  }

  hal_mutex_t mutex = irq_mutex();
  HAL_ASSERT(mutex != nullptr, "hal_gpio_set_mode: mutex allocation failed");
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  const gpio_config_t config = {
      .pin_bit_mask = UINT64_C(1) << pin,
      .mode = idf_mode,
      .pull_up_en = pull_up,
      .pull_down_en = pull_down,
      .intr_type = s_irq_attached[pin] ? s_irq_type[pin] : GPIO_INTR_DISABLE,
  };
  const esp_err_t result = gpio_config(&config);
  hal_mutex_unlock(mutex);
  HAL_ASSERT(result == ESP_OK, "hal_gpio_set_mode: ESP-IDF config failed");
}

void hal_gpio_write(uint8_t pin, bool high) {
  if (!jh_esp32_gpio_output_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_write: inaccessible or invalid output pin");
    return;
  }
  const esp_err_t result = gpio_set_level((gpio_num_t)pin, high ? 1u : 0u);
  HAL_ASSERT(result == ESP_OK, "hal_gpio_write: ESP-IDF write failed");
  (void)result;
}

bool hal_gpio_read(uint8_t pin) {
  if (!jh_esp32_gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_read: inaccessible or invalid board pin");
    return false;
  }
  return gpio_get_level((gpio_num_t)pin) != 0;
}

hal_status_t hal_gpio_attach_interrupt_ex(uint8_t pin, void (*callback)(void),
                                          hal_gpio_irq_mode_t mode,
                                          uint8_t owner_core) {
  if (!jh_esp32_gpio_pin_valid(pin) || callback == nullptr ||
      !gpio_irq_mode_valid(mode) || owner_core >= HAL_TARGET_CPU_CORES) {
    return HAL_EINVAL;
  }
  if (xPortInIsrContext() != pdFALSE) {
    return HAL_ESTATE;
  }
  if (current_core() != owner_core) {
    return HAL_ESTATE;
  }

  hal_mutex_t mutex = irq_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);

  if (s_irq_attached[pin] && s_irq_owner[pin] != owner_core) {
    hal_mutex_unlock(mutex);
    return HAL_ESTATE;
  }
  if (s_service_owner_core != HAL_GPIO_IRQ_CORE_NONE &&
      s_service_owner_core != owner_core) {
    hal_mutex_unlock(mutex);
    return HAL_ESTATE;
  }
  bool installed_service = false;
  if (s_service_owner_core == HAL_GPIO_IRQ_CORE_NONE) {
    const esp_err_t install_result =
        install_irq_service(owner_core, s_irq_priority);
    if (install_result != ESP_OK) {
      hal_mutex_unlock(mutex);
      return install_result == ESP_ERR_INVALID_STATE
                 ? HAL_EBUSY
                 : jh_esp32_status_from_esp_err(install_result);
    }
    installed_service = true;
  }

  const bool reconfigure = s_irq_attached[pin];
  void (*const previous_callback)(void) =
      reconfigure ? __atomic_load_n(&s_callbacks[pin], __ATOMIC_ACQUIRE)
                  : nullptr;
  const gpio_int_type_t previous_type = s_irq_type[pin];
  const gpio_int_type_t type = gpio_irq_type(mode);
  esp_err_t result = gpio_intr_disable((gpio_num_t)pin);
  if (result == ESP_OK && reconfigure) {
    /* IDF's handler table is one entry per pin. Remove the old entry before
     * changing the trigger so same-owner reconfiguration never depends on
     * replacement behaviour internal to a particular IDF release. */
    result = gpio_isr_handler_remove((gpio_num_t)pin);
  }
  if (result == ESP_OK) {
    result = gpio_set_intr_type((gpio_num_t)pin, type);
  }
  if (result == ESP_OK) {
    __atomic_store_n(&s_callbacks[pin], callback, __ATOMIC_RELEASE);
    result = gpio_isr_handler_add((gpio_num_t)pin, gpio_irq_dispatch,
                                  (void *)(uintptr_t)pin);
  }
  if (result != ESP_OK) {
    const esp_err_t operation_result = result;
    esp_err_t restore_result = ESP_OK;
    if (reconfigure) {
      restore_result = gpio_set_intr_type((gpio_num_t)pin, previous_type);
      if (restore_result == ESP_OK) {
        __atomic_store_n(&s_callbacks[pin], previous_callback,
                         __ATOMIC_RELEASE);
        restore_result = gpio_isr_handler_add(
            (gpio_num_t)pin, gpio_irq_dispatch, (void *)(uintptr_t)pin);
      }
      if (restore_result != ESP_OK) {
        (void)gpio_isr_handler_remove((gpio_num_t)pin);
        __atomic_store_n(&s_callbacks[pin], nullptr, __ATOMIC_RELEASE);
        s_irq_attached[pin] = false;
        s_irq_owner[pin] = HAL_GPIO_IRQ_CORE_NONE;
        s_irq_type[pin] = GPIO_INTR_DISABLE;
        HAL_ASSERT(s_attached_count > 0u,
                   "hal_gpio_attach_interrupt_ex: invalid IRQ count");
        if (s_attached_count > 0u) {
          --s_attached_count;
        }
      }
    } else {
      __atomic_store_n(&s_callbacks[pin], nullptr, __ATOMIC_RELEASE);
    }

    if (installed_service || s_attached_count == 0u) {
      const esp_err_t uninstall_result = gpio_uninstall_isr_service();
      if (uninstall_result == ESP_OK) {
        s_service_owner_core = HAL_GPIO_IRQ_CORE_NONE;
      } else if (restore_result == ESP_OK) {
        restore_result = uninstall_result;
      }
    }
    hal_mutex_unlock(mutex);
    return jh_esp32_status_from_esp_err(
        restore_result == ESP_OK ? operation_result : restore_result);
  }

  if (!s_irq_attached[pin]) {
    ++s_attached_count;
  }
  s_irq_attached[pin] = true;
  s_irq_owner[pin] = owner_core;
  s_irq_type[pin] = type;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void),
                               hal_gpio_irq_mode_t mode) {
  const hal_status_t status =
      hal_gpio_attach_interrupt_ex(pin, callback, mode, current_core());
  HAL_ASSERT(status == HAL_OK, "hal_gpio_attach_interrupt: attach failed");
}

hal_status_t hal_gpio_detach_interrupt_ex(uint8_t pin) {
  if (!jh_esp32_gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (xPortInIsrContext() != pdFALSE) {
    return HAL_ESTATE;
  }

  hal_mutex_t mutex = irq_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_irq_attached[pin]) {
    hal_mutex_unlock(mutex);
    return HAL_ENOENT;
  }
  if (s_irq_owner[pin] != current_core()) {
    hal_mutex_unlock(mutex);
    return HAL_ESTATE;
  }

  const esp_err_t remove_result = gpio_isr_handler_remove((gpio_num_t)pin);
  if (remove_result != ESP_OK) {
    hal_mutex_unlock(mutex);
    return jh_esp32_status_from_esp_err(remove_result);
  }

  __atomic_store_n(&s_callbacks[pin], (void (*)(void)) nullptr,
                   __ATOMIC_RELEASE);
  s_irq_attached[pin] = false;
  s_irq_owner[pin] = HAL_GPIO_IRQ_CORE_NONE;
  s_irq_type[pin] = GPIO_INTR_DISABLE;
  --s_attached_count;

  if (s_attached_count == 0u) {
    const esp_err_t uninstall_result = gpio_uninstall_isr_service();
    if (uninstall_result != ESP_OK) {
      hal_mutex_unlock(mutex);
      return jh_esp32_status_from_esp_err(uninstall_result);
    }
    s_service_owner_core = HAL_GPIO_IRQ_CORE_NONE;
  }
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

void hal_gpio_detach_interrupt(uint8_t pin) {
  const hal_status_t status = hal_gpio_detach_interrupt_ex(pin);
  HAL_ASSERT(status == HAL_OK || status == HAL_ENOENT,
             "hal_gpio_detach_interrupt: detach failed");
}

hal_status_t hal_gpio_get_interrupt_owner_ex(uint8_t pin,
                                             uint8_t *out_owner_core) {
  if (out_owner_core == nullptr) {
    return HAL_EINVAL;
  }
  *out_owner_core = HAL_GPIO_IRQ_CORE_NONE;
  if (!jh_esp32_gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (xPortInIsrContext() != pdFALSE) {
    return HAL_ESTATE;
  }

  hal_mutex_t mutex = irq_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_irq_attached[pin]) {
    hal_mutex_unlock(mutex);
    return HAL_ENOENT;
  }
  *out_owner_core = s_irq_owner[pin];
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
  if (priority > HAL_IRQ_PRIORITY_LOW) {
    priority = HAL_IRQ_PRIORITY_DEFAULT;
  }
  if (xPortInIsrContext() != pdFALSE) {
    HAL_ASSERT(false, "hal_gpio_set_irq_priority: called from ISR");
    return;
  }

  hal_mutex_t mutex = irq_mutex();
  HAL_ASSERT(mutex != nullptr,
             "hal_gpio_set_irq_priority: mutex allocation failed");
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (priority == s_irq_priority) {
    hal_mutex_unlock(mutex);
    return;
  }
  if (s_service_owner_core == HAL_GPIO_IRQ_CORE_NONE) {
    s_irq_priority = priority;
    hal_mutex_unlock(mutex);
    return;
  }
  if (current_core() != s_service_owner_core) {
    hal_mutex_unlock(mutex);
    HAL_ASSERT(false,
               "hal_gpio_set_irq_priority: caller does not own ISR service");
    return;
  }

  for (uint8_t pin = 0u; pin < SOC_GPIO_PIN_COUNT; ++pin) {
    if (s_irq_attached[pin]) {
      const esp_err_t result = gpio_isr_handler_remove((gpio_num_t)pin);
      if (result != ESP_OK) {
        hal_mutex_unlock(mutex);
        HAL_ASSERT(false, "hal_gpio_set_irq_priority: handler removal failed");
        return;
      }
    }
  }

  const uint8_t owner_core = s_service_owner_core;
  const hal_irq_priority_t previous_priority = s_irq_priority;
  (void)gpio_uninstall_isr_service();
  s_service_owner_core = HAL_GPIO_IRQ_CORE_NONE;

  esp_err_t result = install_irq_service(owner_core, priority);
  if (result == ESP_OK) {
    result = add_attached_handlers();
  }
  if (result == ESP_OK) {
    s_irq_priority = priority;
    hal_mutex_unlock(mutex);
    return;
  }

  (void)gpio_uninstall_isr_service();
  s_service_owner_core = HAL_GPIO_IRQ_CORE_NONE;
  const esp_err_t restore_install =
      install_irq_service(owner_core, previous_priority);
  const esp_err_t restore_handlers =
      restore_install == ESP_OK ? add_attached_handlers() : restore_install;
  hal_mutex_unlock(mutex);
  HAL_ASSERT(restore_handlers == ESP_OK,
             "hal_gpio_set_irq_priority: ISR service restore failed");
  HAL_ASSERT(false, "hal_gpio_set_irq_priority: priority update failed");
}

#endif // HAL_TARGET_IS_ESP32_FAMILY
