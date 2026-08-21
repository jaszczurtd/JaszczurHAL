#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_I2C_SLAVE

#include "hal/core/hal_mutex_once.h"
#include "hal/i2c/hal_i2c_slave.h"
#include "hal/system/hal_sync.h"
#include "jh_esp32_gpio.h"

#include <driver/i2c_slave.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static_assert(HAL_I2C_SLAVE_REG_MAP_SIZE > 0u,
              "I2C slave register map cannot be empty");
static_assert(HAL_I2C_SLAVE_REG_MAP_SIZE <= 256u,
              "I2C slave register map uses an 8-bit address");

namespace {

constexpr uint8_t kBusCount = 2u;
constexpr size_t kTxSnapshotSize = HAL_I2C_SLAVE_REG_MAP_SIZE * 2u;
constexpr uint32_t kBufferDepth =
    HAL_I2C_SLAVE_REG_MAP_SIZE < 32u
        ? 128u
        : (uint32_t)HAL_I2C_SLAVE_REG_MAP_SIZE * 4u;
constexpr UBaseType_t kWorkerPriority = tskIDLE_PRIORITY + 2u;
constexpr uint32_t kWorkerStackBytes = 3072u;

enum event_bits_t : uint32_t {
  kEventResetTx = UINT32_C(1) << 0u,
  kEventTransmit = UINT32_C(1) << 1u,
  kEventShutdown = UINT32_C(1) << 2u,
};

struct slave_state_t {
  uint8_t registers[HAL_I2C_SLAVE_REG_MAP_SIZE];
  uint16_t register_pointer;
  uint8_t address;
  bool initialized;
  i2c_slave_dev_handle_t handle;
  SemaphoreHandle_t event_ready;
  SemaphoreHandle_t worker_stopped;
  TaskHandle_t worker;
  uint32_t pending_events;
  bool accepting_events;
  uint32_t transaction_count;
};

hal_mutex_t s_init_mutex;
portMUX_TYPE s_register_lock = portMUX_INITIALIZER_UNLOCKED;
slave_state_t s_slaves[kBusCount] = {};

uint8_t bus_index(uint8_t bus) {
  HAL_ASSERT(bus < kBusCount, "hal_i2c_slave: invalid bus index");
  return bus < kBusCount ? bus : 0u;
}

void IRAM_ATTR signal_from_isr(slave_state_t &state, uint32_t event,
                               BaseType_t *task_woken) {
  if (!__atomic_load_n(&state.accepting_events, __ATOMIC_ACQUIRE)) {
    return;
  }
  __atomic_fetch_or(&state.pending_events, event, __ATOMIC_RELEASE);
  if (state.event_ready != nullptr) {
    /* A full binary semaphore is harmless: pending_events retains every event
     * class until the worker atomically consumes it. */
    (void)xSemaphoreGiveFromISR(state.event_ready, task_woken);
  }
}

bool IRAM_ATTR receive_callback(i2c_slave_dev_handle_t,
                                const i2c_slave_rx_done_event_data_t *event,
                                void *argument) {
  slave_state_t &state = *static_cast<slave_state_t *>(argument);
  if (!__atomic_load_n(&state.accepting_events, __ATOMIC_ACQUIRE) ||
      event == nullptr || event->buffer == nullptr || event->length == 0u) {
    return false;
  }

  portENTER_CRITICAL_ISR(&s_register_lock);
  state.register_pointer = event->buffer[0];
  for (uint32_t index = 1u; index < event->length; ++index) {
    if (state.register_pointer < HAL_I2C_SLAVE_REG_MAP_SIZE) {
      state.registers[state.register_pointer++] = event->buffer[index];
    }
  }
  portEXIT_CRITICAL_ISR(&s_register_lock);
  __atomic_fetch_add(&state.transaction_count, 1u, __ATOMIC_RELAXED);

  BaseType_t task_woken = pdFALSE;
  signal_from_isr(state, kEventResetTx, &task_woken);
  return task_woken == pdTRUE;
}

bool IRAM_ATTR request_callback(i2c_slave_dev_handle_t,
                                const i2c_slave_request_event_data_t *,
                                void *argument) {
  slave_state_t &state = *static_cast<slave_state_t *>(argument);
  if (!__atomic_load_n(&state.accepting_events, __ATOMIC_ACQUIRE)) {
    return false;
  }
  __atomic_fetch_add(&state.transaction_count, 1u, __ATOMIC_RELAXED);
  BaseType_t task_woken = pdFALSE;
  signal_from_isr(state, kEventTransmit, &task_woken);
  return task_woken == pdTRUE;
}

void write_snapshot(slave_state_t &state) {
  uint8_t snapshot[kTxSnapshotSize] = {};
  portENTER_CRITICAL_SAFE(&s_register_lock);
  const uint16_t pointer = state.register_pointer;
  size_t output = 0u;
  for (uint16_t reg = pointer;
       reg < HAL_I2C_SLAVE_REG_MAP_SIZE && output < kTxSnapshotSize; ++reg) {
    snapshot[output++] = state.registers[reg];
  }
  state.register_pointer = HAL_I2C_SLAVE_REG_MAP_SIZE;
  portEXIT_CRITICAL_SAFE(&s_register_lock);

  uint32_t total_written = 0u;
  while (total_written < kTxSnapshotSize) {
    uint32_t written = 0u;
    const esp_err_t result = i2c_slave_write(
        state.handle, snapshot + total_written,
        (uint32_t)kTxSnapshotSize - total_written, &written, 20);
    if (result != ESP_OK || written == 0u) {
      break;
    }
    total_written += written;
  }
}

void worker_task(void *argument) {
  slave_state_t &state = *static_cast<slave_state_t *>(argument);
  for (;;) {
    if (xSemaphoreTake(state.event_ready, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    const uint32_t events =
        __atomic_exchange_n(&state.pending_events, 0u, __ATOMIC_ACQ_REL);
    if ((events & kEventShutdown) != 0u) {
      break;
    }
    if ((events & kEventResetTx) != 0u) {
      (void)i2c_slave_reset_tx_fifo(state.handle);
    }
    if ((events & kEventTransmit) != 0u) {
      write_snapshot(state);
    }
  }
  xSemaphoreGive(state.worker_stopped);
  vTaskDelete(nullptr);
}

void stop_worker(slave_state_t &state) {
  if (state.worker == nullptr || state.event_ready == nullptr) {
    return;
  }
  __atomic_fetch_or(&state.pending_events, kEventShutdown, __ATOMIC_RELEASE);
  /* When the binary semaphore is already full, the pending token still wakes
   * the worker and the shutdown bit remains set. Always wait for its ack. */
  (void)xSemaphoreGive(state.event_ready);
  (void)xSemaphoreTake(state.worker_stopped, portMAX_DELAY);
  state.worker = nullptr;
}

void release_slave(slave_state_t &state) {
  __atomic_store_n(&state.accepting_events, false, __ATOMIC_RELEASE);
  if (state.handle != nullptr) {
    const i2c_slave_event_callbacks_t callbacks = {};
    /* Keep valid callback context throughout deregistration. ESP-IDF updates
     * user_ctx before the callback pointers, so passing nullptr would let an
     * in-flight ISR invoke an old callback with a null argument. */
    (void)i2c_slave_register_event_callbacks(state.handle, &callbacks, &state);
  }
  stop_worker(state);
  if (state.handle != nullptr) {
    (void)i2c_del_slave_device(state.handle);
  }
  if (state.event_ready != nullptr) {
    vSemaphoreDelete(state.event_ready);
  }
  if (state.worker_stopped != nullptr) {
    vSemaphoreDelete(state.worker_stopped);
  }
  portENTER_CRITICAL_SAFE(&s_register_lock);
  memset(state.registers, 0, sizeof(state.registers));
  state.register_pointer = 0u;
  state.address = 0u;
  state.initialized = false;
  state.handle = nullptr;
  state.event_ready = nullptr;
  state.worker_stopped = nullptr;
  state.worker = nullptr;
  portEXIT_CRITICAL_SAFE(&s_register_lock);
  __atomic_store_n(&state.pending_events, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&state.transaction_count, 0u, __ATOMIC_RELEASE);
}

} // namespace

void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address) {
  hal_i2c_slave_init_bus(0u, sda_pin, scl_pin, address);
}

void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                            uint8_t address) {
  const uint8_t index = bus_index(bus);
  if (!jh_esp32_gpio_output_pin_valid(sda_pin) ||
      !jh_esp32_gpio_output_pin_valid(scl_pin) || sda_pin == scl_pin ||
      address > 0x7Fu) {
    HAL_ASSERT(false, "hal_i2c_slave_init: invalid pins or address");
    return;
  }
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_init_mutex);
  HAL_ASSERT(mutex != nullptr, "hal_i2c_slave_init: mutex allocation failed");
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  slave_state_t &state = s_slaves[index];
  release_slave(state);

  state.event_ready = xSemaphoreCreateBinary();
  state.worker_stopped = xSemaphoreCreateBinary();
  esp_err_t result =
      state.event_ready != nullptr && state.worker_stopped != nullptr
          ? ESP_OK
          : ESP_ERR_NO_MEM;
  if (result == ESP_OK &&
      xTaskCreate(worker_task, "jh_i2c_target", kWorkerStackBytes, &state,
                  kWorkerPriority, &state.worker) != pdPASS) {
    result = ESP_ERR_NO_MEM;
  }

  i2c_slave_config_t config = {};
  config.i2c_port = (i2c_port_num_t)index;
  config.sda_io_num = (gpio_num_t)sda_pin;
  config.scl_io_num = (gpio_num_t)scl_pin;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.send_buf_depth = kBufferDepth;
  config.receive_buf_depth = kBufferDepth;
  config.slave_addr = address;
  config.addr_bit_len = I2C_ADDR_BIT_LEN_7;
  config.flags.enable_internal_pullup = 1u;
  if (result == ESP_OK) {
    result = i2c_new_slave_device(&config, &state.handle);
  }

  i2c_slave_event_callbacks_t callbacks = {};
  callbacks.on_request = request_callback;
  callbacks.on_receive = receive_callback;
  if (result == ESP_OK) {
    portENTER_CRITICAL_SAFE(&s_register_lock);
    memset(state.registers, 0, sizeof(state.registers));
    state.register_pointer = 0u;
    state.address = address;
    portEXIT_CRITICAL_SAFE(&s_register_lock);
    __atomic_store_n(&state.pending_events, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&state.transaction_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&state.accepting_events, true, __ATOMIC_RELEASE);
    result =
        i2c_slave_register_event_callbacks(state.handle, &callbacks, &state);
  }
  if (result == ESP_OK) {
    portENTER_CRITICAL_SAFE(&s_register_lock);
    state.initialized = true;
    portEXIT_CRITICAL_SAFE(&s_register_lock);
  } else {
    release_slave(state);
  }
  hal_mutex_unlock(mutex);
  HAL_ASSERT(result == ESP_OK,
             "hal_i2c_slave_init: ESP-IDF target setup failed");
}

