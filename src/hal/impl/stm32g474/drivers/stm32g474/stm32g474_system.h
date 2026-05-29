#pragma once

/**
 * @file stm32g474_system.h
 * @brief STM32G474 SoC-specific system services driver.
 *
 * Mirrors @c drivers/rp2040/rp2040_system.h to keep the @c hal_system
 * layer pure dispatch.
 *
 * @par Status
 * Host-stub backend: timing is driven by static counters advanced from
 * @c stm32g474_system_delay_*(); watchdog / bootloader / UID / heap /
 * temperature are observable stubs. A real implementation (RCC, IWDG,
 * system bootloader entry via @c BOOT0 / @c PA13, on-die temperature via
 * ADC1 channel 16, UID via @c UID_BASE) will replace these without
 * changing the public surface.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- Time ------------------------------------------------------------------ */

uint32_t stm32g474_system_millis(void);
uint32_t stm32g474_system_micros(void);
uint64_t stm32g474_system_micros64(void);

void stm32g474_system_delay_ms(uint32_t ms);
void stm32g474_system_delay_us(uint32_t us);

/* -- Watchdog -------------------------------------------------------------- */

void stm32g474_system_watchdog_feed(void);
void stm32g474_system_watchdog_enable(uint32_t ms, bool pause_on_debug);
bool stm32g474_system_watchdog_caused_reboot(void);

/* -- Misc system services -------------------------------------------------- */

/** @brief Idle hint. Real impl will issue @c __WFI(). Stub: no-op. */
void stm32g474_system_idle(void);

/** @brief Cortex-M IPSR check; true inside an exception / IRQ handler.
 *  Compiled to a real @c MRS on ARM targets, returns @c false on host. */
bool stm32g474_system_in_isr(void);

/** @brief Free heap in bytes. Stub returns 0 until newlib/ChibiOS hookup. */
uint32_t stm32g474_system_get_free_heap(void);

/** @brief On-die temperature in °C. Stub returns 0.0f. */
float stm32g474_system_read_chip_temp(void);

/** @brief Jump to the STM32 system bootloader. Stub: no-op. */
void stm32g474_system_enter_bootloader(void);

/** @brief Fill @p uid (exactly 8 bytes) with the device unique id.
 *  Safe no-op if @p uid is @c NULL. Real impl will read three 32-bit
 *  words from @c UID_BASE and fold them into 8 bytes; stub returns a
 *  fixed deterministic placeholder. */
void stm32g474_system_get_device_uid(uint8_t *uid);

/** @brief Format the device unique id as 16 uppercase hex characters
 *  plus a NUL terminator (17 bytes total). */
bool stm32g474_system_get_device_uid_hex(char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
