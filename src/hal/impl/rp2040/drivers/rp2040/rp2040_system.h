#pragma once

/**
 * @file rp2040_system.h
 * @brief RP2040 SoC-specific system services driver.
 *
 * Owns the pico-sdk / arduino-pico bindings that the @c hal_system layer
 * would otherwise have to call directly: hardware watchdog, USB-boot
 * bootloader entry, on-die temperature sensor, free-heap query, unique
 * board id and the @c tight_loop_contents() idle hint.
 *
 * The HAL layer talks to this driver through plain function wrappers and
 * never reaches for any RP2040 register or pico-sdk symbol directly.
 *
 * The "watchdog caused reboot" flag is latched once, during C++ static
 * initialization (BEFORE @c setup() runs) so a later @c
 * rp2040_system_watchdog_enable() call -- which clobbers the watchdog
 * scratch marker -- cannot lose the information.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *backend_name;
  const char *mcu;
  const char *mcu_subtype;
  const char *cpu_arch;
  uint8_t cpu_cores;
  uint32_t ram_total_bytes;
  uint32_t ram_usable_bytes;
  bool has_fpu;
} rp2040_system_arch_info_t;

/** @brief Return static architecture metadata for the active RP2040-family
 * build.
 */
void rp2040_system_get_arch_info(rp2040_system_arch_info_t *out);

/** @brief Feed (kick) the RP2040 hardware watchdog. Wraps @c watchdog_update().
 */
void rp2040_system_watchdog_feed(void);

/** @brief Arm the RP2040 hardware watchdog with the given timeout.
 *  @param ms             Timeout in milliseconds.
 *  @param pause_on_debug If true, pause watchdog while a debugger is attached.
 */
void rp2040_system_watchdog_enable(uint32_t ms, bool pause_on_debug);

/** @brief Returns true iff the previous boot was a genuine watchdog
 *  *timeout* while the application watchdog was armed.
 *
 *  Distinguishes a real starvation from a programmatic reboot via
 *  @c watchdog_reboot() (used by picotool upload, UF2/BOOTSEL relaunch
 *  and @c rp2040.reboot()) by checking the scratch[4] magic written by
 *  @c watchdog_enable().
 *
 *  The flag is latched during C++ static init; callers may read it at
 *  any time after construction. */
bool rp2040_system_watchdog_caused_reboot(void);

/** @brief Yield/idle hint. Wraps @c tight_loop_contents() (no-op on RP2040
 *  but expresses intent and is a documented relax point for the SDK). */
void rp2040_system_idle(void);

/** @brief Free heap, in bytes. Wraps @c rp2040.getFreeHeap(). */
uint32_t rp2040_system_get_free_heap(void);

/** @brief On-die temperature, in °C (±2 °C typical). Wraps @c analogReadTemp().
 */
float rp2040_system_read_chip_temp(void);

/** @brief Jump to the RP2040 USB bootloader (BOOTSEL/UF2 mode). Does not
 * return. Wraps @c reset_usb_boot(0, 0). */
void rp2040_system_enter_bootloader(void) __attribute__((noreturn));

/** @brief Fill @p uid (exactly 8 bytes) with the QSPI flash unique id.
 *  Safe no-op if @p uid is @c NULL. Wraps @c pico_get_unique_board_id(). */
void rp2040_system_get_device_uid(uint8_t *uid);

/** @brief Format the 8-byte unique board id as 16 uppercase hex characters
 *  plus a NUL terminator (17 bytes total).
 *
 *  @param buf    Output buffer.
 *  @param buflen Capacity of @p buf in bytes; must be at least 17.
 *  @return false (without writing anything) if @p buf is @c NULL or
 *          @p buflen is too small; true on success.
 */
bool rp2040_system_get_device_uid_hex(char *buf, size_t buflen);

/** @brief True when called from a Cortex-M exception / IRQ handler.
 *
 *  Reads the ARM Cortex-M @c IPSR register: zero in Thread mode, equal to
 *  the active exception number in Handler mode. The check itself is
 *  Cortex-M-generic, but it lives here because the arduino backend is
 *  currently RP2040-only. */
bool rp2040_system_in_isr(void);

#ifdef __cplusplus
}
#endif
