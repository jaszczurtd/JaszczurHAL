#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/analog/hal_adc.h"
#include "hal/core/hal_app.h"
#include "hal/core/hal_status.h"
#include "hal/core/hal_target.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/i2c/hal_i2c.h"
#include "hal/serial/hal_serial.h"
#include "hal/serial/hal_uart.h"
#include "hal/spi/hal_spi.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "hal/timers/hal_timer.h"
#include "jh_board_config.h"
#include "jh_link_contract.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !HAL_TARGET_IS_ESP32_S3
#error "The ESP32-S3 Phase 2 probe requires the exact esp32s3 target"
#endif

#if !HAL_BOARD_IS_WAVESHARE_ESP32_S3_ZERO
#error "The ESP32-S3 Phase 2 probe requires waveshare-esp32-s3-zero"
#endif

namespace {

constexpr uint8_t kGpioSmokePin = 18u;
constexpr uint8_t kGpioInputPin = 0u;
constexpr uint8_t kAdcSmokePin = 3u;
constexpr uint8_t kUartLoopPin = 17u;
constexpr uint8_t kI2cSdaPin = 8u;
constexpr uint8_t kI2cSclPin = 9u;
constexpr uint8_t kSpiMisoPin = 10u;
constexpr uint8_t kSpiMosiPin = 11u;
constexpr uint8_t kSpiClockPin = 12u;
constexpr uint32_t kReportPeriodMs = 500u;

struct Phase2Results {
  bool system;
  bool sync;
  bool gpio;
  bool adc;
  bool uart;
  bool i2c;
  bool spi;
  bool timer_setup;
  bool stack_guard;
  uint32_t heap_free;
  int32_t temperature_centi_celsius;
  int adc_low;
  int adc_high;
  size_t i2c_found;
};

Phase2Results s_results = {};
hal_mutex_t s_shared_mutex = nullptr;
hal_timer_pool_t s_timer_pool = nullptr;
hal_timer_t s_timer = nullptr;
volatile uint32_t s_gpio_irq_count = 0u;
volatile bool s_gpio_irq_was_isr = false;
volatile uint32_t s_timer_count = 0u;
volatile bool s_timer_was_isr = false;
volatile uint32_t s_task0_count = 0u;
volatile uint32_t s_task1_count = 0u;
volatile uint32_t s_shared_count = 0u;
volatile int32_t s_task0_core = -1;
volatile int32_t s_task1_core = -1;
volatile bool s_serial_ping = false;
volatile bool s_timer_destroyed = false;
uint32_t s_last_report_ms = 0u;
char s_serial_line[16] = {};
size_t s_serial_line_length = 0u;

void gpio_smoke_isr(void) {
  __atomic_store_n(&s_gpio_irq_was_isr, hal_in_isr(), __ATOMIC_RELEASE);
  (void)__atomic_add_fetch(&s_gpio_irq_count, 1u, __ATOMIC_ACQ_REL);
}

void timer_smoke_callback(hal_timer_t, void *) {
  __atomic_store_n(&s_timer_was_isr, hal_in_isr(), __ATOMIC_RELEASE);
  (void)__atomic_add_fetch(&s_timer_count, 1u, __ATOMIC_ACQ_REL);
}

bool test_system(void) {
  hal_system_architecture_t architecture = {};
  char uid[HAL_DEVICE_UID_HEX_BUF_SIZE] = {};
  float temperature = 0.0f;
  hal_fault_info_t fault = {};

  const uint64_t before_us = hal_micros64();
  hal_delay_ms(2u);
  const uint64_t after_us = hal_micros64();
  const hal_status_t architecture_status =
      hal_system_get_current_architecture(&architecture);
  const hal_status_t uid_status = hal_get_device_uid_hex_ex(uid, sizeof(uid));
  const hal_status_t temperature_status = hal_read_chip_temp_ex(&temperature);

  s_results.heap_free = hal_get_free_heap();
  s_results.temperature_centi_celsius =
      static_cast<int32_t>(temperature * 100.0f);

  return architecture_status == HAL_OK &&
         strcmp(architecture.target_name, HAL_TARGET_DESCRIPTOR_ID) == 0 &&
         strcmp(architecture.backend_name, HAL_TARGET_BACKEND_NAME) == 0 &&
         architecture.cpu_cores == HAL_TARGET_CPU_CORES &&
         architecture.is_hardware && architecture.has_rtos &&
         architecture.flash_total_bytes == HAL_BOARD_EXPECTED_FLASH_BYTES &&
         architecture.ram_total_bytes >= HAL_TARGET_RAM_TOTAL_BYTES &&
         architecture.heap_free_bytes > 0u && after_us > before_us &&
         after_us - before_us >= 1000u && uid_status == HAL_OK &&
         strlen(uid) == HAL_DEVICE_UID_BYTES * 2u &&
         temperature_status == HAL_OK && temperature > -40.0f &&
         temperature < 125.0f && !hal_in_isr() &&
         hal_get_last_fault_ex(&fault) == HAL_ENOENT;
}

bool test_sync(void) {
  s_shared_mutex = hal_mutex_create();
  if (s_shared_mutex == nullptr) {
    return false;
  }

  hal_mutex_lock(s_shared_mutex);
  const bool rejected_while_locked = !hal_mutex_try_lock(s_shared_mutex);
  hal_mutex_unlock(s_shared_mutex);
  const bool acquired_after_unlock = hal_mutex_try_lock(s_shared_mutex);
  if (acquired_after_unlock) {
    hal_mutex_unlock(s_shared_mutex);
  }

  hal_critical_section_enter();
  const bool task_context = !hal_in_isr();
  hal_critical_section_exit();
  return rejected_while_locked && acquired_after_unlock && task_context;
}

bool test_gpio(void) {
  const uint8_t owner = static_cast<uint8_t>(xPortGetCoreID());
  hal_gpio_set_mode(kGpioInputPin, HAL_GPIO_INPUT_PULLUP);
  hal_delay_ms(2u);
  const bool pulled_input_high = hal_gpio_read(kGpioInputPin);
  hal_gpio_set_mode(kGpioSmokePin, HAL_GPIO_OUTPUT_LOW);
  if (hal_gpio_read(kGpioSmokePin)) {
    return false;
  }

  hal_status_t status = hal_gpio_attach_interrupt_ex(
      kGpioSmokePin, gpio_smoke_isr, HAL_GPIO_IRQ_RISING, owner);
  uint8_t reported_owner = HAL_GPIO_IRQ_CORE_NONE;
  if (status != HAL_OK ||
      hal_gpio_get_interrupt_owner_ex(kGpioSmokePin, &reported_owner) !=
          HAL_OK ||
      reported_owner != owner) {
    return false;
  }

  /* Exercise same-owner IRQ reconfiguration before producing the edge. */
  status = hal_gpio_attach_interrupt_ex(kGpioSmokePin, gpio_smoke_isr,
                                        HAL_GPIO_IRQ_CHANGE, owner);
  hal_gpio_write(kGpioSmokePin, true);
  hal_delay_ms(5u);
  const bool level_high = hal_gpio_read(kGpioSmokePin);
  hal_gpio_write(kGpioSmokePin, false);
  hal_delay_ms(5u);
  const hal_status_t detach_status =
      hal_gpio_detach_interrupt_ex(kGpioSmokePin);
  const hal_status_t owner_after_detach =
      hal_gpio_get_interrupt_owner_ex(kGpioSmokePin, &reported_owner);

  return pulled_input_high && status == HAL_OK && level_high &&
         detach_status == HAL_OK && owner_after_detach == HAL_ENOENT &&
         reported_owner == HAL_GPIO_IRQ_CORE_NONE &&
         __atomic_load_n(&s_gpio_irq_count, __ATOMIC_ACQUIRE) >= 2u &&
         __atomic_load_n(&s_gpio_irq_was_isr, __ATOMIC_ACQUIRE);
}

bool test_adc(void) {
  hal_adc_set_resolution(12u);
  hal_gpio_set_mode(kAdcSmokePin, HAL_GPIO_INPUT_PULLDOWN);
  hal_delay_ms(5u);
  s_results.adc_low = hal_adc_read(kAdcSmokePin);
  hal_gpio_set_mode(kAdcSmokePin, HAL_GPIO_INPUT_PULLUP);
  hal_delay_ms(5u);
  s_results.adc_high = hal_adc_read(kAdcSmokePin);
  hal_gpio_set_mode(kAdcSmokePin, HAL_GPIO_INPUT);

  return s_results.adc_low >= 0 && s_results.adc_low <= 4095 &&
         s_results.adc_high >= 0 && s_results.adc_high <= 4095 &&
         s_results.adc_high > s_results.adc_low + 256;
}

bool test_uart(void) {
  static const uint8_t kPattern[] = {'J', 'H', 'U', '2'};
  uint8_t received[sizeof(kPattern)] = {};
  hal_uart_t uart =
      hal_uart_create(HAL_UART_PORT_1, kUartLoopPin, kUartLoopPin);
  if (uart == nullptr ||
      hal_uart_begin(uart, 115200u, HAL_UART_CFG_8N1) != HAL_OK) {
    if (uart != nullptr) {
      hal_uart_destroy(uart);
    }
    return false;
  }

  size_t written = 0u;
  bool ok =
      hal_uart_write_ex(uart, kPattern, sizeof(kPattern), &written) == HAL_OK &&
      written == sizeof(kPattern) && hal_uart_flush(uart) == HAL_OK;
  const uint32_t deadline = hal_millis() + 250u;
  while (ok && hal_uart_available(uart) < static_cast<int>(sizeof(kPattern)) &&
         static_cast<int32_t>(hal_millis() - deadline) < 0) {
    hal_delay_ms(1u);
  }
  for (size_t index = 0u; ok && index < sizeof(received); ++index) {
    ok = hal_uart_read_ex(uart, &received[index]) == HAL_OK;
  }
  hal_uart_error_counters_t errors = {};
  ok = ok && memcmp(received, kPattern, sizeof(kPattern)) == 0 &&
       hal_uart_get_error_counters_ex(uart, &errors) == HAL_OK &&
       errors.rx_overrun == 0u && errors.rx_framing == 0u &&
       errors.rx_parity == 0u && errors.rx_break == 0u &&
       errors.rx_buffer_overflow == 0u;
  hal_uart_destroy(uart);
  return ok;
}

bool test_i2c(void) {
  const hal_status_t clear_status = hal_i2c_bus_clear(kI2cSdaPin, kI2cSclPin);
  const hal_status_t init_status =
      hal_i2c_init(kI2cSdaPin, kI2cSclPin, HAL_I2C_CLOCK_STANDARD_HZ);
  uint8_t addresses[16] = {};
  size_t found = 0u;
  const hal_status_t scan_status =
      init_status == HAL_OK ? hal_i2c_scan(addresses, sizeof(addresses), &found,
                                           hal_watchdog_feed)
                            : init_status;
  const uint32_t transaction_count = hal_i2c_get_transaction_count();
  hal_i2c_deinit();
  s_results.i2c_found = found;
  return clear_status == HAL_OK && init_status == HAL_OK &&
         scan_status == HAL_OK && transaction_count > 0u;
}

bool test_spi(void) {
  const hal_spi_settings_t settings = {
      .clock_hz = 2000000u,
      .bit_order = HAL_SPI_MSBFIRST,
      .data_mode = HAL_SPI_MODE0,
  };
  static const uint8_t kTx[] = {0xA5u, 0x5Au, 0xC3u, 0x3Cu};
  uint8_t rx[sizeof(kTx)] = {};
  uint8_t received = 0u;

  bool ok = hal_spi_init(0u, kSpiMisoPin, kSpiMosiPin, kSpiClockPin) == HAL_OK;
  if (!ok) {
    return false;
  }
  hal_spi_lock(0u);
  ok = hal_spi_begin_transaction(0u, &settings) == HAL_OK &&
       hal_spi_transfer_ex(0u, 0xA5u, &received) == HAL_OK &&
       hal_spi_transfer_txrx(0u, kTx, rx, sizeof(kTx)) == HAL_OK &&
       hal_spi_write_dma_ex(0u, kTx, sizeof(kTx)) == HAL_OK &&
       hal_spi_write_dma_async_start_ex(0u, kTx, sizeof(kTx)) == HAL_OK &&
       !hal_spi_write_dma_async_busy(0u) &&
       hal_spi_write_dma_async_wait_ex(0u) == HAL_OK &&
       hal_spi_end_transaction(0u) == HAL_OK;
  hal_spi_unlock(0u);
  hal_spi_deinit(0u);
  return ok;
}

bool start_timer_test(void) {
  s_timer_pool = hal_timer_pool_create_auto(1u);
  if (s_timer_pool == nullptr) {
    return false;
  }
  if (hal_timer_create(s_timer_pool, 20000u, true, timer_smoke_callback,
                       nullptr, &s_timer) != HAL_TIMER_OK) {
    hal_timer_pool_destroy(s_timer_pool);
    s_timer_pool = nullptr;
    return false;
  }
  if (hal_timer_start(s_timer) != HAL_TIMER_OK ||
      hal_timer_pause(s_timer) != HAL_TIMER_OK) {
    return false;
  }
  int64_t remaining_us = 0;
  const bool paused =
      hal_timer_get_state(s_timer) == HAL_TIMER_STATE_PAUSED &&
      hal_timer_get_remaining_us(s_timer, &remaining_us) == HAL_TIMER_OK &&
      remaining_us > 0;
  hal_delay_ms(30u);
  const bool stayed_paused =
      __atomic_load_n(&s_timer_count, __ATOMIC_ACQUIRE) == 0u;
  return paused && stayed_paused && hal_timer_resume(s_timer) == HAL_TIMER_OK;
}

bool test_stack_guard_contract(void) {
  const hal_status_t status = hal_stack_guard_init_ex();
  hal_stack_guard_check();
  return status == HAL_OK && hal_stack_guard_init();
}

void process_debug_serial(void) {
  while (hal_serial_available() > 0) {
    const int value = hal_serial_read();
    if (value < 0) {
      break;
    }
    if (value == '\n' || value == '\r') {
      if (s_serial_line_length > 0u) {
        s_serial_line[s_serial_line_length] = '\0';
        if (strcmp(s_serial_line, "PING") == 0) {
          __atomic_store_n(&s_serial_ping, true, __ATOMIC_RELEASE);
        }
        s_serial_line_length = 0u;
      }
      continue;
    }
    if (s_serial_line_length + 1u < sizeof(s_serial_line)) {
      s_serial_line[s_serial_line_length++] = static_cast<char>(value);
    } else {
      s_serial_line_length = 0u;
    }
  }
}

void increment_shared_counter(void) {
  if (s_shared_mutex == nullptr) {
    return;
  }
  hal_mutex_lock(s_shared_mutex);
  (void)__atomic_add_fetch(&s_shared_count, 1u, __ATOMIC_ACQ_REL);
  hal_mutex_unlock(s_shared_mutex);
}

bool timer_test_complete(void) {
  const bool complete =
      s_results.timer_setup &&
      __atomic_load_n(&s_timer_count, __ATOMIC_ACQUIRE) >= 3u &&
      __atomic_load_n(&s_timer_was_isr, __ATOMIC_ACQUIRE);
  if (complete &&
      !__atomic_exchange_n(&s_timer_destroyed, true, __ATOMIC_ACQ_REL)) {
    if (hal_timer_stop(s_timer) != HAL_TIMER_OK ||
        hal_timer_destroy(s_timer) != HAL_TIMER_OK) {
      return false;
    }
    s_timer = nullptr;
    hal_timer_pool_destroy(s_timer_pool);
    s_timer_pool = nullptr;
  }
  return complete;
}

void report_phase2(void) {
  const uint32_t sequence = __atomic_load_n(&s_task0_count, __ATOMIC_ACQUIRE);
  const uint32_t task1_count =
      __atomic_load_n(&s_task1_count, __ATOMIC_ACQUIRE);
  const uint32_t timer_count =
      __atomic_load_n(&s_timer_count, __ATOMIC_ACQUIRE);
  const uint32_t irq_count =
      __atomic_load_n(&s_gpio_irq_count, __ATOMIC_ACQUIRE);
  const bool serial_ping = __atomic_load_n(&s_serial_ping, __ATOMIC_ACQUIRE);
  const bool timer = timer_test_complete();
  const bool tasks = __atomic_load_n(&s_task0_core, __ATOMIC_ACQUIRE) ==
                         HAL_FREERTOS_TASK0_CORE &&
                     __atomic_load_n(&s_task1_core, __ATOMIC_ACQUIRE) ==
                         HAL_FREERTOS_TASK1_CORE &&
                     task1_count > 0u;
  const bool pass = s_results.system && s_results.sync && s_results.gpio &&
                    s_results.adc && s_results.uart && s_results.i2c &&
                    s_results.spi && timer && s_results.stack_guard && tasks &&
                    serial_ping;

  char line[768] = {};
  (void)snprintf(line, sizeof(line),
                 "JH_ESP32_PHASE2 sequence=%" PRIu32
                 " target=%s board=%s core0=%" PRId32 " core1=%" PRId32
                 " task1=%" PRIu32 " system=%u sync=%u gpio=%u irq=%" PRIu32
                 " irq_isr=%u adc=%u adc_low=%d adc_high=%d uart=%u i2c=%u"
                 " i2c_found=%u spi=%u timer=%u timer_count=%" PRIu32
                 " timer_isr=%u serial_rx=%u stack_guard=%u heap=%" PRIu32
                 " temp_centi=%" PRId32 " status=%s",
                 sequence, HAL_TARGET_DESCRIPTOR_ID, HAL_BOARD_PROFILE_NAME,
                 __atomic_load_n(&s_task0_core, __ATOMIC_ACQUIRE),
                 __atomic_load_n(&s_task1_core, __ATOMIC_ACQUIRE), task1_count,
                 s_results.system ? 1u : 0u, s_results.sync ? 1u : 0u,
                 s_results.gpio ? 1u : 0u, irq_count,
                 __atomic_load_n(&s_gpio_irq_was_isr, __ATOMIC_ACQUIRE) ? 1u
                                                                        : 0u,
                 s_results.adc ? 1u : 0u, s_results.adc_low, s_results.adc_high,
                 s_results.uart ? 1u : 0u, s_results.i2c ? 1u : 0u,
                 static_cast<unsigned int>(s_results.i2c_found),
                 s_results.spi ? 1u : 0u, timer ? 1u : 0u, timer_count,
                 __atomic_load_n(&s_timer_was_isr, __ATOMIC_ACQUIRE) ? 1u : 0u,
                 serial_ping ? 1u : 0u, s_results.stack_guard ? 1u : 0u,
                 s_results.heap_free, s_results.temperature_centi_celsius,
                 pass ? "PASS" : "FAIL");
  hal_serial_println(line);
}

} // namespace

