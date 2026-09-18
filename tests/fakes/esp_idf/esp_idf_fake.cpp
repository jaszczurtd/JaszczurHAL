#include "esp_idf_fake.h"

#include "driver/i2c_slave.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "esp_clk_tree.h"
#include "esp_ipc.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/core/hal_assert.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"
#include "soc/soc_caps.h"
#include "xtensa_api.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

struct fake_idf_semaphore {
  bool full;
  bool deleted;
};

struct fake_idf_task {
  TaskFunction_t function;
  void *argument;
  enum { kReady, kBlocked, kRunning, kDone } state;
  fake_idf_semaphore *waiting;
  bool go;
  std::thread thread;
};

struct fake_i2c_slave_dev {
  int unused;
};
struct fake_rmt_channel {
  bool enabled;
};
struct fake_rmt_encoder {
  int unused;
};
struct hal_mutex_impl_t {
  std::mutex mutex;
};

namespace fake_idf {
namespace {

struct TaskExit {};

/* Never destroyed: a task abandoned by a failing test may still wait on them
 * when the process exits. */
std::mutex &g_lock = *new std::mutex();
std::condition_variable &g_cv = *new std::condition_variable();
std::vector<std::unique_ptr<fake_idf_task>> g_tasks;
std::vector<std::unique_ptr<fake_idf_semaphore>> g_semaphores;
fake_idf_task *g_running = nullptr;
thread_local fake_idf_task *t_self = nullptr;

std::vector<std::string> g_calls;
std::vector<std::string> g_violations;
std::map<std::string, std::deque<esp_err_t>> g_failures;
int g_critical_depth = 0;

struct I2cDevice {
  bool exists;
  fake_i2c_slave_dev handle;
  i2c_slave_event_callbacks_t callbacks;
  void *context;
  std::vector<uint8_t> fifo;
  uint32_t accept_limit;
  bool null_context_window;
} g_i2c = {};
std::function<void()> g_i2c_during_write;

LedcChannel g_ledc[SOC_LEDC_CHANNEL_NUM] = {};
std::vector<std::unique_ptr<fake_rmt_channel>> g_rmt_channels;
std::vector<std::unique_ptr<fake_rmt_encoder>> g_rmt_encoders;

int g_core = 0;
std::map<int, std::map<int, xt_exc_handler>> g_handlers;
std::map<int, size_t> g_handler_installs;

void log_call(const std::string &name) { g_calls.push_back(name); }

void violation(const std::string &what) { g_violations.push_back(what); }

bool injected(const std::string &name, esp_err_t *error) {
  auto found = g_failures.find(name);
  if (found == g_failures.end() || found->second.empty()) {
    return false;
  }
  *error = found->second.front();
  found->second.pop_front();
  return true;
}

/* A driver call that may block must not run inside a critical section. */
esp_err_t driver_call(const std::string &name) {
  log_call(name);
  if (g_critical_depth != 0) {
    violation(name + " called inside a critical section");
  }
  esp_err_t error = ESP_OK;
  return injected(name, &error) ? error : ESP_OK;
}

void resume_locked(std::unique_lock<std::mutex> &lock, fake_idf_task *task) {
  task->state = fake_idf_task::kRunning;
  g_running = task;
  task->go = true;
  g_cv.notify_all();
  g_cv.wait(lock, [] { return g_running == nullptr; });
}

void task_main(fake_idf_task *task) {
  t_self = task;
  {
    std::unique_lock<std::mutex> lock(g_lock);
    g_cv.wait(lock, [task] { return task->go; });
    task->go = false;
  }
  bool deleted = false;
  try {
    task->function(task->argument);
  } catch (const TaskExit &) {
    deleted = true;
  }
  std::lock_guard<std::mutex> lock(g_lock);
  if (!deleted) {
    violation("task function returned without vTaskDelete");
  }
  task->state = fake_idf_task::kDone;
  g_running = nullptr;
  g_cv.notify_all();
}

} // namespace

void reset() {
  {
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto &task : g_tasks) {
      if (task->state != fake_idf_task::kDone) {
        violation("task still alive at reset");
        task->thread.detach();
        (void)task.release();
      } else if (task->thread.joinable()) {
        task->thread.join();
      }
    }
  }
  g_tasks.clear();
  g_semaphores.clear();
  g_calls.clear();
  g_violations.clear();
  g_failures.clear();
  g_critical_depth = 0;
  g_i2c = {};
  g_i2c_during_write = nullptr;
  for (LedcChannel &channel : g_ledc) {
    channel = {};
  }
  g_rmt_channels.clear();
  g_rmt_encoders.clear();
  g_core = 0;
  g_handlers.clear();
  g_handler_installs.clear();
}

