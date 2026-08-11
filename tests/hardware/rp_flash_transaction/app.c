#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/impl/rp2040/drivers/flash/rp_flash_runtime.h>
#include <hal/impl/rp2040/drivers/flash/rp_flash_transaction.h>
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
#define FLASH_TIMEOUT_MS 1000u

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

static hal_status_t run_dma_gate(void) {
  const int channel = dma_claim_unused_channel(false);
  const int timer = dma_claim_unused_timer(false);
  if (channel < 0 || timer < 0) {
    if (channel >= 0) {
      dma_channel_unclaim((uint)channel);
    }
    if (timer >= 0) {
      dma_timer_unclaim((uint)timer);
    }
    return HAL_ENOMEM;
  }

  dma_timer_set_fraction((uint)timer, 1u, 0xffffu);
  dma_channel_config config = dma_channel_get_default_config((uint)channel);
  channel_config_set_read_increment(&config, false);
  channel_config_set_write_increment(&config, false);
  channel_config_set_dreq(&config, dma_get_timer_dreq((uint)timer));
  dma_channel_configure((uint)channel, &config, &s_dma_sink, &s_dma_source,
                        0x100000u, true);

  while (!dma_channel_is_busy((uint)channel)) {
    tight_loop_contents();
  }
  uint32_t counter = 0u;
  const hal_status_t status = jh_rp_flash_transaction_execute(
      noop_operation, &counter, FLASH_TIMEOUT_MS);
  dma_channel_abort((uint)channel);
  dma_channel_unclaim((uint)channel);
  dma_timer_unclaim((uint)timer);
  return status;
}

static hal_status_t run_core1_transaction(void) {
  __atomic_store_n(&s_core1_status, HAL_NONE, __ATOMIC_RELEASE);
  __atomic_store_n(&s_core1_request, true, __ATOMIC_RELEASE);
  const uint64_t start_us = hal_micros64();
  while (__atomic_load_n(&s_core1_status, __ATOMIC_ACQUIRE) == HAL_NONE) {
    if (hal_micros64() - start_us >= 1000000u) {
      return HAL_ETIMEOUT;
    }
    hal_delay_ms(1u);
  }
  return __atomic_load_n(&s_core1_status, __ATOMIC_ACQUIRE);
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
  const int length =
      snprintf((char *)s_response, sizeof(s_response),
               "JHFLASH1 task0=%lu task1=%lu core0=%u core1=%u\n",
               (unsigned long)__atomic_load_n(&s_task0_ticks, __ATOMIC_ACQUIRE),
               (unsigned long)__atomic_load_n(&s_task1_ticks, __ATOMIC_ACQUIRE),
               (unsigned int)__atomic_load_n(&s_task0_core, __ATOMIC_ACQUIRE),
               (unsigned int)__atomic_load_n(&s_task1_core, __ATOMIC_ACQUIRE));
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
  const hal_status_t dma_status = run_dma_gate();
  const hal_status_t xip_status = jh_rp_flash_transaction_execute(
      flash_resident_operation, NULL, FLASH_TIMEOUT_MS);
  const hal_status_t recursive_status = jh_rp_flash_transaction_execute(
      recursive_operation, &counter, FLASH_TIMEOUT_MS);

  const uint32_t flash_offset =
      (uint32_t)PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
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

  const int length =
      snprintf((char *)s_response, sizeof(s_response),
               "JHFLASH-RESULT usb=%d raw=%d noop=%d core1=%d dma=%d xip=%d "
               "recursive=%d flash=%d interrupt=%d recovery=%d count=%lu\n",
               (int)usb_status, (int)raw_status, (int)noop_status,
               (int)core1_status, (int)dma_status, (int)xip_status,
               (int)recursive_status, (int)flash_status, (int)interrupt_status,
               (int)recovery_status, (unsigned long)counter);
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

void app_start(void) {}

void app_task0(void) {
  __atomic_add_fetch(&s_task0_ticks, 1u, __ATOMIC_RELAXED);
  __atomic_store_n(&s_task0_core, (uint8_t)get_core_num(), __ATOMIC_RELEASE);

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
      }
    }
  }
  hal_delay_ms(1u);
}

void app_task1(void) {
  __atomic_add_fetch(&s_task1_ticks, 1u, __ATOMIC_RELAXED);
  __atomic_store_n(&s_task1_core, (uint8_t)get_core_num(), __ATOMIC_RELEASE);
  if (__atomic_exchange_n(&s_core1_request, false, __ATOMIC_ACQ_REL)) {
    uint32_t counter = 0u;
    const hal_status_t status = jh_rp_flash_transaction_execute(
        noop_operation, &counter, FLASH_TIMEOUT_MS);
    __atomic_store_n(&s_core1_status, status, __ATOMIC_RELEASE);
  }
  hal_delay_ms(1u);
}
