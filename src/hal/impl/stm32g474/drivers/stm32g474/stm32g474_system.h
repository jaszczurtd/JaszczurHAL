#pragma once

/**
 * @file stm32g474_system.h
 * @brief STM32G474 SoC-specific system services driver.
 *
 * Mirrors @c drivers/rp2040/rp2040_system.h to keep the @c hal_system
 * layer pure dispatch.
 *
 * @par Status
 * Hardware builds use the RCC and SysTick implementation from the STM32G474
 * port layer. Host builds retain deterministic timing and test hooks. Some
 * auxiliary services remain observable stubs on both targets.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal/core/hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return the main stack reservation in bytes.
 *  On hardware builds this is derived from linker symbols; host fallback uses
 *  the backend default reserve.
 */
uint32_t stm32g474_system_main_stack_bytes(void);

/* -- Time ------------------------------------------------------------------ */

uint32_t stm32g474_system_millis(void);
uint32_t stm32g474_system_micros(void);
uint64_t stm32g474_system_micros64(void);

#ifdef JH_STM32G474_SYSTEM_TESTING
void stm32g474_system_test_set_micros64(uint64_t micros);
void stm32g474_system_test_reset_watchdog(void);
bool stm32g474_system_test_watchdog_enabled(void);
uint32_t stm32g474_system_test_watchdog_prescaler(void);
uint32_t stm32g474_system_test_watchdog_reload(void);
bool stm32g474_system_test_watchdog_pause_on_debug(void);
#endif

void stm32g474_system_delay_ms(uint32_t ms);
void stm32g474_system_delay_us(uint32_t us);

/* -- Watchdog -------------------------------------------------------------- */

void stm32g474_system_watchdog_feed(void);
hal_status_t stm32g474_system_watchdog_enable(uint32_t ms, bool pause_on_debug);
bool stm32g474_system_watchdog_caused_reboot(void);

/* -- Misc system services -------------------------------------------------- */

/** @brief Idle hint. Real impl will issue @c __WFI(). Stub: no-op. */
void stm32g474_system_idle(void);

/** @brief Cortex-M IPSR check; true inside an exception / IRQ handler.
 *  Compiled to a real @c MRS on ARM targets, returns @c false on host. */
bool stm32g474_system_in_isr(void);

/** @brief Runtime newlib heap capacity between `_end` and the stack guard. */
uint32_t stm32g474_system_heap_total_bytes(void);

/** @brief Free runtime newlib heap in bytes. */
uint32_t stm32g474_system_get_free_heap(void);

/** @brief Read the on-die temperature via the ADC1 internal VSENSE channel
 *  (IN16), compensated against the VREFINT channel (IN18) and the factory
 *  TS_CAL1/TS_CAL2/VREFINT_CAL bytes from system memory (RM0440 "Vbat,
 *  temperature sensor and VrefInt channel").
 *  @param out_celsius Destination for the measured die temperature.
 *  @return HAL_OK on success, HAL_EINVAL when @p out_celsius is NULL, or
 *          HAL_EUNSUPPORTED on host-sanity builds (no OTP/ADC to read). */
hal_status_t stm32g474_system_read_chip_temp_ex(float *out_celsius);

/** @brief Two-point VREFINT-ratio-compensated die-temperature interpolation.
 *  Pure math extracted from @ref stm32g474_system_read_chip_temp_ex so it is
 *  testable on host without real ADC/OTP access.
 *  @param ts_raw        Raw ADC1 code from the VSENSE channel (IN16).
 *  @param vref_raw      Raw ADC1 code from the VREFINT channel (IN18).
 *  @param ts_cal1       Factory TS_CAL1 byte (raw code at 30 degC).
 *  @param ts_cal2       Factory TS_CAL2 byte (raw code at 130 degC).
 *  @param vrefint_cal   Factory VREFINT_CAL byte (raw code at 30 degC).
 *  @return Interpolated temperature in degrees Celsius. Returns 0.0f when
 *          @p vref_raw or (ts_cal2 - ts_cal1) is zero (degenerate input). */
float stm32g474_system_calc_chip_temp_celsius(uint16_t ts_raw,
                                              uint16_t vref_raw,
                                              uint16_t ts_cal1,
                                              uint16_t ts_cal2,
                                              uint16_t vrefint_cal);

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