const std::vector<std::string> &calls() { return g_calls; }
void clear_calls() { g_calls.clear(); }

size_t count_calls(const std::string &name) {
  return (size_t)std::count(g_calls.begin(), g_calls.end(), name);
}

long find_call(const std::string &name, size_t start) {
  for (size_t index = start; index < g_calls.size(); ++index) {
    if (g_calls[index] == name) {
      return (long)index;
    }
  }
  return -1;
}

void fail_next(const std::string &function, esp_err_t error, unsigned times) {
  for (unsigned index = 0u; index < times; ++index) {
    g_failures[function].push_back(error);
  }
}

void run_tasks() {
  std::unique_lock<std::mutex> lock(g_lock);
  for (unsigned guard = 0u;; ++guard) {
    if (guard > 100000u) {
      violation("tasks never settled");
      return;
    }
    fake_idf_task *next = nullptr;
    for (auto &task : g_tasks) {
      const bool ready = task->state == fake_idf_task::kReady;
      const bool woken = task->state == fake_idf_task::kBlocked &&
                         task->waiting != nullptr && task->waiting->full;
      if (ready || woken) {
        next = task.get();
        break;
      }
    }
    if (next == nullptr) {
      return;
    }
    resume_locked(lock, next);
  }
}

size_t live_tasks() {
  std::lock_guard<std::mutex> lock(g_lock);
  size_t live = 0u;
  for (const auto &task : g_tasks) {
    live += task->state != fake_idf_task::kDone ? 1u : 0u;
  }
  return live;
}

const std::vector<std::string> &violations() { return g_violations; }

bool i2c_device_exists() { return g_i2c.exists; }

bool i2c_isr_receive(const std::vector<uint8_t> &bytes) {
  if (g_i2c.callbacks.on_receive == nullptr) {
    return false;
  }
  std::vector<uint8_t> buffer = bytes;
  i2c_slave_rx_done_event_data_t event = {};
  event.buffer = buffer.data();
  event.length = (uint32_t)buffer.size();
  return g_i2c.callbacks.on_receive(&g_i2c.handle, &event, g_i2c.context);
}

bool i2c_isr_request() {
  if (g_i2c.callbacks.on_request == nullptr) {
    return false;
  }
  const i2c_slave_request_event_data_t event = {};
  return g_i2c.callbacks.on_request(&g_i2c.handle, &event, g_i2c.context);
}

void i2c_accept_next_write(uint32_t bytes) { g_i2c.accept_limit = bytes + 1u; }

void i2c_during_next_write(std::function<void()> action) {
  g_i2c_during_write = std::move(action);
}

const std::vector<uint8_t> &i2c_tx_fifo() { return g_i2c.fifo; }

bool i2c_null_context_window() { return g_i2c.null_context_window; }

LedcChannel ledc_channel(int channel) {
  return channel >= 0 && channel < SOC_LEDC_CHANNEL_NUM ? g_ledc[channel]
                                                        : LedcChannel{};
}

size_t rmt_live_channels() { return g_rmt_channels.size(); }
size_t rmt_live_encoders() { return g_rmt_encoders.size(); }

void set_core(int core) { g_core = core; }

size_t exception_handlers_installed(int core) {
  return g_handler_installs[core];
}

} // namespace fake_idf

using namespace fake_idf;

/* ---- FreeRTOS ----------------------------------------------------------- */

void fake_idf_critical_enter(portMUX_TYPE *) { ++g_critical_depth; }

void fake_idf_critical_exit(portMUX_TYPE *) {
  if (g_critical_depth == 0) {
    violation("critical section exit without enter");
    return;
  }
  --g_critical_depth;
}

BaseType_t xPortGetCoreID(void) { return g_core; }

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
  log_call("xSemaphoreCreateBinary");
  esp_err_t error = ESP_OK;
  if (injected("xSemaphoreCreateBinary", &error)) {
    return nullptr;
  }
  g_semaphores.push_back(std::make_unique<fake_idf_semaphore>());
  return g_semaphores.back().get();
}

