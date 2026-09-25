#include <hal/analog/hal_adc_scan.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_compiler.h>
#include <hal/core/hal_config.h>
#include <hal/core/hal_status.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/impl/rp2040/drivers/flash/rp_flash_runtime.h>
#include <hal/impl/rp2040/drivers/flash/rp_flash_transaction.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>
#include <hal/system/hal_system.h>
#include <hal/usb/hal_usb.h>

#include <hardware/dma.h>
#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <pico/flash.h>
#include <pico/multicore.h>
#include <pico/platform.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COMMAND_STATUS ((uint8_t)'S')
#define COMMAND_RUN ((uint8_t)'T')
#define COMMAND_LOAD ((uint8_t)'L')
#define FLASH_TIMEOUT_MS 1000u
/* Raw-sector probe: the sector right below the storage reservations, which
 * the KV load probe owns through hal_kv. */
#define FLASH_TEST_OFFSET                                                      \
  ((uint32_t)PICO_FLASH_SIZE_BYTES - (uint32_t)HAL_RP_FLASH_EEPROM_SIZE -      \
   (uint32_t)HAL_RP_FLASH_LITTLEFS_SIZE - FLASH_SECTOR_SIZE)
/* KV load probe: published writes alternating between the cores while a DMA
 * ring runs and the host keeps CDC busy in both directions. */
#define KV_LOAD_WRITES 16u
#define KV_LOAD_KEY 0x4C4Fu
#define KV_LOAD_CORE1_TIMEOUT_US 3000000u
/* ADC scan on the core that writes: three inputs, a block every ~1.5 ms, so
 * a flash transaction spans many blocks. Guard words follow the ring buffer. */
#define SCAN_PINS 3u
#define SCAN_BLOCK_FRAMES 256u
#define SCAN_CONVERSION_NS 2000u
#define SCAN_GUARD_WORDS 8u
#define SCAN_GUARD_PATTERN 0x47554152u

typedef enum {
  FLASH_ACTION_ERASE_PROGRAM = 0,
  FLASH_ACTION_ERASE_CANCEL,
  FLASH_ACTION_PROGRAM
} flash_action_t;

typedef struct {
  flash_action_t action;
  uint32_t offset;
  const uint8_t *page;
} flash_operation_context_t;

static uint8_t s_response[256];
static size_t s_response_length;
static size_t s_response_offset;
static uint8_t s_page[FLASH_PAGE_SIZE];
static volatile uint32_t s_task0_ticks;
static volatile uint32_t s_task1_ticks;
static volatile uint8_t s_task0_core = 0xffu;
static volatile uint8_t s_task1_core = 0xffu;
static volatile bool s_core1_request;
static volatile hal_status_t s_core1_status = HAL_NONE;
static uint32_t s_dma_source = 0x12345678u;
static uint32_t s_dma_sink;
static bool s_led_state;
static bool s_kv_ready;
static volatile bool s_core1_kv_request;
static volatile uint32_t s_core1_kv_value;
static volatile hal_status_t s_core1_kv_status = HAL_NONE;
static volatile uint8_t s_core1_scan_request; /* 1 start, 2 stop */
static volatile hal_status_t s_core1_scan_status = HAL_NONE;
static struct {
  uint16_t buffer[2u * SCAN_BLOCK_FRAMES * SCAN_PINS];
  uint32_t guard[SCAN_GUARD_WORDS];
} s_scan __attribute__((aligned(4)));

extern uint8_t __flash_binary_end;

static hal_status_t
__no_inline_not_in_flash_func(noop_operation)(void *context) {
  uint32_t *counter = (uint32_t *)context;
  if (counter != NULL) {
    ++(*counter);
  }
  return HAL_OK;
}

static void __no_inline_not_in_flash_func(raw_noop_operation)(void *context) {
  uint32_t *counter = (uint32_t *)context;
  if (counter != NULL) {
    ++(*counter);
  }
}