extern "C" void app_start(void) {
  JH_BOARD_CONTRACT_SYMBOL();
  hal_serial_begin(115200u);
  hal_serial_set_flush(true);
  hal_fault_subsystem_init();

  s_results.system = test_system();
  s_results.sync = test_sync();
  s_results.gpio = test_gpio();
  s_results.adc = test_adc();
  s_results.uart = test_uart();
  s_results.i2c = test_i2c();
  s_results.spi = test_spi();
  s_results.timer_setup = start_timer_test();
  s_results.stack_guard = test_stack_guard_contract();

  /* Enable only after bounded startup probes; both tasks feed it thereafter. */
  s_results.system =
      s_results.system && hal_watchdog_enable(30000u, false) == HAL_OK;
}

extern "C" void app_task0(void) {
  __atomic_store_n(&s_task0_core, xPortGetCoreID(), __ATOMIC_RELEASE);
  (void)__atomic_add_fetch(&s_task0_count, 1u, __ATOMIC_ACQ_REL);
  increment_shared_counter();
  process_debug_serial();
  hal_watchdog_feed();
  hal_alive_mark();

  const uint32_t now = hal_millis();
  if (hal_millis_interval_elapsed(now, &s_last_report_ms, kReportPeriodMs)) {
    report_phase2();
  }
  hal_delay_ms(2u);
}

extern "C" void app_task1(void) {
  __atomic_store_n(&s_task1_core, xPortGetCoreID(), __ATOMIC_RELEASE);
  (void)__atomic_add_fetch(&s_task1_count, 1u, __ATOMIC_ACQ_REL);
  increment_shared_counter();
  hal_watchdog_feed();
  hal_delay_ms(2u);
}