static BaseType_t give(SemaphoreHandle_t semaphore, BaseType_t *woken) {
  std::lock_guard<std::mutex> lock(g_lock);
  if (semaphore == nullptr || semaphore->deleted) {
    violation("give on a missing semaphore");
    return pdFAIL;
  }
  if (woken != nullptr) {
    for (const auto &task : g_tasks) {
      if (task->state == fake_idf_task::kBlocked &&
          task->waiting == semaphore) {
        *woken = pdTRUE;
      }
    }
  }
  if (semaphore->full) {
    return pdFAIL;
  }
  semaphore->full = true;
  return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  return give(semaphore, nullptr);
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore,
                                 BaseType_t *higher_priority_task_woken) {
  return give(semaphore, higher_priority_task_woken);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
  if (ticks != 0u && g_critical_depth != 0) {
    violation("blocking take inside a critical section");
  }
  fake_idf_task *self = t_self;
  if (self != nullptr) {
    std::unique_lock<std::mutex> lock(g_lock);
    if (semaphore == nullptr || semaphore->deleted) {
      violation("take on a missing semaphore");
      return pdFALSE;
    }
    if (!semaphore->full) {
      if (ticks == 0u) {
        return pdFALSE;
      }
      self->state = fake_idf_task::kBlocked;
      self->waiting = semaphore;
      g_running = nullptr;
      g_cv.notify_all();
      g_cv.wait(lock, [self] { return self->go; });
      self->go = false;
      self->waiting = nullptr;
    }
    semaphore->full = false;
    return pdTRUE;
  }

  for (int attempt = 0; attempt < 2; ++attempt) {
    {
      std::lock_guard<std::mutex> lock(g_lock);
      if (semaphore == nullptr || semaphore->deleted) {
        violation("take on a missing semaphore");
        return pdFALSE;
      }
      if (semaphore->full) {
        semaphore->full = false;
        return pdTRUE;
      }
    }
    if (ticks == 0u) {
      return pdFALSE;
    }
    run_tasks();
  }
  if (ticks == portMAX_DELAY) {
    violation("caller would block forever");
    throw std::runtime_error("fake FreeRTOS deadlock");
  }
  return pdFALSE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
  log_call("vSemaphoreDelete");
  std::lock_guard<std::mutex> lock(g_lock);
  if (semaphore == nullptr || semaphore->deleted) {
    violation("delete of a missing semaphore");
    return;
  }
  for (const auto &task : g_tasks) {
    if (task->state == fake_idf_task::kBlocked && task->waiting == semaphore) {
      violation("semaphore deleted while a task waits on it");
    }
  }
  semaphore->deleted = true;
}

BaseType_t xTaskCreate(TaskFunction_t function, const char *, uint32_t,
                       void *parameters, UBaseType_t,
                       TaskHandle_t *created_task) {
  log_call("xTaskCreate");
  esp_err_t error = ESP_OK;
  if (injected("xTaskCreate", &error)) {
    return pdFAIL;
  }
  auto task = std::make_unique<fake_idf_task>();
  task->function = function;
  task->argument = parameters;
  task->state = fake_idf_task::kReady;
  task->waiting = nullptr;
  task->go = false;
  fake_idf_task *raw = task.get();
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_tasks.push_back(std::move(task));
  }
  raw->thread = std::thread(task_main, raw);
  if (created_task != nullptr) {
    *created_task = raw;
  }
  return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
  log_call("vTaskDelete");
  if (task != nullptr && task != t_self) {
    violation("deleting another task is not modelled");
    return;
  }
  throw TaskExit{};
}

/* ---- I2C target ---------------------------------------------------------- */

esp_err_t i2c_new_slave_device(const i2c_slave_config_t *,
                               i2c_slave_dev_handle_t *ret_handle) {
  const esp_err_t result = driver_call("i2c_new_slave_device");
  if (result != ESP_OK) {
    return result;
  }
  g_i2c.exists = true;
  g_i2c.fifo.clear();
  *ret_handle = &g_i2c.handle;
  return ESP_OK;
}

esp_err_t i2c_del_slave_device(i2c_slave_dev_handle_t handle) {
  const esp_err_t result = driver_call("i2c_del_slave_device");
  if (handle != &g_i2c.handle || !g_i2c.exists) {
    violation("delete of a missing I2C device");
  }
  /* ESP-IDF frees the device even when final cleanup reports an error. */
  g_i2c.exists = false;
  g_i2c.callbacks = {};
  g_i2c.context = nullptr;
  return result;
}

