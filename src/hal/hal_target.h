#pragma once

/**
 * @file hal_target.h
 * @brief Canonical compile-time TARGET (backend) selection for JaszczurHAL.
 *
 * This is the single source of truth for "which hardware backend am I building
 * against". It replaces the old, fuzzy per-file guards such as
 * `#if !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32)` with one explicit,
 * validated switch that consumers control from their project config.
 *
 * ── How a consumer selects a target ──────────────────────────────────────
 * In `hal_project_config.h` (or via a -D compiler flag) define exactly ONE of:
 *
 *     #define HAL_TARGET_RP2040       // Raspberry Pi RP2040 / arduino-pico
 *     #define HAL_TARGET_STM32G474    // STM32G474 (bare-metal backend)
 *     #define HAL_TARGET_MOCK         // host unit-test / simulation backend
 *
 * If NONE is defined, the target is auto-detected from the toolchain so that
 * existing projects keep building unchanged:
 *   - arduino-pico / RP2040 SDK macros  -> HAL_TARGET_RP2040
 *   - STM32G474xx (CubeG4 / -D)         -> HAL_TARGET_STM32G474
 *   - any host compiler (no ARM target) -> HAL_TARGET_MOCK
 *   - bare-metal ARM with no match      -> hard #error (real misconfiguration)
 *
 * ── What the rest of the library consumes ────────────────────────────────
 *   HAL_TARGET_IS_RP2040 / _IS_STM32G474 / _IS_MOCK   (exactly one == 1)
 *   HAL_TARGET_NAME                                    (string, e.g. "rp2040")
 *   JH_STM32G474_HW                                    (derived: G474 + ARM,
 *                                                       i.e. emit register code
 *                                                       vs host-stub sanity)
 */

/* ── 1. Auto-detect when the consumer did not pick a target explicitly ───── */
#if !defined(HAL_TARGET_RP2040) && !defined(HAL_TARGET_STM32G474) &&           \
    !defined(HAL_TARGET_MOCK)

#if defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2040) ||                    \
    defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO)
/* Specific RP2040 macros first; bare `ARDUINO` is a catch-all because
 * this library's RP2040 backend targets RP2040 (library.properties
 * architectures=rp2040). A future Arduino-based target (e.g. STM32duino)
 * must select HAL_TARGET_* explicitly, which bypasses this block. This
 * catch-all guarantees existing RP2040/Arduino consumers never lose the
 * backend to a missed macro. */
#define HAL_TARGET_RP2040 1
#elif defined(STM32G474xx) || defined(STM32G4)
#define HAL_TARGET_STM32G474 1
#elif !defined(__arm__) && !defined(__thumb__)
/* Host compiler (unit tests / simulation) -> mock backend. */
#define HAL_TARGET_MOCK 1
#else
#error "JaszczurHAL: no target selected and none could be auto-detected. \
Define HAL_TARGET_RP2040 / HAL_TARGET_STM32G474 / HAL_TARGET_MOCK in \
hal_project_config.h (or via a -D flag)."
#endif

#endif

/* ── 2. Normalise to 0/1 booleans the rest of the code can test in #if ───── */
#if defined(HAL_TARGET_RP2040)
#define HAL_TARGET_IS_RP2040 1
#else
#define HAL_TARGET_IS_RP2040 0
#endif

#if defined(HAL_TARGET_STM32G474)
#define HAL_TARGET_IS_STM32G474 1
#else
#define HAL_TARGET_IS_STM32G474 0
#endif

#if defined(HAL_TARGET_MOCK)
#define HAL_TARGET_IS_MOCK 1
#else
#define HAL_TARGET_IS_MOCK 0
#endif

/* ── 3. Enforce "exactly one" target ─────────────────────────────────────── */
#if (HAL_TARGET_IS_RP2040 + HAL_TARGET_IS_STM32G474 + HAL_TARGET_IS_MOCK) != 1
#error "JaszczurHAL: exactly one HAL_TARGET_* must be selected \
(RP2040 / STM32G474 / MOCK)."
#endif

/* ── 4. Human-readable name ──────────────────────────────────────────────── */
#if HAL_TARGET_IS_RP2040
#define HAL_TARGET_NAME "rp2040"
#elif HAL_TARGET_IS_STM32G474
#define HAL_TARGET_NAME "stm32g474"
#else
#define HAL_TARGET_NAME "mock"
#endif

/* ── 4b. Board LED fallback ───────────────────────────────────────────────
 * HAL internals use HAL_LED_BUILTIN so they never need Arduino's
 * pins_arduino.h. Keep LED_BUILTIN as a non-Arduino compatibility fallback;
 * Arduino builds should let Arduino.h provide its own LED_BUILTIN later.
 *
 * Do not include Arduino's pins_arduino.h here. On RP2350 WiFi variants it can
 * re-enter Arduino.h before the variant has defined PICO_RP2350A, which trips
 * Arduino-Pico's RP2350 sanity check. Use local board/MCU knowledge instead. */
#ifndef HAL_LED_BUILTIN
#if HAL_TARGET_IS_RP2040
#if defined(HAL_CYW43_PROFILE_PICOW)
#define HAL_LED_BUILTIN 64u
#elif defined(PIN_LED)
#define HAL_LED_BUILTIN PIN_LED
#elif defined(ARDUINO_RASPBERRY_PI_PICO_W) ||                                  \
    defined(ARDUINO_RASPBERRY_PI_PICO_2W)
#define HAL_LED_BUILTIN 64u
#elif defined(ARDUINO_RASPBERRY_PI_PICO) ||                                    \
    defined(ARDUINO_RASPBERRY_PI_PICO_2) ||                                    \
    defined(ARDUINO_WAVESHARE_RP2040_PLUS)
#define HAL_LED_BUILTIN 25u
#elif !defined(ARDUINO)
#define HAL_LED_BUILTIN 25u
#endif
#elif HAL_TARGET_IS_STM32G474
/* Nucleo-G474RE LD2 = PA5, and HAL GPIO numbering is port*16 + pin. */
#define HAL_LED_BUILTIN 5u
#endif
#endif

#if !defined(LED_BUILTIN) && !defined(ARDUINO) && defined(HAL_LED_BUILTIN)
#define LED_BUILTIN HAL_LED_BUILTIN
#endif

/* ── 5. Derived: real STM32G474 hardware vs host-stub sanity build ────────
 * The STM32G474 backend is selected in both the on-target ARM build and the
 * host "does it still compile" sanity build. Only the former should emit real
 * register/peripheral code; the latter keeps the host stubs. That distinction
 * is purely "is this an ARM compile", so derive it here instead of asking the
 * consumer to pass a second flag. */
#if HAL_TARGET_IS_STM32G474 && (defined(__arm__) || defined(__thumb__))
#ifndef JH_STM32G474_HW
#define JH_STM32G474_HW 1
#endif
#endif