void hal_i2c_slave_deinit(void) { hal_i2c_slave_deinit_bus(0u); }

void hal_i2c_slave_deinit_bus(uint8_t bus) {
  const uint8_t index = bus_index(bus);
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_init_mutex);
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  release_slave(s_slaves[index]);
  hal_mutex_unlock(mutex);
}

void hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value) {
  hal_i2c_slave_reg_write8_bus(0u, reg, value);
}

void hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value) {
  slave_state_t &state = s_slaves[bus_index(bus)];
  if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return;
  }
  portENTER_CRITICAL_SAFE(&s_register_lock);
  state.registers[reg] = value;
  portEXIT_CRITICAL_SAFE(&s_register_lock);
}

void hal_i2c_slave_reg_write16(uint8_t reg, uint16_t value) {
  hal_i2c_slave_reg_write16_bus(0u, reg, value);
}

void hal_i2c_slave_reg_write16_bus(uint8_t bus, uint8_t reg, uint16_t value) {
  slave_state_t &state = s_slaves[bus_index(bus)];
  if ((uint16_t)reg + 1u >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return;
  }
  portENTER_CRITICAL_SAFE(&s_register_lock);
  state.registers[reg] = (uint8_t)(value >> 8u);
  state.registers[(uint16_t)reg + 1u] = (uint8_t)value;
  portEXIT_CRITICAL_SAFE(&s_register_lock);
}

