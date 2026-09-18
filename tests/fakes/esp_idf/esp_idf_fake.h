#pragma once

/*
 * Test control for the host ESP-IDF fake.
 *
 * FreeRTOS tasks run on their own threads but never concurrently with the
 * test: a task runs only when the test hands it control through run_tasks()
 * (or blocks on a semaphore a task must give) and returns control as soon as
 * it blocks or ends. Driver calls are recorded in one ordered log, and every
 * fallible call can be made to fail.
 */

#include "driver/i2c_slave.h"
#include "esp_err.h"

#include <functional>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <vector>

namespace fake_idf {

struct AssertFailure : std::runtime_error {
  explicit AssertFailure(const char *message)
      : std::runtime_error(message != nullptr ? message : "") {}
};

/* Drop all fake state. Every task must have ended. */
void reset();

/* Ordered log of driver and kernel calls, e.g. "i2c_slave_reset_tx_fifo". */
const std::vector<std::string> &calls();
void clear_calls();
size_t count_calls(const std::string &name);
/* Index of the first call with this name at or after start, or -1. */
long find_call(const std::string &name, size_t start = 0u);

/* Make the next `times` calls of a named function return `error`. */
void fail_next(const std::string &function, esp_err_t error,
               unsigned times = 1u);

/* Run every task that can make progress until all are blocked or ended. */
void run_tasks();
size_t live_tasks();
/* Rule violations seen by the fake: blocking inside a critical section,
 * deleting a semaphore a task waits on, and similar. */
const std::vector<std::string> &violations();

/* I2C target: the driver side of one device. */
bool i2c_device_exists();
/* Simulate the ISR callbacks as ESP-IDF invokes them. */
bool i2c_isr_receive(const std::vector<uint8_t> &bytes);
bool i2c_isr_request();
/* Limit how many bytes the next i2c_slave_write() accepts. */
void i2c_accept_next_write(uint32_t bytes);
/* Run an action (typically an ISR) while the next i2c_slave_write() is busy. */
void i2c_during_next_write(std::function<void()> action);
/* Everything handed to i2c_slave_write() since the last TX FIFO reset. */
const std::vector<uint8_t> &i2c_tx_fifo();
/* True if a deregistration left the old ISR callbacks with a null context. */
bool i2c_null_context_window();

/* LEDC channel as the hardware sees it. */
struct LedcChannel {
  bool configured;
  bool running;
  uint32_t idle_level;
  uint32_t duty;
  int gpio;
};
LedcChannel ledc_channel(int channel);

/* RMT objects that were created and not yet deleted. */
size_t rmt_live_channels();
size_t rmt_live_encoders();

/* Fault handlers: which core runs the code, and per-core installs. */
void set_core(int core);
size_t exception_handlers_installed(int core);

} // namespace fake_idf