esp_err_t
i2c_slave_register_event_callbacks(i2c_slave_dev_handle_t handle,
                                   const i2c_slave_event_callbacks_t *cbs,
                                   void *user_data) {
  const esp_err_t result = driver_call("i2c_slave_register_event_callbacks");
  if (result != ESP_OK) {
    return result;
  }
  if (handle != &g_i2c.handle || !g_i2c.exists) {
    violation("callbacks registered on a missing I2C device");
  }
  const i2c_slave_event_callbacks_t previous = g_i2c.callbacks;
  const bool clearing =
      cbs->on_request == nullptr && cbs->on_receive == nullptr;
  if (clearing &&
      (previous.on_request != nullptr || previous.on_receive != nullptr)) {
    /* ESP-IDF stores user_data before the callback pointers, so an ISR in
     * that window runs an old callback with the new context. */
    if (user_data == nullptr) {
      g_i2c.null_context_window = true;
    } else if (previous.on_request != nullptr) {
      const i2c_slave_request_event_data_t event = {};
      (void)previous.on_request(handle, &event, user_data);
    }
  }
  g_i2c.context = user_data;
  g_i2c.callbacks = *cbs;
  return ESP_OK;
}

esp_err_t i2c_slave_write(i2c_slave_dev_handle_t, const uint8_t *data,
                          uint32_t len, uint32_t *write_len, int) {
  const esp_err_t result = driver_call("i2c_slave_write");
  if (result != ESP_OK) {
    return result;
  }
  uint32_t accepted = len;
  if (g_i2c.accept_limit != 0u) {
    accepted = std::min(len, g_i2c.accept_limit - 1u);
    g_i2c.accept_limit = 0u;
  }
  g_i2c.fifo.insert(g_i2c.fifo.end(), data, data + accepted);
  *write_len = accepted;
  if (g_i2c_during_write) {
    std::function<void()> action = std::move(g_i2c_during_write);
    g_i2c_during_write = nullptr;
    action();
  }
  return ESP_OK;
}

esp_err_t i2c_slave_reset_tx_fifo(i2c_slave_dev_handle_t) {
  const esp_err_t result = driver_call("i2c_slave_reset_tx_fifo");
  if (result == ESP_OK) {
    g_i2c.fifo.clear();
  }
  return result;
}

/* ---- LEDC ---------------------------------------------------------------- */

uint32_t ledc_find_suitable_duty_resolution(uint32_t src_clk_freq,
                                            uint32_t timer_freq) {
  uint32_t divider = src_clk_freq / timer_freq;
  uint32_t bits = 0u;
  while (divider > 1u) {
    divider >>= 1u;
    ++bits;
  }
  return bits;
}

esp_err_t ledc_timer_config(const ledc_timer_config_t *) {
  return driver_call("ledc_timer_config");
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *config) {
  const esp_err_t result = driver_call(
      config->deconfigure ? "ledc_channel_deconfigure" : "ledc_channel_config");
  if (result != ESP_OK) {
    return result;
  }
  LedcChannel &channel = g_ledc[config->channel];
  if (config->deconfigure) {
    channel = {};
    return ESP_OK;
  }
  channel.configured = true;
  channel.running = true;
  channel.duty = config->duty;
  channel.gpio = config->gpio_num;
  return ESP_OK;
}

esp_err_t ledc_stop(ledc_mode_t, ledc_channel_t channel, uint32_t idle_level) {
  const esp_err_t result = driver_call("ledc_stop");
  if (result == ESP_OK) {
    g_ledc[channel].running = false;
    g_ledc[channel].idle_level = idle_level;
  }
  return result;
}

esp_err_t ledc_set_duty_and_update(ledc_mode_t, ledc_channel_t channel,
                                   uint32_t duty, uint32_t) {
  const esp_err_t result = driver_call("ledc_set_duty_and_update");
  if (result == ESP_OK) {
    g_ledc[channel].duty = duty;
    g_ledc[channel].running = true;
  }
  return result;
}

esp_err_t esp_clk_tree_src_get_freq_hz(soc_module_clk_t,
                                       esp_clk_tree_src_freq_precision_t,
                                       uint32_t *freq_value) {
  *freq_value = 80000000u;
  return ESP_OK;
}

/* ---- RMT ----------------------------------------------------------------- */

template <typename T>
static bool remove_object(std::vector<std::unique_ptr<T>> &objects, T *object) {
  for (auto it = objects.begin(); it != objects.end(); ++it) {
    if (it->get() == object) {
      objects.erase(it);
      return true;
    }
  }
  return false;
}