static hal_status_t
__no_inline_not_in_flash_func(recursive_operation)(void *context) {
  return jh_rp_flash_transaction_execute(noop_operation, context,
                                         FLASH_TIMEOUT_MS);
}

static hal_status_t __attribute__((noinline))
flash_resident_operation(void *context) {
  (void)context;
  return HAL_OK;
}

static hal_status_t
__no_inline_not_in_flash_func(flash_operation)(void *raw_context) {
  flash_operation_context_t *context = (flash_operation_context_t *)raw_context;
  if (context == NULL || context->page == NULL) {
    return HAL_EINVAL;
  }

  if (context->action == FLASH_ACTION_ERASE_PROGRAM ||
      context->action == FLASH_ACTION_ERASE_CANCEL) {
    flash_range_erase(context->offset, FLASH_SECTOR_SIZE);
  }
  if (context->action == FLASH_ACTION_ERASE_CANCEL) {
    return HAL_ECANCELED;
  }
  flash_range_program(context->offset, context->page, FLASH_PAGE_SIZE);
  return HAL_OK;
}

static bool page_matches(uint32_t offset, const uint8_t *expected) {
  const uint8_t *flash = (const uint8_t *)((uintptr_t)XIP_BASE + offset);
  return memcmp(flash, expected, FLASH_PAGE_SIZE) == 0;
}

static bool page_is_erased(uint32_t offset) {
  const uint8_t *flash = (const uint8_t *)((uintptr_t)XIP_BASE + offset);
  for (size_t index = 0u; index < FLASH_PAGE_SIZE; ++index) {
    if (flash[index] != 0xffu) {
      return false;
    }
  }
  return true;
}

typedef struct {
  int channel;
  int timer;
} paced_dma_t;

/* One 32-bit transfer per DMA timer tick from read to write, count words. */
static hal_status_t start_paced_dma(paced_dma_t *dma, const volatile void *read,
                                    volatile void *write, uint32_t count) {
  dma->channel = dma_claim_unused_channel(false);
  dma->timer = dma_claim_unused_timer(false);
  if (dma->channel < 0 || dma->timer < 0) {
    if (dma->channel >= 0) {
      dma_channel_unclaim((uint)dma->channel);
    }
    if (dma->timer >= 0) {
      dma_timer_unclaim((uint)dma->timer);
    }
    return HAL_ENOMEM;
  }

  dma_timer_set_fraction((uint)dma->timer, 1u, 0xffffu);
  dma_channel_config config =
      dma_channel_get_default_config((uint)dma->channel);
  channel_config_set_read_increment(&config, false);
  channel_config_set_write_increment(&config, false);
  channel_config_set_dreq(&config, dma_get_timer_dreq((uint)dma->timer));
  dma_channel_configure((uint)dma->channel, &config, write, read, count, true);

  while (!dma_channel_is_busy((uint)dma->channel)) {
    tight_loop_contents();
  }
  return HAL_OK;
}

static void stop_paced_dma(paced_dma_t *dma) {
  dma_channel_abort((uint)dma->channel);
  dma_channel_unclaim((uint)dma->channel);
  dma_timer_unclaim((uint)dma->timer);
}

/* A busy RAM-to-RAM channel must not block the transaction. */
static hal_status_t run_dma_ram_probe(void) {
  paced_dma_t dma;
  hal_status_t status =
      start_paced_dma(&dma, &s_dma_source, &s_dma_sink, 0x100000u);
  if (status != HAL_OK) {
    return status;
  }
  uint32_t counter = 0u;
  status = jh_rp_flash_transaction_execute(noop_operation, &counter,
                                           FLASH_TIMEOUT_MS);
  stop_paced_dma(&dma);
  return status;
}