uint8_t hal_i2c_slave_reg_read8(uint8_t reg) {
  return hal_i2c_slave_reg_read8_bus(0u, reg);
}

uint8_t hal_i2c_slave_reg_read8_bus(uint8_t bus, uint8_t reg) {
  slave_state_t &state = s_slaves[bus_index(bus)];
  if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return 0u;
  }
  portENTER_CRITICAL_SAFE(&s_register_lock);
  const uint8_t value = state.registers[reg];
  portEXIT_CRITICAL_SAFE(&s_register_lock);
  return value;
}

uint16_t hal_i2c_slave_reg_read16(uint8_t reg) {
  return hal_i2c_slave_reg_read16_bus(0u, reg);
}

uint16_t hal_i2c_slave_reg_read16_bus(uint8_t bus, uint8_t reg) {
  slave_state_t &state = s_slaves[bus_index(bus)];
  if ((uint16_t)reg + 1u >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return 0u;
  }
  portENTER_CRITICAL_SAFE(&s_register_lock);
  const uint16_t value = ((uint16_t)state.registers[reg] << 8u) |
                         state.registers[(uint16_t)reg + 1u];
  portEXIT_CRITICAL_SAFE(&s_register_lock);
  return value;
}

uint8_t hal_i2c_slave_get_address(void) {
  return hal_i2c_slave_get_address_bus(0u);
}

uint8_t hal_i2c_slave_get_address_bus(uint8_t bus) {
  slave_state_t &state = s_slaves[bus_index(bus)];
  portENTER_CRITICAL_SAFE(&s_register_lock);
  const uint8_t address = state.address;
  portEXIT_CRITICAL_SAFE(&s_register_lock);
  return address;
}

uint32_t hal_i2c_slave_get_transaction_count(void) {
  return hal_i2c_slave_get_transaction_count_bus(0u);
}

uint32_t hal_i2c_slave_get_transaction_count_bus(uint8_t bus) {
  return __atomic_load_n(&s_slaves[bus_index(bus)].transaction_count,
                         __ATOMIC_ACQUIRE);
}

#endif // HAL_ENABLE_I2C_SLAVE
#endif // HAL_TARGET_IS_ESP32_FAMILY