template <typename T>
static bool live_object(const std::vector<std::unique_ptr<T>> &objects,
                        const T *object) {
  for (const auto &item : objects) {
    if (item.get() == object) {
      return true;
    }
  }
  return false;
}

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *,
                             rmt_channel_handle_t *ret_chan) {
  const esp_err_t result = driver_call("rmt_new_tx_channel");
  if (result == ESP_OK) {
    g_rmt_channels.push_back(std::make_unique<fake_rmt_channel>());
    *ret_chan = g_rmt_channels.back().get();
  }
  return result;
}

esp_err_t rmt_del_channel(rmt_channel_handle_t channel) {
  const esp_err_t result = driver_call("rmt_del_channel");
  if (!live_object(g_rmt_channels, channel)) {
    violation("delete of a missing RMT channel");
    return ESP_ERR_INVALID_ARG;
  }
  if (result == ESP_OK) {
    (void)remove_object(g_rmt_channels, channel);
  }
  return result;
}

esp_err_t rmt_enable(rmt_channel_handle_t channel) {
  const esp_err_t result = driver_call("rmt_enable");
  if (result == ESP_OK) {
    channel->enabled = true;
  }
  return result;
}

esp_err_t rmt_disable(rmt_channel_handle_t channel) {
  const esp_err_t result = driver_call("rmt_disable");
  if (!live_object(g_rmt_channels, channel)) {
    violation("disable of a missing RMT channel");
    return ESP_ERR_INVALID_ARG;
  }
  if (result != ESP_OK) {
    return result;
  }
  if (!channel->enabled) {
    return ESP_ERR_INVALID_STATE;
  }
  channel->enabled = false;
  return ESP_OK;
}

esp_err_t rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t *,
                                rmt_encoder_handle_t *ret_encoder) {
  const esp_err_t result = driver_call("rmt_new_bytes_encoder");
  if (result == ESP_OK) {
    g_rmt_encoders.push_back(std::make_unique<fake_rmt_encoder>());
    *ret_encoder = g_rmt_encoders.back().get();
  }
  return result;
}

esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) {
  const esp_err_t result = driver_call("rmt_del_encoder");
  if (!live_object(g_rmt_encoders, encoder)) {
    violation("delete of a missing RMT encoder");
    return ESP_ERR_INVALID_ARG;
  }
  if (result == ESP_OK) {
    (void)remove_object(g_rmt_encoders, encoder);
  }
  return result;
}

esp_err_t rmt_transmit(rmt_channel_handle_t channel,
                       rmt_encoder_handle_t encoder, const void *, size_t,
                       const rmt_transmit_config_t *) {
  if (!live_object(g_rmt_channels, channel) ||
      !live_object(g_rmt_encoders, encoder)) {
    violation("transmit through a missing RMT object");
  }
  return driver_call("rmt_transmit");
}

esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int) {
  return driver_call("rmt_tx_wait_all_done");
}

void esp_rom_delay_us(uint32_t) { log_call("esp_rom_delay_us"); }

/* ---- Fault handlers ------------------------------------------------------ */

esp_err_t esp_ipc_call_blocking(uint32_t cpu_id, esp_ipc_func_t func,
                                void *arg) {
  const esp_err_t result = driver_call("esp_ipc_call_blocking");
  if (result != ESP_OK) {
    return result;
  }
  const int caller = g_core;
  g_core = (int)cpu_id;
  func(arg);
  g_core = caller;
  return ESP_OK;
}

xt_exc_handler xt_set_exception_handler(int n, xt_exc_handler f) {
  xt_exc_handler previous = g_handlers[g_core][n];
  g_handlers[g_core][n] = f;
  ++g_handler_installs[g_core];
  return previous;
}

extern "C" void xt_unhandled_exception(XtExcFrame *) {
  violation("unhandled exception reached");
}

/* ---- HAL services the backends link against ------------------------------ */

extern "C" {

hal_mutex_t hal_mutex_create(void) { return new hal_mutex_impl_t(); }
hal_mutex_t jh_hal_mutex_try_create(void) { return hal_mutex_create(); }
void hal_mutex_lock(hal_mutex_t mutex) { mutex->mutex.lock(); }
bool hal_mutex_try_lock(hal_mutex_t mutex) { return mutex->mutex.try_lock(); }
void hal_mutex_unlock(hal_mutex_t mutex) { mutex->mutex.unlock(); }
void hal_mutex_destroy(hal_mutex_t mutex) { delete mutex; }
void hal_critical_section_enter(void) {}
void hal_critical_section_exit(void) {}

void hal_assert_fail(const char *msg) { throw AssertFailure(msg); }

} // extern "C"