/* A busy channel reading the XIP window must be refused. */
static hal_status_t run_dma_xip_probe(void) {
  paced_dma_t dma;
  const volatile void *flash =
      (const volatile void *)((uintptr_t)XIP_BASE + FLASH_TEST_OFFSET);
  hal_status_t status = start_paced_dma(&dma, flash, &s_dma_sink, 0x100000u);
  if (status != HAL_OK) {
    return status;
  }
  uint32_t counter = 0u;
  status = jh_rp_flash_transaction_execute(noop_operation, &counter,
                                           FLASH_TIMEOUT_MS);
  stop_paced_dma(&dma);
  return status;
}

static hal_status_t run_core1_transaction(void) {
  HAL_ATOMIC_STORE(&s_core1_status, HAL_NONE, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&s_core1_request, true, HAL_ATOMIC_RELEASE);
  const uint64_t start_us = hal_micros64();
  while (HAL_ATOMIC_LOAD(&s_core1_status, HAL_ATOMIC_ACQUIRE) == HAL_NONE) {
    if (hal_micros64() - start_us >= 1000000u) {
      return HAL_ETIMEOUT;
    }
    hal_delay_ms(1u);
  }
  return HAL_ATOMIC_LOAD(&s_core1_status, HAL_ATOMIC_ACQUIRE);
}

static hal_status_t run_usb_quiesce_probe(void) {
  bool mutex_held = false;
  hal_status_t status = jh_rp_usb_flash_quiesce(FLASH_TIMEOUT_MS, &mutex_held);
  const hal_status_t resume_status = jh_rp_usb_flash_resume(mutex_held);
  if (status == HAL_OK) {
    status = resume_status;
  }
  return status;
}

static hal_status_t run_raw_lockout_probe(void) {
  uint32_t counter = 0u;
  const int status =
      flash_safe_execute(raw_noop_operation, &counter, FLASH_TIMEOUT_MS);
  if (status != PICO_OK) {
    return status == PICO_ERROR_TIMEOUT ? HAL_ETIMEOUT : HAL_EHW;
  }
  return counter == 1u ? HAL_OK : HAL_EIO;
}

static void prepare_status(void) {
  hal_fault_info_t fault = {0};
  const bool faulted = hal_get_last_fault(&fault);
  if (faulted) {
    hal_clear_last_fault(); /* report a crash once, then start clean */
  }
  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHFLASH1 task0=%lu task1=%lu core0=%u core1=%u reset=%d fault=%d "
      "pc=%08lx lr=%08lx\n",
      (unsigned long)HAL_ATOMIC_LOAD(&s_task0_ticks, HAL_ATOMIC_ACQUIRE),
      (unsigned long)HAL_ATOMIC_LOAD(&s_task1_ticks, HAL_ATOMIC_ACQUIRE),
      (unsigned int)HAL_ATOMIC_LOAD(&s_task0_core, HAL_ATOMIC_ACQUIRE),
      (unsigned int)HAL_ATOMIC_LOAD(&s_task1_core, HAL_ATOMIC_ACQUIRE),
      (int)hal_get_reset_reason(), faulted ? 1 : 0,
      (unsigned long)(faulted ? fault.pc : 0u),
      (unsigned long)(faulted ? fault.lr : 0u));
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

static void run_transaction_tests(void) {
  uint32_t counter = 0u;
  const hal_status_t noop_status = jh_rp_flash_transaction_execute(
      noop_operation, &counter, FLASH_TIMEOUT_MS);
  const hal_status_t usb_status = run_usb_quiesce_probe();
  const hal_status_t raw_status = run_raw_lockout_probe();
  const hal_status_t core1_status = run_core1_transaction();
  const hal_status_t dma_ram_status = run_dma_ram_probe();
  const hal_status_t dma_xip_status = run_dma_xip_probe();
  const hal_status_t xip_status = jh_rp_flash_transaction_execute(
      flash_resident_operation, NULL, FLASH_TIMEOUT_MS);
  const hal_status_t recursive_status = jh_rp_flash_transaction_execute(
      recursive_operation, &counter, FLASH_TIMEOUT_MS);

  const uint32_t flash_offset = FLASH_TEST_OFFSET;
  const uintptr_t binary_end = (uintptr_t)&__flash_binary_end;
  const uintptr_t test_start = (uintptr_t)XIP_BASE + flash_offset;
  hal_status_t flash_status = HAL_EOVERFLOW;
  hal_status_t interrupt_status = HAL_EOVERFLOW;
  hal_status_t recovery_status = HAL_EOVERFLOW;

  if (binary_end <= test_start) {
    for (size_t index = 0u; index < sizeof(s_page); ++index) {
      s_page[index] = (uint8_t)(0x5au ^ (uint8_t)(index * 29u));
    }
    flash_operation_context_t operation = {FLASH_ACTION_ERASE_PROGRAM,
                                           flash_offset, s_page};
    flash_status = jh_rp_flash_transaction_execute(flash_operation, &operation,
                                                   FLASH_TIMEOUT_MS);
    if (flash_status == HAL_OK && !page_matches(flash_offset, s_page)) {
      flash_status = HAL_EIO;
    }

    operation.action = FLASH_ACTION_ERASE_CANCEL;
    interrupt_status = jh_rp_flash_transaction_execute(
        flash_operation, &operation, FLASH_TIMEOUT_MS);
    if (interrupt_status == HAL_ECANCELED && !page_is_erased(flash_offset)) {
      interrupt_status = HAL_EIO;
    }

    memset(s_page, 0xa5, sizeof(s_page));
    operation.action = FLASH_ACTION_PROGRAM;
    recovery_status = jh_rp_flash_transaction_execute(
        flash_operation, &operation, FLASH_TIMEOUT_MS);
    if (recovery_status == HAL_OK && !page_matches(flash_offset, s_page)) {
      recovery_status = HAL_EIO;
    }
  }

  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHFLASH-RESULT usb=%d raw=%d noop=%d core1=%d dma_ram=%d "
      "dma_xip=%d xip=%d recursive=%d flash=%d interrupt=%d "
      "recovery=%d count=%lu\n",
      (int)usb_status, (int)raw_status, (int)noop_status, (int)core1_status,
      (int)dma_ram_status, (int)dma_xip_status, (int)xip_status,
      (int)recursive_status, (int)flash_status, (int)interrupt_status,
      (int)recovery_status, (unsigned long)counter);
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

static void toggle_led(void) {
  s_led_state = !s_led_state;
  hal_gpio_write(HAL_LED_BUILTIN, s_led_state);
}

static hal_status_t ensure_kv_ready(void) {
  if (s_kv_ready) {
    return HAL_OK;
  }
  hal_status_t status = hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u);
  if (status == HAL_OK) {
    status = hal_kv_init_ex(0u, hal_eeprom_size());
  }
  s_kv_ready = status == HAL_OK;
  return status;
}

static void stream_line(const char *format, uint32_t index, uint32_t core,
                        int status) {
  char line[64];
  const int length = snprintf(line, sizeof(line), format, (unsigned long)index,
                              (unsigned int)core, status);
  if (length > 0 && (size_t)length < sizeof(line)) {
    size_t written = 0u;
    (void)hal_usb_cdc_write((const uint8_t *)line, (size_t)length, 100u,
                            &written);
  }
}

static hal_status_t core1_kv_write(uint32_t value) {
  HAL_ATOMIC_STORE(&s_core1_kv_status, HAL_NONE, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&s_core1_kv_value, value, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&s_core1_kv_request, true, HAL_ATOMIC_RELEASE);
  const uint64_t start_us = hal_micros64();
  while (HAL_ATOMIC_LOAD(&s_core1_kv_status, HAL_ATOMIC_ACQUIRE) == HAL_NONE) {
    if (hal_micros64() - start_us >= KV_LOAD_CORE1_TIMEOUT_US) {
      return HAL_ETIMEOUT;
    }
    hal_delay_ms(1u);
  }
  return HAL_ATOMIC_LOAD(&s_core1_kv_status, HAL_ATOMIC_ACQUIRE);
}

static hal_status_t core1_scan(uint8_t request) {
  HAL_ATOMIC_STORE(&s_core1_scan_status, HAL_NONE, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&s_core1_scan_request, request, HAL_ATOMIC_RELEASE);
  const uint64_t start_us = hal_micros64();
  while (HAL_ATOMIC_LOAD(&s_core1_scan_status, HAL_ATOMIC_ACQUIRE) ==
         HAL_NONE) {
    if (hal_micros64() - start_us >= KV_LOAD_CORE1_TIMEOUT_US) {
      return HAL_ETIMEOUT;
    }
    hal_delay_ms(1u);
  }
  return HAL_ATOMIC_LOAD(&s_core1_scan_status, HAL_ATOMIC_ACQUIRE);
}

static hal_status_t start_scan_here(void) {
  hal_adc_scan_config_t config;
  memset(&config, 0, sizeof(config));
  config.pins[0] = 26u;
  config.pins[1] = 27u;
  config.pins[2] = 28u;
  config.pin_count = SCAN_PINS;
  config.conversion_period_ns = SCAN_CONVERSION_NS;
  config.buffer = s_scan.buffer;
  config.block_frames = SCAN_BLOCK_FRAMES;
  return hal_adc_scan_start(&config);
}

static void arm_scan_guard(void) {
  for (size_t index = 0u; index < SCAN_GUARD_WORDS; ++index) {
    s_scan.guard[index] = SCAN_GUARD_PATTERN ^ (uint32_t)index;
  }
}

static bool scan_guard_intact(void) {
  for (size_t index = 0u; index < SCAN_GUARD_WORDS; ++index) {
    if (s_scan.guard[index] != (SCAN_GUARD_PATTERN ^ (uint32_t)index)) {
      return false;
    }
  }
  return true;
}

/* Published KV writes from both cores under a running DMA ring and CDC
 * traffic: the pattern of an application that streams telemetry over USB,
 * scans an ADC by DMA and persists a diagnostic code from its control core. */
static void run_kv_load_probe(void) {
  const hal_status_t init_status = ensure_kv_ready();
  paced_dma_t dma = {-1, -1};
  hal_status_t dma_status = HAL_NONE;
  hal_status_t scan_status = HAL_NONE;
  hal_adc_scan_block_t block;
  uint32_t blocks = 0u;
  bool guard = false;
  hal_status_t core0_status = HAL_OK;
  hal_status_t core1_status = HAL_OK;
  hal_status_t readback_status = HAL_NONE;
  uint32_t failures = 0u;
  uint32_t writes = 0u;
  uint32_t last_value = 0u;

  if (init_status == HAL_OK) {
    dma_status = start_paced_dma(&dma, &s_dma_source, &s_dma_sink, 0x0fffffffu);
  }
  if (init_status == HAL_OK && dma_status == HAL_OK) {
    arm_scan_guard();
    scan_status = core1_scan(1u);
  }
  if (init_status == HAL_OK && dma_status == HAL_OK && scan_status == HAL_OK) {
    for (uint32_t index = 0u; index < KV_LOAD_WRITES; ++index) {
      const uint32_t value = 0xa5000000u | index;
      const uint32_t core = index & 1u;
      const hal_status_t status = core == 0u
                                      ? hal_kv_set_u32_ex(KV_LOAD_KEY, value)
                                      : core1_kv_write(value);
      writes++;
      last_value = value;
      if (status != HAL_OK) {
        failures++;
        if (core == 0u && core0_status == HAL_OK) {
          core0_status = status;
        }
        if (core == 1u && core1_status == HAL_OK) {
          core1_status = status;
        }
      }
      toggle_led();
      stream_line("JHLOAD i=%lu core=%u status=%d\n", index, core, (int)status);
    }
    uint32_t stored = 0u;
    readback_status = hal_kv_get_u32_ex(KV_LOAD_KEY, &stored);
    if (readback_status == HAL_OK && stored != last_value) {
      readback_status = HAL_EIO;
    }
    if (hal_adc_scan_take(&block) == HAL_OK) {
      blocks = block.sequence;
    }
    (void)core1_scan(2u);
    guard = scan_guard_intact();
    stop_paced_dma(&dma);
  } else if (dma_status == HAL_OK) {
    stop_paced_dma(&dma);
  }

  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHLOAD-RESULT init=%d dma=%d scan=%d kv0=%d kv1=%d fail=%lu rb=%d "
      "writes=%lu blocks=%lu guard=%d\n",
      (int)init_status, (int)dma_status, (int)scan_status, (int)core0_status,
      (int)core1_status, (unsigned long)failures, (int)readback_status,
      (unsigned long)writes, (unsigned long)blocks, guard ? 1 : 0);
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

void app_start(void) {
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
  hal_gpio_write(HAL_LED_BUILTIN, false);
}

void app_task0(void) {
  HAL_ATOMIC_ADD_FETCH(&s_task0_ticks, 1u, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_task0_core, (uint8_t)get_core_num(), HAL_ATOMIC_RELEASE);

  if (s_response_offset < s_response_length) {
    size_t written = 0u;
    (void)hal_usb_cdc_write(s_response + s_response_offset,
                            s_response_length - s_response_offset, 100u,
                            &written);
    s_response_offset += written;
  } else {
    uint8_t command = 0u;
    size_t received = 0u;
    if (hal_usb_cdc_read(&command, 1u, &received) == HAL_OK && received == 1u) {
      if (command == COMMAND_STATUS) {
        prepare_status();
      } else if (command == COMMAND_RUN) {
        run_transaction_tests();
      } else if (command == COMMAND_LOAD) {
        run_kv_load_probe();
      }
    }
  }
  hal_delay_ms(1u);
}

void app_task1(void) {
  HAL_ATOMIC_ADD_FETCH(&s_task1_ticks, 1u, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_task1_core, (uint8_t)get_core_num(), HAL_ATOMIC_RELEASE);
  if (HAL_ATOMIC_EXCHANGE(&s_core1_request, false, HAL_ATOMIC_ACQ_REL)) {
    uint32_t counter = 0u;
    const hal_status_t status = jh_rp_flash_transaction_execute(
        noop_operation, &counter, FLASH_TIMEOUT_MS);
    HAL_ATOMIC_STORE(&s_core1_status, status, HAL_ATOMIC_RELEASE);
  }
  const uint8_t scan_request =
      HAL_ATOMIC_EXCHANGE(&s_core1_scan_request, 0u, HAL_ATOMIC_ACQ_REL);
  if (scan_request == 1u) {
    HAL_ATOMIC_STORE(&s_core1_scan_status, start_scan_here(),
                     HAL_ATOMIC_RELEASE);
  } else if (scan_request == 2u) {
    HAL_ATOMIC_STORE(&s_core1_scan_status, hal_adc_scan_stop(),
                     HAL_ATOMIC_RELEASE);
  }
  if (HAL_ATOMIC_EXCHANGE(&s_core1_kv_request, false, HAL_ATOMIC_ACQ_REL)) {
    const uint32_t value =
        HAL_ATOMIC_LOAD(&s_core1_kv_value, HAL_ATOMIC_ACQUIRE);
    const hal_status_t status = hal_kv_set_u32_ex(KV_LOAD_KEY, value);
    HAL_ATOMIC_STORE(&s_core1_kv_status, status, HAL_ATOMIC_RELEASE);
  }
  hal_delay_ms(1u);
}
